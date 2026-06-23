#include "node_mqtt_transport.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_fallback_runtime.h"
#include "node_mqtt_internal.h"
#include "node_protocol.h"
#include "sdkconfig.h"

static const char *TAG = "node_mqtt_transport";

node_config_t g_node_mqtt_config;
esp_mqtt_client_handle_t g_node_mqtt_client;
volatile bool g_node_mqtt_connected;

static char s_uri[96];
static char s_rx_payload[NODE_MQTT_PAYLOAD_MAX];
static char s_command_topic[NODE_MQTT_TOPIC_MAX];
static StaticTask_t s_heartbeat_task_storage;
static StackType_t s_heartbeat_task_stack[3072];
static bool s_heartbeat_task_started;
static StaticTask_t s_command_task_storage;
static StackType_t s_command_task_stack[4096];
static bool s_command_task_started;
static StaticQueue_t s_command_queue_storage;
static uint8_t s_command_queue_buffer[NODE_MQTT_COMMAND_QUEUE_LEN * sizeof(node_mqtt_command_message_t)];
static QueueHandle_t s_command_queue;
static StaticQueue_t s_admin_queue_storage;
static uint8_t s_admin_queue_buffer[NODE_MQTT_ADMIN_QUEUE_LEN * sizeof(node_mqtt_admin_message_t *)];
static QueueHandle_t s_admin_queue;
static StaticQueue_t s_result_queue_storage;
static uint8_t s_result_queue_buffer[NODE_MQTT_RESULT_QUEUE_LEN * sizeof(node_mqtt_deferred_result_t)];
static QueueHandle_t s_result_queue;
static volatile bool s_disconnect_after_result_overflow;

static void *alloc_admin_message_buffer(size_t size)
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

static bool publish_deferred_result_once(const node_mqtt_deferred_result_t *result)
{
    esp_err_t err = ESP_FAIL;

    if (!result || !g_node_mqtt_connected || !node_mqtt_publish_lock(0)) {
        return false;
    }
    err = node_mqtt_publish_result_fields_locked(result->request_id,
                                                 result->command,
                                                 result->status,
                                                 result->error_code,
                                                 NULL);
    node_mqtt_publish_unlock();
    return err == ESP_OK;
}

static void enqueue_deferred_result(const node_mqtt_command_message_t *message,
                                    const char *status,
                                    const char *error_code)
{
    node_mqtt_deferred_result_t result = {0};

    if (message) {
        snprintf(result.request_id, sizeof(result.request_id), "%s", message->request_id);
        snprintf(result.command, sizeof(result.command), "%s", message->command);
    }
    snprintf(result.status, sizeof(result.status), "%s", status ? status : "failed");
    snprintf(result.error_code, sizeof(result.error_code), "%s", error_code ? error_code : "internal_error");

    if (!s_result_queue || xQueueSend(s_result_queue, &result, 0) != pdPASS) {
        if (publish_deferred_result_once(&result)) {
            ESP_LOGW(TAG,
                     "result queue full; published overflow terminal result request_id=%s",
                     result.request_id);
            return;
        }
        s_disconnect_after_result_overflow = true;
        ESP_LOGE(TAG,
                 "result queue full; could not publish terminal result request_id=%s, scheduling MQTT reconnect",
                 result.request_id);
    }
}

static void publish_deferred_result(const node_mqtt_deferred_result_t *result)
{
    if (!result) {
        return;
    }
    esp_err_t err = node_mqtt_publish_result_fields_reliable(result->request_id,
                                                             result->command,
                                                             result->status,
                                                             result->error_code,
                                                             NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to publish deferred result request_id=%s", result->request_id);
    }
}

