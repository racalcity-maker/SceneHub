#include "mqtt_core.h"
#include "mqtt_core_internal.h"

#include <stddef.h>
#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"

#define MQTT_BRIDGE_JOB_POOL_LEN 32

typedef struct {
    event_bus_type_t type;
    const char *topic;
} event_topic_map_t;

static const event_topic_map_t k_outgoing_map[] = {
    {EVENT_AUDIO_PLAY, "audio/play"},
    {EVENT_SYSTEM_STATUS, "sys/broker/metrics"},
    {EVENT_CARD_OK, "access/card/ok"},
    {EVENT_CARD_BAD, "access/card/bad"},
    {EVENT_RELAY_CMD, "relay/cmd"},
    {EVENT_WEB_COMMAND, "web/cmd"},
};

static const event_topic_map_t k_incoming_map[] = {
    {EVENT_AUDIO_PLAY, "audio/play"},
    {EVENT_RELAY_CMD, "relay/"},
    {EVENT_WEB_COMMAND, "web/cmd"},
};

static EXT_RAM_BSS_ATTR event_bus_message_t s_bridge_job_pool[MQTT_BRIDGE_JOB_POOL_LEN];
static bool s_bridge_job_pool_in_use[MQTT_BRIDGE_JOB_POOL_LEN];
static portMUX_TYPE s_bridge_job_pool_lock = portMUX_INITIALIZER_UNLOCKED;

static event_bus_message_t *bridge_job_alloc(void)
{
    event_bus_message_t *slot = NULL;

    portENTER_CRITICAL(&s_bridge_job_pool_lock);
    for (size_t i = 0; i < MQTT_BRIDGE_JOB_POOL_LEN; ++i) {
        if (!s_bridge_job_pool_in_use[i]) {
            s_bridge_job_pool_in_use[i] = true;
            slot = &s_bridge_job_pool[i];
            break;
        }
    }
    portEXIT_CRITICAL(&s_bridge_job_pool_lock);

    if (slot) {
        memset(slot, 0, sizeof(*slot));
    }
    return slot;
}

static void bridge_job_free(event_bus_message_t *slot)
{
    if (!slot) {
        return;
    }

    ptrdiff_t index = slot - s_bridge_job_pool;
    if (index < 0 || index >= MQTT_BRIDGE_JOB_POOL_LEN) {
        return;
    }

    portENTER_CRITICAL(&s_bridge_job_pool_lock);
    s_bridge_job_pool_in_use[index] = false;
    portEXIT_CRITICAL(&s_bridge_job_pool_lock);
}

const char *find_topic_by_type(event_bus_type_t type)
{
    for (size_t i = 0; i < sizeof(k_outgoing_map) / sizeof(k_outgoing_map[0]); ++i) {
        if (k_outgoing_map[i].type == type) {
            return k_outgoing_map[i].topic;
        }
    }
    return NULL;
}

const char *mqtt_core_topic_for_event(event_bus_type_t type)
{
    return find_topic_by_type(type);
}

event_bus_type_t find_type_by_topic(const char *topic)
{
    if (!topic) {
        return EVENT_NONE;
    }
    for (size_t i = 0; i < sizeof(k_incoming_map) / sizeof(k_incoming_map[0]); ++i) {
        const char *t = k_incoming_map[i].topic;
        size_t len = strlen(t);
        if (strncmp(topic, t, len) == 0) {
            return k_incoming_map[i].type;
        }
    }
    return EVENT_NONE;
}

static void publish_event_message(void *ctx)
{
    event_bus_message_t *msg = (event_bus_message_t *)ctx;
    if (!msg) {
        return;
    }
    const char *topic = msg->topic[0] ? msg->topic : find_topic_by_type(msg->type);
    if (!topic) {
        bridge_job_free(msg);
        return;
    }
    mqtt_core_publish(topic, msg->payload);
    bridge_job_free(msg);
}

void on_event_bus_message(const event_bus_message_t *msg)
{
    if (!msg) {
        return;
    }
    if (!msg->topic[0] && !find_topic_by_type(msg->type)) {
        return;
    }

    event_bus_message_t *copy = bridge_job_alloc();
    if (!copy) {
        return;
    }
    *copy = *msg;
    if (event_bus_post_job(publish_event_message, copy, 0) != ESP_OK) {
        bridge_job_free(copy);
    }
}
