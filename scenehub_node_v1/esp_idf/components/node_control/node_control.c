#include "node_control.h"
#include "node_control_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "node_hardware_io.h"
#include "node_rule_api.h"
#include "sdkconfig.h"

typedef enum {
    NODE_CONTROL_REQ_COMMAND = 0,
    NODE_CONTROL_REQ_UPDATE_LED_CONFIG,
} node_control_request_kind_t;

typedef struct {
    const char *request_id;
    const char *command;
    const char *args_json;
    node_control_command_source_t source;
    node_control_result_t *result_out;
} node_control_command_payload_t;

typedef enum {
    NODE_CONTROL_REQ_SLOT_FREE = 0,
    NODE_CONTROL_REQ_SLOT_QUEUED,
    NODE_CONTROL_REQ_SLOT_COPYING,
    NODE_CONTROL_REQ_SLOT_PROCESSING,
    NODE_CONTROL_REQ_SLOT_COMPLETED,
    NODE_CONTROL_REQ_SLOT_CANCELLED,
    NODE_CONTROL_REQ_SLOT_ABANDONED,
} node_control_request_slot_state_t;

typedef struct {
    node_control_request_kind_t kind;
    node_control_request_slot_state_t state;
    TaskHandle_t reply_task;
    esp_err_t err;
    union {
        node_control_command_payload_t command;
        struct {
            node_led_strip_config_t led_strips[NODE_LED_STRIP_MAX];
            size_t count;
        } led_config;
    } payload;
} node_control_request_t;

enum {
    NODE_CONTROL_QUEUE_LEN = 8,
    NODE_CONTROL_TASK_STACK_WORDS = 3072,
    NODE_CONTROL_REPLY_TIMEOUT_MS = 2000,
};

node_config_t g_node_control_config;
static bool s_initialized;
static StaticQueue_t s_request_queue_storage;
static uint8_t s_request_queue_buffer[NODE_CONTROL_QUEUE_LEN * sizeof(node_control_request_t *)];
static QueueHandle_t s_request_queue;
static node_control_request_t s_request_slots[NODE_CONTROL_QUEUE_LEN];
static StaticSemaphore_t s_request_slot_lock_storage;
static SemaphoreHandle_t s_request_slot_lock;
static StaticTask_t s_task_storage;
static StackType_t s_task_stack[NODE_CONTROL_TASK_STACK_WORDS];
static TaskHandle_t s_task;
static char s_command_request_id[48];
static char s_command_name[64];
static char *s_command_args_json;
static node_control_result_t *s_command_result_scratch;

static bool ensure_request_owner(void);
static esp_err_t node_control_execute_inline(const node_control_command_t *command, node_control_result_t *out_result);
static esp_err_t node_control_update_led_config_inline(const node_led_strip_config_t *led_strips, size_t count);
static bool copy_text_checked(char *dst, size_t dst_size, const char *src);
static node_control_request_t *acquire_request_slot(void);
static void release_request_slot(node_control_request_t *request);
static bool prepare_command_request(node_control_request_t *request,
                                    const node_control_command_t *command,
                                    node_control_result_t *out_result);
static bool prepare_command_scratch(const node_control_request_t *request, node_control_command_t *out_command);
static void *alloc_control_scratch(size_t size);

void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

