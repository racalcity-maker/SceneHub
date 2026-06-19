#include "node_admin_control.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "node_board.h"
#include "node_control.h"
#include "node_driver_nfc_reader_runtime.h"
#include "node_rule_api.h"
#include "sdkconfig.h"

typedef enum {
    NODE_ADMIN_REQ_SAVE_BASE = 0,
    NODE_ADMIN_REQ_SAVE_LED,
    NODE_ADMIN_REQ_SAVE_NFC_CARDS,
    NODE_ADMIN_REQ_RESET_WIFI,
    NODE_ADMIN_REQ_FACTORY_RESET,
    NODE_ADMIN_REQ_RESTART,
    NODE_ADMIN_REQ_VALIDATE_RULES,
    NODE_ADMIN_REQ_APPLY_RULES,
    NODE_ADMIN_REQ_CLEAR_RULES,
    NODE_ADMIN_REQ_PAUSE_RULES,
    NODE_ADMIN_REQ_RESUME_RULES,
    NODE_ADMIN_REQ_REINIT_NFC,
} node_admin_request_type_t;

typedef struct {
    node_admin_request_type_t type;
    const node_config_t *config;
    const node_led_strip_config_t *led_strips;
    size_t led_count;
    const node_nfc_known_card_t *nfc_cards;
    size_t nfc_card_count;
    const char *rule_bundle_json;
    node_rule_bundle_metadata_t *rule_metadata;
    char *error_code;
    size_t error_code_size;
    node_admin_control_result_t *result;
    TaskHandle_t reply_task;
} node_admin_request_t;

typedef struct {
    node_config_t config_scratch;
    node_nfc_reader_config_t nfc_reader_scratch;
} node_admin_scratch_t;

static node_config_t *s_live_config;
static StaticSemaphore_t s_config_mutex_storage;
static SemaphoreHandle_t s_config_mutex;
static StaticQueue_t s_request_queue_storage;
static uint8_t s_request_queue_buffer[2 * sizeof(node_admin_request_t)];
static QueueHandle_t s_request_queue;
static StaticTask_t s_task_storage;
static StackType_t *s_task_stack_mem;
static TaskHandle_t s_task;
static bool s_initialized;
static node_admin_scratch_t *s_scratch;
static const char *TAG = "node_admin_ctl";

#define s_config_scratch (s_scratch->config_scratch)
#define s_nfc_reader_scratch (s_scratch->nfc_reader_scratch)

static void *alloc_admin_buffer(size_t size)
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

static bool ensure_admin_scratch(void)
{
    if (s_scratch) {
        return true;
    }

    s_scratch = (node_admin_scratch_t *)alloc_admin_buffer(sizeof(*s_scratch));
    if (!s_scratch) {
        ESP_LOGE(TAG, "admin scratch alloc failed (%u bytes)", (unsigned)sizeof(*s_scratch));
        return false;
    }
    return true;
}

static StackType_t *allocate_admin_task_stack(void)
{
    size_t stack_bytes = 4096U * sizeof(StackType_t);

    if (s_task_stack_mem) {
        return s_task_stack_mem;
    }

    /* Admin task performs NVS/flash operations, so its stack must stay internal. */
    s_task_stack_mem = (StackType_t *)heap_caps_malloc(stack_bytes,
                                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_task_stack_mem) {
        memset(s_task_stack_mem, 0, stack_bytes);
        ESP_LOGI(TAG, "admin task stack source=internal_heap bytes=%u", (unsigned)stack_bytes);
    }
    return s_task_stack_mem;
}

static bool lock_config(void)
{
    if (!s_config_mutex) {
        s_config_mutex = xSemaphoreCreateMutexStatic(&s_config_mutex_storage);
    }
    return s_config_mutex && xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void unlock_config(void)
{
    if (s_config_mutex) {
        xSemaphoreGive(s_config_mutex);
    }
}

static void write_result(node_admin_control_result_t *result,
                         esp_err_t err,
                         bool restart_required,
                         bool applied,
                         bool restarting)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    result->err = err;
    result->restart_required = restart_required;
    result->applied = applied;
    result->restarting = restarting;
}

