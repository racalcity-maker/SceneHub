#include "node_action_router.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "node_limits.h"
#include "node_control.h"
#include "node_mqtt_transport.h"
#include "node_rule_action_port.h"
#include "sdkconfig.h"

static const char *TAG = "node_action_router";

enum {
    NODE_ACTION_ROUTER_PUBLISH_QUEUE_LEN = 8,
    NODE_ACTION_ROUTER_EVENT_JSON_MAX = 128,
};

typedef enum {
    NODE_ACTION_ROUTER_PUBLISH_EVENT = 0,
    NODE_ACTION_ROUTER_PUBLISH_INPUT_CHANGE,
} node_action_router_publish_kind_t;

typedef struct {
    node_action_router_publish_kind_t kind;
    uint8_t channel;
    int32_t value;
    char event_name[NODE_DRIVER_EVENT_NAME_MAX_LEN];
    char args_json[NODE_ACTION_ROUTER_EVENT_JSON_MAX];
} node_action_router_publish_request_t;

static StaticQueue_t s_publish_queue_storage;
static uint8_t s_publish_queue_buffer[NODE_ACTION_ROUTER_PUBLISH_QUEUE_LEN *
                                      sizeof(node_action_router_publish_request_t)];
static QueueHandle_t s_publish_queue;
static StaticTask_t s_publish_task_storage;
static StackType_t *s_publish_task_stack;
static TaskHandle_t s_publish_task;
static node_control_result_t s_rule_result;
static bool s_port_bound;

static StackType_t *allocate_publish_task_stack(void)
{
    const size_t stack_words = 3072U;
    const size_t stack_bytes = stack_words * sizeof(StackType_t);

    if (s_publish_task_stack) {
        return s_publish_task_stack;
    }
#if CONFIG_SPIRAM
    s_publish_task_stack = (StackType_t *)heap_caps_malloc(stack_bytes,
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_publish_task_stack) {
        return s_publish_task_stack;
    }
#endif
    s_publish_task_stack = (StackType_t *)heap_caps_malloc(stack_bytes, MALLOC_CAP_8BIT);
    return s_publish_task_stack;
}

static void publish_task(void *arg)
{
    (void)arg;

    while (true) {
        node_action_router_publish_request_t request = {0};
        esp_err_t err = ESP_OK;

        if (xQueueReceive(s_publish_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (request.kind) {
        case NODE_ACTION_ROUTER_PUBLISH_EVENT:
            err = node_mqtt_transport_publish_event(request.event_name, request.args_json);
            break;
        case NODE_ACTION_ROUTER_PUBLISH_INPUT_CHANGE:
            err = node_mqtt_transport_publish_input_change(request.channel, request.value);
            break;
        default:
            err = ESP_ERR_INVALID_ARG;
            break;
        }

        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "publish request failed kind=%d err=%s", (int)request.kind, esp_err_to_name(err));
        }
    }
}

static bool ensure_publish_owner(void)
{
    if (!s_publish_queue) {
        s_publish_queue = xQueueCreateStatic(NODE_ACTION_ROUTER_PUBLISH_QUEUE_LEN,
                                             sizeof(node_action_router_publish_request_t),
                                             s_publish_queue_buffer,
                                             &s_publish_queue_storage);
    }
    if (s_publish_queue && !s_publish_task) {
        StackType_t *task_stack = allocate_publish_task_stack();
        if (!task_stack) {
            return false;
        }
        s_publish_task = xTaskCreateStatic(publish_task,
                                           "node_evt_pub",
                                           3072U,
                                           NULL,
                                           tskIDLE_PRIORITY + 1,
                                           task_stack,
                                           &s_publish_task_storage);
    }
    return s_publish_queue && s_publish_task;
}

static esp_err_t enqueue_publish_request(const node_action_router_publish_request_t *request)
{
    if (!request) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ensure_publish_owner()) {
        return ESP_ERR_NO_MEM;
    }
    if (xQueueSend(s_publish_queue, request, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t execute_command_binding(const char *command, const char *args_json)
{
    node_control_command_t control = {0};
    esp_err_t err = ESP_OK;

    if (!command || command[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    control.request_id = "rule_engine";
    control.command = command;
    control.args_json = args_json;
    control.source = NODE_CONTROL_SOURCE_LOCAL_RULE;

    memset(&s_rule_result, 0, sizeof(s_rule_result));
    err = node_control_submit(&control, &s_rule_result);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "rule command rejected command=%s status=%s error=%s err=%s",
                 command,
                 s_rule_result.status,
                 s_rule_result.error_code,
                 esp_err_to_name(err));
        return err;
    }
    if (strcmp(s_rule_result.status, "rejected") == 0) {
        ESP_LOGW(TAG,
                 "rule command rejected command=%s error=%s",
                 command,
                 s_rule_result.error_code);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t emit_event_binding(const char *event_name, const char *args_json)
{
    node_action_router_publish_request_t request = {0};
    int written = 0;

    if (!event_name || event_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_mqtt_transport_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    request.kind = NODE_ACTION_ROUTER_PUBLISH_EVENT;
    written = snprintf(request.event_name, sizeof(request.event_name), "%s", event_name);
    if (written < 0 || written >= (int)sizeof(request.event_name)) {
        return ESP_ERR_INVALID_SIZE;
    }
    written = snprintf(request.args_json,
                       sizeof(request.args_json),
                       "%s",
                       (args_json && args_json[0]) ? args_json : "{}");
    if (written < 0 || written >= (int)sizeof(request.args_json)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return enqueue_publish_request(&request);
}

static esp_err_t publish_input_change_binding(uint8_t channel, int32_t value)
{
    node_action_router_publish_request_t request = {0};

    if (channel == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_mqtt_transport_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    request.kind = NODE_ACTION_ROUTER_PUBLISH_INPUT_CHANGE;
    request.channel = channel;
    request.value = value;
    return enqueue_publish_request(&request);
}

esp_err_t node_action_router_init(void)
{
    static const node_rule_action_port_t port = {
        .execute_command = execute_command_binding,
        .emit_event = emit_event_binding,
        .publish_input_change = publish_input_change_binding,
    };
    esp_err_t err = ESP_OK;

    if (s_port_bound) {
        return ESP_OK;
    }
    if (!ensure_publish_owner()) {
        return ESP_ERR_NO_MEM;
    }
    err = node_rule_action_port_bind(&port);
    if (err == ESP_OK) {
        s_port_bound = true;
    }
    return err;
}
