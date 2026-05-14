#include "mqtt_core.h"
#include "mqtt_core_internal.h"

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "config_store.h"
#include "event_bus.h"
#include "device_control_ingest.h"

// Минимальный MQTT 3.1.1 брокер: QoS0/1, retain, LWT, простая ACL (prefix-based), без QoS2/username/password/TLS.

static const char *TAG = "mqtt_core";

mqtt_session_t *s_sessions = NULL;
StackType_t *s_session_stacks[MQTT_MAX_CLIENTS];
StaticTask_t *s_session_tcbs[MQTT_MAX_CLIENTS];
uint8_t *s_session_rx_bufs[MQTT_MAX_CLIENTS];
uint8_t *s_session_tx_bufs[MQTT_MAX_CLIENTS];
retain_entry_t *s_retain = NULL;
SemaphoreHandle_t s_lock = NULL;
uint8_t s_client_count = 0;
int s_listen_sock = -1;
TaskHandle_t s_accept_task = NULL;
StackType_t *s_accept_stack = NULL;
StaticTask_t *s_accept_tcb = NULL;
esp_timer_handle_t s_sweep_timer = NULL;
bool s_event_handler_registered = false;

static EXT_RAM_BSS_ATTR mqtt_session_t s_session_storage[MQTT_MAX_CLIENTS];
static EXT_RAM_BSS_ATTR retain_entry_t s_retain_storage[MQTT_RETAIN_MAX];
static EXT_RAM_BSS_ATTR StackType_t s_session_stack_storage[MQTT_MAX_CLIENTS][MQTT_CLIENT_STACK];
static StaticTask_t s_session_tcb_storage[MQTT_MAX_CLIENTS];
static EXT_RAM_BSS_ATTR uint8_t s_session_rx_storage[MQTT_MAX_CLIENTS][MQTT_MAX_PACKET + 1];
static EXT_RAM_BSS_ATTR uint8_t s_session_tx_storage[MQTT_MAX_CLIENTS][MQTT_MAX_PACKET];
static EXT_RAM_BSS_ATTR StackType_t s_accept_stack_storage[MQTT_ACCEPT_STACK];
static StaticTask_t s_accept_tcb_storage;
static StaticSemaphore_t s_lock_storage;
static bool s_storage_ready = false;

static void mqtt_core_bind_static_storage(void)
{
    if (s_storage_ready) {
        return;
    }
    memset(s_session_storage, 0, sizeof(s_session_storage));
    memset(s_retain_storage, 0, sizeof(s_retain_storage));
    s_sessions = s_session_storage;
    s_retain = s_retain_storage;
    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        s_session_stacks[i] = s_session_stack_storage[i];
        s_session_tcbs[i] = &s_session_tcb_storage[i];
        s_session_rx_bufs[i] = s_session_rx_storage[i];
        s_session_tx_bufs[i] = s_session_tx_storage[i];
    }
    s_accept_stack = s_accept_stack_storage;
    s_accept_tcb = &s_accept_tcb_storage;
    s_storage_ready = true;
}

size_t session_index(const mqtt_session_t *sess)
{
    if (!sess || !s_sessions) {
        return MQTT_MAX_CLIENTS;
    }
    return (size_t)(sess - s_sessions);
}

bool ensure_session_task_storage(size_t idx)
{
    if (idx >= MQTT_MAX_CLIENTS) {
        return false;
    }
    mqtt_core_bind_static_storage();
    return s_session_stacks[idx] && s_session_tcbs[idx];
}

uint8_t *ensure_session_rx_buffer(size_t idx)
{
    if (idx >= MQTT_MAX_CLIENTS) {
        return NULL;
    }
    mqtt_core_bind_static_storage();
    return s_session_rx_bufs[idx];
}

uint8_t *ensure_session_tx_buffer(size_t idx)
{
    if (idx >= MQTT_MAX_CLIENTS) {
        return NULL;
    }
    mqtt_core_bind_static_storage();
    return s_session_tx_bufs[idx];
}

