#include "node_driver_nfc_reader_runtime.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "node_driver_nfc_contract.h"
#include "node_driver_nfc_reader.h"
#include "node_driver_pn532_i2c.h"
#include "node_event_router.h"
#include "sdkconfig.h"

static const char *TAG = "node_drv_nfc_rt";

enum {
    NODE_NFC_RUNTIME_QUEUE_LEN = 8,
};

typedef enum {
    NODE_NFC_RUNTIME_REQ_NONE = 0,
    NODE_NFC_RUNTIME_REQ_RESET,
    NODE_NFC_RUNTIME_REQ_REINIT,
    NODE_NFC_RUNTIME_REQ_RELOAD,
    NODE_NFC_RUNTIME_REQ_SCAN,
} node_nfc_runtime_request_kind_t;

typedef struct {
    node_nfc_runtime_request_kind_t kind;
    bool present;
    char reader_id[NODE_DRIVER_ID_MAX_LEN + 1];
    char uid[NODE_DRIVER_UID_TEXT_MAX_LEN];
} node_nfc_runtime_request_t;

typedef struct {
    bool initialized;
    bool started;
    bool enabled;
    bool stable_present;
    bool pending_valid;
    bool pending_present;
    uint32_t pending_since_ms;
    uint32_t seen_count;
    int32_t stable_token_id;
    char stable_uid[NODE_DRIVER_UID_TEXT_MAX_LEN];
    char last_seen_uid[NODE_DRIVER_UID_TEXT_MAX_LEN];
    char pending_uid[NODE_DRIVER_UID_TEXT_MAX_LEN];
} node_nfc_runtime_state_t;

static node_nfc_runtime_state_t s_runtime;
static node_nfc_reader_config_t *s_runtime_config;
static StaticQueue_t s_runtime_queue_storage;
static uint8_t s_runtime_queue_buffer[NODE_NFC_RUNTIME_QUEUE_LEN * sizeof(node_nfc_runtime_request_t)];
static QueueHandle_t s_runtime_queue;
static StaticTask_t s_runtime_task_storage;
static StackType_t *s_runtime_task_stack_mem;
static TaskHandle_t s_runtime_task;
static StaticSemaphore_t s_reload_mutex_storage;
static SemaphoreHandle_t s_reload_mutex;
static node_nfc_reader_config_t *s_reload_config;
static bool s_reload_pending;

static void nfc_runtime_task(void *arg);
static void emit_card_removed(void);

static void *alloc_runtime_buffer(size_t size)
{
    void *ptr = NULL;

    if (size == 0) {
        return NULL;
    }
#if CONFIG_SPIRAM
    ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, size);
        return ptr;
    }
#endif
    ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

static bool ensure_reload_config(void)
{
    if (s_reload_config) {
        return true;
    }

    s_reload_config = (node_nfc_reader_config_t *)alloc_runtime_buffer(sizeof(*s_reload_config));
    if (!s_reload_config) {
        ESP_LOGE(TAG, "reload config alloc failed (%u bytes)", (unsigned)sizeof(*s_reload_config));
        return false;
    }
    return true;
}

static bool ensure_runtime_config(void)
{
    if (s_runtime_config) {
        return true;
    }

    s_runtime_config = (node_nfc_reader_config_t *)alloc_runtime_buffer(sizeof(*s_runtime_config));
    if (!s_runtime_config) {
        ESP_LOGE(TAG, "runtime config alloc failed (%u bytes)", (unsigned)sizeof(*s_runtime_config));
        return false;
    }
    return true;
}

