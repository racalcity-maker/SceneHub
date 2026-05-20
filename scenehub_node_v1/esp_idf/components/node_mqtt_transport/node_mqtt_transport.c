#include "node_mqtt_transport.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_hardware_io.h"
#include "node_mqtt_internal.h"
#include "node_protocol.h"

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
static StaticTask_t s_input_task_storage;
static StackType_t s_input_task_stack[3072];
static bool s_input_task_started;

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
        ESP_LOGI(TAG, "connected");
        if (node_protocol_command_topic(s_command_topic, sizeof(s_command_topic), g_node_mqtt_config.node_id) == ESP_OK) {
            esp_mqtt_client_subscribe(g_node_mqtt_client, s_command_topic, 1);
            ESP_LOGI(TAG, "subscribed %s", s_command_topic);
        }
        node_mqtt_publish_heartbeat_and_status(true);
        break;
    case MQTT_EVENT_DISCONNECTED:
        g_node_mqtt_connected = false;
        ESP_LOGW(TAG, "disconnected");
        break;
    case MQTT_EVENT_DATA:
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
        node_mqtt_handle_command_payload(s_rx_payload);
        break;
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

static void input_task(void *arg)
{
    (void)arg;
    while (true) {
        if (g_node_mqtt_connected && node_mqtt_publish_lock(pdMS_TO_TICKS(100))) {
            for (uint8_t channel = 1; channel <= NODE_UNIVERSAL_IO_MAX; ++channel) {
                bool active = false;
                esp_err_t err = node_hardware_io_read_input(channel, &active);
                if (err == ESP_ERR_NOT_FOUND) {
                    continue;
                }
                if (err != ESP_OK) {
                    continue;
                }
                bool changed = false;
                err = node_hardware_io_observe_input_change(channel, active, &changed);
                if (err != ESP_OK) {
                    continue;
                }
                if (changed) {
                    char args_json[64];
                    int n = snprintf(args_json,
                                     sizeof(args_json),
                                     "{\"channel\":%u,\"value\":%u}",
                                     (unsigned)channel,
                                     active ? 1U : 0U);
                    if (n > 0 && n < (int)sizeof(args_json)) {
                        node_mqtt_publish_event_locked("input.changed", args_json);
                        node_mqtt_publish_status_locked();
                    }
                }
            }
            node_mqtt_publish_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
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
    if (!s_input_task_started) {
        TaskHandle_t handle = xTaskCreateStatic(input_task,
                                                "node_mqtt_in",
                                                sizeof(s_input_task_stack) / sizeof(s_input_task_stack[0]),
                                                NULL,
                                                tskIDLE_PRIORITY + 1,
                                                s_input_task_stack,
                                                &s_input_task_storage);
        if (!handle) {
            return ESP_ERR_NO_MEM;
        }
        s_input_task_started = true;
    }

    ESP_LOGI(TAG, "mqtt start uri=%s client_id=%s", s_uri, client_id);
    return ESP_OK;
}