bool ensure_accept_task_storage(void)
{
    mqtt_core_bind_static_storage();
    return s_accept_stack && s_accept_tcb;
}

void mqtt_core_get_client_stats(mqtt_client_stats_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        if (!s_sessions[i].active) continue;
        out->total++;
    }
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

uint8_t mqtt_core_client_count(void)
{
    uint8_t count = 0;
    lock();
    count = s_client_count;
    unlock();
    return count;
}

int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

void unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

void request_session_close(mqtt_session_t *sess, const char *reason, int err)
{
    if (!sess) {
        return;
    }
    const char *cid = sess->client_id[0] ? sess->client_id : "<unknown>";
    ESP_LOGW(TAG, "%s for %s (err=%d)", reason ? reason : "session closing", cid, err);
    sess->closing = true;
    if (sess->sock >= 0) {
        int sock = sess->sock;
        sess->sock = -1;
        shutdown(sock, SHUT_RDWR);
        closesocket(sock);
    }
}

void request_session_close_if_current(size_t slot,
                                      int sock,
                                      const char *client_id,
                                      const char *reason,
                                      int err)
{
    int close_sock = -1;
    if (!s_sessions || slot >= MQTT_MAX_CLIENTS) {
        return;
    }
    lock();
    mqtt_session_t *sess = &s_sessions[slot];
    if (sess->active && sess->sock == sock) {
        const char *cid = sess->client_id[0] ? sess->client_id : "<unknown>";
        ESP_LOGW(TAG, "%s for %s (err=%d)", reason ? reason : "session closing", cid, err);
        sess->closing = true;
        close_sock = sess->sock;
        sess->sock = -1;
    } else {
        ESP_LOGW(TAG,
                 "%s for stale session %s (err=%d)",
                 reason ? reason : "session close skipped",
                 client_id && client_id[0] ? client_id : "<unknown>",
                 err);
    }
    unlock();
    if (close_sock >= 0) {
        shutdown(close_sock, SHUT_RDWR);
        closesocket(close_sock);
    }
}

esp_err_t mqtt_core_init(void)
{
    mqtt_core_bind_static_storage();
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    esp_err_t ingest_err = device_control_ingest_init();
    if (ingest_err != ESP_OK) {
        ESP_LOGE(TAG, "failed to init control ingest: %s", esp_err_to_name(ingest_err));
        return ingest_err;
    }
    if (!s_event_handler_registered) {
        esp_err_t err = event_bus_register_handler(on_event_bus_message);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to register event handler: %s", esp_err_to_name(err));
            return err;
        }
        s_event_handler_registered = true;
    }
    return ESP_OK;
}

esp_err_t mqtt_core_start(void)
{
    const app_config_t *cfg = config_store_get();
    return mqtt_core_start_server(cfg->mqtt.port);
}

esp_err_t mqtt_core_publish(const char *topic, const char *payload)
{
    if (!topic || !payload) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_sessions || !s_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    publish_to_subscribers(topic, payload, 0, false, NULL);
    return ESP_OK;
}

esp_err_t mqtt_core_inject_message(const char *topic, const char *payload)
{
    if (!topic || !payload) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ingest_err = device_control_ingest_handle_mqtt(topic, payload);
    if (ingest_err != ESP_OK && ingest_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "control ingest failed for %s: %s", topic, esp_err_to_name(ingest_err));
    }

    scenehub_event_type_t type = find_type_by_topic(topic);
    if (type != SCENEHUB_EVENT_NONE) {
        scenehub_event_t typed = {0};
        if (scenehub_event_make_text(&typed, type, topic, payload) == ESP_OK) {
#if MQTT_CORE_DEBUG
            ESP_LOGI(TAG, "[MQTT IN] %s -> event %d", topic, type);
#endif
            event_bus_post(&typed, pdMS_TO_TICKS(100));
        }
    }

    scenehub_event_t generic = {0};
    if (scenehub_event_make_text(&generic, SCENEHUB_EVENT_MQTT_MESSAGE, topic, payload) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return event_bus_post(&generic, pdMS_TO_TICKS(100));
}
