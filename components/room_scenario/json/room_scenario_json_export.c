#include "../room_scenario_internal.h"

#include <string.h>

#include "cJSON.h"

static esp_err_t json_add_object_string_optional(cJSON *obj,
                                                 const char *name,
                                                 const char *json)
{
    cJSON *params = NULL;
    if (!obj || !name) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!json || !json[0]) {
        return ESP_OK;
    }
    params = cJSON_Parse(json);
    if (!cJSON_IsObject(params)) {
        cJSON_Delete(params);
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_AddItemToObject(obj, name, params);
    return ESP_OK;
}

static const char *reactive_trigger_kind_to_str(room_scenario_reactive_trigger_kind_t kind)
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

static const char *reactive_policy_mode_to_str(room_scenario_reactive_policy_mode_t mode)
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

static const char *reactive_result_action_to_str(room_scenario_reactive_result_action_t action)
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

static const char *command_group_mode_to_str(room_scenario_command_group_mode_t mode)
{
    return mode == ROOM_SCENARIO_COMMAND_GROUP_PARALLEL ? "parallel" : "sequential";
}

static esp_err_t room_scenario_export_command_json(const room_scenario_device_command_t *command,
                                                   cJSON *obj)
{
    esp_err_t err = ESP_OK;
    if (!command || !cJSON_IsObject(obj)) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(obj, "device_id", command->device_id);
    cJSON_AddStringToObject(obj, "command_id", command->command_id);
    err = json_add_object_string_optional(obj, "params", command->params_json);
    return err;
}