static StackType_t *allocate_runtime_task_stack(void)
{
    size_t stack_bytes = 6144U * sizeof(StackType_t);

    if (s_runtime_task_stack_mem) {
        return s_runtime_task_stack_mem;
    }
#if CONFIG_SPIRAM && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    s_runtime_task_stack_mem = (StackType_t *)heap_caps_malloc(stack_bytes,
                                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_runtime_task_stack_mem) {
        memset(s_runtime_task_stack_mem, 0, stack_bytes);
        ESP_LOGI(TAG, "nfc runtime stack source=psram bytes=%u", (unsigned)stack_bytes);
        return s_runtime_task_stack_mem;
    }
    ESP_LOGW(TAG, "nfc runtime stack psram alloc failed; using internal heap fallback");
#endif
    s_runtime_task_stack_mem = (StackType_t *)heap_caps_malloc(stack_bytes, MALLOC_CAP_8BIT);
    if (s_runtime_task_stack_mem) {
        memset(s_runtime_task_stack_mem, 0, stack_bytes);
        ESP_LOGI(TAG, "nfc runtime stack source=internal_heap bytes=%u", (unsigned)stack_bytes);
    }
    return s_runtime_task_stack_mem;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool text_present(const char *text)
{
    return text && text[0] != '\0';
}

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void fill_runtime_text(char *dst, size_t dst_size, const char *value, const char *fallback)
{
    copy_text(dst, dst_size, (value && value[0] != '\0') ? value : fallback);
}

static const char *map_pn532_runtime_error_code(const node_pn532_i2c_diag_t *diag,
                                                int err_code,
                                                bool recovery_phase)
{
    if (diag && diag->pending_hw_reset) {
        return recovery_phase ? "recovery_pending" : "hw_reset_pending";
    }
    if (diag && !diag->bus_ready) {
        if (err_code == ESP_ERR_TIMEOUT) {
            return recovery_phase ? "bus_recovery_pending" : "bus_timeout";
        }
        return recovery_phase ? "bus_recovery_pending" : "bus_error";
    }
    switch (err_code) {
    case ESP_OK:
        return recovery_phase ? "session_not_ready" : "init_pending";
    case ESP_ERR_TIMEOUT:
        return recovery_phase ? "reader_not_responding" : "reader_not_found";
    case ESP_ERR_INVALID_RESPONSE:
        return recovery_phase ? "invalid_response" : "reader_not_responding";
    case ESP_ERR_INVALID_STATE:
        return recovery_phase ? "session_not_ready" : "init_pending";
    case ESP_ERR_INVALID_SIZE:
        return "protocol_error";
    case ESP_ERR_NO_MEM:
        return "driver_no_mem";
    case ESP_ERR_NOT_SUPPORTED:
        return "unsupported_driver";
    default:
        return recovery_phase ? "transport_error" : "init_failed";
    }
}

static void update_driver_health(node_nfc_reader_runtime_status_t *out_status)
{
    node_pn532_i2c_diag_t diag = {0};
    int err_code = ESP_OK;

    if (!out_status) {
        return;
    }

    fill_runtime_text(out_status->health, sizeof(out_status->health), "", "disabled");
    fill_runtime_text(out_status->state, sizeof(out_status->state), "", "disabled");
    out_status->error_code[0] = '\0';
    out_status->driver_ready = false;

    if (!out_status->enabled) {
        return;
    }
    if (!s_runtime_config || strcmp(s_runtime_config->driver_impl, "pn532") != 0) {
        fill_runtime_text(out_status->health, sizeof(out_status->health), "", "error");
        fill_runtime_text(out_status->state, sizeof(out_status->state), "", "unsupported");
        fill_runtime_text(out_status->error_code, sizeof(out_status->error_code), "", "unsupported_driver");
        return;
    }

    node_driver_pn532_i2c_get_diag(&diag);
    out_status->driver_ready = diag.session_ready;
    if (diag.session_ready) {
        fill_runtime_text(out_status->health, sizeof(out_status->health), "", "ok");
        fill_runtime_text(out_status->state, sizeof(out_status->state), "", "online");
        return;
    }

    err_code = diag.last_poll_err != ESP_OK ? diag.last_poll_err : diag.last_init_err;
    if (diag.ever_ready) {
        fill_runtime_text(out_status->health, sizeof(out_status->health), "", "degraded");
        fill_runtime_text(out_status->state, sizeof(out_status->state), "", "recovery");
        fill_runtime_text(out_status->error_code,
                          sizeof(out_status->error_code),
                          "",
                          map_pn532_runtime_error_code(&diag, err_code, true));
        return;
    }

    fill_runtime_text(out_status->health, sizeof(out_status->health), "", "error");
    fill_runtime_text(out_status->state, sizeof(out_status->state), "", "offline");
    if (diag.next_init_attempt_ms != 0 || err_code != ESP_OK) {
        fill_runtime_text(out_status->error_code,
                          sizeof(out_status->error_code),
                          "",
                          map_pn532_runtime_error_code(&diag, err_code, false));
    } else {
        fill_runtime_text(out_status->error_code, sizeof(out_status->error_code), "", "init_pending");
    }
}

