#include "node_mqtt_internal.h"

#include <stdio.h>

#include "esp_timer.h"
#include "freertos/semphr.h"
#include "node_protocol.h"

static char s_tx_payload[NODE_MQTT_PAYLOAD_MAX];
static char s_topic[NODE_MQTT_TOPIC_MAX];
static uint32_t s_status_seq;
static StaticSemaphore_t s_publish_mutex_storage;
static SemaphoreHandle_t s_publish_mutex;

int64_t node_mqtt_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

esp_err_t node_mqtt_publish_init(void)
{
    if (!s_publish_mutex) {
        s_publish_mutex = xSemaphoreCreateMutexStatic(&s_publish_mutex_storage);
    }
    return s_publish_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

bool node_mqtt_publish_lock(TickType_t timeout_ticks)
{
    return s_publish_mutex && xSemaphoreTake(s_publish_mutex, timeout_ticks) == pdTRUE;
}

void node_mqtt_publish_unlock(void)
{
    if (s_publish_mutex) {
        xSemaphoreGive(s_publish_mutex);
    }
}

static esp_err_t publish_locked(const char *topic, const char *payload, int qos, bool retain)
{
    if (!g_node_mqtt_client || !topic || !payload) {
        return ESP_ERR_INVALID_STATE;
    }
    int msg_id = esp_mqtt_client_publish(g_node_mqtt_client, topic, payload, 0, qos, retain ? 1 : 0);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t node_mqtt_publish_result_fields_locked(const char *request_id,
                                                 const char *command,
                                                 const char *status,
                                                 const char *error_code,
                                                 const char *data_json)
{
    if (node_protocol_result_topic(s_topic, sizeof(s_topic), g_node_mqtt_config.node_id) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }

    int n = snprintf(s_tx_payload,
                     sizeof(s_tx_payload),
                     "{\"request_id\":\"%s\",\"command\":\"%s\",\"status\":\"%s\",\"ts_ms\":%lld",
                     request_id ? request_id : "",
                     command ? command : "",
                     status ? status : "failed",
                     (long long)node_mqtt_now_ms());
    if (n < 0 || n >= (int)sizeof(s_tx_payload)) {
        return ESP_ERR_NO_MEM;
    }
    size_t len = (size_t)n;
    if (error_code && error_code[0]) {
        n = snprintf(s_tx_payload + len,
                     sizeof(s_tx_payload) - len,
                     ",\"error\":{\"code\":\"%s\"}",
                     error_code);
        if (n < 0 || (size_t)n >= sizeof(s_tx_payload) - len) {
            return ESP_ERR_NO_MEM;
        }
        len += (size_t)n;
    }
    if (data_json && data_json[0]) {
        n = snprintf(s_tx_payload + len,
                     sizeof(s_tx_payload) - len,
                     ",\"data\":%s",
                     data_json);
        if (n < 0 || (size_t)n >= sizeof(s_tx_payload) - len) {
            return ESP_ERR_NO_MEM;
        }
        len += (size_t)n;
    }
    n = snprintf(s_tx_payload + len, sizeof(s_tx_payload) - len, "}");
    if (n < 0 || (size_t)n >= sizeof(s_tx_payload) - len) {
        return ESP_ERR_NO_MEM;
    }
    return publish_locked(s_topic, s_tx_payload, 0, false);
}

esp_err_t node_mqtt_publish_result_locked(const char *request_id,
                                          const char *command,
                                          const node_control_result_t *result)
{
    return node_mqtt_publish_result_fields_locked(request_id,
                                                  command,
                                                  result ? result->status : "failed",
                                                  result ? result->error_code : "internal_error",
                                                  result ? result->data_json : NULL);
}

esp_err_t node_mqtt_publish_status_locked(void)
{
    ++s_status_seq;
    if (node_protocol_status_topic(s_topic, sizeof(s_topic), g_node_mqtt_config.node_id) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    int n = snprintf(s_tx_payload,
                     sizeof(s_tx_payload),
                     "{\"ts_ms\":%lld,\"fw_version\":\"0.1.0\",\"mode\":\"normal\","
                     "\"state\":\"idle\",\"health\":\"ok\","
                     "\"capabilities\":[\"heartbeat\",\"status\",\"describe_interface\",\"node.identify\",\"node.get_status\",\"relay.set\",\"relay.pulse\",\"relay.all_off\",\"mosfet.set\",\"mosfet.fade\",\"mosfet.pulse\",\"mosfet.blink\",\"mosfet.breathe\",\"mosfet.all_off\",\"mosfet.effect\",\"io.set\",\"io.all_off\",\"node.all_off\",\"led.off\",\"led.solid\",\"led.blink\",\"led.breathe\",\"led.effect\",\"input.changed\"],"
                     "\"runtime\":{\"active\":false},\"status_seq\":%u}",
                     (long long)node_mqtt_now_ms(),
                     (unsigned)s_status_seq);
    if (n < 0 || n >= (int)sizeof(s_tx_payload)) {
        return ESP_ERR_NO_MEM;
    }
    return publish_locked(s_topic, s_tx_payload, 0, false);
}

esp_err_t node_mqtt_publish_event_locked(const char *event_name, const char *args_json)
{
    if (node_protocol_event_topic(s_topic, sizeof(s_topic), g_node_mqtt_config.node_id) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    int n = snprintf(s_tx_payload,
                     sizeof(s_tx_payload),
                     "{\"event\":\"%s\",\"args\":%s,\"ts_ms\":%lld}",
                     event_name ? event_name : "",
                     (args_json && args_json[0]) ? args_json : "{}",
                     (long long)node_mqtt_now_ms());
    if (n < 0 || n >= (int)sizeof(s_tx_payload)) {
        return ESP_ERR_NO_MEM;
    }
    return publish_locked(s_topic, s_tx_payload, 0, false);
}

void node_mqtt_publish_heartbeat_and_status(bool include_status)
{
    if (!g_node_mqtt_connected || !node_mqtt_publish_lock(pdMS_TO_TICKS(500))) {
        return;
    }

    if (node_protocol_heartbeat_topic(s_topic, sizeof(s_topic), g_node_mqtt_config.node_id) == ESP_OK) {
        snprintf(s_tx_payload,
                 sizeof(s_tx_payload),
                 "{\"ts_ms\":%lld,\"uptime_ms\":%lld,\"status_seq\":%u}",
                 (long long)node_mqtt_now_ms(),
                 (long long)(esp_timer_get_time() / 1000),
                 (unsigned)(s_status_seq + 1U));
        publish_locked(s_topic, s_tx_payload, 0, false);
    }

    if (include_status) {
        node_mqtt_publish_status_locked();
    }
    node_mqtt_publish_unlock();
}
