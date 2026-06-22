#include "node_provisioning_config_api_internal.h"
#include "node_provisioning_internal.h"

#include <stdio.h>
#include <string.h>

#include "node_runtime_mode.h"

static bool add_string_array(cJSON *parent,
                             const char *key,
                             const char *const *values,
                             size_t value_count)
{
    cJSON *array = NULL;

    if (!parent || !key) {
        return false;
    }
    array = cJSON_AddArrayToObject(parent, key);
    if (!array) {
        return false;
    }
    for (size_t i = 0; i < value_count; ++i) {
        cJSON *item = cJSON_CreateString(values[i]);
        if (!item) {
            return false;
        }
        cJSON_AddItemToArray(array, item);
    }
    return true;
}

static bool add_command_spec(cJSON *commands,
                             const char *command,
                             const char *target_type,
                             const char *args)
{
    cJSON *item = NULL;

    if (!commands || !command) {
        return false;
    }
    item = cJSON_CreateObject();
    if (!item) {
        return false;
    }
    if (!cJSON_AddStringToObject(item, "command", command)) {
        cJSON_Delete(item);
        return false;
    }
    if (target_type && target_type[0] != '\0' &&
        !cJSON_AddStringToObject(item, "target_type", target_type)) {
        cJSON_Delete(item);
        return false;
    }
    if (args && args[0] != '\0' && !cJSON_AddStringToObject(item, "args", args)) {
        cJSON_Delete(item);
        return false;
    }
    cJSON_AddItemToArray(commands, item);
    return true;
}

static bool add_basic_resource(cJSON *array,
                               const char *name,
                               const char *type,
                               uint8_t channel,
                               int gpio,
                               const char *label)
{
    cJSON *item = NULL;

    if (!array || !name || !type) {
        return false;
    }
    item = cJSON_CreateObject();
    if (!item) {
        return false;
    }
    if (!cJSON_AddStringToObject(item, "name", name) ||
        !cJSON_AddStringToObject(item, "type", type) ||
        !cJSON_AddNumberToObject(item, "channel", channel) ||
        !cJSON_AddNumberToObject(item, "gpio", gpio)) {
        cJSON_Delete(item);
        return false;
    }
    if (label && label[0] != '\0' && !cJSON_AddStringToObject(item, "label", label)) {
        cJSON_Delete(item);
        return false;
    }
    cJSON_AddItemToArray(array, item);
    return true;
}