static void drain_deferred_results(void)
{
    node_mqtt_deferred_result_t result;

    while (s_result_queue && xQueueReceive(s_result_queue, &result, 0) == pdPASS) {
        publish_deferred_result(&result);
    }
}

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        g_node_mqtt_connected = true;
        (void)node_fallback_runtime_note_mqtt_state(true);
        ESP_LOGI(TAG, "connected");
        if (node_protocol_command_topic(s_command_topic, sizeof(s_command_topic), g_node_mqtt_config.node_id) == ESP_OK) {
            esp_mqtt_client_subscribe(g_node_mqtt_client, s_command_topic, 1);
            ESP_LOGI(TAG, "subscribed %s", s_command_topic);
        }
        node_mqtt_publish_heartbeat_and_status(true);
        break;
    case MQTT_EVENT_DISCONNECTED:
        g_node_mqtt_connected = false;
        (void)node_fallback_runtime_note_mqtt_state(false);
        ESP_LOGW(TAG, "disconnected");
        break;
    case MQTT_EVENT_DATA:
    {
        node_mqtt_command_message_t message = {0};
        node_mqtt_admin_message_t *admin_message = NULL;
        char command_name[NODE_MQTT_COMMAND_MAX] = {0};
        bool is_admin = false;

        if (event->current_data_offset != 0 || event->total_data_len != event->data_len ||
            event->data_len <= 0 || event->data_len >= (int)sizeof(s_rx_payload)) {
            ESP_LOGW(TAG,
                     "reject fragmented/oversize command len=%d total=%d offset=%d",
                     event->data_len,
                     event->total_data_len,
                     event->current_data_offset);
            break;
        }
        memcpy(s_rx_payload, event->data, event->data_len);
        s_rx_payload[event->data_len] = '\0';

        if (!node_mqtt_json_extract_string(s_rx_payload, "command", command_name, sizeof(command_name))) {
            enqueue_deferred_result(&message, "rejected", "invalid_request");
            break;
        }

        is_admin = node_mqtt_command_is_admin(command_name);
        if (is_admin) {
            admin_message = (node_mqtt_admin_message_t *)alloc_admin_message_buffer(sizeof(*admin_message));
            if (!admin_message) {
                snprintf(message.request_id, sizeof(message.request_id), "%s", "");
                snprintf(message.command, sizeof(message.command), "%s", command_name);
                message.valid = false;
                enqueue_deferred_result(&message, "failed", "no_mem");
                break;
            }
            (void)node_mqtt_parse_admin_command_payload(s_rx_payload, admin_message);
            if (!s_admin_queue || xQueueSend(s_admin_queue, &admin_message, 0) != pdPASS) {
                ESP_LOGW(TAG, "admin queue full");
                snprintf(message.request_id, sizeof(message.request_id), "%s", admin_message->request_id);
                snprintf(message.command, sizeof(message.command), "%s", admin_message->command);
                message.valid = admin_message->valid;
                enqueue_deferred_result(&message,
                                        "rejected",
                                        admin_message->valid ? "busy" : "invalid_request");
                free(admin_message);
            }
            break;
        }

        (void)node_mqtt_parse_command_payload(s_rx_payload, &message);
        if (!s_command_queue || xQueueSend(s_command_queue, &message, 0) != pdPASS) {
            ESP_LOGW(TAG, "command queue full");
            enqueue_deferred_result(&message,
                                    "rejected",
                                    message.valid ? "busy" : "invalid_request");
        }
        break;
    }
    default:
        break;
    }
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    while (true) {
        node_mqtt_publish_heartbeat_and_status(false);
        vTaskDelay(pdMS_TO_TICKS(NODE_MQTT_HEARTBEAT_MS));
    }
}

