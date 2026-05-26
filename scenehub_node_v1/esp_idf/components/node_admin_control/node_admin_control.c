#include "node_admin_control.h"

#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "node_board.h"
#include "node_control.h"

static const char *TAG = "node_admin_ctl";

typedef enum {
    NODE_ADMIN_REQ_SAVE_BASE = 0,
    NODE_ADMIN_REQ_SAVE_LED,
    NODE_ADMIN_REQ_RESET_WIFI,
    NODE_ADMIN_REQ_FACTORY_RESET,
    NODE_ADMIN_REQ_RESTART,
} node_admin_request_type_t;

typedef struct {
    node_admin_request_type_t type;
    const node_config_t *config;
    const node_led_strip_config_t *led_strips;
    size_t led_count;
    node_admin_control_result_t *result;
    TaskHandle_t reply_task;
} node_admin_request_t;

static node_config_t *s_live_config;
static StaticSemaphore_t s_config_mutex_storage;
static SemaphoreHandle_t s_config_mutex;
static StaticQueue_t s_request_queue_storage;
static uint8_t s_request_queue_buffer[2 * sizeof(node_admin_request_t)];
static QueueHandle_t s_request_queue;
static StaticTask_t s_task_storage;
static StackType_t s_task_stack[4096];
static TaskHandle_t s_task;
static bool s_initialized;

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
    node_config_t factory_config;
    esp_err_t err = node_config_factory_reset();

    if (err != ESP_OK) {
        return err;
    }

    node_config_set_factory_defaults(&factory_config);
    err = node_board_apply_factory_pin_config(&factory_config);
    if (err != ESP_OK) {
        return err;
    }

    if (!lock_config()) {
        return ESP_ERR_TIMEOUT;
    }
    *s_live_config = factory_config;
    unlock_config();
    return ESP_OK;
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
        default:
            err = ESP_ERR_INVALID_ARG;
            break;
        }

        write_result(request.result, err, restart_required, applied, restarting);
        if (request.reply_task) {
            xTaskNotifyGive(request.reply_task);
        }

        if (request.type == NODE_ADMIN_REQ_RESTART && err == ESP_OK) {
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
        s_task = xTaskCreateStatic(node_admin_task,
                                   "node_admin",
                                   sizeof(s_task_stack) / sizeof(s_task_stack[0]),
                                   NULL,
                                   tskIDLE_PRIORITY + 2,
                                   s_task_stack,
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
