#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "node_config.h"
#include "node_event_router.h"

typedef struct {
    bool initialized;
    bool paused;
    bool rules_enabled_by_mode;
} node_rule_engine_status_t;

esp_err_t node_rule_engine_init(const node_config_t *config);
esp_err_t node_rule_engine_reset(void);
esp_err_t node_rule_engine_set_runtime_enabled(bool enabled);
esp_err_t node_rule_engine_pause(void);
esp_err_t node_rule_engine_resume(void);
void node_rule_engine_get_status(node_rule_engine_status_t *out_status);
esp_err_t node_rule_engine_dispatch_event(const node_rule_event_t *event);
esp_err_t node_rule_engine_dispatch_local_event(const char *event_name,
                                                const char *source_id,
                                                int32_t token_id,
                                                const char *uid);
esp_err_t node_rule_engine_dispatch_mqtt_command(const char *command_name);
