#include "orchestrator/orchestrator_api_view.h"

static esp_err_t api_add_json_object_string(cJSON *obj, const char *name, const char *json)
{
    cJSON *parsed = NULL;
    if (!obj || !name) {
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
    cJSON_AddItemToObject(obj, name, parsed);
    return ESP_OK;
}

static esp_err_t api_add_event_refs(cJSON *obj,
                                    const orch_room_scenario_event_ref_t *events,
                                    uint8_t count)
{
    cJSON *array = NULL;
    if (!obj || (!events && count > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    array = cJSON_CreateArray();
    if (!array) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < count && i < ORCH_ROOM_SCENARIO_MAX_EVENT_REFS; ++i) {
        cJSON *event = cJSON_CreateObject();
        if (!event) {
            cJSON_Delete(array);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(event, "device_id", events[i].device_id);
        cJSON_AddStringToObject(event, "event_id", events[i].event_id);
        cJSON_AddItemToArray(array, event);
    }
    cJSON_AddItemToObject(obj, "events", array);
    cJSON_AddNumberToObject(obj, "event_count", count);
    return ESP_OK;
}

static esp_err_t api_add_flag_refs(cJSON *obj,
                                   const char *array_name,
                                   const char *key_name,
                                   const char *count_name,
                                   const orch_room_scenario_flag_ref_t *flags,
                                   uint8_t count)
{
    cJSON *array = NULL;
    if (!obj || !array_name || !key_name || (!flags && count > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    array = cJSON_CreateArray();
    if (!array) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < count && i < ORCH_ROOM_SCENARIO_MAX_FLAG_REFS; ++i) {
        cJSON *flag = cJSON_CreateObject();
        if (!flag) {
            cJSON_Delete(array);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(flag, key_name, flags[i].name);
        cJSON_AddBoolToObject(flag, "value", flags[i].value);
        cJSON_AddItemToArray(array, flag);
    }
    cJSON_AddItemToObject(obj, array_name, array);
    if (count_name && count_name[0]) {
        cJSON_AddNumberToObject(obj, count_name, count);
    }
    return ESP_OK;
}

static esp_err_t api_add_command_object(cJSON *obj, const orch_room_scenario_command_entry_t *command)
{
    if (!obj || !command) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(obj, "device_id", command->device_id);
    cJSON_AddStringToObject(obj, "command_id", command->command_id);
    return api_add_json_object_string(obj, "params", command->params_json);
}

static esp_err_t api_add_command_array(cJSON *obj,
                                       const char *array_name,
                                       const orch_room_scenario_command_entry_t *commands,
                                       uint8_t count)
{
    cJSON *array = NULL;
    if (!obj || !array_name || (!commands && count > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    array = cJSON_CreateArray();
    if (!array) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < count; ++i) {
        cJSON *command = cJSON_CreateObject();
        esp_err_t err = ESP_OK;
        if (!command) {
            cJSON_Delete(array);
            return ESP_ERR_NO_MEM;
        }
        err = api_add_command_object(command, &commands[i]);
        if (err != ESP_OK) {
            cJSON_Delete(command);
            cJSON_Delete(array);
            return err;
        }
        cJSON_AddItemToArray(array, command);
    }
    cJSON_AddItemToObject(obj, array_name, array);
    return ESP_OK;
}

static cJSON *api_build_step_json(const orch_room_scenario_step_entry_t *step)
{
    cJSON *step_obj = NULL;
    esp_err_t err = ESP_OK;
    bool needs_event_refs = true;
    bool needs_flag_refs = true;
    if (!step) {
        return NULL;
    }
    step_obj = cJSON_CreateObject();
    if (!step_obj) {
        return NULL;
    }
    cJSON_AddStringToObject(step_obj, "id", step->id);
    cJSON_AddStringToObject(step_obj, "label", step->label);
    cJSON_AddStringToObject(step_obj, "type", step->type_text);
    cJSON_AddBoolToObject(step_obj, "enabled", step->enabled);
    if (step->allow_operator_skip) {
        cJSON_AddBoolToObject(step_obj, "allow_operator_skip", true);
    }
    if (step->operator_skip_label[0]) {
        cJSON_AddStringToObject(step_obj, "operator_skip_label", step->operator_skip_label);
    }
    switch (step->type) {
    case ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        cJSON_AddStringToObject(step_obj, "device_id", step->device_id);
        cJSON_AddStringToObject(step_obj, "command_id", step->command_id);
        err = api_add_json_object_string(step_obj, "params", step->params_json);
        break;
    case ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        err = api_add_command_array(step_obj, "commands", step->commands, step->command_count);
        if (err == ESP_OK) {
            cJSON_AddNumberToObject(step_obj, "command_count", step->command_count);
        }
        break;
    case ORCH_ROOM_SCENARIO_STEP_WAIT_TIME:
        cJSON_AddNumberToObject(step_obj, "duration_ms", step->duration_ms);
        break;
    case ORCH_ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        cJSON_AddStringToObject(step_obj, "device_id", step->device_id);
        cJSON_AddStringToObject(step_obj, "event_id", step->event_id);
        if (step->timeout_ms > 0) {
            cJSON_AddNumberToObject(step_obj, "timeout_ms", step->timeout_ms);
        }
        if (step->timeout_message[0]) {
            cJSON_AddStringToObject(step_obj, "timeout_message", step->timeout_message);
        }
        break;
    case ORCH_ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        cJSON_AddStringToObject(step_obj, "prompt", step->operator_prompt);
        cJSON_AddStringToObject(step_obj, "approve_label", step->operator_approve_label);
        cJSON_AddStringToObject(step_obj, "operator_prompt", step->operator_prompt);
        cJSON_AddStringToObject(step_obj, "operator_approve_label", step->operator_approve_label);
        break;
    case ORCH_ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        cJSON_AddStringToObject(step_obj, "message", step->operator_message);
        cJSON_AddStringToObject(step_obj, "operator_message", step->operator_message);
        break;
    case ORCH_ROOM_SCENARIO_STEP_SET_FLAG:
        cJSON_AddStringToObject(step_obj, "flag_name", step->flag_name);
        cJSON_AddBoolToObject(step_obj, "value", step->flag_value);
        cJSON_AddBoolToObject(step_obj, "flag_value", step->flag_value);
        break;
    case ORCH_ROOM_SCENARIO_STEP_WAIT_FLAGS:
        err = api_add_flag_refs(step_obj, "flags", "flag_name", "flag_count", step->flags, step->flag_count);
        needs_flag_refs = false;
        if (err == ESP_OK && step->timeout_ms > 0) {
            cJSON_AddNumberToObject(step_obj, "timeout_ms", step->timeout_ms);
        }
        if (err == ESP_OK && step->timeout_message[0]) {
            cJSON_AddStringToObject(step_obj, "timeout_message", step->timeout_message);
        }
        break;
    case ORCH_ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
    case ORCH_ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        err = api_add_event_refs(step_obj, step->events, step->event_count);
        needs_event_refs = false;
        break;
    case ORCH_ROOM_SCENARIO_STEP_END_GAME:
        break;
    default:
        break;
    }
    if (err != ESP_OK) {
        cJSON_Delete(step_obj);
        return NULL;
    }
    if (needs_event_refs && api_add_event_refs(step_obj, step->events, step->event_count) != ESP_OK) {
        cJSON_Delete(step_obj);
        return NULL;
    }
    if (needs_flag_refs &&
        api_add_flag_refs(step_obj, "flags", "name", "flag_count", step->flags, step->flag_count) != ESP_OK) {
        cJSON_Delete(step_obj);
        return NULL;
    }
    if (step->type != ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP) {
        cJSON_AddNumberToObject(step_obj, "command_count", step->command_count);
    }
    return step_obj;
}

static cJSON *api_build_reactive_action_json(const orch_room_scenario_detail_t *scenario,
                                             const orch_room_scenario_reactive_action_entry_t *action)
{
    cJSON *obj = NULL;
    esp_err_t err = ESP_OK;
    if (!scenario || !action) {
        return NULL;
    }
    obj = cJSON_CreateObject();
    if (!obj) {
        return NULL;
    }
    if (action->id[0]) {
        cJSON_AddStringToObject(obj, "id", action->id);
    }
    if (action->label[0]) {
        cJSON_AddStringToObject(obj, "label", action->label);
    }
    cJSON_AddStringToObject(obj, "type", action->type_text);
    switch (action->type) {
    case ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        cJSON_AddStringToObject(obj, "device_id", action->device_id);
        cJSON_AddStringToObject(obj, "command_id", action->command_id);
        err = api_add_json_object_string(obj, "params", action->params_json);
        break;
    case ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        cJSON_AddStringToObject(obj, "mode", action->group_mode_text);
        if ((size_t)action->group_command_start_index + action->group_command_count >
            scenario->reactive_group_command_count) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            err = api_add_command_array(obj,
                                        "commands",
                                        &scenario->reactive_group_commands[action->group_command_start_index],
                                        action->group_command_count);
        }
        break;
    case ORCH_ROOM_SCENARIO_STEP_WAIT_TIME:
        cJSON_AddNumberToObject(obj, "duration_ms", action->duration_ms);
        break;
    case ORCH_ROOM_SCENARIO_STEP_SET_FLAG:
        cJSON_AddStringToObject(obj, "flag", action->flag_name);
        cJSON_AddBoolToObject(obj, "value", action->flag_value);
        break;
    case ORCH_ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        cJSON_AddStringToObject(obj, "message", action->operator_message);
        break;
    default:
        err = ESP_ERR_INVALID_ARG;
        break;
    }
    if (err != ESP_OK) {
        cJSON_Delete(obj);
        return NULL;
    }
    return obj;
}

static esp_err_t api_add_branch_steps(cJSON *branch_obj,
                                      const orch_room_scenario_detail_t *scenario,
                                      const orch_room_scenario_branch_detail_t *branch)
{
    cJSON *steps = NULL;
    if (!branch_obj || !scenario || !branch) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((size_t)branch->step_start_index + branch->step_count > scenario->summary.step_count) {
        return ESP_ERR_INVALID_ARG;
    }
    steps = cJSON_CreateArray();
    if (!steps) {
        return ESP_ERR_NO_MEM;
    }
    for (uint16_t i = 0; i < branch->step_count; ++i) {
        cJSON *step_obj = api_build_step_json(&scenario->steps[branch->step_start_index + i]);
        if (!step_obj) {
            cJSON_Delete(steps);
            return ESP_ERR_INVALID_STATE;
        }
        cJSON_AddItemToArray(steps, step_obj);
    }
    cJSON_AddItemToObject(branch_obj, "steps", steps);
    return ESP_OK;
}

static esp_err_t api_add_reactive_branch_v2(cJSON *branch_obj,
                                            const orch_room_scenario_detail_t *scenario,
                                            const orch_room_scenario_branch_detail_t *branch)
{
    cJSON *trigger = NULL;
    cJSON *policy = NULL;
    cJSON *reentry = NULL;
    cJSON *variants = NULL;
    cJSON *result = NULL;
    cJSON *on_complete = NULL;
    if (!branch_obj || !scenario || !branch) {
        return ESP_ERR_INVALID_ARG;
    }
    policy = cJSON_AddObjectToObject(branch_obj, "policy");
    reentry = cJSON_AddObjectToObject(branch_obj, "reentry");
    if (!policy || !reentry) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(policy, "mode", branch->policy_mode_text);
    cJSON_AddNumberToObject(policy, "cooldown_ms", branch->cooldown_ms);
    cJSON_AddNumberToObject(policy, "max_fire_count", branch->max_fire_count);
    cJSON_AddStringToObject(reentry, "mode", branch->reentry_mode_text);
    if (branch->cooldown_ms > 0) {
        cJSON_AddNumberToObject(branch_obj, "cooldown_ms", branch->cooldown_ms);
    }
    if (branch->max_fire_count > 0) {
        cJSON_AddNumberToObject(branch_obj, "max_fire_count", branch->max_fire_count);
    }
    if (branch->run_once) {
        cJSON_AddBoolToObject(branch_obj, "run_once", true);
    }
    trigger = cJSON_AddObjectToObject(branch_obj, "trigger");
    variants = cJSON_AddArrayToObject(branch_obj, "variants");
    result = cJSON_AddObjectToObject(branch_obj, "result_policy");
    on_complete = cJSON_AddArrayToObject(branch_obj, "on_complete");
    if (!trigger || !variants || !result || !on_complete) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(trigger, "kind", branch->trigger.kind_text);
    if (branch->trigger.device_id[0]) {
        cJSON_AddStringToObject(trigger, "device_id", branch->trigger.device_id);
    }
    if (branch->trigger.event_id[0]) {
        cJSON_AddStringToObject(trigger, "event_id", branch->trigger.event_id);
    }
    if (branch->trigger.flag_name[0]) {
        cJSON_AddStringToObject(trigger, "flag_name", branch->trigger.flag_name);
    }
    if (branch->trigger.operator_event[0]) {
        cJSON_AddStringToObject(trigger, "operator_event", branch->trigger.operator_event);
    }
    if (branch->trigger.runtime_event[0]) {
        cJSON_AddStringToObject(trigger, "runtime_event", branch->trigger.runtime_event);
    }
    if (api_add_flag_refs(branch_obj,
                          "guard_flags",
                          "flag",
                          NULL,
                          branch->guard_flags,
                          branch->guard_flag_count) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    if ((size_t)branch->variant_start_index + branch->variant_count > scenario->reactive_variant_count) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < branch->variant_count; ++i) {
        const orch_room_scenario_reactive_variant_entry_t *variant =
            &scenario->reactive_variants[branch->variant_start_index + i];
        cJSON *variant_obj = cJSON_CreateObject();
        cJSON *actions = NULL;
        if (!variant_obj) {
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(variant_obj, "id", variant->id);
        cJSON_AddStringToObject(variant_obj, "label", variant->label);
        actions = cJSON_AddArrayToObject(variant_obj, "actions");
        if (!actions) {
            cJSON_Delete(variant_obj);
            return ESP_ERR_NO_MEM;
        }
        if ((size_t)variant->action_start_index + variant->action_count > scenario->reactive_action_count) {
            cJSON_Delete(variant_obj);
            return ESP_ERR_INVALID_ARG;
        }
        for (uint8_t action_index = 0; action_index < variant->action_count; ++action_index) {
            cJSON *action_obj = api_build_reactive_action_json(
                scenario,
                &scenario->reactive_actions[variant->action_start_index + action_index]);
            if (!action_obj) {
                cJSON_Delete(variant_obj);
                return ESP_ERR_INVALID_STATE;
            }
            cJSON_AddItemToArray(actions, action_obj);
        }
        cJSON_AddItemToArray(variants, variant_obj);
    }
    cJSON_AddStringToObject(result, "on_done", branch->result_policy.on_done_text);
    cJSON_AddStringToObject(result, "on_fail", branch->result_policy.on_fail_text);
    cJSON_AddStringToObject(result, "on_timeout", branch->result_policy.on_timeout_text);
    if (branch->result_policy.flag[0]) {
        cJSON_AddStringToObject(result, "flag", branch->result_policy.flag);
    }
    if ((size_t)branch->on_complete_action_start_index + branch->on_complete_action_count >
        scenario->reactive_action_count) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < branch->on_complete_action_count; ++i) {
        cJSON *action_obj = api_build_reactive_action_json(
            scenario,
            &scenario->reactive_actions[branch->on_complete_action_start_index + i]);
        if (!action_obj) {
            return ESP_ERR_INVALID_STATE;
        }
        cJSON_AddItemToArray(on_complete, action_obj);
    }
    return ESP_OK;
}

static esp_err_t api_add_branch_json(cJSON *branches,
                                     const orch_room_scenario_detail_t *scenario,
                                     const orch_room_scenario_branch_detail_t *branch)
{
    cJSON *branch_obj = NULL;
    esp_err_t err = ESP_OK;
    if (!branches || !scenario || !branch) {
        return ESP_ERR_INVALID_ARG;
    }
    branch_obj = cJSON_CreateObject();
    if (!branch_obj) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(branch_obj, "id", branch->id);
    cJSON_AddStringToObject(branch_obj, "name", branch->name);
    cJSON_AddStringToObject(branch_obj, "type", branch->type_text);
    cJSON_AddBoolToObject(branch_obj, "enabled", branch->enabled);
    cJSON_AddBoolToObject(branch_obj, "required_for_completion", branch->required_for_completion);
    if (branch->priority > 0) {
        cJSON_AddNumberToObject(branch_obj, "priority", branch->priority);
    }
    if (branch->type == ROOM_SCENARIO_BRANCH_REACTIVE &&
        (branch->variant_count > 0 || branch->trigger.kind != ROOM_SCENARIO_REACTIVE_TRIGGER_NONE)) {
        err = api_add_reactive_branch_v2(branch_obj, scenario, branch);
    } else {
        err = api_add_branch_steps(branch_obj, scenario, branch);
    }
    if (err != ESP_OK) {
        cJSON_Delete(branch_obj);
        return err;
    }
    cJSON_AddItemToArray(branches, branch_obj);
    return ESP_OK;
}

cJSON *orchestrator_api_view_room_scenarios(const char *room_id,
                                            const orch_room_scenario_detail_t *scenarios,
                                            size_t scenario_count)
{
    if (!room_id || (!scenarios && scenario_count > 0)) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();
    if (!root || !items) {
        if (root) {
            cJSON_Delete(root);
        }
        if (items) {
            cJSON_Delete(items);
        }
        return NULL;
    }

    cJSON_AddStringToObject(root, "room_id", room_id);
    cJSON_AddNumberToObject(root, "count", (double)scenario_count);
    for (size_t i = 0; i < scenario_count; ++i) {
        const orch_room_scenario_detail_t *scenario = &scenarios[i];
        cJSON *item = cJSON_CreateObject();
        cJSON *steps = cJSON_CreateArray();
        cJSON *issues = cJSON_CreateArray();
        if (!item || !steps || !issues) {
            if (item) {
                cJSON_Delete(item);
            }
            if (steps) {
                cJSON_Delete(steps);
            }
            if (issues) {
                cJSON_Delete(issues);
            }
            cJSON_Delete(root);
            cJSON_Delete(items);
            return NULL;
        }
        cJSON_AddStringToObject(item, "room_id", scenario->summary.room_id);
        cJSON_AddStringToObject(item, "id", scenario->summary.id);
        cJSON_AddStringToObject(item, "name", scenario->summary.name);
        cJSON_AddNumberToObject(item, "step_count", (double)scenario->summary.step_count);
        cJSON_AddBoolToObject(item, "valid", scenario->summary.valid);
        cJSON_AddNumberToObject(item,
                                "validation_issue_count",
                                (double)scenario->summary.validation_issue_count);
        for (size_t step_index = 0; step_index < scenario->summary.step_count &&
                                    step_index < ORCH_ROOM_SCENARIO_MAX_STEPS; ++step_index) {
            cJSON *step_obj = api_build_step_json(&scenario->steps[step_index]);
            if (!step_obj) {
                cJSON_Delete(steps);
                cJSON_Delete(issues);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            cJSON_AddItemToArray(steps, step_obj);
        }
        cJSON_AddItemToObject(item, "steps", steps);
        if (scenario->branch_count > 0) {
            cJSON *branches = cJSON_CreateArray();
            if (!branches) {
                cJSON_Delete(issues);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            for (uint8_t branch_index = 0; branch_index < scenario->branch_count; ++branch_index) {
                if (api_add_branch_json(branches, scenario, &scenario->branches[branch_index]) != ESP_OK) {
                    cJSON_Delete(branches);
                    cJSON_Delete(issues);
                    cJSON_Delete(item);
                    cJSON_Delete(root);
                    cJSON_Delete(items);
                    return NULL;
                }
            }
            cJSON_AddItemToObject(item, "branches", branches);
        }
        for (size_t issue_index = 0;
             issue_index < scenario->summary.validation_issue_count &&
             issue_index < ROOM_SCENARIO_VALIDATION_MAX_ISSUES;
             ++issue_index) {
            const orch_room_scenario_validation_issue_entry_t *issue =
                &scenario->validation_issues[issue_index];
            cJSON *issue_obj = cJSON_CreateObject();
            if (!issue_obj) {
                cJSON_Delete(issues);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            cJSON_AddStringToObject(issue_obj, "level", issue->level_text);
            cJSON_AddNumberToObject(issue_obj, "step_index", issue->step_index);
            cJSON_AddStringToObject(issue_obj, "code", issue->code);
            cJSON_AddStringToObject(issue_obj, "message", issue->message);
            cJSON_AddItemToArray(issues, issue_obj);
        }
        cJSON_AddItemToObject(item, "validation_issues", issues);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "scenarios", items);
    return root;
}