static void clear_runtime_card_state(void)
{
    s_runtime.stable_present = false;
    s_runtime.pending_valid = false;
    s_runtime.pending_present = false;
    s_runtime.pending_since_ms = 0;
    s_runtime.seen_count = 0;
    s_runtime.stable_token_id = 0;
    s_runtime.stable_uid[0] = '\0';
    s_runtime.pending_uid[0] = '\0';
}

static bool lock_reload_config(void)
{
    if (!s_reload_mutex) {
        s_reload_mutex = xSemaphoreCreateMutexStatic(&s_reload_mutex_storage);
    }
    return s_reload_mutex && xSemaphoreTake(s_reload_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void unlock_reload_config(void)
{
    if (s_reload_mutex) {
        xSemaphoreGive(s_reload_mutex);
    }
}

static void reset_runtime_card_state(bool emit_removed)
{
    if (emit_removed && s_runtime.stable_present) {
        emit_card_removed();
    }
    clear_runtime_card_state();
}

static bool ensure_runtime_owner(void)
{
    if (!s_runtime_queue) {
        s_runtime_queue = xQueueCreateStatic(NODE_NFC_RUNTIME_QUEUE_LEN,
                                             sizeof(node_nfc_runtime_request_t),
                                             s_runtime_queue_buffer,
                                             &s_runtime_queue_storage);
    }
    if (!ensure_reload_config() || !ensure_runtime_config()) {
        return false;
    }
    if (s_runtime_queue && !s_runtime_task) {
        StackType_t *task_stack = allocate_runtime_task_stack();

        if (!task_stack) {
            return false;
        }
        s_runtime_task = xTaskCreateStatic(nfc_runtime_task,
                                           "node_nfc_drv",
                                           6144,
                                           NULL,
                                           tskIDLE_PRIORITY + 1,
                                           task_stack,
                                           &s_runtime_task_storage);
    }
    return s_runtime_queue && s_runtime_task;
}

static bool observation_matches(bool present_a,
                                const char *uid_a,
                                bool present_b,
                                const char *uid_b)
{
    if (present_a != present_b) {
        return false;
    }
    if (!present_a) {
        return true;
    }
    return strcmp(uid_a ? uid_a : "", uid_b ? uid_b : "") == 0;
}

static void dispatch_local_event(const char *event_name, int32_t token_id, const char *uid)
{
    esp_err_t err = ESP_OK;

    if (!text_present(event_name)) {
        return;
    }
    err = node_event_router_route_local_event(event_name,
                                              s_runtime_config ? s_runtime_config->id : "",
                                              token_id,
                                              uid);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG,
                 "local_event dispatch failed event=%s reader=%s err=%s",
                 event_name,
                 s_runtime_config ? s_runtime_config->id : "",
                 esp_err_to_name(err));
    }
}

