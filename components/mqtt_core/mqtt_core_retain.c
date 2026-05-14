#include "mqtt_core_internal.h"

#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "mqtt_core";

typedef struct {
    char topic[MQTT_MAX_TOPIC];
    char payload[MQTT_MAX_PAYLOAD];
    uint8_t qos;
} retain_delivery_t;

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

void deliver_retain(mqtt_session_t *sess, const char *filter)
{
    retain_delivery_t *delivery = NULL;
    size_t slot = session_index(sess);
    int sock = sess ? sess->sock : -1;
    char client_id[CONFIG_STORE_CLIENT_ID_MAX] = {0};
    if (!s_retain || !sess || !filter || sock < 0) {
        return;
    }
    delivery = heap_caps_calloc(1, sizeof(*delivery), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!delivery) {
        delivery = heap_caps_calloc(1, sizeof(*delivery), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!delivery) {
        ESP_LOGW(TAG, "retain delivery alloc failed");
        return;
    }
    strncpy(client_id, sess->client_id, sizeof(client_id) - 1);
    for (size_t i = 0; i < MQTT_RETAIN_MAX; ++i) {
        bool matched = false;
        memset(delivery, 0, sizeof(*delivery));
        lock();
        if (!s_retain[i].in_use) {
            unlock();
            continue;
        }
        if (topic_matches_filter(filter, s_retain[i].topic)) {
            strncpy(delivery->topic, s_retain[i].topic, sizeof(delivery->topic) - 1);
            strncpy(delivery->payload, s_retain[i].payload, sizeof(delivery->payload) - 1);
            delivery->qos = s_retain[i].qos;
            matched = true;
        }
        unlock();
        if (!matched) {
            continue;
        }
        if (send_publish_packet_to_session_slot(slot,
                                                sock,
                                                client_id,
                                                delivery->topic,
                                                delivery->payload,
                                                delivery->qos,
                                                true,
                                                0) < 0) {
            heap_caps_free(delivery);
            return;
        }
    }
    heap_caps_free(delivery);
}