static esp_err_t room_scenario_export_step_json(const room_scenario_step_t *step,
                                                cJSON *steps)
{
    cJSON *obj = cJSON_CreateObject();
    esp_err_t err = ESP_OK;
    if (!obj) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(obj, "id", step->id);
    cJSON_AddStringToObject(obj, "label", step->label);
    cJSON_AddBoolToObject(obj, "enabled", step->enabled);
    cJSON_AddStringToObject(obj, "type", room_scenario_step_type_to_str(step->type));
    if (step->allow_operator_skip) {
        cJSON_AddBoolToObject(obj, "allow_operator_skip", true);
    }
    if (step->operator_skip_label[0]) {
        cJSON_AddStringToObject(obj, "operator_skip_label", step->operator_skip_label);
    }
    switch (step->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        err = room_scenario_export_command_json(&step->data.device_command, obj);
        if (err != ESP_OK) {
            cJSON_Delete(obj);
            return err;
        }
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP: {
        cJSON *commands = cJSON_AddArrayToObject(obj, "commands");
        if (!commands) {
            cJSON_Delete(obj);
            return ESP_ERR_NO_MEM;
        }
        for (uint8_t i = 0; i < step->data.device_command_group.command_count; ++i) {
            cJSON *command = cJSON_CreateObject();
            if (!command) {
                cJSON_Delete(obj);
                return ESP_ERR_NO_MEM;
            }
            cJSON_AddStringToObject(command,
                                    "device_id",
                                    step->data.device_command_group.commands[i].device_id);
            cJSON_AddStringToObject(command,
                                    "command_id",
                                    step->data.device_command_group.commands[i].command_id);
            err = json_add_object_string_optional(command,
                                                  "params",
                                                  step->data.device_command_group.commands[i].params_json);
            if (err != ESP_OK) {
                cJSON_Delete(command);
                cJSON_Delete(obj);
                return err;
            }
            if (!cJSON_AddItemToArray(commands, command)) {
                cJSON_Delete(command);
                cJSON_Delete(obj);
                return ESP_ERR_NO_MEM;
            }
        }
        break;
    }
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        cJSON_AddNumberToObject(obj, "duration_ms", step->data.wait_time.duration_ms);
        break;
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        cJSON_AddStringToObject(obj, "device_id", step->data.wait_device_event.device_id);
        cJSON_AddStringToObject(obj, "event_id", step->data.wait_device_event.event_id);
        if (step->data.wait_device_event.timeout_ms > 0) {
            cJSON_AddNumberToObject(obj, "timeout_ms", step->data.wait_device_event.timeout_ms);
        }
        if (step->data.wait_device_event.timeout_message[0]) {
            cJSON_AddStringToObject(obj,
                                    "timeout_message",
                                    step->data.wait_device_event.timeout_message);
        }
        break;
    case ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        cJSON_AddStringToObject(obj, "prompt", step->data.operator_approval.prompt);
        cJSON_AddStringToObject(obj, "approve_label", step->data.operator_approval.approve_label);
        break;
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        cJSON_AddStringToObject(obj, "message", step->data.operator_message.message);
        break;
    case ROOM_SCENARIO_STEP_SET_FLAG:
        cJSON_AddStringToObject(obj, "flag_name", step->data.set_flag.name);
        cJSON_AddBoolToObject(obj, "value", step->data.set_flag.value);
        break;
    case ROOM_SCENARIO_STEP_WAIT_FLAGS: {
        cJSON *flags = cJSON_AddArrayToObject(obj, "flags");
        if (!flags) {
            cJSON_Delete(obj);
            return ESP_ERR_NO_MEM;
        }
        for (uint8_t i = 0; i < step->data.wait_flags.flag_count; ++i) {
            cJSON *flag = cJSON_CreateObject();
            if (!flag) {
                cJSON_Delete(obj);
                return ESP_ERR_NO_MEM;
            }
            cJSON_AddStringToObject(flag, "flag_name", step->data.wait_flags.flags[i].name);
            cJSON_AddBoolToObject(flag, "value", step->data.wait_flags.flags[i].value);
            if (!cJSON_AddItemToArray(flags, flag)) {
                cJSON_Delete(flag);
                cJSON_Delete(obj);
                return ESP_ERR_NO_MEM;
            }
        }
        if (step->data.wait_flags.timeout_ms > 0) {
            cJSON_AddNumberToObject(obj, "timeout_ms", step->data.wait_flags.timeout_ms);
        }
        if (step->data.wait_flags.timeout_message[0]) {
            cJSON_AddStringToObject(obj,
                                    "timeout_message",
                                    step->data.wait_flags.timeout_message);
        }
        break;
    }
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT: {
        cJSON *events = cJSON_AddArrayToObject(obj, "events");
        if (!events) {
            cJSON_Delete(obj);
            return ESP_ERR_NO_MEM;
        }
        for (uint8_t i = 0; i < step->data.wait_any_device_event.event_count; ++i) {
            cJSON *event = cJSON_CreateObject();
            if (!event) {
                cJSON_Delete(obj);
                return ESP_ERR_NO_MEM;
            }
            cJSON_AddStringToObject(event,
                                    "device_id",
                                    step->data.wait_any_device_event.events[i].device_id);
            cJSON_AddStringToObject(event,
                                    "event_id",
                                    step->data.wait_any_device_event.events[i].event_id);
            if (!cJSON_AddItemToArray(events, event)) {
                cJSON_Delete(event);
                cJSON_Delete(obj);
                return ESP_ERR_NO_MEM;
            }
        }
        break;
    }
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS: {
        cJSON *events = cJSON_AddArrayToObject(obj, "events");
        if (!events) {
            cJSON_Delete(obj);
            return ESP_ERR_NO_MEM;
        }
        for (uint8_t i = 0; i < step->data.wait_all_device_events.event_count; ++i) {
            cJSON *event = cJSON_CreateObject();
            if (!event) {
                cJSON_Delete(obj);
                return ESP_ERR_NO_MEM;
            }
            cJSON_AddStringToObject(event,
                                    "device_id",
                                    step->data.wait_all_device_events.events[i].device_id);
            cJSON_AddStringToObject(event,
                                    "event_id",
                                    step->data.wait_all_device_events.events[i].event_id);
            if (!cJSON_AddItemToArray(events, event)) {
                cJSON_Delete(event);
                cJSON_Delete(obj);
                return ESP_ERR_NO_MEM;
            }
        }
        break;
    }
    case ROOM_SCENARIO_STEP_END_GAME:
        break;
    default:
        cJSON_Delete(obj);
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_AddItemToArray(steps, obj)) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t room_scenario_export_reactive_action_json(
    const room_scenario_t *s,
    const room_scenario_reactive_action_t *action,
    cJSON *actions)
{
    cJSON *obj = cJSON_CreateObject();
    esp_err_t err = ESP_OK;
    if (!s || !action || !actions || !obj) {
        cJSON_Delete(obj);
        return ESP_ERR_INVALID_ARG;
    }
    if (action->id[0]) {
        cJSON_AddStringToObject(obj, "id", action->id);
    }
    if (action->label[0]) {
        cJSON_AddStringToObject(obj, "label", action->label);
    }
    cJSON_AddStringToObject(obj, "type", room_scenario_step_type_to_str(action->type));
    switch (action->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        err = room_scenario_export_command_json(&action->data.device_command, obj);
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP: {
        cJSON *commands = NULL;
        if ((size_t)action->group_command_start_index + action->group_command_count >
            s->reactive_group_command_count) {
            cJSON_Delete(obj);
            return ESP_ERR_INVALID_ARG;
        }
        cJSON_AddStringToObject(obj, "mode", command_group_mode_to_str(action->group_mode));
        commands = cJSON_AddArrayToObject(obj, "commands");
        if (!commands) {
            cJSON_Delete(obj);
            return ESP_ERR_NO_MEM;
        }
        for (uint8_t i = 0; i < action->group_command_count; ++i) {
            cJSON *command = cJSON_CreateObject();
            const room_scenario_device_command_t *src =
                &s->reactive_group_commands[action->group_command_start_index + i];
            if (!command) {
                cJSON_Delete(obj);
                return ESP_ERR_NO_MEM;
            }
            err = room_scenario_export_command_json(src, command);
            if (err != ESP_OK || !cJSON_AddItemToArray(commands, command)) {
                cJSON_Delete(command);
                cJSON_Delete(obj);
                return err != ESP_OK ? err : ESP_ERR_NO_MEM;
            }
        }
        break;
    }
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        cJSON_AddNumberToObject(obj, "duration_ms", action->data.wait_time.duration_ms);
        break;
    case ROOM_SCENARIO_STEP_SET_FLAG:
        cJSON_AddStringToObject(obj, "flag", action->data.set_flag.name);
        cJSON_AddBoolToObject(obj, "value", action->data.set_flag.value);
        break;
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        cJSON_AddStringToObject(obj, "message", action->data.operator_message.message);
        break;
    default:
        cJSON_Delete(obj);
        return ESP_ERR_INVALID_ARG;
    }
    if (err != ESP_OK || !cJSON_AddItemToArray(actions, obj)) {
        cJSON_Delete(obj);
        return err != ESP_OK ? err : ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t room_scenario_export_reactive_branch_v2_json(
    const room_scenario_t *s,
    const room_scenario_branch_t *branch,
    cJSON *obj)
{
    cJSON *trigger = NULL;
    cJSON *guards = NULL;
    cJSON *variants = NULL;
    cJSON *result = NULL;
    cJSON *complete = NULL;
    esp_err_t err = ESP_OK;
    if (!s || !branch || !obj) {
        return ESP_ERR_INVALID_ARG;
    }
    trigger = cJSON_AddObjectToObject(obj, "trigger");
    if (!trigger) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(trigger, "kind", reactive_trigger_kind_to_str(branch->trigger.kind));
    if (branch->trigger.device_id[0]) {
        cJSON_AddStringToObject(trigger, "device_id", branch->trigger.device_id);
    }
    if (branch->trigger.event_id[0]) {
        cJSON_AddStringToObject(trigger, "event_id", branch->trigger.event_id);
    }
    if (branch->trigger.flag_name[0]) {
        cJSON_AddStringToObject(trigger, "flag_name", branch->trigger.flag_name);
    }
    if (branch->trigger.runtime_event[0]) {
        cJSON_AddStringToObject(trigger, "event_id", branch->trigger.runtime_event);
    }
    if (branch->trigger.operator_event[0]) {
        cJSON_AddStringToObject(trigger, "event_id", branch->trigger.operator_event);
    }
    guards = cJSON_AddArrayToObject(obj, "guard_flags");
    if (!guards) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < branch->guard_flag_count; ++i) {
        cJSON *guard = cJSON_CreateObject();
        if (!guard) {
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(guard, "flag", branch->guard_flags[i].name);
        cJSON_AddBoolToObject(guard, "value", branch->guard_flags[i].value);
        if (!cJSON_AddItemToArray(guards, guard)) {
            cJSON_Delete(guard);
            return ESP_ERR_NO_MEM;
        }
    }
    variants = cJSON_AddArrayToObject(obj, "variants");
    if (!variants ||
        (size_t)branch->variant_start_index + branch->variant_count > s->reactive_variant_count) {
        return variants ? ESP_ERR_INVALID_ARG : ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < branch->variant_count; ++i) {
        const room_scenario_reactive_variant_t *variant =
            &s->reactive_variants[branch->variant_start_index + i];
        cJSON *variant_obj = cJSON_CreateObject();
        cJSON *actions = NULL;
        if (!variant_obj) {
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(variant_obj, "id", variant->id);
        cJSON_AddStringToObject(variant_obj, "label", variant->label);
        actions = cJSON_AddArrayToObject(variant_obj, "actions");
        if (!actions ||
            (size_t)variant->action_start_index + variant->action_count > s->reactive_action_count) {
            cJSON_Delete(variant_obj);
            return actions ? ESP_ERR_INVALID_ARG : ESP_ERR_NO_MEM;
        }
        for (uint8_t action_index = 0; action_index < variant->action_count; ++action_index) {
            err = room_scenario_export_reactive_action_json(
                s,
                &s->reactive_actions[variant->action_start_index + action_index],
                actions);
            if (err != ESP_OK) {
                cJSON_Delete(variant_obj);
                return err;
            }
        }
        if (!cJSON_AddItemToArray(variants, variant_obj)) {
            cJSON_Delete(variant_obj);
            return ESP_ERR_NO_MEM;
        }
    }
    result = cJSON_AddObjectToObject(obj, "result_policy");
    if (!result) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(result, "on_done", reactive_result_action_to_str(branch->result_on_done));
    cJSON_AddStringToObject(result, "on_fail", reactive_result_action_to_str(branch->result_on_fail));
    cJSON_AddStringToObject(result, "on_timeout", reactive_result_action_to_str(branch->result_on_timeout));
    if (branch->result_flag[0]) {
        cJSON_AddStringToObject(result, "flag", branch->result_flag);
    }
    complete = cJSON_AddArrayToObject(obj, "on_complete");
    if (!complete ||
        (size_t)branch->on_complete_action_start_index + branch->on_complete_action_count >
            s->reactive_action_count) {
        return complete ? ESP_ERR_INVALID_ARG : ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < branch->on_complete_action_count; ++i) {
        err = room_scenario_export_reactive_action_json(
            s,
            &s->reactive_actions[branch->on_complete_action_start_index + i],
            complete);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t room_scenario_export_branch_json(const room_scenario_t *s,
                                                  const room_scenario_branch_t *branch,
                                                  cJSON *branches)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON *steps = NULL;
    uint32_t end_index = 0;
    esp_err_t err = ESP_OK;
    if (!obj) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(obj, "id", branch->id);
    cJSON_AddStringToObject(obj, "name", branch->name);
    cJSON_AddStringToObject(obj, "type", room_scenario_branch_type_to_str(branch->type));
    cJSON_AddBoolToObject(obj, "enabled", branch->enabled);
    cJSON_AddBoolToObject(obj, "required_for_completion", branch->required_for_completion);
    if (branch->priority > 0) {
        cJSON_AddNumberToObject(obj, "priority", branch->priority);
    }
    if (branch->type == ROOM_SCENARIO_BRANCH_REACTIVE) {
        cJSON *policy = cJSON_AddObjectToObject(obj, "policy");
        cJSON *reentry = cJSON_AddObjectToObject(obj, "reentry");
        if (!policy || !reentry) {
            cJSON_Delete(obj);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(policy, "mode", reactive_policy_mode_to_str(branch->policy_mode));
        cJSON_AddNumberToObject(policy, "cooldown_ms", branch->cooldown_ms);
        cJSON_AddNumberToObject(policy, "max_fire_count", branch->max_fire_count);
        cJSON_AddStringToObject(reentry,
                                "mode",
                                room_scenario_reentry_mode_to_str(branch->reentry_mode));
        if (branch->cooldown_ms > 0) {
            cJSON_AddNumberToObject(obj, "cooldown_ms", branch->cooldown_ms);
        }
        if (branch->max_fire_count > 0) {
            cJSON_AddNumberToObject(obj, "max_fire_count", branch->max_fire_count);
        }
        if (branch->run_once) {
            cJSON_AddBoolToObject(obj, "run_once", true);
        }
        if (branch->variant_count > 0 || branch->trigger.kind != ROOM_SCENARIO_REACTIVE_TRIGGER_NONE) {
            err = room_scenario_export_reactive_branch_v2_json(s, branch, obj);
            if (err != ESP_OK) {
                cJSON_Delete(obj);
                return err;
            }
        }
    }
    if (branch->type == ROOM_SCENARIO_BRANCH_REACTIVE && branch->variant_count > 0) {
        if (!cJSON_AddItemToArray(branches, obj)) {
            cJSON_Delete(obj);
            return ESP_ERR_NO_MEM;
        }
        return ESP_OK;
    }
    steps = cJSON_AddArrayToObject(obj, "steps");
    if (!steps) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    end_index = (uint32_t)branch->step_start_index + branch->step_count;
    if (end_index > s->step_count) {
        cJSON_Delete(obj);
        return ESP_ERR_INVALID_ARG;
    }
    for (uint32_t i = branch->step_start_index; i < end_index; ++i) {
        err = room_scenario_export_step_json(&s->steps[i], steps);
        if (err != ESP_OK) {
            cJSON_Delete(obj);
            return err;
        }
    }
    if (!cJSON_AddItemToArray(branches, obj)) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t room_scenario_to_json(const room_scenario_t *s, cJSON *out)
{
    cJSON *steps = NULL;
    cJSON *branches = NULL;
    esp_err_t err = ESP_OK;
    if (!s || !cJSON_IsObject(out)) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_scenario_validate_structural(s);
    if (err != ESP_OK) {
        return err;
    }
    cJSON_AddStringToObject(out, "id", s->id);
    cJSON_AddStringToObject(out, "name", s->name);
    cJSON_AddStringToObject(out, "room_id", s->room_id);
    if (s->branch_count > 0) {
        branches = cJSON_AddArrayToObject(out, "branches");
        if (!branches) {
            return ESP_ERR_NO_MEM;
        }
        for (size_t i = 0; i < s->branch_count; ++i) {
            err = room_scenario_export_branch_json(s, &s->branches[i], branches);
            if (err != ESP_OK) {
                return err;
            }
        }
        return ESP_OK;
    }
    steps = cJSON_AddArrayToObject(out, "steps");
    if (!steps) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < s->step_count; ++i) {
        err = room_scenario_export_step_json(&s->steps[i], steps);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