static void emit_card_removed(void)
{
    char event_name[NODE_DRIVER_EVENT_NAME_MAX_LEN] = {0};

    if (!s_runtime_config || !text_present(s_runtime_config->id)) {
        return;
    }
    snprintf(event_name, sizeof(event_name), "%s_card_removed", s_runtime_config->id);
    dispatch_local_event(event_name, s_runtime.stable_token_id, s_runtime.stable_uid);
}

static void emit_card_seen(const char *uid)
{
    node_nfc_card_resolution_t resolution = {0};
    char event_name[NODE_DRIVER_EVENT_NAME_MAX_LEN] = {0};

    if (!s_runtime_config || !text_present(s_runtime_config->id) || !text_present(uid)) {
        return;
    }

    s_runtime.stable_token_id = 0;
    copy_text(s_runtime.last_seen_uid, sizeof(s_runtime.last_seen_uid), uid);
    if (node_driver_nfc_contract_resolve_card(s_runtime_config, uid, &resolution) == ESP_OK && resolution.matched) {
        s_runtime.stable_token_id = resolution.token_id;
    }

    snprintf(event_name, sizeof(event_name), "%s_card_seen", s_runtime_config->id);
    dispatch_local_event(event_name, s_runtime.stable_token_id, uid);
    if (resolution.matched && text_present(resolution.event_name)) {
        dispatch_local_event(resolution.event_name, resolution.token_id, uid);
    }
}

static void commit_observation(bool present, const char *uid)
{
    bool changed = !observation_matches(s_runtime.stable_present,
                                        s_runtime.stable_uid,
                                        present,
                                        uid);

    if (!changed) {
        return;
    }

    if (s_runtime.stable_present) {
        emit_card_removed();
    }

    s_runtime.stable_present = present;
    copy_text(s_runtime.stable_uid, sizeof(s_runtime.stable_uid), present ? uid : "");
    s_runtime.seen_count = present ? (s_runtime.seen_count + 1U) : 0U;
    s_runtime.stable_token_id = 0;

    if (present) {
        emit_card_seen(uid);
        ESP_LOGI(TAG,
                 "reader event seen id=%s uid=%s token=%ld count=%lu",
                 s_runtime_config ? s_runtime_config->id : "",
                 s_runtime.stable_uid,
                 (long)s_runtime.stable_token_id,
                 (unsigned long)s_runtime.seen_count);
    } else {
        ESP_LOGI(TAG, "reader event removed id=%s", s_runtime_config ? s_runtime_config->id : "");
    }
}

static void handle_scan_request(const node_nfc_runtime_request_t *request)
{
    uint32_t now_ms_value = now_ms();
    uint32_t debounce_ms = s_runtime_config ? s_runtime_config->debounce_ms : 0;
    bool matches_stable = false;

    if (!request || !s_runtime.started || !s_runtime.enabled) {
        return;
    }
    if (s_runtime_config &&
        text_present(request->reader_id) &&
        strcmp(request->reader_id, s_runtime_config->id) != 0) {
        return;
    }

    matches_stable = observation_matches(s_runtime.stable_present,
                                         s_runtime.stable_uid,
                                         request->present,
                                         request->uid);
    if (matches_stable) {
        s_runtime.pending_valid = false;
        s_runtime.pending_uid[0] = '\0';
        return;
    }

    if (debounce_ms == 0) {
        commit_observation(request->present, request->uid);
        s_runtime.pending_valid = false;
        s_runtime.pending_uid[0] = '\0';
        return;
    }

    if (!s_runtime.pending_valid ||
        !observation_matches(s_runtime.pending_present,
                             s_runtime.pending_uid,
                             request->present,
                             request->uid)) {
        s_runtime.pending_valid = true;
        s_runtime.pending_present = request->present;
        s_runtime.pending_since_ms = now_ms_value;
        copy_text(s_runtime.pending_uid, sizeof(s_runtime.pending_uid), request->present ? request->uid : "");
        return;
    }

    if ((now_ms_value - s_runtime.pending_since_ms) < debounce_ms) {
        return;
    }

    commit_observation(s_runtime.pending_present, s_runtime.pending_uid);
    s_runtime.pending_valid = false;
    s_runtime.pending_uid[0] = '\0';
}

