#include "node_runtime_mode.h"

#include <string.h>

node_operation_mode_t node_runtime_mode_normalize(node_operation_mode_t mode)
{
    switch (mode) {
    case NODE_OPERATION_MODE_SCENEHUB:
    case NODE_OPERATION_MODE_STANDALONE:
    case NODE_OPERATION_MODE_FALLBACK:
        return mode;
    default:
        return NODE_OPERATION_MODE_SCENEHUB;
    }
}

const char *node_runtime_mode_name(node_operation_mode_t mode)
{
    switch (node_runtime_mode_normalize(mode)) {
    case NODE_OPERATION_MODE_STANDALONE:
        return "standalone";
    case NODE_OPERATION_MODE_FALLBACK:
        return "fallback";
    case NODE_OPERATION_MODE_SCENEHUB:
    default:
        return "scenehub";
    }
}

bool node_runtime_mode_from_name(const char *name, node_operation_mode_t *out_mode)
{
    node_operation_mode_t mode = NODE_OPERATION_MODE_SCENEHUB;

    if (!name || !out_mode) {
        return false;
    }
    if (strcmp(name, "scenehub") == 0) {
        mode = NODE_OPERATION_MODE_SCENEHUB;
    } else if (strcmp(name, "standalone") == 0) {
        mode = NODE_OPERATION_MODE_STANDALONE;
    } else if (strcmp(name, "fallback") == 0) {
        mode = NODE_OPERATION_MODE_FALLBACK;
    } else {
        return false;
    }
    *out_mode = mode;
    return true;
}

bool node_runtime_mode_requires_controller(const node_config_t *config)
{
    node_operation_mode_t mode = NODE_OPERATION_MODE_SCENEHUB;

    if (!config) {
        return true;
    }
    mode = node_runtime_mode_normalize((node_operation_mode_t)config->operation_mode);
    return mode == NODE_OPERATION_MODE_SCENEHUB || mode == NODE_OPERATION_MODE_FALLBACK;
}

bool node_runtime_mode_should_start_mqtt(const node_config_t *config)
{
    node_operation_mode_t mode = NODE_OPERATION_MODE_SCENEHUB;

    if (!config) {
        return false;
    }
    mode = node_runtime_mode_normalize((node_operation_mode_t)config->operation_mode);
    if (config->controller_host[0] == '\0') {
        return false;
    }
    if (mode == NODE_OPERATION_MODE_STANDALONE) {
        return config->standalone_mqtt_enabled;
    }
    return true;
}

bool node_runtime_mode_rules_enabled(const node_config_t *config)
{
    node_operation_mode_t mode = NODE_OPERATION_MODE_SCENEHUB;

    if (!config) {
        return false;
    }
    mode = node_runtime_mode_normalize((node_operation_mode_t)config->operation_mode);
    /*
     * Fallback activation is a Phase 8 feature: it should start only after
     * bounded hub-offline detection and must not behave like standalone while
     * Wi-Fi/MQTT are healthy. Until that policy exists, keep local rules
     * enabled only in explicit standalone mode.
     */
    return mode == NODE_OPERATION_MODE_STANDALONE;
}
