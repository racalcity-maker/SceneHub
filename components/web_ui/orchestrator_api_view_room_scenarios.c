#include "orchestrator_api_view.h"

static const char *api_room_scenario_step_type_str(orch_room_scenario_step_type_t type)
{
    switch (type) {
    case ORCH_ROOM_SCENARIO_STEP_WAIT_TIME:
        return "wait_time";
    case ORCH_ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        return "operator_approval";
    case ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        return "device_command";
    case ORCH_ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        return "wait_device_event";
    case ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        return "device_command_group";
    case ORCH_ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        return "show_operator_message";
    case ORCH_ROOM_SCENARIO_STEP_SET_FLAG:
        return "set_flag";
    case ORCH_ROOM_SCENARIO_STEP_WAIT_FLAGS:
        return "wait_flags";
    case ORCH_ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        return "wait_any_device_event";
    case ORCH_ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        return "wait_all_device_events";
    case ORCH_ROOM_SCENARIO_STEP_END_GAME:
        return "end_game";
    default:
        return "operator_approval";
    }
}

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

static esp_err_t api_add_flag_refs(cJSON *obj,
                                   const char *array_name,
                                   const char *count_name,
                                   const orch_room_scenario_flag_ref_t *flags,
                                   uint8_t count)
{
    cJSON *array = NULL;
    if (!obj || !array_name || (!flags && count > 0)) {
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
        cJSON_AddStringToObject(flag, "name", flags[i].name);
        cJSON_AddBoolToObject(flag, "value", flags[i].value);
        cJSON_AddItemToArray(array, flag);
    }
    cJSON_AddItemToObject(obj, array_name, array);
    if (count_name && count_name[0]) {
        cJSON_AddNumberToObject(obj, count_name, count);
    }
    return ESP_OK;
}

static const char *api_room_scenario_validation_level_str(room_scenario_validation_level_t level)
{
    switch (level) {
    case ROOM_SCENARIO_VALIDATION_WARNING:
        return "warning";
    case ROOM_SCENARIO_VALIDATION_ERROR:
    default:
        return "error";
    }
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
        if (!item || !steps) {
            if (item) {
                cJSON_Delete(item);
            }
            if (steps) {
                cJSON_Delete(steps);
            }
            cJSON_Delete(root);
            cJSON_Delete(items);
            return NULL;
        }
        cJSON_AddStringToObject(item, "id", scenario->summary.id);
        cJSON_AddStringToObject(item, "name", scenario->summary.name);
        cJSON_AddNumberToObject(item, "step_count", (double)scenario->summary.step_count);
        cJSON_AddBoolToObject(item, "valid", scenario->summary.valid);
        cJSON_AddNumberToObject(item,
                                "validation_issue_count",
                                (double)scenario->summary.validation_issue_count);
        for (size_t step_index = 0; step_index < scenario->summary.step_count &&
                                    step_index < ORCH_ROOM_SCENARIO_MAX_STEPS; ++step_index) {
            const orch_room_scenario_step_entry_t *step = &scenario->steps[step_index];
            cJSON *step_obj = cJSON_CreateObject();
            if (!step_obj) {
                cJSON_Delete(steps);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            cJSON_AddStringToObject(step_obj, "id", step->id);
            cJSON_AddStringToObject(step_obj, "label", step->label);
            cJSON_AddStringToObject(step_obj, "type", api_room_scenario_step_type_str(step->type));
            cJSON_AddBoolToObject(step_obj, "enabled", step->enabled);
            cJSON_AddStringToObject(step_obj, "device_id", step->device_id);
            cJSON_AddStringToObject(step_obj, "scenario_id", step->scenario_id);
            cJSON_AddStringToObject(step_obj, "command_id", step->command_id);
            cJSON_AddStringToObject(step_obj, "event_id", step->event_id);
            if (api_add_json_object_string(step_obj, "params", step->params_json) != ESP_OK) {
                cJSON_Delete(step_obj);
                cJSON_Delete(steps);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            cJSON_AddNumberToObject(step_obj, "duration_ms", step->duration_ms);
            cJSON_AddStringToObject(step_obj, "event_type", step->event_type);
            cJSON_AddStringToObject(step_obj, "source_id", step->source_id);
            cJSON_AddStringToObject(step_obj, "operator_prompt", step->operator_prompt);
            cJSON_AddStringToObject(step_obj, "operator_approve_label", step->operator_approve_label);
            cJSON_AddStringToObject(step_obj, "operator_message", step->operator_message);
            cJSON_AddStringToObject(step_obj, "flag_name", step->flag_name);
            cJSON_AddBoolToObject(step_obj, "flag_value", step->flag_value);
            cJSON *event_refs = cJSON_CreateArray();
            if (!event_refs) {
                cJSON_Delete(step_obj);
                cJSON_Delete(steps);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            for (uint8_t event_index = 0;
                 event_index < step->event_count && event_index < ORCH_ROOM_SCENARIO_MAX_EVENT_REFS;
                 ++event_index) {
                cJSON *event = cJSON_CreateObject();
                if (!event) {
                    cJSON_Delete(event_refs);
                    cJSON_Delete(step_obj);
                    cJSON_Delete(steps);
                    cJSON_Delete(item);
                    cJSON_Delete(root);
                    cJSON_Delete(items);
                    return NULL;
                }
                cJSON_AddStringToObject(event, "device_id", step->events[event_index].device_id);
                cJSON_AddStringToObject(event, "event_id", step->events[event_index].event_id);
                cJSON_AddItemToArray(event_refs, event);
            }
            cJSON_AddItemToObject(step_obj, "events", event_refs);
            cJSON_AddNumberToObject(step_obj, "event_count", step->event_count);
            if (api_add_flag_refs(step_obj,
                                  "flags",
                                  "flag_count",
                                  step->flags,
                                  step->flag_count) != ESP_OK) {
                cJSON_Delete(step_obj);
                cJSON_Delete(steps);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            cJSON_AddNumberToObject(step_obj, "command_count", step->command_count);
            cJSON_AddItemToArray(steps, step_obj);
        }
        cJSON_AddItemToObject(item, "steps", steps);
        cJSON *issues = cJSON_CreateArray();
        if (!issues) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            cJSON_Delete(items);
            return NULL;
        }
        for (size_t issue_index = 0;
             issue_index < scenario->summary.validation_issue_count &&
             issue_index < ROOM_SCENARIO_VALIDATION_MAX_ISSUES;
             ++issue_index) {
            const room_scenario_validation_issue_t *issue = &scenario->validation_issues[issue_index];
            cJSON *issue_obj = cJSON_CreateObject();
            if (!issue_obj) {
                cJSON_Delete(issues);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            cJSON_AddStringToObject(issue_obj,
                                    "level",
                                    api_room_scenario_validation_level_str(issue->level));
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
