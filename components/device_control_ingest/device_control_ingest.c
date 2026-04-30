#include "device_control_ingest.h"

#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "quest_common_utils.h"
#include "event_bus.h"

typedef struct {
    bool in_use;
    device_control_ingest_device_t state;
} dci_slot_t;

static const char *TAG = "control_ingest";
static dci_slot_t *s_slots = NULL;
static SemaphoreHandle_t s_lock = NULL;
static uint32_t s_generation = 0;

static uint64_t dci_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static device_control_ingest_device_t *dci_alloc_snapshot(void)
{
    device_control_ingest_device_t *snapshot =
        heap_caps_calloc(1, sizeof(*snapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snapshot) {
        snapshot = heap_caps_calloc(1, sizeof(*snapshot), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return snapshot;
}

static bool dci_streq(const char *lhs, const char *rhs)
{
    if (!lhs || !rhs) {
        return false;
    }
    return strcmp(lhs, rhs) == 0;
}

static bool dci_parse_topic(const char *topic,
                            char *out_device_id,
                            size_t out_device_id_size,
                            device_control_topic_t *out_kind)
{
    const char *prefix = "cp/v1/dev/";
    const char *segment = NULL;
    const char *tail = NULL;
    size_t prefix_len = strlen(prefix);
    size_t device_id_len = 0;

    if (!topic || !out_device_id || out_device_id_size == 0 || !out_kind) {
        return false;
    }
    out_device_id[0] = '\0';
    *out_kind = DEVICE_CONTROL_TOPIC_UNKNOWN;

    if (strncmp(topic, prefix, prefix_len) != 0) {
        return false;
    }
    segment = topic + prefix_len;
    tail = strchr(segment, '/');
    if (!tail) {
        return false;
    }
    device_id_len = (size_t)(tail - segment);
    if (device_id_len == 0 || device_id_len >= out_device_id_size) {
        return false;
    }

    memcpy(out_device_id, segment, device_id_len);
    out_device_id[device_id_len] = '\0';
    tail++;
    if (!tail[0] || strchr(tail, '/')) {
        return false;
    }

    if (dci_streq(tail, "heartbeat")) {
        *out_kind = DEVICE_CONTROL_TOPIC_HEARTBEAT;
        return true;
    }
    if (dci_streq(tail, "status")) {
        *out_kind = DEVICE_CONTROL_TOPIC_STATUS;
        return true;
    }
    if (dci_streq(tail, "diag")) {
        *out_kind = DEVICE_CONTROL_TOPIC_DIAG;
        return true;
    }
    if (dci_streq(tail, "result")) {
        *out_kind = DEVICE_CONTROL_TOPIC_RESULT;
        return true;
    }

    return false;
}

static void dci_post_status_event(const device_control_ingest_device_t *state)
{
    if (!state || !state->device_id[0]) {
        return;
    }

    event_bus_message_t msg = {
        .type = EVENT_DEVICE_STATUS,
        .payload_type = EVENT_BUS_PAYLOAD_DEVICE_STATUS,
    };
    quest_str_copy(msg.payload, sizeof(msg.payload), state->status_state);
    quest_str_copy(msg.data.device_status.device_id,
                sizeof(msg.data.device_status.device_id),
                state->device_id);
    quest_str_copy(msg.data.device_status.connectivity,
                sizeof(msg.data.device_status.connectivity),
                "online");
    quest_str_copy(msg.data.device_status.health,
                sizeof(msg.data.device_status.health),
                state->status_health[0] ? state->status_health : "unknown");
    quest_str_copy(msg.data.device_status.state,
                sizeof(msg.data.device_status.state),
                state->status_state[0] ? state->status_state : "unknown");
    msg.data.device_status.timestamp_ms = state->last_seen_ms;
    (void)event_bus_post_priority(&msg, EVENT_BUS_PRIORITY_HIGH, 0);
}

static void dci_post_runtime_event(const device_control_ingest_device_t *state)
{
    if (!state || !state->device_id[0] || !state->has_status) {
        return;
    }

    event_bus_message_t msg = {
        .type = EVENT_DEVICE_RUNTIME,
        .payload_type = EVENT_BUS_PAYLOAD_DEVICE_RUNTIME,
    };
    quest_str_copy(msg.payload, sizeof(msg.payload), state->status_state);
    quest_str_copy(msg.data.device_runtime.device_id,
                sizeof(msg.data.device_runtime.device_id),
                state->device_id);
    quest_str_copy(msg.data.device_runtime.runtime_type,
                sizeof(msg.data.device_runtime.runtime_type),
                "control_status");
    quest_str_copy(msg.data.device_runtime.state,
                sizeof(msg.data.device_runtime.state),
                state->status_state[0] ? state->status_state : "unknown");
    msg.data.device_runtime.active = state->status_runtime_active;
    msg.data.device_runtime.timestamp_ms = state->last_seen_ms;
    (void)event_bus_post(&msg, 0);
}

static void dci_post_control_event(const device_control_ingest_device_t *state)
{
    if (!state || !state->device_id[0] || !state->has_result) {
        return;
    }

    event_bus_message_t msg = {
        .type = EVENT_DEVICE_CONTROL,
        .payload_type = EVENT_BUS_PAYLOAD_DEVICE_CONTROL,
    };
    quest_str_copy(msg.payload, sizeof(msg.payload), state->result_status);
    quest_str_copy(msg.data.device_control.device_id,
                sizeof(msg.data.device_control.device_id),
                state->device_id);
    quest_str_copy(msg.data.device_control.action_id,
                sizeof(msg.data.device_control.action_id),
                state->result_request_id[0] ? state->result_request_id : state->result_command);
    quest_str_copy(msg.data.device_control.source,
                sizeof(msg.data.device_control.source),
                "result");
    msg.data.device_control.timestamp_ms = state->last_seen_ms;
    (void)event_bus_post_priority(&msg, EVENT_BUS_PRIORITY_HIGH, 0);
}

static uint64_t dci_json_u64(const cJSON *obj, const char *key, uint64_t fallback)
{
    const cJSON *item = NULL;
    if (!obj || !key) {
        return fallback;
    }
    item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsNumber(item) || item->valuedouble < 0.0) {
        return fallback;
    }
    return (uint64_t)item->valuedouble;
}

static uint32_t dci_json_u32(const cJSON *obj, const char *key, uint32_t fallback)
{
    uint64_t value = dci_json_u64(obj, key, fallback);
    if (value > UINT32_MAX) {
        return fallback;
    }
    return (uint32_t)value;
}

static bool dci_json_bool(const cJSON *obj, const char *key, bool fallback)
{
    const cJSON *item = NULL;
    if (!obj || !key) {
        return fallback;
    }
    item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || (!cJSON_IsBool(item) && !cJSON_IsNumber(item))) {
        return fallback;
    }
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return item->valueint != 0;
}

static void dci_json_copy(const cJSON *obj, const char *key, char *dst, size_t dst_size)
{
    const cJSON *item = NULL;
    if (!obj || !key || !dst || dst_size == 0) {
        return;
    }
    item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsString(item) || !item->valuestring) {
        return;
    }
    quest_str_copy(dst, dst_size, item->valuestring);
}

