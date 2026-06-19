#include "node_control_internal.h"

#include <string.h>

#include "esp_log.h"
#include "node_capability.h"
#include "node_fallback_runtime.h"
#include "node_hardware_io.h"
#include "node_rule_compile.h"
#include "node_rule_engine.h"
#include "node_runtime_mode.h"

static const char *TAG = "node_control_desc";

esp_err_t execute_get_status(node_control_result_t *result)
{
    node_hardware_io_status_t status = node_hardware_io_get_status();
    const node_rule_compiled_bundle_t *compiled = node_rule_compile_peek_active();
    node_rule_engine_status_t rules_runtime = {0};
    node_fallback_runtime_status_t fallback_runtime = {0};
    const char *rules_status = "inactive";
    bool has_bundle = false;

    node_rule_engine_get_status(&rules_runtime);
    node_fallback_runtime_get_status(&fallback_runtime);
    if (compiled) {
        rules_status = node_rule_compile_status_name(compiled->status);
        has_bundle = compiled->metadata.has_bundle;
    }

    int n = snprintf(result->data_json,
                     sizeof(result->data_json),
                     "{\"operation_mode\":\"%s\",\"standalone_mqtt_enabled\":%s,\"hardware\":{\"relays\":%u,\"mosfets\":%u,\"universal_inputs\":%u,"
                     "\"universal_outputs\":%u,\"led_strips\":%u},"
                     "\"rules\":{\"supported\":%s,\"enabled_by_mode\":%s,\"initialized\":%s,\"paused\":%s,"
                     "\"compile_status\":\"%s\",\"has_bundle\":%s,\"bundle_id\":\"%s\",\"generation\":%lu,"
                     "\"compiled_rules\":%lu,\"compiled_actions\":%lu},"
                     "\"fallback\":{\"configured\":%s,\"initialized\":%s,\"enabled\":%s,\"state\":\"%s\","
                     "\"wifi_ready\":%s,\"mqtt_connected\":%s,\"rules_active\":%s,"
                     "\"timeout_ms\":%lu,\"return_delay_ms\":%lu,\"return_policy\":\"%s\"}}",
                     node_runtime_mode_name((node_operation_mode_t)g_node_control_config.operation_mode),
                     g_node_control_config.standalone_mqtt_enabled ? "true" : "false",
                     (unsigned)status.configured_relays,
                     (unsigned)status.configured_mosfets,
                     (unsigned)status.configured_universal_inputs,
                     (unsigned)status.configured_universal_outputs,
                     (unsigned)status.configured_led_strips,
                     node_runtime_mode_rules_enabled(&g_node_control_config) ? "true" : "false",
                     rules_runtime.rules_enabled_by_mode ? "true" : "false",
                     rules_runtime.initialized ? "true" : "false",
                     rules_runtime.paused ? "true" : "false",
                     rules_status,
                     has_bundle ? "true" : "false",
                     has_bundle ? compiled->metadata.bundle_id : "",
                     (unsigned long)(has_bundle ? compiled->metadata.generation : 0U),
                     (unsigned long)(compiled ? compiled->rule_count : 0U),
                     (unsigned long)(compiled ? compiled->total_action_count : 0U),
                     (g_node_control_config.operation_mode == NODE_OPERATION_MODE_FALLBACK &&
                      fallback_runtime.fallback_timeout_ms > 0U)
                         ? "true"
                         : "false",
                     fallback_runtime.initialized ? "true" : "false",
                     fallback_runtime.enabled ? "true" : "false",
                     node_fallback_runtime_state_name(fallback_runtime.state),
                     fallback_runtime.wifi_ready ? "true" : "false",
                     fallback_runtime.mqtt_connected ? "true" : "false",
                     fallback_runtime.fallback_rules_active ? "true" : "false",
                     (unsigned long)fallback_runtime.fallback_timeout_ms,
                     (unsigned long)fallback_runtime.fallback_return_delay_ms,
                     node_fallback_runtime_return_policy_name(fallback_runtime.return_policy));
    if (n < 0 || n >= (int)sizeof(result->data_json)) {
        return result_rejected(result, "internal_error");
    }
    result_done(result);
    return ESP_OK;
}

esp_err_t execute_describe_interface(node_control_result_t *result)
{
    size_t written = 0;
    static const char prefix[] = "{\"device_description\":";
    static const char suffix[] = "}";
    const size_t prefix_len = sizeof(prefix) - 1;
    const size_t suffix_len = sizeof(suffix) - 1;

    if (sizeof(result->data_json) <= prefix_len + suffix_len) {
        return result_rejected(result, "internal_error");
    }

    memcpy(result->data_json, prefix, prefix_len);
    esp_err_t err = node_capability_write_device_description(&g_node_control_config,
                                                             result->data_json + prefix_len,
                                                             sizeof(result->data_json) - prefix_len - suffix_len,
                                                             &written);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "device_description does not fit result buffer cap=%u err=%s",
                 (unsigned)sizeof(result->data_json),
                 esp_err_to_name(err));
        return result_rejected(result, "internal_error");
    }
    memcpy(result->data_json + prefix_len + written, suffix, suffix_len + 1);
    result_done(result);
    return ESP_OK;
}
