#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_limits.h"

typedef enum {
    NODE_RULE_EVENT_NONE = 0,
    NODE_RULE_EVENT_BOOT,
    NODE_RULE_EVENT_INPUT_EDGE,
    NODE_RULE_EVENT_INPUT_HOLD,
    NODE_RULE_EVENT_TIMER,
    NODE_RULE_EVENT_LOCAL,
    NODE_RULE_EVENT_MQTT_COMMAND,
} node_rule_event_type_t;

typedef struct {
    node_rule_event_type_t type;
    uint32_t timestamp_ms;
    char event_name[NODE_DRIVER_EVENT_NAME_MAX_LEN];
    char source_id[NODE_DRIVER_ID_MAX_LEN + 1];
    uint8_t input_channel;
    uint16_t timer_index;
    int32_t value;
    int32_t token_id;
    char uid[NODE_DRIVER_UID_TEXT_MAX_LEN];
    uint32_t duration_ms;
} node_rule_event_t;

typedef esp_err_t (*node_event_router_sink_fn)(const node_rule_event_t *event);

esp_err_t node_event_router_init(void);
esp_err_t node_event_router_reset(void);
esp_err_t node_event_router_set_sink(node_event_router_sink_fn sink);
esp_err_t node_event_router_route_event(const node_rule_event_t *event);
esp_err_t node_event_router_route_local_event(const char *event_name,
                                              const char *source_id,
                                              int32_t token_id,
                                              const char *uid);
void node_event_router_make_boot_event(node_rule_event_t *out_event);
void node_event_router_make_input_edge_event(node_rule_event_t *out_event,
                                             uint8_t input_channel,
                                             int32_t value);
void node_event_router_make_input_hold_event(node_rule_event_t *out_event,
                                             uint8_t input_channel,
                                             int32_t value,
                                             uint32_t duration_ms);
void node_event_router_make_timer_event(node_rule_event_t *out_event,
                                        uint16_t timer_index,
                                        const char *timer_name,
                                        uint32_t duration_ms);
void node_event_router_make_local_event(node_rule_event_t *out_event,
                                        const char *event_name,
                                        const char *source_id,
                                        int32_t token_id,
                                        const char *uid);
void node_event_router_make_mqtt_command_event(node_rule_event_t *out_event, const char *command_name);
