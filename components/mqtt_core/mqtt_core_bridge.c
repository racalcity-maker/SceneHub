#include "mqtt_core.h"
#include "mqtt_core_internal.h"

#include <stddef.h>
#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"

#define MQTT_BRIDGE_JOB_POOL_LEN 32

typedef struct {
    scenehub_event_type_t type;
    const char *topic;
} event_topic_map_t;

static const event_topic_map_t k_outgoing_map[] = {
    {SCENEHUB_EVENT_AUDIO_PLAY, "audio/play"},
    {SCENEHUB_EVENT_SYSTEM_STATUS, "sys/broker/metrics"},
    {SCENEHUB_EVENT_CARD_OK, "access/card/ok"},
    {SCENEHUB_EVENT_CARD_BAD, "access/card/bad"},
    {SCENEHUB_EVENT_RELAY_CMD, "relay/cmd"},
    {SCENEHUB_EVENT_WEB_COMMAND, "web/cmd"},
};

static const event_topic_map_t k_incoming_map[] = {
    {SCENEHUB_EVENT_AUDIO_PLAY, "audio/play"},
    {SCENEHUB_EVENT_RELAY_CMD, "relay/"},
    {SCENEHUB_EVENT_WEB_COMMAND, "web/cmd"},
};

static EXT_RAM_BSS_ATTR scenehub_event_t s_bridge_job_pool[MQTT_BRIDGE_JOB_POOL_LEN];
static bool s_bridge_job_pool_in_use[MQTT_BRIDGE_JOB_POOL_LEN];
static portMUX_TYPE s_bridge_job_pool_lock = portMUX_INITIALIZER_UNLOCKED;

static scenehub_event_t *bridge_job_alloc(void)
{
    scenehub_event_t *slot = NULL;

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

static void bridge_job_free(scenehub_event_t *slot)
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

const char *find_topic_by_type(scenehub_event_type_t type)
{
    for (size_t i = 0; i < sizeof(k_outgoing_map) / sizeof(k_outgoing_map[0]); ++i) {
        if (k_outgoing_map[i].type == type) {
            return k_outgoing_map[i].topic;
        }
    }
    return NULL;
}

const char *mqtt_core_topic_for_event(scenehub_event_type_t type)
{
    return find_topic_by_type(type);
}

scenehub_event_type_t find_type_by_topic(const char *topic)
{
    if (!topic) {
        return SCENEHUB_EVENT_NONE;
    }
    for (size_t i = 0; i < sizeof(k_incoming_map) / sizeof(k_incoming_map[0]); ++i) {
        const char *t = k_incoming_map[i].topic;
        size_t len = strlen(t);
        if (strncmp(topic, t, len) == 0) {
            return k_incoming_map[i].type;
        }
    }
    return SCENEHUB_EVENT_NONE;
}

static void publish_event_message(void *ctx)
{
    scenehub_event_t *msg = (scenehub_event_t *)ctx;
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

bool mqtt_core_should_bridge_event(const scenehub_event_t *msg)
{
    if (!msg) {
        return false;
    }
    if (msg->origin == SCENEHUB_EVENT_ORIGIN_MQTT ||
        msg->type == SCENEHUB_EVENT_MQTT_MESSAGE) {
        return false;
    }
    return msg->topic[0] || find_topic_by_type(msg->type);
}

void on_event_bus_message(const scenehub_event_t *msg)
{
    if (!mqtt_core_should_bridge_event(msg)) {
        return;
    }

    scenehub_event_t *copy = bridge_job_alloc();
    if (!copy) {
        return;
    }
    *copy = *msg;
    if (event_bus_post_job(publish_event_message, copy, 0) != ESP_OK) {
        bridge_job_free(copy);
    }
}
