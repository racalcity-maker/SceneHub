#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY = 0,
    NODE_FALLBACK_RUNTIME_STATE_HUB_OFFLINE_PENDING,
    NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE,
    NODE_FALLBACK_RUNTIME_STATE_HUB_RETURN_PENDING,
} node_fallback_runtime_state_t;

typedef enum {
    NODE_FALLBACK_RETURN_POLICY_AUTO_ON_STABLE_MQTT = 0,
    NODE_FALLBACK_RETURN_POLICY_MANUAL_STAY_ACTIVE,
} node_fallback_runtime_return_policy_t;

typedef struct {
    bool enabled;
    uint32_t fallback_timeout_ms;
    uint32_t fallback_return_delay_ms;
    node_fallback_runtime_return_policy_t return_policy;
} node_fallback_runtime_config_t;

typedef struct {
    bool initialized;
    bool enabled;
    bool wifi_ready;
    bool mqtt_connected;
    bool fallback_rules_active;
    node_fallback_runtime_state_t state;
    uint32_t state_since_ms;
    uint32_t deadline_ms;
    uint32_t fallback_timeout_ms;
    uint32_t fallback_return_delay_ms;
    node_fallback_runtime_return_policy_t return_policy;
} node_fallback_runtime_status_t;

esp_err_t node_fallback_runtime_init(void);
esp_err_t node_fallback_runtime_configure(const node_fallback_runtime_config_t *config);
esp_err_t node_fallback_runtime_note_wifi_state(bool connected);
esp_err_t node_fallback_runtime_note_mqtt_state(bool connected);
void node_fallback_runtime_get_status(node_fallback_runtime_status_t *out_status);
const char *node_fallback_runtime_state_name(node_fallback_runtime_state_t state);
const char *node_fallback_runtime_return_policy_name(node_fallback_runtime_return_policy_t policy);