static void apply_led_overlay(node_config_t *config,
                              const node_led_strip_config_t *led_strips,
                              size_t count)
{
    if (!config || !led_strips || count > NODE_LED_STRIP_MAX) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        config->led_strips[i].blink = led_strips[i].blink;
        config->led_strips[i].breathe = led_strips[i].breathe;
        config->led_strips[i].effects = led_strips[i].effects;
    }
}

static esp_err_t copy_live_config(node_config_t *out_config)
{
    if (!out_config || !s_live_config) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lock_config()) {
        return ESP_ERR_TIMEOUT;
    }
    *out_config = *s_live_config;
    unlock_config();
    return ESP_OK;
}

static esp_err_t handle_save_base(const node_admin_request_t *request)
{
    esp_err_t err = ESP_OK;

    if (!request || !request->config) {
        return ESP_ERR_INVALID_ARG;
    }

    err = node_config_save(request->config);
    if (err == ESP_OK) {
        err = node_config_save_led_editor(request->config->led_strips, NODE_LED_STRIP_MAX);
    }
    if (err != ESP_OK) {
        return err;
    }

    if (!lock_config()) {
        return ESP_ERR_TIMEOUT;
    }
    *s_live_config = *request->config;
    unlock_config();
    return ESP_OK;
}

static esp_err_t handle_save_led(const node_admin_request_t *request)
{
    esp_err_t err = ESP_OK;

    if (!request || !request->led_strips || request->led_count > NODE_LED_STRIP_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    err = node_config_save_led_editor(request->led_strips, request->led_count);
    if (err != ESP_OK) {
        return err;
    }

    if (!lock_config()) {
        return ESP_ERR_TIMEOUT;
    }
    apply_led_overlay(s_live_config, request->led_strips, request->led_count);
    unlock_config();

    return node_control_update_led_config(request->led_strips, request->led_count);
}

static esp_err_t handle_save_nfc_cards(const node_admin_request_t *request)
{
    esp_err_t err = ESP_OK;

    if (!request || (!request->nfc_cards && request->nfc_card_count > 0) ||
        request->nfc_card_count > NODE_DRIVER_NFC_KNOWN_CARD_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ensure_admin_scratch()) {
        return ESP_ERR_NO_MEM;
    }

    memset(&s_config_scratch, 0, sizeof(s_config_scratch));
    err = copy_live_config(&s_config_scratch);
    if (err != ESP_OK) {
        return err;
    }

    memset(&s_nfc_reader_scratch, 0, sizeof(s_nfc_reader_scratch));
    err = node_driver_nfc_reader_load_factory_config(&s_config_scratch, &s_nfc_reader_scratch);
    if (err != ESP_OK) {
        return err;
    }
    s_nfc_reader_scratch.known_card_count = request->nfc_card_count;
    memset(s_nfc_reader_scratch.known_cards, 0, sizeof(s_nfc_reader_scratch.known_cards));
    for (size_t i = 0; i < request->nfc_card_count; ++i) {
        s_nfc_reader_scratch.known_cards[i] = request->nfc_cards[i];
    }
    err = node_driver_nfc_reader_validate_config(&s_nfc_reader_scratch, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = node_driver_nfc_reader_save_known_cards(s_nfc_reader_scratch.known_cards,
                                                  s_nfc_reader_scratch.known_card_count);
    if (err != ESP_OK) {
        return err;
    }
    return node_driver_nfc_reader_runtime_reload(&s_config_scratch);
}

static esp_err_t handle_reset_wifi(void)
{
    esp_err_t err = node_config_reset_wifi();

    if (err != ESP_OK) {
        return err;
    }
    if (!lock_config()) {
        return ESP_ERR_TIMEOUT;
    }
    s_live_config->wifi_ssid[0] = '\0';
    s_live_config->wifi_password[0] = '\0';
    unlock_config();
    return ESP_OK;
}

static esp_err_t handle_factory_reset(void)
{
    esp_err_t err = node_config_factory_reset();

    if (err != ESP_OK) {
        return err;
    }
    err = node_rule_api_clear_bundle();
    if (err != ESP_OK) {
        return err;
    }
    if (!ensure_admin_scratch()) {
        return ESP_ERR_NO_MEM;
    }

    memset(&s_config_scratch, 0, sizeof(s_config_scratch));
    node_config_set_factory_defaults(&s_config_scratch);
    err = node_board_apply_factory_pin_config(&s_config_scratch);
    if (err != ESP_OK) {
        return err;
    }

    if (!lock_config()) {
        return ESP_ERR_TIMEOUT;
    }
    *s_live_config = s_config_scratch;
    unlock_config();
    return ESP_OK;
}

static esp_err_t handle_apply_rules(const node_admin_request_t *request)
{
    esp_err_t err = ESP_OK;

    if (!request || !request->rule_bundle_json) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ensure_admin_scratch()) {
        return ESP_ERR_NO_MEM;
    }

    memset(&s_config_scratch, 0, sizeof(s_config_scratch));
    err = copy_live_config(&s_config_scratch);
    if (err != ESP_OK) {
        return err;
    }

    return node_rule_api_apply_bundle_for_config(request->rule_bundle_json,
                                                 &s_config_scratch,
                                                 request->rule_metadata,
                                                 request->error_code,
                                                 request->error_code_size);
}

static esp_err_t handle_validate_rules(const node_admin_request_t *request)
{
    esp_err_t err = ESP_OK;

    if (!request || !request->rule_bundle_json) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ensure_admin_scratch()) {
        return ESP_ERR_NO_MEM;
    }

    memset(&s_config_scratch, 0, sizeof(s_config_scratch));
    err = copy_live_config(&s_config_scratch);
    if (err != ESP_OK) {
        return err;
    }

    return node_rule_api_validate_bundle_for_config(request->rule_bundle_json,
                                                    &s_config_scratch,
                                                    request->rule_metadata,
                                                    request->error_code,
                                                    request->error_code_size);
}

