#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_limits.h"

typedef struct {
    char id[NODE_DRIVER_EVENT_NAME_MAX_LEN];
    char label[NODE_RULE_EXPORT_LABEL_MAX_LEN + 1];
    uint8_t claim_count;
    char claims[NODE_RULE_MAX_EXPORT_CLAIMS][NODE_DRIVER_EVENT_NAME_MAX_LEN];
} node_runtime_snapshot_export_command_t;

typedef struct {
    char id[NODE_DRIVER_EVENT_NAME_MAX_LEN];
    char label[NODE_RULE_EXPORT_LABEL_MAX_LEN + 1];
} node_runtime_snapshot_export_event_t;

typedef struct {
    bool rules_initialized;
    bool rules_paused;
    bool rules_enabled_by_mode;
    bool has_bundle;
    uint32_t generation;
    size_t compiled_rules;
    size_t compiled_actions;
    char bundle_id[NODE_RULE_BUNDLE_ID_MAX_LEN + 1];
    char compile_status[16];
    size_t emit_count;
    char emit_names[NODE_RULE_MAX_EMIT_EVENTS][NODE_DRIVER_EVENT_NAME_MAX_LEN];
    size_t export_command_count;
    node_runtime_snapshot_export_command_t export_commands[NODE_RULE_MAX_EXPORT_COMMANDS];
    size_t export_event_count;
    node_runtime_snapshot_export_event_t export_events[NODE_RULE_MAX_EXPORT_EVENTS];
    bool fallback_initialized;
    bool fallback_enabled;
    bool fallback_wifi_ready;
    bool fallback_mqtt_connected;
    bool fallback_rules_active;
    char fallback_state[32];
    uint32_t fallback_timeout_ms;
    uint32_t fallback_return_delay_ms;
    char fallback_return_policy[40];
} node_runtime_snapshot_t;

esp_err_t node_runtime_snapshot_capture(node_runtime_snapshot_t *out);
