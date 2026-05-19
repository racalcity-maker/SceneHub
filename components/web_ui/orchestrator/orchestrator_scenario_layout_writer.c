#include "orchestrator/orchestrator_scenario_layout_writer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_attr.h"
#include "gm/web_ui_gm_runtime_json_writer.h"
#include "web_ui_utils.h"

static EXT_RAM_BSS_ATTR char s_room_scenario_json_chunk[2048];

static const char *orch_layout_reactive_trigger_kind_to_str(room_scenario_reactive_trigger_kind_t kind)
{
    switch (kind) {
    case ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT:
        return "device_event";
    case ROOM_SCENARIO_REACTIVE_TRIGGER_FLAG_CHANGED:
        return "flag_changed";
    case ROOM_SCENARIO_REACTIVE_TRIGGER_OPERATOR_EVENT:
        return "operator_event";
    case ROOM_SCENARIO_REACTIVE_TRIGGER_RUNTIME_EVENT:
        return "runtime_event";
    case ROOM_SCENARIO_REACTIVE_TRIGGER_NONE:
    default:
        return "none";
    }
}

static const char *orch_layout_reactive_policy_mode_to_str(room_scenario_reactive_policy_mode_t mode)
{
    switch (mode) {
    case ROOM_SCENARIO_REACTIVE_POLICY_ROTATE:
        return "rotate";
    case ROOM_SCENARIO_REACTIVE_POLICY_RANDOM:
        return "random";
    case ROOM_SCENARIO_REACTIVE_POLICY_ESCALATE:
        return "escalate";
    case ROOM_SCENARIO_REACTIVE_POLICY_SINGLE:
    default:
        return "single";
    }
}

static const char *orch_layout_reactive_result_action_to_str(room_scenario_reactive_result_action_t action)
{
    switch (action) {
    case ROOM_SCENARIO_REACTIVE_RESULT_SET_FLAG:
        return "set_flag";
    case ROOM_SCENARIO_REACTIVE_RESULT_FAIL_REACTION:
        return "fail_reaction";
    case ROOM_SCENARIO_REACTIVE_RESULT_FAIL_SCENARIO:
        return "fail_scenario";
    case ROOM_SCENARIO_REACTIVE_RESULT_RETRY:
        return "retry";
    case ROOM_SCENARIO_REACTIVE_RESULT_CONTINUE:
    default:
        return "continue";
    }
}

static esp_err_t orch_write_layout_json_object_field(gm_runtime_json_writer_t *writer,
                                                     bool *first,
                                                     const char *key,
                                                     const char *json)
{
    cJSON *parsed = NULL;
    char *printed = NULL;
    esp_err_t err = ESP_OK;
    if (!writer || !first || !key) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!json || !json[0]) {
        return ESP_OK;
    }
    parsed = cJSON_Parse(json);
    if (!cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        return ESP_ERR_INVALID_ARG;
    }
    printed = cJSON_PrintUnformatted(parsed);
    cJSON_Delete(parsed);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }
    err = gm_runtime_json_begin_field(writer, first, key);
    if (err == ESP_OK) {
        err = gm_runtime_json_write_raw(writer, printed);
    }
    cJSON_free(printed);
    return err;
}