static dci_slot_t *dci_find_slot_locked(const char *device_id)
{
    if (!s_slots || !device_id || !device_id[0]) {
        return NULL;
    }
    for (size_t i = 0; i < DEVICE_CONTROL_INGEST_MAX_DEVICES; ++i) {
        dci_slot_t *slot = &s_slots[i];
        if (!slot->in_use) {
            continue;
        }
        if (strcmp(slot->state.device_id, device_id) == 0) {
            return slot;
        }
    }
    return NULL;
}

static dci_slot_t *dci_alloc_slot_locked(const char *device_id)
{
    dci_slot_t *free_slot = NULL;
    dci_slot_t *oldest_slot = NULL;
    if (!s_slots || !device_id || !device_id[0]) {
        return NULL;
    }

    for (size_t i = 0; i < DEVICE_CONTROL_INGEST_MAX_DEVICES; ++i) {
        dci_slot_t *slot = &s_slots[i];
        if (!slot->in_use) {
            if (!free_slot) {
                free_slot = slot;
            }
            continue;
        }
        if (strcmp(slot->state.device_id, device_id) == 0) {
            return slot;
        }
        if (!oldest_slot || slot->state.last_contract_rx_ms < oldest_slot->state.last_contract_rx_ms) {
            oldest_slot = slot;
        }
    }

    if (!free_slot) {
        free_slot = oldest_slot;
    }
    if (!free_slot) {
        return NULL;
    }
    if (free_slot->in_use) {
        ESP_LOGW(TAG, "evicting control state for %s", free_slot->state.device_id);
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->in_use = true;
    quest_str_copy(free_slot->state.device_id, sizeof(free_slot->state.device_id), device_id);
    return free_slot;
}

static esp_err_t dci_apply_heartbeat_locked(dci_slot_t *slot, const cJSON *root, uint64_t rx_ms)
{
    if (!slot || !root) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.has_heartbeat = true;
    slot->state.heartbeat_ts_ms = dci_json_u64(root, "ts_ms", slot->state.heartbeat_ts_ms);
    slot->state.heartbeat_rx_ms = rx_ms;
    slot->state.heartbeat_uptime_ms = dci_json_u64(root, "uptime_ms", slot->state.heartbeat_uptime_ms);
    slot->state.heartbeat_status_seq = dci_json_u32(root, "status_seq", slot->state.heartbeat_status_seq);
    dci_json_copy(root, "boot_id", slot->state.heartbeat_boot_id, sizeof(slot->state.heartbeat_boot_id));
    slot->state.heartbeat_count++;
    return ESP_OK;
}

static esp_err_t dci_apply_status_locked(dci_slot_t *slot, const cJSON *root, uint64_t rx_ms)
{
    const cJSON *runtime = NULL;
    if (!slot || !root) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.has_status = true;
    slot->state.status_ts_ms = dci_json_u64(root, "ts_ms", slot->state.status_ts_ms);
    slot->state.status_rx_ms = rx_ms;
    dci_json_copy(root, "boot_id", slot->state.status_boot_id, sizeof(slot->state.status_boot_id));
    dci_json_copy(root, "fw_version", slot->state.status_fw_version, sizeof(slot->state.status_fw_version));
    dci_json_copy(root, "mode", slot->state.status_mode, sizeof(slot->state.status_mode));
    dci_json_copy(root, "state", slot->state.status_state, sizeof(slot->state.status_state));
    dci_json_copy(root, "health", slot->state.status_health, sizeof(slot->state.status_health));

    runtime = cJSON_GetObjectItemCaseSensitive(root, "runtime");
    if (runtime && cJSON_IsObject(runtime)) {
        slot->state.status_runtime_active = dci_json_bool(runtime,
                                                          "active",
                                                          slot->state.status_runtime_active);
    } else {
        slot->state.status_runtime_active = dci_json_bool(root,
                                                          "runtime_active",
                                                          slot->state.status_runtime_active);
    }
    slot->state.status_count++;
    return ESP_OK;
}

static esp_err_t dci_apply_diag_locked(dci_slot_t *slot, const cJSON *root, uint64_t rx_ms)
{
    if (!slot || !root) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.has_diag = true;
    slot->state.diag_ts_ms = dci_json_u64(root, "ts_ms", slot->state.diag_ts_ms);
    slot->state.diag_rx_ms = rx_ms;
    dci_json_copy(root, "level", slot->state.diag_level, sizeof(slot->state.diag_level));
    dci_json_copy(root, "code", slot->state.diag_code, sizeof(slot->state.diag_code));
    dci_json_copy(root, "message", slot->state.diag_message, sizeof(slot->state.diag_message));
    slot->state.diag_count++;
    return ESP_OK;
}

static esp_err_t dci_apply_result_locked(dci_slot_t *slot, const cJSON *root, uint64_t rx_ms)
{
    if (!slot || !root) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.has_result = true;
    slot->state.result_ts_ms = dci_json_u64(root, "ts_ms", slot->state.result_ts_ms);
    slot->state.result_rx_ms = rx_ms;
    dci_json_copy(root,
                  "request_id",
                  slot->state.result_request_id,
                  sizeof(slot->state.result_request_id));
    dci_json_copy(root,
                  "command",
                  slot->state.result_command,
                  sizeof(slot->state.result_command));
    dci_json_copy(root, "status", slot->state.result_status, sizeof(slot->state.result_status));
    dci_json_copy(root,
                  "error_code",
                  slot->state.result_error_code,
                  sizeof(slot->state.result_error_code));
    dci_json_copy(root,
                  "message",
                  slot->state.result_message,
                  sizeof(slot->state.result_message));
    slot->state.result_data_json[0] = '\0';
    const cJSON *data = cJSON_GetObjectItem(root, "data");
    if (data) {
        char *printed = cJSON_PrintUnformatted(data);
        if (printed) {
            quest_str_copy(slot->state.result_data_json,
                        sizeof(slot->state.result_data_json),
                        printed);
            cJSON_free(printed);
        }
    }
    slot->state.result_count++;
    return ESP_OK;
}

esp_err_t device_control_ingest_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_slots) {
        s_slots = heap_caps_calloc(DEVICE_CONTROL_INGEST_MAX_DEVICES,
                                   sizeof(dci_slot_t),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_slots) {
            s_slots = heap_caps_calloc(DEVICE_CONTROL_INGEST_MAX_DEVICES,
                                       sizeof(dci_slot_t),
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!s_slots) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t device_control_ingest_reset(void)
{
    if (!s_lock || !s_slots) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memset(s_slots, 0, DEVICE_CONTROL_INGEST_MAX_DEVICES * sizeof(dci_slot_t));
    s_generation++;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t device_control_ingest_handle_mqtt(const char *topic, const char *payload)
{
    char device_id[QUEST_ID_MAX_LEN] = {0};
    device_control_topic_t kind = DEVICE_CONTROL_TOPIC_UNKNOWN;
    cJSON *root = NULL;
    dci_slot_t *slot = NULL;
    device_control_ingest_device_t *snapshot = NULL;
    uint64_t now_ms = dci_now_ms();
    esp_err_t err = ESP_OK;

    if (!topic || !payload) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_lock || !s_slots) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!dci_parse_topic(topic, device_id, sizeof(device_id), &kind) ||
        kind == DEVICE_CONTROL_TOPIC_UNKNOWN) {
        return ESP_ERR_NOT_FOUND;
    }
    snapshot = dci_alloc_snapshot();
    if (!snapshot) {
        return ESP_ERR_NO_MEM;
    }

    root = cJSON_Parse(payload);
    if (!root || !cJSON_IsObject(root)) {
        if (root) {
            cJSON_Delete(root);
        }
        ESP_LOGW(TAG, "invalid JSON for topic %s", topic);
        heap_caps_free(snapshot);
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        cJSON_Delete(root);
        heap_caps_free(snapshot);
        return ESP_ERR_TIMEOUT;
    }
    slot = dci_alloc_slot_locked(device_id);
    if (!slot) {
        xSemaphoreGive(s_lock);
        cJSON_Delete(root);
        heap_caps_free(snapshot);
        return ESP_ERR_NO_MEM;
    }

    slot->state.last_seen_ms = now_ms;
    slot->state.last_contract_rx_ms = now_ms;
    switch (kind) {
    case DEVICE_CONTROL_TOPIC_HEARTBEAT:
        err = dci_apply_heartbeat_locked(slot, root, now_ms);
        break;
    case DEVICE_CONTROL_TOPIC_STATUS:
        err = dci_apply_status_locked(slot, root, now_ms);
        break;
    case DEVICE_CONTROL_TOPIC_DIAG:
        err = dci_apply_diag_locked(slot, root, now_ms);
        break;
    case DEVICE_CONTROL_TOPIC_RESULT:
        err = dci_apply_result_locked(slot, root, now_ms);
        break;
    case DEVICE_CONTROL_TOPIC_UNKNOWN:
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }
    if (err == ESP_OK) {
        *snapshot = slot->state;
    }
    xSemaphoreGive(s_lock);
    cJSON_Delete(root);
    if (err == ESP_OK) {
        s_generation++;
        if (kind == DEVICE_CONTROL_TOPIC_HEARTBEAT || kind == DEVICE_CONTROL_TOPIC_STATUS ||
            kind == DEVICE_CONTROL_TOPIC_DIAG) {
            dci_post_status_event(snapshot);
        }
        if (kind == DEVICE_CONTROL_TOPIC_STATUS) {
            dci_post_runtime_event(snapshot);
        }
        if (kind == DEVICE_CONTROL_TOPIC_RESULT) {
            dci_post_control_event(snapshot);
        }
    }
    heap_caps_free(snapshot);
    return err;
}

esp_err_t device_control_ingest_get_device(const char *device_id, device_control_ingest_device_t *out)
{
    dci_slot_t *slot = NULL;
    if (!device_id || !device_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_lock || !s_slots) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    slot = dci_find_slot_locked(device_id);
    if (!slot) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    *out = slot->state;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

size_t device_control_ingest_count(void)
{
    size_t count = 0;
    if (!s_lock || !s_slots) {
        return 0;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    for (size_t i = 0; i < DEVICE_CONTROL_INGEST_MAX_DEVICES; ++i) {
        if (s_slots[i].in_use) {
            count++;
        }
    }
    xSemaphoreGive(s_lock);
    return count;
}

uint32_t device_control_ingest_generation(void)
{
    uint32_t generation = 0;
    if (!s_lock || !s_slots) {
        return 0;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    generation = s_generation;
    xSemaphoreGive(s_lock);
    return generation;
}

esp_err_t device_control_ingest_list_devices(device_control_ingest_device_t *out,
                                             size_t max_count,
                                             size_t *out_count)
{
    size_t count = 0;
    if (!out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (max_count > 0 && !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_lock || !s_slots) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < DEVICE_CONTROL_INGEST_MAX_DEVICES; ++i) {
        if (!s_slots[i].in_use) {
            continue;
        }
        if (count < max_count) {
            out[count] = s_slots[i].state;
        }
        count++;
    }
    xSemaphoreGive(s_lock);
    *out_count = count;
    return (count > max_count) ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

bool device_control_ingest_is_online(const device_control_ingest_device_t *state,
                                     uint64_t now_ms,
                                     uint32_t timeout_ms)
{
    uint32_t effective_timeout = timeout_ms ? timeout_ms : DEVICE_CONTROL_INGEST_DEFAULT_ONLINE_TIMEOUT_MS;
    if (!state || !state->device_id[0] || state->last_seen_ms == 0) {
        return false;
    }
    if (now_ms <= state->last_seen_ms) {
        return true;
    }
    return (now_ms - state->last_seen_ms) <= effective_timeout;
}
