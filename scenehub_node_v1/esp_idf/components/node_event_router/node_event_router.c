#include "node_event_router.h"

#include <stdio.h>
#include <string.h>

static bool s_initialized;

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void clear_event(node_rule_event_t *event)
{
    if (!event) {
        return;
    }
    memset(event, 0, sizeof(*event));
}

esp_err_t node_event_router_init(void)
{
    s_initialized = true;
    return ESP_OK;
}

esp_err_t node_event_router_reset(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

void node_event_router_make_boot_event(node_rule_event_t *out_event)
{
    clear_event(out_event);
    if (!out_event) {
        return;
    }
    out_event->type = NODE_RULE_EVENT_BOOT;
    copy_text(out_event->event_name, sizeof(out_event->event_name), "boot");
}

void node_event_router_make_input_edge_event(node_rule_event_t *out_event,
                                             uint8_t input_channel,
                                             int32_t value)
{
    clear_event(out_event);
    if (!out_event) {
        return;
    }
    out_event->type = NODE_RULE_EVENT_INPUT_EDGE;
    out_event->input_channel = input_channel;
    out_event->value = value;
}

void node_event_router_make_input_hold_event(node_rule_event_t *out_event,
                                             uint8_t input_channel,
                                             int32_t value,
                                             uint32_t duration_ms)
{
    clear_event(out_event);
    if (!out_event) {
        return;
    }
    out_event->type = NODE_RULE_EVENT_INPUT_HOLD;
    out_event->input_channel = input_channel;
    out_event->value = value;
    out_event->duration_ms = duration_ms;
}

void node_event_router_make_timer_event(node_rule_event_t *out_event,
                                        uint16_t timer_index,
                                        const char *timer_name,
                                        uint32_t duration_ms)
{
    clear_event(out_event);
    if (!out_event) {
        return;
    }
    out_event->type = NODE_RULE_EVENT_TIMER;
    out_event->timer_index = timer_index;
    out_event->duration_ms = duration_ms;
    copy_text(out_event->event_name, sizeof(out_event->event_name), timer_name);
}

void node_event_router_make_local_event(node_rule_event_t *out_event,
                                        const char *event_name,
                                        const char *source_id,
                                        int32_t token_id,
                                        const char *uid)
{
    clear_event(out_event);
    if (!out_event) {
        return;
    }
    out_event->type = NODE_RULE_EVENT_LOCAL;
    out_event->token_id = token_id;
    copy_text(out_event->event_name, sizeof(out_event->event_name), event_name);
    copy_text(out_event->source_id, sizeof(out_event->source_id), source_id);
    copy_text(out_event->uid, sizeof(out_event->uid), uid);
}

void node_event_router_make_mqtt_command_event(node_rule_event_t *out_event, const char *command_name)
{
    clear_event(out_event);
    if (!out_event) {
        return;
    }
    out_event->type = NODE_RULE_EVENT_MQTT_COMMAND;
    copy_text(out_event->event_name, sizeof(out_event->event_name), command_name);
}
