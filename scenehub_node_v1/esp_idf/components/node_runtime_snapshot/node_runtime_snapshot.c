#include "node_runtime_snapshot.h"

#include <stdio.h>
#include <string.h>

#include "node_fallback_runtime.h"
#include "node_rule_api.h"
#include "node_rule_compile.h"

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void capture_rules(node_runtime_snapshot_t *snapshot)
{
    node_rule_api_runtime_status_t runtime = {0};
    const node_rule_compiled_bundle_t *compiled = NULL;

    if (!snapshot) {
        return;
    }

    node_rule_api_get_runtime_status(&runtime);
    compiled = node_rule_compile_peek_active();

    snapshot->rules_initialized = runtime.initialized;
    snapshot->rules_paused = runtime.paused;
    snapshot->rules_enabled_by_mode = runtime.rules_enabled_by_mode;
    snapshot->has_bundle = runtime.has_bundle;
    snapshot->generation = runtime.generation;
    snapshot->compiled_rules = runtime.compiled_rules;
    snapshot->compiled_actions = runtime.compiled_actions;
    copy_text(snapshot->bundle_id, sizeof(snapshot->bundle_id), runtime.bundle_id);
    copy_text(snapshot->compile_status, sizeof(snapshot->compile_status), runtime.compile_status);

    if (!compiled || compiled->status != NODE_RULE_COMPILE_STATUS_READY) {
        return;
    }

    snapshot->emit_count = compiled->emit_count > NODE_RULE_MAX_EMIT_EVENTS
                               ? NODE_RULE_MAX_EMIT_EVENTS
                               : compiled->emit_count;
    for (size_t i = 0; i < snapshot->emit_count; ++i) {
        copy_text(snapshot->emit_names[i],
                  sizeof(snapshot->emit_names[i]),
                  compiled->emit_names[i]);
    }

    snapshot->export_command_count =
        compiled->export_command_count > NODE_RULE_MAX_EXPORT_COMMANDS
            ? NODE_RULE_MAX_EXPORT_COMMANDS
            : compiled->export_command_count;
    for (size_t i = 0; i < snapshot->export_command_count; ++i) {
        const node_rule_exported_command_t *src = &compiled->export_commands[i];
        node_runtime_snapshot_export_command_t *dst = &snapshot->export_commands[i];

        copy_text(dst->id, sizeof(dst->id), src->id);
        copy_text(dst->label, sizeof(dst->label), src->label);
        dst->claim_count = src->claim_count > NODE_RULE_MAX_EXPORT_CLAIMS
                               ? NODE_RULE_MAX_EXPORT_CLAIMS
                               : src->claim_count;
        for (size_t claim = 0; claim < dst->claim_count; ++claim) {
            copy_text(dst->claims[claim], sizeof(dst->claims[claim]), src->claims[claim]);
        }
    }

    snapshot->export_event_count =
        compiled->export_event_count > NODE_RULE_MAX_EXPORT_EVENTS
            ? NODE_RULE_MAX_EXPORT_EVENTS
            : compiled->export_event_count;
    for (size_t i = 0; i < snapshot->export_event_count; ++i) {
        const node_rule_exported_event_t *src = &compiled->export_events[i];
        node_runtime_snapshot_export_event_t *dst = &snapshot->export_events[i];

        copy_text(dst->id, sizeof(dst->id), src->id);
        copy_text(dst->label, sizeof(dst->label), src->label);
    }
}

static void capture_fallback(node_runtime_snapshot_t *snapshot)
{
    node_fallback_runtime_status_t fallback = {0};

    if (!snapshot) {
        return;
    }

    node_fallback_runtime_get_status(&fallback);

    snapshot->fallback_initialized = fallback.initialized;
    snapshot->fallback_enabled = fallback.enabled;
    snapshot->fallback_wifi_ready = fallback.wifi_ready;
    snapshot->fallback_mqtt_connected = fallback.mqtt_connected;
    snapshot->fallback_rules_active = fallback.fallback_rules_active;
    snapshot->fallback_timeout_ms = fallback.fallback_timeout_ms;
    snapshot->fallback_return_delay_ms = fallback.fallback_return_delay_ms;
    copy_text(snapshot->fallback_state,
              sizeof(snapshot->fallback_state),
              node_fallback_runtime_state_name(fallback.state));
    copy_text(snapshot->fallback_return_policy,
              sizeof(snapshot->fallback_return_policy),
              node_fallback_runtime_return_policy_name(fallback.return_policy));
}

esp_err_t node_runtime_snapshot_capture(node_runtime_snapshot_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    capture_rules(out);
    capture_fallback(out);
    return ESP_OK;
}
