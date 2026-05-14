#include "device_control_ingest_internal.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "quest_common_utils.h"

static const char *TAG = "control_ingest";
dci_slot_t **dci_s_slots = NULL;
SemaphoreHandle_t dci_s_lock = NULL;
StaticSemaphore_t dci_s_lock_storage;
portMUX_TYPE dci_s_lock_init_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t dci_s_generation = 0;
char dci_s_last_changed_device_id[QUEST_ID_MAX_LEN] = {0};

static dci_slot_t *dci_alloc_slot_storage(void)
{
    dci_slot_t *slot = heap_caps_calloc(1, sizeof(*slot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!slot) {
        slot = heap_caps_calloc(1, sizeof(*slot), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return slot;
}

static void dci_free_all_slot_storage(void)
{
    if (!dci_s_slots) {
        return;
    }
    for (size_t i = 0; i < DEVICE_CONTROL_INGEST_MAX_DEVICES; ++i) {
        if (dci_s_slots[i]) {
            heap_caps_free(dci_s_slots[i]);
            dci_s_slots[i] = NULL;
        }
    }
}

static esp_err_t dci_alloc_all_slot_storage(void)
{
    for (size_t i = 0; i < DEVICE_CONTROL_INGEST_MAX_DEVICES; ++i) {
        dci_s_slots[i] = dci_alloc_slot_storage();
        if (!dci_s_slots[i]) {
            dci_free_all_slot_storage();
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

uint64_t dci_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
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
    if (dci_streq(tail, "event")) {
        *out_kind = DEVICE_CONTROL_TOPIC_EVENT;
        return true;
    }

    return false;
}

dci_slot_t *dci_find_slot_locked(const char *device_id)
{
    if (!dci_s_slots || !device_id || !device_id[0]) {
        return NULL;
    }
    for (size_t i = 0; i < DEVICE_CONTROL_INGEST_MAX_DEVICES; ++i) {
        dci_slot_t *slot = dci_s_slots[i];
        if (!slot || !slot->in_use) {
            continue;
        }
        if (strcmp(slot->state.device_id, device_id) == 0) {
            return slot;
        }
    }
    return NULL;
}

dci_slot_t *dci_alloc_slot_locked(const char *device_id)
{
    dci_slot_t *free_slot = NULL;
    dci_slot_t *oldest_slot = NULL;
    if (!dci_s_slots || !device_id || !device_id[0]) {
        return NULL;
    }

    for (size_t i = 0; i < DEVICE_CONTROL_INGEST_MAX_DEVICES; ++i) {
        dci_slot_t *slot = dci_s_slots[i];
        if (!slot) {
            return NULL;
        }
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

esp_err_t device_control_ingest_init(void)
{
    if (!dci_s_lock) {
        portENTER_CRITICAL(&dci_s_lock_init_lock);
        if (!dci_s_lock) {
            dci_s_lock = xSemaphoreCreateMutexStatic(&dci_s_lock_storage);
        }
        portEXIT_CRITICAL(&dci_s_lock_init_lock);
        if (!dci_s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!dci_s_slots) {
        dci_s_slots = heap_caps_calloc(DEVICE_CONTROL_INGEST_MAX_DEVICES,
                                       sizeof(*dci_s_slots),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!dci_s_slots) {
            dci_s_slots = heap_caps_calloc(DEVICE_CONTROL_INGEST_MAX_DEVICES,
                                           sizeof(*dci_s_slots),
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!dci_s_slots) {
            return ESP_ERR_NO_MEM;
        }
        esp_err_t slots_err = dci_alloc_all_slot_storage();
        if (slots_err != ESP_OK) {
            heap_caps_free(dci_s_slots);
            dci_s_slots = NULL;
            return slots_err;
        }
    }
    return ESP_OK;
}

esp_err_t device_control_ingest_reset(void)
{
    if (!dci_s_lock || !dci_s_slots) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(dci_s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < DEVICE_CONTROL_INGEST_MAX_DEVICES; ++i) {
        if (!dci_s_slots[i]) {
            continue;
        }
        memset(dci_s_slots[i], 0, sizeof(*dci_s_slots[i]));
    }
    dci_s_last_changed_device_id[0] = '\0';
    dci_s_generation++;
    xSemaphoreGive(dci_s_lock);
    return ESP_OK;
}

esp_err_t device_control_ingest_handle_mqtt(const char *topic, const char *payload)
{
    char device_id[QUEST_ID_MAX_LEN] = {0};
    device_control_topic_t kind = DEVICE_CONTROL_TOPIC_UNKNOWN;
    dci_slot_t *slot = NULL;
    dci_event_snapshot_t snapshot = {0};
    uint64_t now_ms = dci_now_ms();
    esp_err_t err = ESP_OK;

    if (!topic || !payload) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!dci_s_lock || !dci_s_slots) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!dci_parse_topic(topic, device_id, sizeof(device_id), &kind) ||
        kind == DEVICE_CONTROL_TOPIC_UNKNOWN) {
        return ESP_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(dci_s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    slot = dci_alloc_slot_locked(device_id);
    if (!slot) {
        xSemaphoreGive(dci_s_lock);
        return ESP_ERR_NO_MEM;
    }

    slot->state.last_seen_ms = now_ms;
    slot->state.last_contract_rx_ms = now_ms;
    switch (kind) {
    case DEVICE_CONTROL_TOPIC_HEARTBEAT:
        err = dci_apply_heartbeat_text_locked(slot, payload, now_ms);
        break;
    case DEVICE_CONTROL_TOPIC_STATUS:
        err = dci_apply_status_text_locked(slot, payload, now_ms);
        break;
    case DEVICE_CONTROL_TOPIC_DIAG:
        err = dci_apply_diag_text_locked(slot, payload, now_ms);
        break;
    case DEVICE_CONTROL_TOPIC_RESULT:
        err = dci_apply_result_text_locked(slot, payload, now_ms);
        break;
    case DEVICE_CONTROL_TOPIC_EVENT:
        err = dci_apply_event_text_locked(slot, payload, now_ms);
        break;
    case DEVICE_CONTROL_TOPIC_UNKNOWN:
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }
    if (err == ESP_OK) {
        dci_capture_event_snapshot(&slot->state, &snapshot);
        quest_str_copy(dci_s_last_changed_device_id,
                       sizeof(dci_s_last_changed_device_id),
                       device_id);
    }
    xSemaphoreGive(dci_s_lock);
    if (err == ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG, "invalid payload for topic %s", topic);
    }
    if (err == ESP_OK) {
        dci_s_generation++;
        if (kind == DEVICE_CONTROL_TOPIC_HEARTBEAT || kind == DEVICE_CONTROL_TOPIC_STATUS ||
            kind == DEVICE_CONTROL_TOPIC_DIAG) {
            dci_post_status_event(&snapshot);
        }
        if (kind == DEVICE_CONTROL_TOPIC_STATUS) {
            dci_post_runtime_event(&snapshot);
        }
        if (kind == DEVICE_CONTROL_TOPIC_RESULT || kind == DEVICE_CONTROL_TOPIC_EVENT) {
            dci_post_control_event(&snapshot);
        }
    }
    return err;
}

esp_err_t device_control_ingest_get_device(const char *device_id, device_control_ingest_device_t *out)
{
    dci_slot_t *slot = NULL;
    if (!device_id || !device_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!dci_s_lock || !dci_s_slots) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(dci_s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    slot = dci_find_slot_locked(device_id);
    if (!slot) {
        xSemaphoreGive(dci_s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    *out = slot->state;
    xSemaphoreGive(dci_s_lock);
    return ESP_OK;
}

size_t device_control_ingest_count(void)
{
    size_t count = 0;
    if (!dci_s_lock || !dci_s_slots) {
        return 0;
    }
    if (xSemaphoreTake(dci_s_lock, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    for (size_t i = 0; i < DEVICE_CONTROL_INGEST_MAX_DEVICES; ++i) {
        if (dci_s_slots[i] && dci_s_slots[i]->in_use) {
            count++;
        }
    }
    xSemaphoreGive(dci_s_lock);
    return count;
}

uint32_t device_control_ingest_generation(void)
{
    uint32_t generation = 0;
    if (!dci_s_lock || !dci_s_slots) {
        return 0;
    }
    if (xSemaphoreTake(dci_s_lock, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    generation = dci_s_generation;
    xSemaphoreGive(dci_s_lock);
    return generation;
}

esp_err_t device_control_ingest_get_last_changed_device_id(char *out_device_id, size_t out_device_id_size)
{
    if (!out_device_id || out_device_id_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out_device_id[0] = '\0';
    if (!dci_s_lock || !dci_s_slots) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(dci_s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    quest_str_copy(out_device_id, out_device_id_size, dci_s_last_changed_device_id);
    xSemaphoreGive(dci_s_lock);
    return out_device_id[0] ? ESP_OK : ESP_ERR_NOT_FOUND;
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
    if (!dci_s_lock || !dci_s_slots) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(dci_s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < DEVICE_CONTROL_INGEST_MAX_DEVICES; ++i) {
        dci_slot_t *slot = dci_s_slots[i];
        if (!slot || !slot->in_use) {
            continue;
        }
        if (count < max_count) {
            out[count] = slot->state;
        }
        count++;
    }
    xSemaphoreGive(dci_s_lock);
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