esp_err_t node_provisioning_rules_context_get(httpd_req_t *req)
{
    static const char *const supported_triggers[] = {
        "boot",
        "input_edge",
        "input_hold",
        "timer",
        "local_event",
    };
    static const char *const supported_conditions[] = {
        "state_equals",
        "phase_is",
        "input_equals",
        "event_field_equals",
        "all_inputs_equal",
        "not",
        "all",
        "any",
    };
    static const char *const supported_actions[] = {
        "command",
        "set_state",
        "set_phase",
        "emit_event",
        "start_timer",
        "cancel_timer",
        "sequence",
        "choose",
    };
    static const char *const condition_event_fields[] = {
        "token_id",
    };
    static const char *const local_event_payload_fields[] = {
        "source_id",
        "token_id",
        "uid",
    };
    cJSON *root = NULL;
    cJSON *resources = NULL;
    cJSON *inputs = NULL;
    cJSON *outputs = NULL;
    cJSON *led_strips = NULL;
    cJSON *drivers = NULL;
    cJSON *commands = NULL;
    cJSON *engine = NULL;
    cJSON *limits = NULL;
    cJSON *notes = NULL;
    esp_err_t err = ESP_OK;
    char name[48];

    if (!ensure_provisioning_scratch()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context no mem");
    }
    if (node_admin_control_get_config(&s_get_config) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    }

    root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context no mem");
    }

    if (!cJSON_AddBoolToObject(root, "ok", true) ||
        !cJSON_AddStringToObject(root,
                                 "operation_mode",
                                 node_runtime_mode_name((node_operation_mode_t)s_get_config.operation_mode)) ||
        !cJSON_AddBoolToObject(root, "standalone_mqtt_enabled", s_get_config.standalone_mqtt_enabled) ||
        !cJSON_AddNumberToObject(root, "fallback_timeout_ms", s_get_config.fallback_timeout_ms) ||
        !cJSON_AddNumberToObject(root, "fallback_return_delay_ms", s_get_config.fallback_return_delay_ms) ||
        !cJSON_AddStringToObject(root,
                                 "fallback_return_policy",
                                 fallback_return_policy_text(s_get_config.fallback_return_policy))) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
    }

    resources = cJSON_AddObjectToObject(root, "resources");
    inputs = resources ? cJSON_AddArrayToObject(resources, "inputs") : NULL;
    outputs = resources ? cJSON_AddArrayToObject(resources, "outputs") : NULL;
    led_strips = resources ? cJSON_AddArrayToObject(resources, "led_strips") : NULL;
    drivers = resources ? cJSON_AddArrayToObject(resources, "drivers") : NULL;
    commands = cJSON_AddArrayToObject(root, "commands");
    engine = cJSON_AddObjectToObject(root, "engine");
    limits = cJSON_AddObjectToObject(root, "limits");
    notes = cJSON_AddArrayToObject(root, "authoring_notes");
    if (!resources || !inputs || !outputs || !led_strips || !drivers || !commands || !engine || !limits || !notes) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
    }

    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_universal_pin_config_t *pin = &s_get_config.universal_io[i];
        cJSON *item = NULL;

        if (!pin->enabled || pin->role != NODE_PIN_UNIVERSAL_INPUT) {
            continue;
        }
        snprintf(name, sizeof(name), "input_%u", (unsigned)pin->channel);
        item = cJSON_CreateObject();
        if (!item ||
            !cJSON_AddStringToObject(item, "name", name) ||
            !cJSON_AddStringToObject(item, "type", "input") ||
            !cJSON_AddNumberToObject(item, "channel", pin->channel) ||
            !cJSON_AddNumberToObject(item, "gpio", pin->gpio) ||
            !cJSON_AddNumberToObject(item, "debounce_ms", pin->debounce_ms)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
        if (pin->label[0] != '\0' && !cJSON_AddStringToObject(item, "label", pin->label)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
        cJSON_AddItemToArray(inputs, item);
    }
    for (size_t i = 0; i < NODE_RELAY_MAX; ++i) {
        const node_output_pin_config_t *pin = &s_get_config.relays[i];

        if (!pin->enabled) {
            continue;
        }
        snprintf(name, sizeof(name), "relay_%u", (unsigned)pin->channel);
        if (!add_basic_resource(outputs, name, "relay", pin->channel, pin->gpio, pin->label)) {
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
    }
    for (size_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        const node_output_pin_config_t *pin = &s_get_config.mosfets[i];

        if (!pin->enabled) {
            continue;
        }
        snprintf(name, sizeof(name), "mosfet_%u", (unsigned)pin->channel);
        if (!add_basic_resource(outputs, name, "mosfet", pin->channel, pin->gpio, pin->label)) {
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
    }
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_universal_pin_config_t *pin = &s_get_config.universal_io[i];

        if (!pin->enabled || pin->role != NODE_PIN_UNIVERSAL_OUTPUT) {
            continue;
        }
        snprintf(name, sizeof(name), "output_%u", (unsigned)pin->channel);
        if (!add_basic_resource(outputs, name, "io_output", pin->channel, pin->gpio, pin->label)) {
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
    }
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *strip = &s_get_config.led_strips[i];
        cJSON *item = NULL;

        if (!strip->enabled) {
            continue;
        }
        snprintf(name, sizeof(name), "strip_%u", (unsigned)strip->channel);
        item = cJSON_CreateObject();
        if (!item ||
            !cJSON_AddStringToObject(item, "name", name) ||
            !cJSON_AddStringToObject(item, "type", "led_strip") ||
            !cJSON_AddNumberToObject(item, "channel", strip->channel) ||
            !cJSON_AddNumberToObject(item, "gpio", strip->gpio) ||
            !cJSON_AddNumberToObject(item, "pixel_count", strip->pixel_count) ||
            !cJSON_AddBoolToObject(item, "rgbw", strip->rgbw)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
        if (strip->label[0] != '\0' && !cJSON_AddStringToObject(item, "label", strip->label)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
        cJSON_AddItemToArray(led_strips, item);
    }
    {
        memset(&s_nfc_reader_scratch, 0, sizeof(s_nfc_reader_scratch));
        if (node_driver_nfc_config_api_load_factory_config(&s_get_config, &s_nfc_reader_scratch) == ESP_OK) {
            cJSON *item = cJSON_CreateObject();
            cJSON *known_cards = NULL;

            if (!item ||
                !cJSON_AddStringToObject(item, "id", s_nfc_reader_scratch.id) ||
                !cJSON_AddStringToObject(item, "type", "nfc_reader") ||
                !cJSON_AddStringToObject(item, "driver", s_nfc_reader_scratch.driver_impl) ||
                !cJSON_AddStringToObject(item, "bus", s_nfc_reader_scratch.bus) ||
                !cJSON_AddBoolToObject(item, "enabled", s_nfc_reader_scratch.enabled) ||
                !cJSON_AddNumberToObject(item, "poll_interval_ms", s_nfc_reader_scratch.poll_interval_ms) ||
                !cJSON_AddNumberToObject(item, "debounce_ms", s_nfc_reader_scratch.debounce_ms)) {
                cJSON_Delete(item);
                cJSON_Delete(root);
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
            }
            known_cards = cJSON_AddArrayToObject(item, "known_cards");
            if (!known_cards) {
                cJSON_Delete(item);
                cJSON_Delete(root);
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
            }
            for (size_t i = 0; i < s_nfc_reader_scratch.known_card_count; ++i) {
                cJSON *card = cJSON_CreateObject();

                if (!card ||
                    !cJSON_AddStringToObject(card, "name", s_nfc_reader_scratch.known_cards[i].name) ||
                    !cJSON_AddStringToObject(card, "uid", s_nfc_reader_scratch.known_cards[i].uid) ||
                    !cJSON_AddNumberToObject(card, "token_id", s_nfc_reader_scratch.known_cards[i].token_id)) {
                    cJSON_Delete(card);
                    cJSON_Delete(item);
                    cJSON_Delete(root);
                    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
                }
                if (s_nfc_reader_scratch.known_cards[i].event_name[0] != '\0' &&
                    !cJSON_AddStringToObject(card, "event", s_nfc_reader_scratch.known_cards[i].event_name)) {
                    cJSON_Delete(card);
                    cJSON_Delete(item);
                    cJSON_Delete(root);
                    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
                }
                cJSON_AddItemToArray(known_cards, card);
            }
            cJSON_AddItemToArray(drivers, item);
        }
    }

    if (!add_command_spec(commands, "relay.set", "relay", "{output,on}") ||
        !add_command_spec(commands, "relay.pulse", "relay", "{output,duration_ms}") ||
        !add_command_spec(commands, "relay.all_off", "", "{}") ||
        !add_command_spec(commands, "mosfet.set", "mosfet", "{output,value}") ||
        !add_command_spec(commands, "mosfet.fade", "mosfet", "{output,target[,duration_ms]}") ||
        !add_command_spec(commands, "mosfet.pulse", "mosfet", "{output,value[,duration_ms]}") ||
        !add_command_spec(commands, "mosfet.blink", "mosfet", "{output[,value,final_value,on_ms,off_ms,count]}") ||
        !add_command_spec(commands, "mosfet.breathe", "mosfet", "{output[,min,max,final_value,fade_ms,hold_ms,count]}") ||
        !add_command_spec(commands, "mosfet.effect", "mosfet", "{output,effect[,value,final_value,on_ms,off_ms,fade_ms,hold_ms,count,min,max]}") ||
        !add_command_spec(commands, "mosfet.all_off", "", "{}") ||
        !add_command_spec(commands, "io.set", "io_output", "{output,on}") ||
        !add_command_spec(commands, "io.all_off", "", "{}") ||
        !add_command_spec(commands, "led.off", "led_strip", "{output}") ||
        !add_command_spec(commands, "led.solid", "led_strip", "{output,color}") ||
        !add_command_spec(commands, "led.blink", "led_strip", "{output,color[,on_ms,off_ms,count]}") ||
        !add_command_spec(commands, "led.breathe", "led_strip", "{output,color[,duration_ms,step_ms,count]}") ||
        !add_command_spec(commands, "led.effect", "led_strip", "{output,effect[,duration_ms,step_ms,count,size,intensity,density,fade,palette_mode,color,secondary_color,background_color]}") ||
        !add_command_spec(commands, "node.all_off", "", "{}")) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
    }

    if (!add_string_array(engine,
                          "supported_triggers",
                          supported_triggers,
                          sizeof(supported_triggers) / sizeof(supported_triggers[0])) ||
        !add_string_array(engine,
                          "supported_conditions",
                          supported_conditions,
                          sizeof(supported_conditions) / sizeof(supported_conditions[0])) ||
        !add_string_array(engine,
                          "supported_actions",
                          supported_actions,
                          sizeof(supported_actions) / sizeof(supported_actions[0])) ||
        !add_string_array(engine,
                          "condition_event_fields",
                          condition_event_fields,
                          sizeof(condition_event_fields) / sizeof(condition_event_fields[0])) ||
        !add_string_array(engine,
                          "local_event_payload_fields",
                          local_event_payload_fields,
                          sizeof(local_event_payload_fields) / sizeof(local_event_payload_fields[0])) ||
        !cJSON_AddStringToObject(engine,
                                 "wait_model",
                                 "Use phase changes plus timers or later events; do not model blocking waits.") ||
        !cJSON_AddStringToObject(engine,
                                 "branch_model",
                                 "Use all/any/not and choose; loops and recursion are unsupported.")) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
    }

    if (!cJSON_AddNumberToObject(limits, "max_bundle_bytes", NODE_RULE_BUNDLE_STORE_MAX_LEN) ||
        !cJSON_AddNumberToObject(limits, "max_bundle_bytes_http", NODE_RULE_BUNDLE_HTTP_MAX_LEN) ||
        !cJSON_AddNumberToObject(limits, "max_bundle_bytes_mqtt_admin", NODE_RULE_BUNDLE_MQTT_MAX_LEN) ||
        !cJSON_AddNumberToObject(limits, "max_rules", NODE_RULE_MAX_RULES) ||
        !cJSON_AddNumberToObject(limits, "max_total_actions", NODE_RULE_MAX_ACTIONS_TOTAL) ||
        !cJSON_AddNumberToObject(limits, "max_actions_per_rule", NODE_RULE_MAX_ACTIONS_PER_RULE) ||
        !cJSON_AddNumberToObject(limits, "max_action_nesting", NODE_RULE_MAX_ACTION_NESTING) ||
        !cJSON_AddNumberToObject(limits, "max_emit_events", NODE_RULE_MAX_EMIT_EVENTS) ||
        !cJSON_AddNumberToObject(limits, "max_state_keys", NODE_RULE_MAX_STATE_KEYS) ||
        !cJSON_AddNumberToObject(limits, "max_timers", NODE_RULE_MAX_TIMERS) ||
        !cJSON_AddNumberToObject(limits, "max_phases", NODE_RULE_MAX_PHASES) ||
        !cJSON_AddNumberToObject(limits, "max_group_inputs", NODE_RULE_MAX_GROUP_INPUTS)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
    }

    {
        cJSON *note1 = cJSON_CreateString("Use only logical resource names listed under resources.");
        cJSON *note2 =
            cJSON_CreateString("In standalone mode, apply stores the bundle and reboot activates it.");
        cJSON *note3 = cJSON_CreateString(
            "For RFID/NFC flows, map driver card events into local_event rules and branch with event_field_equals on token_id.");
        cJSON *note4 = cJSON_CreateString(
            "Unsupported trigger kinds or conditions must be rejected at compile time instead of emulated in script.");

        if (!note1 || !note2 || !note3 || !note4) {
            cJSON_Delete(note1);
            cJSON_Delete(note2);
            cJSON_Delete(note3);
            cJSON_Delete(note4);
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
        cJSON_AddItemToArray(notes, note1);
        cJSON_AddItemToArray(notes, note2);
        cJSON_AddItemToArray(notes, note3);
        cJSON_AddItemToArray(notes, note4);
    }
    if (!cJSON_AddStringToObject(root,
                                 "prompt_template",
                                 "Author a standalone_bundle JSON for this node. Use only listed resources, commands, triggers, conditions and actions. Respect limits. Model waits with set_phase plus start_timer and later timer or local_event rules. Do not generate loops, recursion or arbitrary expressions.")) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
    }

    if (!lock_config_json()) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context busy");
    }
    if (!ensure_config_json_buffer()) {
        unlock_config_json();
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context no mem");
    }
    if (!cJSON_PrintPreallocated(root,
                                 s_config_json,
                                 NODE_PROVISIONING_CONFIG_JSON_CAPACITY,
                                 true)) {
        cJSON_Delete(root);
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context response too large");
    }
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    err = httpd_resp_send(req, s_config_json, HTTPD_RESP_USE_STRLEN);
    unlock_config_json();
    return err;
}
