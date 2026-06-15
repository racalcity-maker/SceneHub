#include "mqtt_core_internal.h"

#include <string.h>

#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "mqtt_core";
static portMUX_TYPE s_session_tx_lock_init_lock = portMUX_INITIALIZER_UNLOCKED;

#define MQTT_SESSION_REUSE_DELAY_MS 500

static void reset_session_runtime_fields(mqtt_session_t *s)
{
    s->sock = -1;
    s->task = NULL;
    s->active = false;
    s->closing = false;
    s->retiring = false;
    s->suppress_will = false;
    s->reusable_after_ms = 0;
    memset(s->client_id, 0, sizeof(s->client_id));
    s->keepalive = 0;
    s->last_rx_ms = 0;
    memset(s->subs, 0, sizeof(s->subs));
    s->sub_count = 0;
    memset(&s->will, 0, sizeof(s->will));
    memset(s->qos1_pending, 0, sizeof(s->qos1_pending));
}

static void reap_retired_session_slots_locked(void)
{
    int64_t now = now_ms();

    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        mqtt_session_t *s = &s_sessions[i];
        if (!s->active && s->retiring && now >= s->reusable_after_ms) {
            s->retiring = false;
            s->task = NULL;
            s->reusable_after_ms = 0;
        }
    }
}

mqtt_session_t *alloc_session(void)
{
    if (!s_sessions) {
        return NULL;
    }
    reap_retired_session_slots_locked();
    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        if (!s_sessions[i].active && !s_sessions[i].retiring) {
            portENTER_CRITICAL(&s_session_tx_lock_init_lock);
            if (!s_sessions[i].tx_lock) {
                s_sessions[i].tx_lock = xSemaphoreCreateMutexStatic(&s_sessions[i].tx_lock_buf);
            }
            portEXIT_CRITICAL(&s_session_tx_lock_init_lock);
            SemaphoreHandle_t tx_lock = s_sessions[i].tx_lock;
            if (!tx_lock) {
                return NULL;
            }
            reset_session_runtime_fields(&s_sessions[i]);
            s_sessions[i].active = true;
            // Pre-CONNECT sessions should not look infinitely idle to the sweep timer.
            s_sessions[i].last_rx_ms = now_ms();
            s_client_count++;
            return &s_sessions[i];
        }
    }
    return NULL;
}

mqtt_session_t *find_session_by_client_id(const char *client_id)
{
    if (!client_id || !client_id[0]) {
        return NULL;
    }
    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        if (s_sessions[i].active &&
            !s_sessions[i].closing &&
            !s_sessions[i].retiring &&
            strcmp(s_sessions[i].client_id, client_id) == 0) {
            return &s_sessions[i];
        }
    }
    return NULL;
}

void free_session(mqtt_session_t *s)
{
    int close_sock = -1;
    SemaphoreHandle_t tx_lock = NULL;
    if (!s) {
        return;
    }
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (s->task && s->task != current) {
        lock();
        s->suppress_will = true;
        s->closing = true;
        if (s->sock >= 0) {
            shutdown(s->sock, SHUT_RDWR);
        }
        unlock();
        return;
    }
    if (s->tx_lock) {
        tx_lock = s->tx_lock;
        xSemaphoreTake(tx_lock, portMAX_DELAY);
    }
    lock();
    mqtt_qos1_clear_session_locked(s);
    if (s->sock >= 0) {
        close_sock = s->sock;
    }
    s->sock = -1;
    s->task = NULL;
    s->closing = true;
    s->suppress_will = true;
    s->sub_count = 0;
    memset(&s->will, 0, sizeof(s->will));
    unlock();
    if (close_sock >= 0) {
        shutdown(close_sock, SHUT_RDWR);
        closesocket(close_sock);
    }
    if (tx_lock) {
        xSemaphoreGive(tx_lock);
    }

    lock();
    s->active = false;
    s->closing = false;
    s->retiring = true;
    s->reusable_after_ms = now_ms() + MQTT_SESSION_REUSE_DELAY_MS;
    if (s_client_count > 0) {
        s_client_count--;
    }
    unlock();
}

void sweep_idle_sessions(void)
{
    int64_t now = now_ms();
    for (size_t i = 0; i < MQTT_MAX_CLIENTS; ++i) {
        int close_sock = -1;

        lock();
        mqtt_session_t *s = &s_sessions[i];
        if (!s->active) {
            unlock();
            continue;
        }
        int64_t idle_ms = now - s->last_rx_ms;
        int64_t limit_ms = (s->keepalive > 0) ? (int64_t)s->keepalive * 1500 : 60000;
        if (idle_ms >= limit_ms) {
            close_sock = request_session_prepare_close_locked(s, "sweep: closing idle session", 0);
        }
        unlock();

        request_session_close_socket(close_sock);
    }
}

void send_will_if_needed(mqtt_session_t *sess)
{
    if (sess->will.has && !sess->suppress_will) {
        ESP_LOGI(TAG, "sending will for %s", sess->client_id);
        publish_to_subscribers(sess->will.topic, sess->will.payload, sess->will.qos, sess->will.retain, sess);
    }
}