static void handle_request(const node_nfc_runtime_request_t *request)
{
    node_nfc_reader_config_t config = {0};
    bool have_reload = false;

    if (!request) {
        return;
    }

    switch (request->kind) {
    case NODE_NFC_RUNTIME_REQ_RESET:
        reset_runtime_card_state(true);
        break;
    case NODE_NFC_RUNTIME_REQ_REINIT:
        reset_runtime_card_state(true);
        if (s_runtime_config && strcmp(s_runtime_config->driver_impl, "pn532") == 0) {
            (void)node_driver_pn532_i2c_request_reinit();
        }
        break;
    case NODE_NFC_RUNTIME_REQ_RELOAD:
        if (!lock_reload_config()) {
            break;
        }
        if (s_reload_pending) {
            config = *s_reload_config;
            s_reload_pending = false;
            have_reload = true;
        }
        unlock_reload_config();
        if (!have_reload) {
            break;
        }
        reset_runtime_card_state(true);
        memset(s_runtime.last_seen_uid, 0, sizeof(s_runtime.last_seen_uid));
        *s_runtime_config = config;
        s_runtime.started = true;
        s_runtime.enabled = config.enabled;
        if (s_runtime.enabled && s_runtime_config && strcmp(s_runtime_config->driver_impl, "pn532") == 0) {
            (void)node_driver_pn532_i2c_start(s_runtime_config);
            (void)node_driver_pn532_i2c_request_reinit();
        }
        break;
    case NODE_NFC_RUNTIME_REQ_SCAN:
        handle_scan_request(request);
        break;
    case NODE_NFC_RUNTIME_REQ_NONE:
    default:
        break;
    }
}