static esp_err_t handle_clear_rules(void)
{
    return node_rule_api_clear_bundle();
}

static esp_err_t handle_pause_rules(void)
{
    return node_rule_api_pause();
}

static esp_err_t handle_resume_rules(void)
{
    return node_rule_api_resume();
}

static esp_err_t handle_reinit_nfc(void)
{
    return node_driver_nfc_reader_runtime_reinit();
}

static void node_admin_task(void *arg)
{
    (void)arg;
    while (true) {
        node_admin_request_t request = {0};
        esp_err_t err = ESP_FAIL;
        bool restart_required = false;
        bool applied = false;
        bool restarting = false;

        if (xQueueReceive(s_request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (request.type) {
        case NODE_ADMIN_REQ_SAVE_BASE:
            err = handle_save_base(&request);
            restart_required = err == ESP_OK;
            break;
        case NODE_ADMIN_REQ_SAVE_LED:
            err = handle_save_led(&request);
            applied = err == ESP_OK;
            break;
        case NODE_ADMIN_REQ_SAVE_NFC_CARDS:
            err = handle_save_nfc_cards(&request);
            applied = err == ESP_OK;
            break;
        case NODE_ADMIN_REQ_RESET_WIFI:
            err = handle_reset_wifi();
            restart_required = err == ESP_OK;
            break;
        case NODE_ADMIN_REQ_FACTORY_RESET:
            err = handle_factory_reset();
            restart_required = err == ESP_OK;
            break;
        case NODE_ADMIN_REQ_RESTART:
            err = ESP_OK;
            restarting = true;
            break;
        case NODE_ADMIN_REQ_VALIDATE_RULES:
            err = handle_validate_rules(&request);
            break;
        case NODE_ADMIN_REQ_APPLY_RULES:
            err = handle_apply_rules(&request);
            break;
        case NODE_ADMIN_REQ_CLEAR_RULES:
            err = handle_clear_rules();
            break;
        case NODE_ADMIN_REQ_PAUSE_RULES:
            err = handle_pause_rules();
            applied = err == ESP_OK;
            break;
        case NODE_ADMIN_REQ_RESUME_RULES:
            err = handle_resume_rules();
            applied = err == ESP_OK;
            break;
        case NODE_ADMIN_REQ_REINIT_NFC:
            err = handle_reinit_nfc();
            applied = err == ESP_OK;
            break;
        default:
            err = ESP_ERR_INVALID_ARG;
            break;
        }

        write_result(request.result, err, restart_required, applied, restarting);
        if (request.reply_task) {
            xTaskNotifyGive(request.reply_task);
        }

        if (request.type == NODE_ADMIN_REQ_RESTART && err == ESP_OK) {
            ESP_LOGW(TAG, "restart source=provisioning_api reason=explicit_restart");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
    }
}

static esp_err_t submit_request(node_admin_request_t *request)
{
    if (!request || !s_request_queue || !s_task) {
        return ESP_ERR_INVALID_STATE;
    }

    request->reply_task = xTaskGetCurrentTaskHandle();
    if (xQueueSend(s_request_queue, request, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) == 0) {
        return ESP_ERR_TIMEOUT;
    }
    return request->result ? request->result->err : ESP_OK;
}

esp_err_t node_admin_control_init(node_config_t *live_config)
{
    if (!live_config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ensure_admin_scratch()) {
        return ESP_ERR_NO_MEM;
    }

    if (!s_config_mutex) {
        s_config_mutex = xSemaphoreCreateMutexStatic(&s_config_mutex_storage);
    }
    if (!s_request_queue) {
        s_request_queue = xQueueCreateStatic(2,
                                             sizeof(node_admin_request_t),
                                             s_request_queue_buffer,
                                             &s_request_queue_storage);
    }
    if (!s_task) {
        StackType_t *task_stack = allocate_admin_task_stack();

        if (!task_stack) {
            return ESP_ERR_NO_MEM;
        }
        s_task = xTaskCreateStatic(node_admin_task,
                                   "node_admin",
                                   4096,
                                   NULL,
                                   tskIDLE_PRIORITY + 2,
                                   task_stack,
                                   &s_task_storage);
    }
    if (!s_config_mutex || !s_request_queue || !s_task) {
        return ESP_ERR_NO_MEM;
    }

    s_live_config = live_config;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t node_admin_control_get_config(node_config_t *out_config)
{
    if (!s_initialized || !s_live_config || !out_config) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lock_config()) {
        return ESP_ERR_TIMEOUT;
    }
    *out_config = *s_live_config;
    unlock_config();
    return ESP_OK;
}

esp_err_t node_admin_control_save_base(const node_config_t *config, node_admin_control_result_t *out_result)
{
    node_admin_request_t request = {
        .type = NODE_ADMIN_REQ_SAVE_BASE,
        .config = config,
        .result = out_result,
    };

    if (out_result) {
        write_result(out_result, ESP_ERR_INVALID_STATE, false, false, false);
    }
    return submit_request(&request);
}

esp_err_t node_admin_control_save_led(const node_led_strip_config_t *led_strips,
                                      size_t count,
                                      node_admin_control_result_t *out_result)
{
    node_admin_request_t request = {
        .type = NODE_ADMIN_REQ_SAVE_LED,
        .led_strips = led_strips,
        .led_count = count,
        .result = out_result,
    };

    if (out_result) {
        write_result(out_result, ESP_ERR_INVALID_STATE, false, false, false);
    }
    return submit_request(&request);
}

esp_err_t node_admin_control_save_nfc_cards(const node_nfc_known_card_t *cards,
                                            size_t count,
                                            node_admin_control_result_t *out_result)
{
    node_admin_request_t request = {
        .type = NODE_ADMIN_REQ_SAVE_NFC_CARDS,
        .nfc_cards = cards,
        .nfc_card_count = count,
        .result = out_result,
    };

    if (out_result) {
        write_result(out_result, ESP_ERR_INVALID_STATE, false, false, false);
    }
    return submit_request(&request);
}

esp_err_t node_admin_control_reset_wifi(node_admin_control_result_t *out_result)
{
    node_admin_request_t request = {
        .type = NODE_ADMIN_REQ_RESET_WIFI,
        .result = out_result,
    };

    if (out_result) {
        write_result(out_result, ESP_ERR_INVALID_STATE, false, false, false);
    }
    return submit_request(&request);
}

esp_err_t node_admin_control_factory_reset(node_admin_control_result_t *out_result)
{
    node_admin_request_t request = {
        .type = NODE_ADMIN_REQ_FACTORY_RESET,
        .result = out_result,
    };

    if (out_result) {
        write_result(out_result, ESP_ERR_INVALID_STATE, false, false, false);
    }
    return submit_request(&request);
}

esp_err_t node_admin_control_restart(node_admin_control_result_t *out_result)
{
    node_admin_request_t request = {
        .type = NODE_ADMIN_REQ_RESTART,
        .result = out_result,
    };

    if (out_result) {
        write_result(out_result, ESP_ERR_INVALID_STATE, false, false, false);
    }
    return submit_request(&request);
}

esp_err_t node_admin_control_validate_rules(const char *raw_json,
                                            node_rule_bundle_metadata_t *out_metadata,
                                            char *out_error_code,
                                            size_t out_error_code_size,
                                            node_admin_control_result_t *out_result)
{
    node_admin_request_t request = {
        .type = NODE_ADMIN_REQ_VALIDATE_RULES,
        .rule_bundle_json = raw_json,
        .rule_metadata = out_metadata,
        .error_code = out_error_code,
        .error_code_size = out_error_code_size,
        .result = out_result,
    };

    if (out_result) {
        write_result(out_result, ESP_ERR_INVALID_STATE, false, false, false);
    }
    if (out_error_code && out_error_code_size > 0) {
        out_error_code[0] = '\0';
    }
    if (out_metadata) {
        memset(out_metadata, 0, sizeof(*out_metadata));
    }
    return submit_request(&request);
}

esp_err_t node_admin_control_apply_rules(const char *raw_json,
                                         node_rule_bundle_metadata_t *out_metadata,
                                         char *out_error_code,
                                         size_t out_error_code_size,
                                         node_admin_control_result_t *out_result)
{
    node_admin_request_t request = {
        .type = NODE_ADMIN_REQ_APPLY_RULES,
        .rule_bundle_json = raw_json,
        .rule_metadata = out_metadata,
        .error_code = out_error_code,
        .error_code_size = out_error_code_size,
        .result = out_result,
    };

    if (out_result) {
        write_result(out_result, ESP_ERR_INVALID_STATE, false, false, false);
    }
    if (out_error_code && out_error_code_size > 0) {
        out_error_code[0] = '\0';
    }
    if (out_metadata) {
        memset(out_metadata, 0, sizeof(*out_metadata));
    }
    return submit_request(&request);
}

esp_err_t node_admin_control_get_rules(node_rule_store_entry_t *out_entry)
{
    if (!s_initialized || !out_entry) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(out_entry, 0, sizeof(*out_entry));
    return node_rule_api_get_bundle(out_entry);
}

esp_err_t node_admin_control_clear_rules(node_admin_control_result_t *out_result)
{
    node_admin_request_t request = {
        .type = NODE_ADMIN_REQ_CLEAR_RULES,
        .result = out_result,
    };

    if (out_result) {
        write_result(out_result, ESP_ERR_INVALID_STATE, false, false, false);
    }
    return submit_request(&request);
}

esp_err_t node_admin_control_pause_rules(node_admin_control_result_t *out_result)
{
    node_admin_request_t request = {
        .type = NODE_ADMIN_REQ_PAUSE_RULES,
        .result = out_result,
    };

    if (out_result) {
        write_result(out_result, ESP_ERR_INVALID_STATE, false, false, false);
    }
    return submit_request(&request);
}

esp_err_t node_admin_control_resume_rules(node_admin_control_result_t *out_result)
{
    node_admin_request_t request = {
        .type = NODE_ADMIN_REQ_RESUME_RULES,
        .result = out_result,
    };

    if (out_result) {
        write_result(out_result, ESP_ERR_INVALID_STATE, false, false, false);
    }
    return submit_request(&request);
}

esp_err_t node_admin_control_reinit_nfc(node_admin_control_result_t *out_result)
{
    node_admin_request_t request = {
        .type = NODE_ADMIN_REQ_REINIT_NFC,
        .result = out_result,
    };

    if (out_result) {
        write_result(out_result, ESP_ERR_INVALID_STATE, false, false, false);
    }
    return submit_request(&request);
}