static void command_task(void *arg)
{
    node_mqtt_command_message_t message;
    node_mqtt_admin_message_t *admin_message = NULL;

    (void)arg;
    while (true) {
        drain_deferred_results();
        if (s_disconnect_after_result_overflow) {
            s_disconnect_after_result_overflow = false;
            if (g_node_mqtt_client) {
                ESP_LOGW(TAG, "disconnecting after terminal result overflow");
                esp_mqtt_client_disconnect(g_node_mqtt_client);
            }
        }
        if (s_admin_queue && xQueueReceive(s_admin_queue, &admin_message, 0) == pdPASS) {
            if (admin_message) {
                node_mqtt_process_admin_command_message(admin_message);
                free(admin_message);
                admin_message = NULL;
            }
            continue;
        }
        if (xQueueReceive(s_command_queue, &message, pdMS_TO_TICKS(100)) == pdPASS) {
            node_mqtt_process_command_message(&message);
        }
    }
}

esp_err_t node_mqtt_transport_start(const node_config_t *config)
{
    if (!config || config->node_id[0] == '\0' || config->controller_host[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_node_mqtt_client) {
        return ESP_OK;
    }
    g_node_mqtt_config = *config;
    ESP_RETURN_ON_ERROR(node_mqtt_publish_init(), TAG, "publish init failed");

    if (!s_command_queue) {
        s_command_queue = xQueueCreateStatic(NODE_MQTT_COMMAND_QUEUE_LEN,
                                             sizeof(node_mqtt_command_message_t),
                                             s_command_queue_buffer,
                                             &s_command_queue_storage);
        if (!s_command_queue) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_admin_queue) {
        s_admin_queue = xQueueCreateStatic(NODE_MQTT_ADMIN_QUEUE_LEN,
                                           sizeof(node_mqtt_admin_message_t *),
                                           s_admin_queue_buffer,
                                           &s_admin_queue_storage);
        if (!s_admin_queue) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_result_queue) {
        s_result_queue = xQueueCreateStatic(NODE_MQTT_RESULT_QUEUE_LEN,
                                            sizeof(node_mqtt_deferred_result_t),
                                            s_result_queue_buffer,
                                            &s_result_queue_storage);
        if (!s_result_queue) {
            return ESP_ERR_NO_MEM;
        }
    }

    int n = snprintf(s_uri, sizeof(s_uri), "mqtt://%s:%u", g_node_mqtt_config.controller_host, (unsigned)g_node_mqtt_config.mqtt_port);
    if (n <= 0 || n >= (int)sizeof(s_uri)) {
        return ESP_ERR_NO_MEM;
    }

    const char *client_id = g_node_mqtt_config.mqtt_client_id[0] ? g_node_mqtt_config.mqtt_client_id : g_node_mqtt_config.node_id;
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_uri,
        .credentials.client_id = client_id,
        .network.disable_auto_reconnect = false,
    };

    g_node_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!g_node_mqtt_client) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(g_node_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    esp_err_t err = esp_mqtt_client_start(g_node_mqtt_client);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(g_node_mqtt_client);
        g_node_mqtt_client = NULL;
        return err;
    }

    if (!s_heartbeat_task_started) {
        TaskHandle_t handle = xTaskCreateStatic(heartbeat_task,
                                                "node_mqtt_hb",
                                                sizeof(s_heartbeat_task_stack) / sizeof(s_heartbeat_task_stack[0]),
                                                NULL,
                                                tskIDLE_PRIORITY + 1,
                                                s_heartbeat_task_stack,
                                                &s_heartbeat_task_storage);
        if (!handle) {
            return ESP_ERR_NO_MEM;
        }
        s_heartbeat_task_started = true;
    }
    if (!s_command_task_started) {
        TaskHandle_t handle = xTaskCreateStatic(command_task,
                                                "node_mqtt_cmd",
                                                sizeof(s_command_task_stack) / sizeof(s_command_task_stack[0]),
                                                NULL,
                                                tskIDLE_PRIORITY + 2,
                                                s_command_task_stack,
                                                &s_command_task_storage);
        if (!handle) {
            return ESP_ERR_NO_MEM;
        }
        s_command_task_started = true;
    }

    ESP_LOGI(TAG, "mqtt start uri=%s client_id=%s", s_uri, client_id);
    return ESP_OK;
}