static void nfc_runtime_task(void *arg)
{
    (void)arg;

    while (true) {
        node_nfc_runtime_request_t request = {0};

        if (xQueueReceive(s_runtime_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        handle_request(&request);
        while (xQueueReceive(s_runtime_queue, &request, 0) == pdTRUE) {
            handle_request(&request);
        }
    }
}

static esp_err_t enqueue_request(const node_nfc_runtime_request_t *request)
{
    if (!request) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_runtime_queue || !s_runtime_task) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_runtime_queue, request, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t node_driver_nfc_reader_runtime_init(void)
{
    if (s_runtime.initialized) {
        return ESP_OK;
    }
    if (!ensure_runtime_config()) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_runtime, 0, sizeof(s_runtime));
    s_runtime.initialized = true;
    return ESP_OK;
}

esp_err_t node_driver_nfc_reader_runtime_start(const node_config_t *node_config)
{
    node_nfc_reader_config_t config = {0};
    esp_err_t err = ESP_OK;

    if (!node_config) {
        return ESP_ERR_INVALID_ARG;
    }
    err = node_driver_nfc_reader_runtime_init();
    if (err != ESP_OK) {
        return err;
    }

    memset(s_runtime_config, 0, sizeof(*s_runtime_config));
    clear_runtime_card_state();
    memset(s_runtime.last_seen_uid, 0, sizeof(s_runtime.last_seen_uid));
    s_runtime.started = false;
    s_runtime.enabled = false;

    err = node_driver_nfc_reader_load_factory_config(node_config, &config);
    if (err == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (!ensure_runtime_owner()) {
        return ESP_ERR_NO_MEM;
    }

    *s_runtime_config = config;
    s_runtime.started = true;
    s_runtime.enabled = config.enabled;
    if (s_runtime.enabled && s_runtime_config && strcmp(s_runtime_config->driver_impl, "pn532") == 0) {
        err = node_driver_pn532_i2c_start(s_runtime_config);
        if (err != ESP_OK) {
            s_runtime.started = false;
            s_runtime.enabled = false;
            memset(s_runtime_config, 0, sizeof(*s_runtime_config));
            return err;
        }
    }
    ESP_LOGI(TAG,
             "runtime owner ready id=%s impl=%s bus=%s poll=%lu debounce=%lu",
             s_runtime_config ? s_runtime_config->id : "",
             s_runtime_config ? s_runtime_config->driver_impl : "",
             s_runtime_config ? s_runtime_config->bus : "",
             s_runtime_config ? (unsigned long)s_runtime_config->poll_interval_ms : 0UL,
             s_runtime_config ? (unsigned long)s_runtime_config->debounce_ms : 0UL);
    return ESP_OK;
}

esp_err_t node_driver_nfc_reader_runtime_reset(void)
{
    node_nfc_runtime_request_t request = {.kind = NODE_NFC_RUNTIME_REQ_RESET};

    if (!s_runtime.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_runtime.started) {
        clear_runtime_card_state();
        return ESP_OK;
    }
    return enqueue_request(&request);
}

esp_err_t node_driver_nfc_reader_runtime_submit_scan(const char *reader_id, const char *uid)
{
    node_nfc_runtime_request_t request = {0};

    if (!s_runtime.initialized || !s_runtime.started || !s_runtime.enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    request.kind = NODE_NFC_RUNTIME_REQ_SCAN;
    request.present = text_present(uid);
    copy_text(request.reader_id, sizeof(request.reader_id), reader_id);
    copy_text(request.uid, sizeof(request.uid), request.present ? uid : "");
    return enqueue_request(&request);
}

esp_err_t node_driver_nfc_reader_runtime_reinit(void)
{
    node_nfc_runtime_request_t request = {.kind = NODE_NFC_RUNTIME_REQ_REINIT};

    if (!s_runtime.initialized || !s_runtime.started || !s_runtime.enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    return enqueue_request(&request);
}

esp_err_t node_driver_nfc_reader_runtime_reload(const node_config_t *node_config)
{
    node_nfc_reader_config_t config = {0};
    node_nfc_runtime_request_t request = {.kind = NODE_NFC_RUNTIME_REQ_RELOAD};
    esp_err_t err = ESP_OK;

    if (!node_config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_runtime.initialized || !s_runtime.started) {
        return ESP_ERR_INVALID_STATE;
    }
    err = node_driver_nfc_reader_load_factory_config(node_config, &config);
    if (err != ESP_OK) {
        return err;
    }
    if (!lock_reload_config()) {
        return ESP_ERR_TIMEOUT;
    }
    *s_reload_config = config;
    s_reload_pending = true;
    unlock_reload_config();
    return enqueue_request(&request);
}

void node_driver_nfc_reader_runtime_get_status(node_nfc_reader_runtime_status_t *out_status)
{
    if (!out_status) {
        return;
    }

    memset(out_status, 0, sizeof(*out_status));
    out_status->initialized = s_runtime.initialized;
    out_status->started = s_runtime.started;
    out_status->enabled = s_runtime.enabled;
    out_status->card_present = s_runtime.stable_present;
    out_status->poll_interval_ms = s_runtime_config ? s_runtime_config->poll_interval_ms : 0;
    out_status->debounce_ms = s_runtime_config ? s_runtime_config->debounce_ms : 0;
    out_status->seen_count = s_runtime.seen_count;
    out_status->token_id = s_runtime.stable_token_id;
    copy_text(out_status->reader_id, sizeof(out_status->reader_id), s_runtime_config ? s_runtime_config->id : "");
    copy_text(out_status->uid, sizeof(out_status->uid), s_runtime.stable_uid);
    copy_text(out_status->last_seen_uid, sizeof(out_status->last_seen_uid), s_runtime.last_seen_uid);
    update_driver_health(out_status);
}