const char *command_source_text(node_control_command_source_t source)
{
    switch (source) {
    case NODE_CONTROL_SOURCE_HUB:
        return "hub";
    case NODE_CONTROL_SOURCE_LOCAL_PREVIEW:
        return "preview";
    case NODE_CONTROL_SOURCE_LOCAL_UI:
        return "local_ui";
    case NODE_CONTROL_SOURCE_LOCAL_RULE:
        return "local_rule";
    case NODE_CONTROL_SOURCE_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *command_request_id_text(const node_control_command_t *command)
{
    return (command && command->request_id && command->request_id[0]) ? command->request_id : "-";
}

const char *command_source_safe(const node_control_command_t *command)
{
    return command_source_text(command ? command->source : NODE_CONTROL_SOURCE_UNKNOWN);
}

void result_done(node_control_result_t *result)
{
    copy_text(result->status, sizeof(result->status), "done");
    result->error_code[0] = '\0';
}

void result_started(node_control_result_t *result)
{
    copy_text(result->status, sizeof(result->status), "started");
    result->error_code[0] = '\0';
}

esp_err_t result_rejected(node_control_result_t *result, const char *code)
{
    copy_text(result->status, sizeof(result->status), "rejected");
    copy_text(result->error_code, sizeof(result->error_code), code);
    return ESP_ERR_INVALID_ARG;
}

static bool node_control_is_owner_task(void)
{
    return s_task && xTaskGetCurrentTaskHandle() == s_task;
}

static bool copy_text_checked(char *dst, size_t dst_size, const char *src)
{
    int written = 0;

    if (!dst || dst_size == 0) {
        return false;
    }
    written = snprintf(dst, dst_size, "%s", src ? src : "");
    return written >= 0 && written < (int)dst_size;
}

static void *alloc_control_scratch(size_t size)
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

static node_control_request_t *acquire_request_slot(void)
{
    node_control_request_t *request = NULL;

    if (!s_request_slot_lock) {
        return NULL;
    }
    xSemaphoreTake(s_request_slot_lock, portMAX_DELAY);
    for (size_t i = 0; i < NODE_CONTROL_QUEUE_LEN; ++i) {
        if (s_request_slots[i].state != NODE_CONTROL_REQ_SLOT_FREE) {
            continue;
        }
        request = &s_request_slots[i];
        memset(request, 0, sizeof(*request));
        request->state = NODE_CONTROL_REQ_SLOT_QUEUED;
        break;
    }
    xSemaphoreGive(s_request_slot_lock);
    return request;
}

static void release_request_slot(node_control_request_t *request)
{
    if (!request || !s_request_slot_lock) {
        return;
    }
    xSemaphoreTake(s_request_slot_lock, portMAX_DELAY);
    memset(request, 0, sizeof(*request));
    request->state = NODE_CONTROL_REQ_SLOT_FREE;
    xSemaphoreGive(s_request_slot_lock);
}

static bool prepare_command_request(node_control_request_t *request,
                                    const node_control_command_t *command,
                                    node_control_result_t *out_result)
{
    if (!request || !command || !command->command) {
        return false;
    }
    request->kind = NODE_CONTROL_REQ_COMMAND;
    request->payload.command.request_id = command->request_id;
    request->payload.command.command = command->command;
    request->payload.command.args_json = command->args_json;
    request->payload.command.source = command->source;
    request->payload.command.result_out = out_result;
    return true;
}

static bool prepare_command_scratch(const node_control_request_t *request, node_control_command_t *out_command)
{
    if (!request || !out_command || request->kind != NODE_CONTROL_REQ_COMMAND) {
        return false;
    }
    if (!copy_text_checked(s_command_request_id,
                           sizeof(s_command_request_id),
                           request->payload.command.request_id) ||
        !copy_text_checked(s_command_name,
                           sizeof(s_command_name),
                           request->payload.command.command) ||
        !s_command_args_json ||
        !copy_text_checked(s_command_args_json,
                           NODE_MQTT_PAYLOAD_MAX_LEN,
                           request->payload.command.args_json)) {
        return false;
    }

    memset(out_command, 0, sizeof(*out_command));
    out_command->request_id = s_command_request_id;
    out_command->command = s_command_name;
    out_command->args_json = s_command_args_json;
    out_command->source = request->payload.command.source;
    return true;
}

static esp_err_t submit_request_and_wait(node_control_request_t *request)
{
    if (!request || !s_request_queue || !s_task) {
        return ESP_ERR_INVALID_STATE;
    }

    request->reply_task = xTaskGetCurrentTaskHandle();
    request->err = ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_request_queue, &request, pdMS_TO_TICKS(1000)) != pdTRUE) {
        release_request_slot(request);
        return ESP_ERR_TIMEOUT;
    }

    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NODE_CONTROL_REPLY_TIMEOUT_MS)) == 0) {
        while (true) {
            node_control_request_slot_state_t state = NODE_CONTROL_REQ_SLOT_FREE;
            esp_err_t err = ESP_ERR_TIMEOUT;

            xSemaphoreTake(s_request_slot_lock, portMAX_DELAY);
            state = request->state;

            if (state == NODE_CONTROL_REQ_SLOT_COMPLETED) {
                err = request->err;
                memset(request, 0, sizeof(*request));
                request->state = NODE_CONTROL_REQ_SLOT_FREE;
                xSemaphoreGive(s_request_slot_lock);
                (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
                return err;
            }
            if (state == NODE_CONTROL_REQ_SLOT_QUEUED) {
                request->state = NODE_CONTROL_REQ_SLOT_CANCELLED;
                xSemaphoreGive(s_request_slot_lock);
                return ESP_ERR_TIMEOUT;
            }
            if (state == NODE_CONTROL_REQ_SLOT_PROCESSING) {
                request->state = NODE_CONTROL_REQ_SLOT_ABANDONED;
                xSemaphoreGive(s_request_slot_lock);
                return ESP_ERR_TIMEOUT;
            }
            if (state == NODE_CONTROL_REQ_SLOT_COPYING) {
                xSemaphoreGive(s_request_slot_lock);
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
            xSemaphoreGive(s_request_slot_lock);
            return ESP_ERR_TIMEOUT;
        }
    }

    {
        esp_err_t err = request->err;

        release_request_slot(request);
        return err;
    }
}

static void control_task(void *arg)
{
    (void)arg;

    while (true) {
        node_control_request_t *request = NULL;
        node_control_command_t command = {0};
        esp_err_t err = ESP_ERR_INVALID_STATE;

        if (xQueueReceive(s_request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!request) {
            continue;
        }

        xSemaphoreTake(s_request_slot_lock, portMAX_DELAY);
        if (request->kind == NODE_CONTROL_REQ_COMMAND) {
            if (request->state == NODE_CONTROL_REQ_SLOT_CANCELLED) {
                memset(request, 0, sizeof(*request));
                request->state = NODE_CONTROL_REQ_SLOT_FREE;
                xSemaphoreGive(s_request_slot_lock);
                continue;
            }
            request->state = NODE_CONTROL_REQ_SLOT_COPYING;
            xSemaphoreGive(s_request_slot_lock);
            if (!prepare_command_scratch(request, &command)) {
                request->err = ESP_ERR_INVALID_SIZE;
                xSemaphoreTake(s_request_slot_lock, portMAX_DELAY);
                if (request->state == NODE_CONTROL_REQ_SLOT_ABANDONED) {
                    memset(request, 0, sizeof(*request));
                    request->state = NODE_CONTROL_REQ_SLOT_FREE;
                    xSemaphoreGive(s_request_slot_lock);
                    continue;
                }
                if (request->payload.command.result_out) {
                    result_rejected(request->payload.command.result_out, "command_too_large");
                }
                request->state = NODE_CONTROL_REQ_SLOT_COMPLETED;
                TaskHandle_t reply = request->reply_task;
                xSemaphoreGive(s_request_slot_lock);
                if (reply) {
                    xTaskNotifyGive(reply);
                }
                continue;
            }
            xSemaphoreTake(s_request_slot_lock, portMAX_DELAY);
            request->state = NODE_CONTROL_REQ_SLOT_PROCESSING;
            xSemaphoreGive(s_request_slot_lock);
        } else {
            if (request->state == NODE_CONTROL_REQ_SLOT_CANCELLED) {
                memset(request, 0, sizeof(*request));
                request->state = NODE_CONTROL_REQ_SLOT_FREE;
                xSemaphoreGive(s_request_slot_lock);
                continue;
            }
            request->state = NODE_CONTROL_REQ_SLOT_PROCESSING;
            xSemaphoreGive(s_request_slot_lock);
        }

        switch (request->kind) {
        case NODE_CONTROL_REQ_COMMAND:
            if (!s_command_result_scratch) {
                err = ESP_ERR_NO_MEM;
                break;
            }
            memset(s_command_result_scratch, 0, sizeof(*s_command_result_scratch));
            err = node_control_execute_inline(&command, s_command_result_scratch);
            break;
        case NODE_CONTROL_REQ_UPDATE_LED_CONFIG:
            err = node_control_update_led_config_inline(request->payload.led_config.led_strips,
                                                        request->payload.led_config.count);
            break;
        default:
            err = ESP_ERR_INVALID_ARG;
            break;
        }

        request->err = err;
        xSemaphoreTake(s_request_slot_lock, portMAX_DELAY);
        if (request->state == NODE_CONTROL_REQ_SLOT_ABANDONED) {
            memset(request, 0, sizeof(*request));
            request->state = NODE_CONTROL_REQ_SLOT_FREE;
            xSemaphoreGive(s_request_slot_lock);
            continue;
        }
        if (request->kind == NODE_CONTROL_REQ_COMMAND &&
            request->payload.command.result_out &&
            s_command_result_scratch) {
            *request->payload.command.result_out = *s_command_result_scratch;
        }
        request->state = NODE_CONTROL_REQ_SLOT_COMPLETED;
        TaskHandle_t reply = request->reply_task;
        xSemaphoreGive(s_request_slot_lock);
        if (reply) {
            xTaskNotifyGive(reply);
        }
    }
}

static bool ensure_request_owner(void)
{
    if (!s_request_queue) {
        s_request_queue = xQueueCreateStatic(NODE_CONTROL_QUEUE_LEN,
                                             sizeof(node_control_request_t *),
                                             s_request_queue_buffer,
                                             &s_request_queue_storage);
    }
    if (!s_request_slot_lock) {
        s_request_slot_lock = xSemaphoreCreateMutexStatic(&s_request_slot_lock_storage);
    }
    if (!s_command_args_json) {
        s_command_args_json = (char *)alloc_control_scratch(NODE_MQTT_PAYLOAD_MAX_LEN);
    }
    if (!s_command_result_scratch) {
        s_command_result_scratch =
            (node_control_result_t *)alloc_control_scratch(sizeof(*s_command_result_scratch));
    }
    if (s_request_queue && !s_task) {
        s_task = xTaskCreateStatic(control_task,
                                   "node_control",
                                   NODE_CONTROL_TASK_STACK_WORDS,
                                   NULL,
                                   tskIDLE_PRIORITY + 2,
                                   s_task_stack,
                                   &s_task_storage);
    }
    return s_request_queue && s_request_slot_lock && s_command_args_json &&
           s_command_result_scratch && s_task;
}

esp_err_t node_control_init(const node_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ensure_request_owner()) {
        return ESP_ERR_NO_MEM;
    }
    g_node_control_config = *config;
    s_initialized = true;
    return ESP_OK;
}

static esp_err_t node_control_update_led_config_inline(const node_led_strip_config_t *led_strips, size_t count)
{
    if (!led_strips || count > NODE_LED_STRIP_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < count; ++i) {
        g_node_control_config.led_strips[i].blink = led_strips[i].blink;
        g_node_control_config.led_strips[i].breathe = led_strips[i].breathe;
        g_node_control_config.led_strips[i].effects = led_strips[i].effects;
    }
    return ESP_OK;
}

esp_err_t node_control_update_led_config(const node_led_strip_config_t *led_strips, size_t count)
{
    node_control_request_t *request = NULL;

    if (!led_strips || count > NODE_LED_STRIP_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ensure_request_owner()) {
        return ESP_ERR_NO_MEM;
    }
    if (node_control_is_owner_task()) {
        return node_control_update_led_config_inline(led_strips, count);
    }

    request = acquire_request_slot();
    if (!request) {
        return ESP_ERR_NO_MEM;
    }
    request->kind = NODE_CONTROL_REQ_UPDATE_LED_CONFIG;
    memcpy(request->payload.led_config.led_strips,
           led_strips,
           count * sizeof(request->payload.led_config.led_strips[0]));
    request->payload.led_config.count = count;
    return submit_request_and_wait(request);
}

static esp_err_t node_control_execute_inline(const node_control_command_t *command, node_control_result_t *out_result)
{
    if (!command || !command->command || !out_result) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_result, 0, sizeof(*out_result));

    if (strcmp(command->command, "node.get_status") == 0) {
        return execute_get_status(out_result);
    }
    if (strcmp(command->command, "node.identify") == 0) {
        esp_err_t err = node_hardware_io_identify(150, 2);
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            return result_rejected(out_result, "internal_error");
        }
        result_done(out_result);
        return ESP_OK;
    }
    if (strcmp(command->command, "describe_interface") == 0) {
        return execute_describe_interface(out_result);
    }
    if (strcmp(command->command, "relay.set") == 0) {
        return execute_output_set(NODE_HW_OUTPUT_RELAY, command->args_json, out_result);
    }
    if (strcmp(command->command, "relay.pulse") == 0) {
        return execute_output_pulse(NODE_HW_OUTPUT_RELAY, command->args_json, out_result);
    }
    if (strcmp(command->command, "relay.effect") == 0) {
        return execute_relay_effect(command->args_json, out_result);
    }
    if (strcmp(command->command, "relay.all_off") == 0) {
        return execute_output_all_off(NODE_HW_OUTPUT_RELAY, out_result, "relay_all_off_failed");
    }
    if (strcmp(command->command, "mosfet.set") == 0) {
        return execute_mosfet_set(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.fade") == 0) {
        return execute_mosfet_fade(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.pulse") == 0) {
        return execute_mosfet_pulse(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.blink") == 0) {
        return execute_mosfet_blink(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.breathe") == 0) {
        return execute_mosfet_breathe(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.all_off") == 0) {
        return execute_mosfet_all_off(out_result);
    }
    if (strcmp(command->command, "mosfet.effect") == 0) {
        return execute_mosfet_effect_alias(command->args_json, out_result);
    }
    if (strcmp(command->command, "io.set") == 0) {
        return execute_output_set(NODE_HW_OUTPUT_UNIVERSAL_IO, command->args_json, out_result);
    }
    if (strcmp(command->command, "io.all_off") == 0) {
        return execute_output_all_off(NODE_HW_OUTPUT_UNIVERSAL_IO, out_result, "io_all_off_failed");
    }
    if (strcmp(command->command, "node.all_off") == 0) {
        return execute_node_all_off(out_result);
    }
    if (strcmp(command->command, "led.off") == 0) {
        return execute_led_off(command, out_result);
    }
    if (strcmp(command->command, "led.solid") == 0) {
        return execute_led_solid(command, out_result);
    }
    if (strcmp(command->command, "led.blink") == 0) {
        return execute_led_blink(command, out_result, false);
    }
    if (strcmp(command->command, "led.breathe") == 0) {
        return execute_led_breathe(command, out_result, false);
    }
    if (strcmp(command->command, "led.effect") == 0) {
        return execute_led_effect(command, out_result, false);
    }
    if (strcmp(command->command, "led.preview.blink") == 0) {
        return execute_led_blink(command, out_result, true);
    }
    if (strcmp(command->command, "led.preview.breathe") == 0) {
        return execute_led_breathe(command, out_result, true);
    }
    if (strcmp(command->command, "led.preview.effect") == 0) {
        return execute_led_effect(command, out_result, true);
    }
    if (command->source == NODE_CONTROL_SOURCE_HUB ||
        command->source == NODE_CONTROL_SOURCE_LOCAL_UI) {
        esp_err_t err = node_rule_api_dispatch_mqtt_command(command->command);
        if (err == ESP_OK) {
            result_done(out_result);
            return ESP_OK;
        }
        if (err == ESP_ERR_INVALID_STATE) {
            return result_rejected(out_result, "rules_inactive");
        }
    }
    return result_rejected(out_result, "not_supported");
}

esp_err_t node_control_submit(const node_control_command_t *command, node_control_result_t *out_result)
{
    node_control_request_t *request = NULL;

    if (!command || !command->command || !out_result) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ensure_request_owner()) {
        return ESP_ERR_NO_MEM;
    }
    if (node_control_is_owner_task()) {
        return node_control_execute_inline(command, out_result);
    }

    request = acquire_request_slot();
    if (!request) {
        return ESP_ERR_NO_MEM;
    }
    if (!prepare_command_request(request, command, out_result)) {
        release_request_slot(request);
        return result_rejected(out_result, "command_too_large");
    }
    return submit_request_and_wait(request);
}

esp_err_t node_control_execute(const node_control_command_t *command, node_control_result_t *out_result)
{
    // Compatibility alias for older internal code. Prefer node_control_submit().
    return node_control_submit(command, out_result);
}
