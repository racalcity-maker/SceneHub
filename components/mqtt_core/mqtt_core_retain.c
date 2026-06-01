#include "mqtt_core_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "mqtt_core";

typedef struct {
    char topic[MQTT_MAX_TOPIC];
    char payload[MQTT_MAX_PAYLOAD];
    uint8_t qos;
} retain_delivery_t;

static EXT_RAM_BSS_ATTR retain_delivery_t s_retain_delivery;
static SemaphoreHandle_t s_retain_delivery_lock = NULL;
static StaticSemaphore_t s_retain_delivery_lock_storage;
static portMUX_TYPE s_retain_delivery_lock_init_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t retain_init(void)
{
    if (s_retain_delivery_lock) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_retain_delivery_lock_init_lock);
    if (!s_retain_delivery_lock) {
        s_retain_delivery_lock = xSemaphoreCreateMutexStatic(&s_retain_delivery_lock_storage);
    }
    portEXIT_CRITICAL(&s_retain_delivery_lock_init_lock);
    if (!s_retain_delivery_lock) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_retain_delivery, 0, sizeof(s_retain_delivery));
    return ESP_OK;
}

static esp_err_t retain_delivery_lock(void)
{
    if (!s_retain_delivery_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    return xSemaphoreTake(s_retain_delivery_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void retain_delivery_unlock(void)
{
    if (s_retain_delivery_lock) {
        xSemaphoreGive(s_retain_delivery_lock);
    }
}

static void retain_free_entry(retain_entry_t *slot)
{
    if (!slot) {
        return;
    }
    slot->payload[0] = '\0';
    slot->payload_len = 0;
}

static retain_entry_t *retain_get(const char *topic)
{
    if (!s_retain || !topic) {
        return NULL;
    }
    for (size_t i = 0; i < MQTT_RETAIN_MAX; ++i) {
        if (s_retain[i].in_use && strcmp(s_retain[i].topic, topic) == 0) {
            return &s_retain[i];
        }
    }
    return NULL;
}

void retain_store(const char *topic, const char *payload, uint8_t qos)
{
    if (!s_retain || !topic || !payload) {
        return;
    }
    retain_entry_t *slot = retain_get(topic);
    size_t len = strnlen(payload, MQTT_MAX_PAYLOAD - 1);

    // MQTT retained clear semantics: retained publish with empty payload deletes stored entry.
    if (len == 0) {
        if (slot) {
            retain_free_entry(slot);
            slot->in_use = false;
            slot->topic[0] = '\0';
            slot->qos = 0;
        }
        return;
    }

    if (!slot) {
        for (size_t i = 0; i < MQTT_RETAIN_MAX; ++i) {
            if (!s_retain[i].in_use) {
                slot = &s_retain[i];
                break;
            }
        }
    }
    if (!slot) {
        ESP_LOGW(TAG, "retain table full, dropping %s", topic);
        return;
    }
    retain_free_entry(slot);
    slot->in_use = true;
    strncpy(slot->topic, topic, sizeof(slot->topic) - 1);
    slot->topic[sizeof(slot->topic) - 1] = '\0';
    memcpy(slot->payload, payload, len);
    slot->payload[len] = '\0';
    slot->payload_len = len;
    slot->qos = qos;
}

void deliver_retain(mqtt_session_t *sess, const char *filter, uint8_t subscription_qos)
{
    size_t slot = session_index(sess);
    int sock = sess ? sess->sock : -1;
    char client_id[CONFIG_STORE_CLIENT_ID_MAX] = {0};
    if (!s_retain || !sess || !filter || sock < 0) {
        return;
    }
    if (retain_delivery_lock() != ESP_OK) {
        ESP_LOGW(TAG, "retain delivery scratch busy");
        return;
    }
    strncpy(client_id, sess->client_id, sizeof(client_id) - 1);
    for (size_t i = 0; i < MQTT_RETAIN_MAX; ++i) {
        bool matched = false;
        memset(&s_retain_delivery, 0, sizeof(s_retain_delivery));
        lock();
        if (!s_retain[i].in_use) {
            unlock();
            continue;
        }
        if (topic_matches_filter(filter, s_retain[i].topic)) {
            strncpy(s_retain_delivery.topic, s_retain[i].topic, sizeof(s_retain_delivery.topic) - 1);
            strncpy(s_retain_delivery.payload, s_retain[i].payload, sizeof(s_retain_delivery.payload) - 1);
            s_retain_delivery.qos = s_retain[i].qos;
            matched = true;
        }
        unlock();
        if (!matched) {
            continue;
        }
        uint8_t delivery_qos = mqtt_effective_delivery_qos(s_retain_delivery.qos,
                                                           subscription_qos);
        uint16_t pid = delivery_qos ? mqtt_next_packet_id() : 0;
        if (send_publish_packet_to_session_slot(slot,
                                                sock,
                                                client_id,
                                                s_retain_delivery.topic,
                                                s_retain_delivery.payload,
                                                delivery_qos,
                                                true,
                                                pid) < 0) {
            retain_delivery_unlock();
            return;
        }
    }
    retain_delivery_unlock();
}