static esp_err_t orch_write_layout_command_object(gm_runtime_json_writer_t *writer,
                                                  const char *device_id,
                                                  const char *command_id,
                                                  const char *params_json)
{
    bool first = true;
    esp_err_t err = ESP_OK;
    if (!writer) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "{");
    if (err != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, &first, "device_id", device_id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, &first, "command_id", command_id)) != ESP_OK ||
        (err = orch_write_layout_json_object_field(writer, &first, "params", params_json)) != ESP_OK ||
        (err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

static esp_err_t orch_write_layout_reactive_command_array(gm_runtime_json_writer_t *writer,
                                                          const room_scenario_device_command_t *commands,
                                                          uint8_t count)
{
    esp_err_t err = ESP_OK;
    if (!writer || (!commands && count > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < count; ++i) {
        if (i > 0 && (err = gm_runtime_json_write_raw(writer, ",")) != ESP_OK) {
            return err;
        }
        err = orch_write_layout_command_object(writer,
                                               commands[i].device_id,
                                               commands[i].command_id,
                                               commands[i].params_json);
        if (err != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t orch_write_layout_reactive_trigger(gm_runtime_json_writer_t *writer,
                                                    const room_scenario_reactive_trigger_t *trigger)
{
    bool first = true;
    esp_err_t err = ESP_OK;
    if (!writer || !trigger) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "{");
    if (err != ESP_OK ||
        (err = gm_runtime_json_write_string_field(
             writer,
             &first,
             "kind",
             orch_layout_reactive_trigger_kind_to_str(trigger->kind))) != ESP_OK) {
        return err;
    }
    if (trigger->device_id[0] &&
        (err = gm_runtime_json_write_string_field(writer, &first, "device_id", trigger->device_id)) != ESP_OK) {
        return err;
    }
    if (trigger->event_id[0] &&
        (err = gm_runtime_json_write_string_field(writer, &first, "event_id", trigger->event_id)) != ESP_OK) {
        return err;
    }
    if (trigger->flag_name[0] &&
        (err = gm_runtime_json_write_string_field(writer, &first, "flag_name", trigger->flag_name)) != ESP_OK) {
        return err;
    }
    if (trigger->operator_event[0] &&
        (err = gm_runtime_json_write_string_field(writer, &first, "operator_event", trigger->operator_event)) != ESP_OK) {
        return err;
    }
    if (trigger->runtime_event[0] &&
        (err = gm_runtime_json_write_string_field(writer, &first, "runtime_event", trigger->runtime_event)) != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_raw(writer, "}");
}

static esp_err_t orch_write_layout_reactive_guards(gm_runtime_json_writer_t *writer,
                                                   const room_scenario_flag_condition_t *guards,
                                                   uint8_t count)
{
    esp_err_t err = ESP_OK;
    if (!writer || (!guards && count > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < count; ++i) {
        bool first = true;
        if (i > 0 && (err = gm_runtime_json_write_raw(writer, ",")) != ESP_OK) {
            return err;
        }
        err = gm_runtime_json_write_raw(writer, "{");
        if (err != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer, &first, "flag", guards[i].name)) != ESP_OK ||
            (err = gm_runtime_json_write_bool_field(writer, &first, "value", guards[i].value)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t orch_write_layout_step_command_group(gm_runtime_json_writer_t *writer,
                                                      const room_scenario_device_command_group_t *group)
{
    esp_err_t err = ESP_OK;
    if (!writer || !group) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < group->command_count && i < ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS; ++i) {
        if (i > 0 && (err = gm_runtime_json_write_raw(writer, ",")) != ESP_OK) {
            return err;
        }
        err = orch_write_layout_command_object(writer,
                                               group->commands[i].device_id,
                                               group->commands[i].command_id,
                                               group->commands[i].params_json);
        if (err != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t orch_write_layout_event_array(gm_runtime_json_writer_t *writer,
                                               const room_scenario_device_event_ref_t *events,
                                               uint8_t count)
{
    esp_err_t err = ESP_OK;
    if (!writer || (!events && count > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < count; ++i) {
        bool first = true;
        if (i > 0 && (err = gm_runtime_json_write_raw(writer, ",")) != ESP_OK) {
            return err;
        }
        err = gm_runtime_json_write_raw(writer, "{");
        if (err != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer, &first, "device_id", events[i].device_id)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer, &first, "event_id", events[i].event_id)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t orch_write_layout_flag_array(gm_runtime_json_writer_t *writer,
                                              const room_scenario_flag_condition_t *flags,
                                              uint8_t count)
{
    esp_err_t err = ESP_OK;
    if (!writer || (!flags && count > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < count; ++i) {
        bool first = true;
        if (i > 0 && (err = gm_runtime_json_write_raw(writer, ",")) != ESP_OK) {
            return err;
        }
        err = gm_runtime_json_write_raw(writer, "{");
        if (err != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer, &first, "flag_name", flags[i].name)) != ESP_OK ||
            (err = gm_runtime_json_write_bool_field(writer, &first, "value", flags[i].value)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t orch_write_scenario_layout_step(gm_runtime_json_writer_t *writer,
                                                 const room_scenario_step_t *step)
{
    bool first = true;
    esp_err_t err = ESP_OK;
    if (!writer || !step) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "{");
    if (err != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, &first, "id", step->id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, &first, "label", step->label)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, &first, "type", room_scenario_step_type_to_str(step->type))) != ESP_OK ||
        (err = gm_runtime_json_write_bool_field(writer, &first, "enabled", step->enabled)) != ESP_OK) {
        return err;
    }
    switch (step->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        err = gm_runtime_json_write_string_field(writer, &first, "device_id", step->data.device_command.device_id);
        if (err == ESP_OK) {
            err = gm_runtime_json_write_string_field(writer, &first, "command_id", step->data.device_command.command_id);
        }
        if (err == ESP_OK) {
            err = orch_write_layout_json_object_field(writer,
                                                     &first,
                                                     "params",
                                                     step->data.device_command.params_json);
        }
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        err = gm_runtime_json_begin_field(writer, &first, "commands");
        if (err == ESP_OK) {
            err = orch_write_layout_step_command_group(writer, &step->data.device_command_group);
        }
        break;
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        err = gm_runtime_json_write_string_field(writer, &first, "device_id", step->data.wait_device_event.device_id);
        if (err == ESP_OK) {
            err = gm_runtime_json_write_string_field(writer, &first, "event_id", step->data.wait_device_event.event_id);
        }
        break;
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        err = gm_runtime_json_begin_field(writer, &first, "events");
        if (err == ESP_OK) {
            err = orch_write_layout_event_array(writer,
                                                step->data.wait_any_device_event.events,
                                                step->data.wait_any_device_event.event_count);
        }
        break;
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        err = gm_runtime_json_begin_field(writer, &first, "events");
        if (err == ESP_OK) {
            err = orch_write_layout_event_array(writer,
                                                step->data.wait_all_device_events.events,
                                                step->data.wait_all_device_events.event_count);
        }
        break;
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        err = gm_runtime_json_write_uint64_field(writer, &first, "duration_ms", step->data.wait_time.duration_ms);
        break;
    case ROOM_SCENARIO_STEP_WAIT_FLAGS:
        err = gm_runtime_json_begin_field(writer, &first, "flags");
        if (err == ESP_OK) {
            err = orch_write_layout_flag_array(writer,
                                               step->data.wait_flags.flags,
                                               step->data.wait_flags.flag_count);
        }
        if (err == ESP_OK && step->data.wait_flags.timeout_ms > 0) {
            err = gm_runtime_json_write_uint64_field(writer,
                                                     &first,
                                                     "timeout_ms",
                                                     step->data.wait_flags.timeout_ms);
        }
        if (err == ESP_OK && step->data.wait_flags.timeout_message[0]) {
            err = gm_runtime_json_write_string_field(writer,
                                                     &first,
                                                     "timeout_message",
                                                     step->data.wait_flags.timeout_message);
        }
        break;
    case ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        err = gm_runtime_json_write_string_field(writer, &first, "prompt", step->data.operator_approval.prompt);
        if (err == ESP_OK) {
            err = gm_runtime_json_write_string_field(writer, &first, "operator_prompt", step->data.operator_approval.prompt);
        }
        if (err == ESP_OK) {
            err = gm_runtime_json_write_string_field(writer, &first, "approve_label", step->data.operator_approval.approve_label);
        }
        if (err == ESP_OK) {
            err = gm_runtime_json_write_string_field(writer,
                                                    &first,
                                                    "operator_approve_label",
                                                    step->data.operator_approval.approve_label);
        }
        break;
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        err = gm_runtime_json_write_string_field(writer, &first, "message", step->data.operator_message.message);
        break;
    case ROOM_SCENARIO_STEP_SET_FLAG:
        err = gm_runtime_json_write_string_field(writer, &first, "flag_name", step->data.set_flag.name);
        if (err == ESP_OK) {
            err = gm_runtime_json_write_bool_field(writer, &first, "value", step->data.set_flag.value);
        }
        break;
    default:
        err = ESP_OK;
        break;
    }
    if (err != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_raw(writer, "}");
}

static esp_err_t orch_write_scenario_layout_action(gm_runtime_json_writer_t *writer,
                                                   const room_scenario_t *scenario,
                                                   const room_scenario_reactive_action_t *action)
{
    bool first = true;
    esp_err_t err = ESP_OK;
    if (!writer || !scenario || !action) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "{");
    if (err != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, &first, "id", action->id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, &first, "label", action->label)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, &first, "type", room_scenario_step_type_to_str(action->type))) != ESP_OK ||
        (err = gm_runtime_json_write_bool_field(writer, &first, "enabled", true)) != ESP_OK) {
        return err;
    }
    switch (action->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        err = gm_runtime_json_write_string_field(writer, &first, "device_id", action->data.device_command.device_id);
        if (err == ESP_OK) {
            err = gm_runtime_json_write_string_field(writer, &first, "command_id", action->data.device_command.command_id);
        }
        if (err == ESP_OK) {
            err = orch_write_layout_json_object_field(writer,
                                                     &first,
                                                     "params",
                                                     action->data.device_command.params_json);
        }
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP: {
        err = gm_runtime_json_begin_field(writer, &first, "commands");
        if (err == ESP_OK) {
            size_t available = action->group_command_start_index < scenario->reactive_group_command_count
                                   ? scenario->reactive_group_command_count - action->group_command_start_index
                                   : 0;
            uint8_t count = action->group_command_count < available
                                ? action->group_command_count
                                : (uint8_t)available;
            err = orch_write_layout_reactive_command_array(
                writer,
                count > 0 ? &scenario->reactive_group_commands[action->group_command_start_index] : NULL,
                count);
        }
        break;
    }
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        err = gm_runtime_json_write_uint64_field(writer, &first, "duration_ms", action->data.wait_time.duration_ms);
        break;
    case ROOM_SCENARIO_STEP_SET_FLAG:
        err = gm_runtime_json_write_string_field(writer, &first, "flag_name", action->data.set_flag.name);
        if (err == ESP_OK) {
            err = gm_runtime_json_write_bool_field(writer, &first, "value", action->data.set_flag.value);
        }
        break;
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        err = gm_runtime_json_write_string_field(writer, &first, "message", action->data.operator_message.message);
        break;
    default:
        err = ESP_OK;
        break;
    }
    if (err != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_raw(writer, "}");
}

static esp_err_t orch_write_scenario_layout_steps(gm_runtime_json_writer_t *writer,
                                                  const room_scenario_t *scenario,
                                                  const room_scenario_branch_t *branch)
{
    esp_err_t err = ESP_OK;
    if (!writer || !scenario || !branch) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint16_t i = 0; i < branch->step_count; ++i) {
        size_t step_index = (size_t)branch->step_start_index + i;
        if (step_index >= scenario->step_count || step_index >= ROOM_SCENARIO_MAX_STEPS) {
            break;
        }
        if (i > 0 && (err = gm_runtime_json_write_raw(writer, ",")) != ESP_OK) {
            return err;
        }
        err = orch_write_scenario_layout_step(writer, &scenario->steps[step_index]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t orch_write_scenario_layout_all_steps(gm_runtime_json_writer_t *writer,
                                                      const room_scenario_t *scenario)
{
    esp_err_t err = ESP_OK;
    if (!writer || !scenario) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < scenario->step_count && i < ROOM_SCENARIO_MAX_STEPS; ++i) {
        if (i > 0 && (err = gm_runtime_json_write_raw(writer, ",")) != ESP_OK) {
            return err;
        }
        err = orch_write_scenario_layout_step(writer, &scenario->steps[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t orch_write_scenario_layout_variants(gm_runtime_json_writer_t *writer,
                                                     const room_scenario_t *scenario,
                                                     const room_scenario_branch_t *branch)
{
    esp_err_t err = ESP_OK;
    if (!writer || !scenario || !branch) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < branch->variant_count; ++i) {
        size_t variant_index = (size_t)branch->variant_start_index + i;
        const room_scenario_reactive_variant_t *variant = NULL;
        bool variant_first = true;
        if (variant_index >= scenario->reactive_variant_count ||
            variant_index >= ROOM_SCENARIO_MAX_REACTIVE_VARIANTS) {
            break;
        }
        variant = &scenario->reactive_variants[variant_index];
        if (i > 0 && (err = gm_runtime_json_write_raw(writer, ",")) != ESP_OK) {
            return err;
        }
        err = gm_runtime_json_write_raw(writer, "{");
        if (err != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer, &variant_first, "id", variant->id)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer, &variant_first, "label", variant->label)) != ESP_OK ||
            (err = gm_runtime_json_begin_field(writer, &variant_first, "actions")) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, "[")) != ESP_OK) {
            return err;
        }
        for (uint8_t action_i = 0; action_i < variant->action_count; ++action_i) {
            size_t action_index = (size_t)variant->action_start_index + action_i;
            if (action_index >= scenario->reactive_action_count ||
                action_index >= ROOM_SCENARIO_MAX_REACTIVE_ACTIONS) {
                break;
            }
            if (action_i > 0 && (err = gm_runtime_json_write_raw(writer, ",")) != ESP_OK) {
                return err;
            }
            err = orch_write_scenario_layout_action(writer,
                                                    scenario,
                                                    &scenario->reactive_actions[action_index]);
            if (err != ESP_OK) {
                return err;
            }
        }
        if ((err = gm_runtime_json_write_raw(writer, "]}")) != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t orch_write_scenario_layout_action_range(gm_runtime_json_writer_t *writer,
                                                         const room_scenario_t *scenario,
                                                         uint16_t start_index,
                                                         uint8_t count)
{
    esp_err_t err = ESP_OK;
    if (!writer || !scenario) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < count; ++i) {
        size_t action_index = (size_t)start_index + i;
        if (action_index >= scenario->reactive_action_count ||
            action_index >= ROOM_SCENARIO_MAX_REACTIVE_ACTIONS) {
            break;
        }
        if (i > 0 && (err = gm_runtime_json_write_raw(writer, ",")) != ESP_OK) {
            return err;
        }
        err = orch_write_scenario_layout_action(writer, scenario, &scenario->reactive_actions[action_index]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t orch_write_scenario_layout_reactive_v2_fields(gm_runtime_json_writer_t *writer,
                                                               bool *branch_first,
                                                               const room_scenario_t *scenario,
                                                               const room_scenario_branch_t *branch)
{
    bool policy_first = true;
    bool reentry_first = true;
    bool result_first = true;
    esp_err_t err = ESP_OK;
    if (!writer || !branch_first || !scenario || !branch) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_runtime_json_begin_field(writer, branch_first, "trigger");
    if (err != ESP_OK ||
        (err = orch_write_layout_reactive_trigger(writer, &branch->trigger)) != ESP_OK ||
        (err = gm_runtime_json_begin_field(writer, branch_first, "guard_flags")) != ESP_OK ||
        (err = orch_write_layout_reactive_guards(writer, branch->guard_flags, branch->guard_flag_count)) != ESP_OK ||
        (err = gm_runtime_json_begin_field(writer, branch_first, "policy")) != ESP_OK ||
        (err = gm_runtime_json_write_raw(writer, "{")) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(
             writer,
             &policy_first,
             "mode",
             orch_layout_reactive_policy_mode_to_str(branch->policy_mode))) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, &policy_first, "cooldown_ms", branch->cooldown_ms)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, &policy_first, "max_fire_count", branch->max_fire_count)) != ESP_OK ||
        (err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK ||
        (err = gm_runtime_json_begin_field(writer, branch_first, "reentry")) != ESP_OK ||
        (err = gm_runtime_json_write_raw(writer, "{")) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(
             writer,
             &reentry_first,
             "mode",
             room_scenario_reentry_mode_to_str(branch->reentry_mode))) != ESP_OK ||
        (err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK ||
        (err = gm_runtime_json_begin_field(writer, branch_first, "result_policy")) != ESP_OK ||
        (err = gm_runtime_json_write_raw(writer, "{")) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(
             writer,
             &result_first,
             "on_done",
             orch_layout_reactive_result_action_to_str(branch->result_on_done))) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(
             writer,
             &result_first,
             "on_fail",
             orch_layout_reactive_result_action_to_str(branch->result_on_fail))) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(
             writer,
             &result_first,
             "on_timeout",
             orch_layout_reactive_result_action_to_str(branch->result_on_timeout))) != ESP_OK) {
        return err;
    }
    if (branch->result_flag[0] &&
        (err = gm_runtime_json_write_string_field(writer, &result_first, "flag", branch->result_flag)) != ESP_OK) {
        return err;
    }
    if ((err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK ||
        (err = gm_runtime_json_begin_field(writer, branch_first, "variants")) != ESP_OK ||
        (err = orch_write_scenario_layout_variants(writer, scenario, branch)) != ESP_OK ||
        (err = gm_runtime_json_begin_field(writer, branch_first, "on_complete")) != ESP_OK ||
        (err = orch_write_scenario_layout_action_range(writer,
                                                       scenario,
                                                       branch->on_complete_action_start_index,
                                                       branch->on_complete_action_count)) != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

esp_err_t orchestrator_scenario_layout_writer_send(httpd_req_t *req,
                                                   const char *room_id,
                                                   const room_scenario_t *scenario)
{
    gm_runtime_json_writer_t writer = {
        .req = req,
        .chunk = s_room_scenario_json_chunk,
        .capacity = sizeof(s_room_scenario_json_chunk),
        .len = 0,
    };
    bool root_first = true;
    bool item_first = true;
    esp_err_t err = ESP_OK;

    if (!req || !room_id || !scenario) {
        return ESP_ERR_INVALID_ARG;
    }
    err = httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_write_raw(&writer, "{");
    if (err != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer, &root_first, "room_id", room_id)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &root_first, "count", 1)) != ESP_OK ||
        (err = gm_runtime_json_begin_field(&writer, &root_first, "scenarios")) != ESP_OK ||
        (err = gm_runtime_json_write_raw(&writer, "[{")) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer, &item_first, "room_id", scenario->room_id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer, &item_first, "id", scenario->id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer, &item_first, "name", scenario->name)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &item_first, "step_count", scenario->step_count)) != ESP_OK ||
        (err = gm_runtime_json_write_bool_field(&writer, &item_first, "valid", true)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &item_first, "validation_issue_count", 0)) != ESP_OK ||
        (err = gm_runtime_json_begin_field(&writer, &item_first, "steps")) != ESP_OK ||
        (err = orch_write_scenario_layout_all_steps(&writer, scenario)) != ESP_OK ||
        (err = gm_runtime_json_begin_field(&writer, &item_first, "branches")) != ESP_OK ||
        (err = gm_runtime_json_write_raw(&writer, "[")) != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < scenario->branch_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        const room_scenario_branch_t *branch = &scenario->branches[i];
        bool branch_first = true;
        if (i > 0 && (err = gm_runtime_json_write_raw(&writer, ",")) != ESP_OK) {
            return err;
        }
        err = gm_runtime_json_write_raw(&writer, "{");
        if (err != ESP_OK ||
            (err = gm_runtime_json_write_string_field(&writer, &branch_first, "id", branch->id)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(&writer, &branch_first, "name", branch->name)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(&writer,
                                                      &branch_first,
                                                      "type",
                                                      room_scenario_branch_type_to_str(branch->type))) != ESP_OK ||
            (err = gm_runtime_json_write_bool_field(&writer, &branch_first, "enabled", branch->enabled)) != ESP_OK ||
            (err = gm_runtime_json_write_bool_field(&writer,
                                                    &branch_first,
                                                    "required_for_completion",
                                                    branch->required_for_completion)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(&writer, &branch_first, "step_count", branch->step_count)) != ESP_OK) {
            return err;
        }
        if (branch->type == ROOM_SCENARIO_BRANCH_REACTIVE) {
            err = gm_runtime_json_write_uint64_field(&writer, &branch_first, "priority", branch->priority);
            if (err != ESP_OK ||
                (err = gm_runtime_json_write_uint64_field(&writer, &branch_first, "cooldown_ms", branch->cooldown_ms)) != ESP_OK ||
                (err = gm_runtime_json_write_uint64_field(&writer, &branch_first, "max_fire_count", branch->max_fire_count)) != ESP_OK ||
                (err = gm_runtime_json_write_bool_field(&writer, &branch_first, "run_once", branch->run_once)) != ESP_OK) {
                return err;
            }
        }
        if ((err = gm_runtime_json_begin_field(&writer, &branch_first, "steps")) != ESP_OK ||
            (err = orch_write_scenario_layout_steps(&writer, scenario, branch)) != ESP_OK) {
            return err;
        }
        if (branch->type == ROOM_SCENARIO_BRANCH_REACTIVE &&
            (branch->variant_count > 0 || branch->trigger.kind != ROOM_SCENARIO_REACTIVE_TRIGGER_NONE)) {
            err = orch_write_scenario_layout_reactive_v2_fields(&writer, &branch_first, scenario, branch);
            if (err != ESP_OK) {
                return err;
            }
        }
        if ((err = gm_runtime_json_write_raw(&writer, "}")) != ESP_OK) {
            return err;
        }
    }
    if ((err = gm_runtime_json_write_raw(&writer, "]}]}")) != ESP_OK ||
        (err = gm_runtime_json_flush(&writer)) != ESP_OK) {
        return err;
    }
    return web_ui_http_resp_send_chunk(req, NULL, 0);
}
