#include "room_scenario_internal.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"

esp_err_t room_scenario_import_reactive_branch_v2_json(const cJSON *branch_obj,
                                                          room_scenario_t *scenario,
                                                          room_scenario_branch_t *branch);

static esp_err_t json_copy_string_required(const cJSON *obj,
                                           const char *name,
                                           char *dst,
                                           size_t dst_len)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!cJSON_IsString(item) || !item->valuestring || !item->valuestring[0] ||
        strlen(item->valuestring) >= dst_len) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(dst, dst_len, "%s", item->valuestring);
    return ESP_OK;
}

static esp_err_t json_copy_string_optional(const cJSON *obj,
                                           const char *name,
                                           char *dst,
                                           size_t dst_len)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!item || cJSON_IsNull(item)) {
        dst[0] = '\0';
        return ESP_OK;
    }
    if (!cJSON_IsString(item) || !item->valuestring || strlen(item->valuestring) >= dst_len) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(dst, dst_len, "%s", item->valuestring);
    return ESP_OK;
}

static esp_err_t json_get_uint32_required(const cJSON *obj,
                                          const char *name,
                                          uint32_t *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    uint32_t value = 0;
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > (double)UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    value = (uint32_t)item->valuedouble;
    if ((double)value != item->valuedouble) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = value;
    return ESP_OK;
}

static esp_err_t json_get_uint32_optional(const cJSON *obj,
                                          const char *name,
                                          uint32_t *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    uint32_t value = 0;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!item || cJSON_IsNull(item)) {
        *out = 0;
        return ESP_OK;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > (double)UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    value = (uint32_t)item->valuedouble;
    if ((double)value != item->valuedouble) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = value;
    return ESP_OK;
}

static esp_err_t json_copy_object_string_optional(const cJSON *obj,
                                                  const char *name,
                                                  char *dst,
                                                  size_t dst_len)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    char *printed = NULL;
    if (!dst || dst_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    dst[0] = '\0';
    if (!item || cJSON_IsNull(item)) {
        return ESP_OK;
    }
    if (!cJSON_IsObject(item)) {
        return ESP_ERR_INVALID_ARG;
    }
    printed = cJSON_PrintUnformatted(item);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }
    if (strlen(printed) >= dst_len) {
        cJSON_free(printed);
        return ESP_ERR_INVALID_SIZE;
    }
    snprintf(dst, dst_len, "%s", printed);
    cJSON_free(printed);
    return ESP_OK;
}

static esp_err_t reactive_policy_mode_from_str(const char *s,
                                               room_scenario_reactive_policy_mode_t *out)
{
    if (!s || !s[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcasecmp(s, "single") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_POLICY_SINGLE;
        return ESP_OK;
    }
    if (strcasecmp(s, "rotate") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_POLICY_ROTATE;
        return ESP_OK;
    }
    if (strcasecmp(s, "random") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_POLICY_RANDOM;
        return ESP_OK;
    }
    if (strcasecmp(s, "escalate") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_POLICY_ESCALATE;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}


static esp_err_t room_scenario_import_command_json(const cJSON *obj,
                                                   room_scenario_device_command_t *command)
{
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(obj) || !command) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(command, 0, sizeof(*command));
    err = json_copy_string_required(obj,
                                    "device_id",
                                    command->device_id,
                                    sizeof(command->device_id));
    if (err != ESP_OK) {
        return err;
    }
    err = json_copy_string_required(obj,
                                    "command_id",
                                    command->command_id,
                                    sizeof(command->command_id));
    if (err != ESP_OK) {
        return err;
    }
    return json_copy_object_string_optional(obj,
                                            "params",
                                            command->params_json,
                                            sizeof(command->params_json));
}

static esp_err_t room_scenario_import_step_json(const cJSON *obj,
                                                room_scenario_step_t *step)
{
    const cJSON *enabled = NULL;
    const cJSON *type = NULL;
    const cJSON *allow_skip = NULL;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(obj)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(step, 0, sizeof(*step));
    err = json_copy_string_required(obj, "id", step->id, sizeof(step->id));
    if (err != ESP_OK) {
        return err;
    }
    err = json_copy_string_optional(obj, "label", step->label, sizeof(step->label));
    if (err != ESP_OK) {
        return err;
    }
    enabled = cJSON_GetObjectItemCaseSensitive(obj, "enabled");
    if (!enabled) {
        step->enabled = true;
    } else if (cJSON_IsBool(enabled)) {
        step->enabled = cJSON_IsTrue(enabled);
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    type = cJSON_GetObjectItemCaseSensitive(obj, "type");
    if (!cJSON_IsString(type) || !type->valuestring ||
        room_scenario_step_type_from_str(type->valuestring, &step->type) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    allow_skip = cJSON_GetObjectItemCaseSensitive(obj, "allow_operator_skip");
    if (allow_skip) {
        if (!cJSON_IsBool(allow_skip)) {
            return ESP_ERR_INVALID_ARG;
        }
        step->allow_operator_skip = cJSON_IsTrue(allow_skip);
    }
    err = json_copy_string_optional(obj,
                                    "operator_skip_label",
                                    step->operator_skip_label,
                                    sizeof(step->operator_skip_label));
    if (err != ESP_OK) {
        return err;
    }
    switch (step->type) {
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        return json_get_uint32_required(obj,
                                        "duration_ms",
                                        &step->data.wait_time.duration_ms);
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        return room_scenario_import_command_json(obj, &step->data.device_command);
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP: {
        const cJSON *commands = cJSON_GetObjectItemCaseSensitive(obj, "commands");
        int count = cJSON_IsArray(commands) ? cJSON_GetArraySize(commands) : 0;
        if (count <= 0 || count > ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS) {
            return ESP_ERR_INVALID_ARG;
        }
        step->data.device_command_group.command_count = (uint8_t)count;
        for (int i = 0; i < count; ++i) {
            const cJSON *command = cJSON_GetArrayItem(commands, i);
            err = json_copy_string_required(command,
                                            "device_id",
                                            step->data.device_command_group.commands[i].device_id,
                                            sizeof(step->data.device_command_group.commands[i].device_id));
            if (err == ESP_OK) {
                err = json_copy_string_required(command,
                                                "command_id",
                                                step->data.device_command_group.commands[i].command_id,
                                                sizeof(step->data.device_command_group.commands[i].command_id));
            }
            if (err == ESP_OK) {
                err = json_copy_object_string_optional(command,
                                                       "params",
                                                       step->data.device_command_group.commands[i].params_json,
                                                       sizeof(step->data.device_command_group.commands[i].params_json));
            }
            if (err != ESP_OK) {
                return err;
            }
        }
        return ESP_OK;
    }
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        err = json_copy_string_required(obj,
                                        "device_id",
                                        step->data.wait_device_event.device_id,
                                        sizeof(step->data.wait_device_event.device_id));
        if (err != ESP_OK) {
            return err;
        }
        err = json_copy_string_required(obj,
                                        "event_id",
                                        step->data.wait_device_event.event_id,
                                        sizeof(step->data.wait_device_event.event_id));
        if (err != ESP_OK) {
            return err;
        }
        err = json_get_uint32_optional(obj,
                                       "timeout_ms",
                                       &step->data.wait_device_event.timeout_ms);
        if (err != ESP_OK) {
            return err;
        }
        return json_copy_string_optional(obj,
                                         "timeout_message",
                                         step->data.wait_device_event.timeout_message,
                                         sizeof(step->data.wait_device_event.timeout_message));
    case ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        err = json_copy_string_required(obj,
                                        "prompt",
                                        step->data.operator_approval.prompt,
                                        sizeof(step->data.operator_approval.prompt));
        if (err != ESP_OK) {
            return err;
        }
        return json_copy_string_optional(obj,
                                         "approve_label",
                                         step->data.operator_approval.approve_label,
                                         sizeof(step->data.operator_approval.approve_label));
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        return json_copy_string_required(obj,
                                         "message",
                                         step->data.operator_message.message,
                                         sizeof(step->data.operator_message.message));
    case ROOM_SCENARIO_STEP_SET_FLAG: {
        const cJSON *value = NULL;
        err = json_copy_string_required(obj,
                                        "flag_name",
                                        step->data.set_flag.name,
                                        sizeof(step->data.set_flag.name));
        if (err != ESP_OK) {
            return err;
        }
        value = cJSON_GetObjectItemCaseSensitive(obj, "value");
        if (!cJSON_IsBool(value)) {
            return ESP_ERR_INVALID_ARG;
        }
        step->data.set_flag.value = cJSON_IsTrue(value);
        return ESP_OK;
    }
    case ROOM_SCENARIO_STEP_WAIT_FLAGS: {
        const cJSON *flags = cJSON_GetObjectItemCaseSensitive(obj, "flags");
        int count = cJSON_IsArray(flags) ? cJSON_GetArraySize(flags) : 0;
        if (count <= 0 || count > ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS) {
            return ESP_ERR_INVALID_ARG;
        }
        step->data.wait_flags.flag_count = (uint8_t)count;
        for (int i = 0; i < count; ++i) {
            const cJSON *flag = cJSON_GetArrayItem(flags, i);
            const cJSON *value = NULL;
            err = json_copy_string_required(flag,
                                            "flag_name",
                                            step->data.wait_flags.flags[i].name,
                                            sizeof(step->data.wait_flags.flags[i].name));
            if (err != ESP_OK) {
                return err;
            }
            value = cJSON_GetObjectItemCaseSensitive(flag, "value");
            if (!cJSON_IsBool(value)) {
                return ESP_ERR_INVALID_ARG;
            }
            step->data.wait_flags.flags[i].value = cJSON_IsTrue(value);
        }
        err = json_get_uint32_optional(obj, "timeout_ms", &step->data.wait_flags.timeout_ms);
        if (err != ESP_OK) {
            return err;
        }
        return json_copy_string_optional(obj,
                                         "timeout_message",
                                         step->data.wait_flags.timeout_message,
                                         sizeof(step->data.wait_flags.timeout_message));
    }
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT: {
        const cJSON *events = cJSON_GetObjectItemCaseSensitive(obj, "events");
        int count = cJSON_IsArray(events) ? cJSON_GetArraySize(events) : 0;
        if (count <= 0 || count > ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS) {
            return ESP_ERR_INVALID_ARG;
        }
        step->data.wait_any_device_event.event_count = (uint8_t)count;
        for (int i = 0; i < count; ++i) {
            const cJSON *event = cJSON_GetArrayItem(events, i);
            err = json_copy_string_required(event,
                                            "device_id",
                                            step->data.wait_any_device_event.events[i].device_id,
                                            sizeof(step->data.wait_any_device_event.events[i].device_id));
            if (err == ESP_OK) {
                err = json_copy_string_required(event,
                                                "event_id",
                                                step->data.wait_any_device_event.events[i].event_id,
                                                sizeof(step->data.wait_any_device_event.events[i].event_id));
            }
            if (err != ESP_OK) {
                return err;
            }
        }
        return ESP_OK;
    }
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS: {
        const cJSON *events = cJSON_GetObjectItemCaseSensitive(obj, "events");
        int count = cJSON_IsArray(events) ? cJSON_GetArraySize(events) : 0;
        if (count <= 0 || count > ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS) {
            return ESP_ERR_INVALID_ARG;
        }
        step->data.wait_all_device_events.event_count = (uint8_t)count;
        for (int i = 0; i < count; ++i) {
            const cJSON *event = cJSON_GetArrayItem(events, i);
            err = json_copy_string_required(event,
                                            "device_id",
                                            step->data.wait_all_device_events.events[i].device_id,
                                            sizeof(step->data.wait_all_device_events.events[i].device_id));
            if (err == ESP_OK) {
                err = json_copy_string_required(event,
                                                "event_id",
                                                step->data.wait_all_device_events.events[i].event_id,
                                                sizeof(step->data.wait_all_device_events.events[i].event_id));
            }
            if (err != ESP_OK) {
                return err;
            }
        }
        return ESP_OK;
    }
    case ROOM_SCENARIO_STEP_END_GAME:
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t room_scenario_import_steps_array(const cJSON *steps,
                                                  room_scenario_t *scenario,
                                                  size_t *io_step_index,
                                                  uint16_t *out_count)
{
    int step_count = 0;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsArray(steps) || !scenario || !io_step_index || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    step_count = cJSON_GetArraySize(steps);
    if (step_count < 0 ||
        *io_step_index + (size_t)step_count > ROOM_SCENARIO_MAX_STEPS) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (int i = 0; i < step_count; ++i) {
        const cJSON *step_obj = cJSON_GetArrayItem(steps, i);
        err = room_scenario_import_step_json(step_obj, &scenario->steps[*io_step_index]);
        if (err != ESP_OK) {
            return err;
        }
        (*io_step_index)++;
    }
    *out_count = (uint16_t)step_count;
    scenario->step_count = *io_step_index;
    return ESP_OK;
}


static esp_err_t room_scenario_import_branches_json(const cJSON *branches,
                                                    room_scenario_t *scenario)
{
    int branch_count = 0;
    size_t step_index = 0;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsArray(branches) || !scenario) {
        return ESP_ERR_INVALID_ARG;
    }
    branch_count = cJSON_GetArraySize(branches);
    if (branch_count <= 0 || branch_count > ROOM_SCENARIO_MAX_BRANCHES) {
        return ESP_ERR_INVALID_SIZE;
    }
    scenario->branch_count = (size_t)branch_count;
    for (int i = 0; i < branch_count; ++i) {
        const cJSON *branch_obj = cJSON_GetArrayItem(branches, i);
        const cJSON *steps = NULL;
        const cJSON *enabled = NULL;
        const cJSON *required = NULL;
        const cJSON *type = NULL;
        const cJSON *run_once = NULL;
        const cJSON *policy = NULL;
        const cJSON *reentry = NULL;
        uint32_t priority = 0;
        room_scenario_branch_t *branch = &scenario->branches[i];
        if (!cJSON_IsObject(branch_obj)) {
            return ESP_ERR_INVALID_ARG;
        }
        err = json_copy_string_required(branch_obj, "id", branch->id, sizeof(branch->id));
        if (err != ESP_OK) {
            return err;
        }
        err = json_copy_string_required(branch_obj, "name", branch->name, sizeof(branch->name));
        if (err != ESP_OK) {
            return err;
        }
        type = cJSON_GetObjectItemCaseSensitive(branch_obj, "type");
        if (!type || cJSON_IsNull(type)) {
            branch->type = ROOM_SCENARIO_BRANCH_NORMAL;
        } else if (!cJSON_IsString(type) || !type->valuestring ||
                   room_scenario_branch_type_from_str(type->valuestring, &branch->type) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }
        enabled = cJSON_GetObjectItemCaseSensitive(branch_obj, "enabled");
        if (!enabled) {
            branch->enabled = true;
        } else if (cJSON_IsBool(enabled)) {
            branch->enabled = cJSON_IsTrue(enabled);
        } else {
            return ESP_ERR_INVALID_ARG;
        }
        err = json_get_uint32_optional(branch_obj, "priority", &priority);
        if (err != ESP_OK || priority > UINT16_MAX) {
            return err != ESP_OK ? err : ESP_ERR_INVALID_ARG;
        }
        branch->priority = (uint16_t)priority;
        required = cJSON_GetObjectItemCaseSensitive(branch_obj, "required_for_completion");
        if (!required) {
            branch->required_for_completion = branch->type == ROOM_SCENARIO_BRANCH_NORMAL;
        } else if (cJSON_IsBool(required)) {
            branch->required_for_completion = cJSON_IsTrue(required);
        } else {
            return ESP_ERR_INVALID_ARG;
        }
        err = json_get_uint32_optional(branch_obj, "cooldown_ms", &branch->cooldown_ms);
        if (err != ESP_OK) {
            return err;
        }
        err = json_get_uint32_optional(branch_obj, "max_fire_count", &branch->max_fire_count);
        if (err != ESP_OK) {
            return err;
        }
        policy = cJSON_GetObjectItemCaseSensitive(branch_obj, "policy");
        branch->policy_mode = ROOM_SCENARIO_REACTIVE_POLICY_SINGLE;
        if (policy) {
            const cJSON *mode = NULL;
            if (!cJSON_IsObject(policy)) {
                return ESP_ERR_INVALID_ARG;
            }
            mode = cJSON_GetObjectItemCaseSensitive(policy, "mode");
            if (mode && (!cJSON_IsString(mode) || !mode->valuestring ||
                         reactive_policy_mode_from_str(mode->valuestring,
                                                       &branch->policy_mode) != ESP_OK)) {
                return ESP_ERR_INVALID_ARG;
            }
            err = json_get_uint32_optional(policy, "cooldown_ms", &branch->cooldown_ms);
            if (err != ESP_OK) {
                return err;
            }
            err = json_get_uint32_optional(policy, "max_fire_count", &branch->max_fire_count);
            if (err != ESP_OK) {
                return err;
            }
        }
        run_once = cJSON_GetObjectItemCaseSensitive(branch_obj, "run_once");
        if (run_once) {
            if (!cJSON_IsBool(run_once)) {
                return ESP_ERR_INVALID_ARG;
            }
            branch->run_once = cJSON_IsTrue(run_once);
        }
        branch->reentry_mode = ROOM_SCENARIO_REENTRY_IGNORE;
        reentry = cJSON_GetObjectItemCaseSensitive(branch_obj, "reentry");
        if (reentry) {
            const cJSON *mode = NULL;
            if (!cJSON_IsObject(reentry)) {
                return ESP_ERR_INVALID_ARG;
            }
            mode = cJSON_GetObjectItemCaseSensitive(reentry, "mode");
            if (mode) {
                if (!cJSON_IsString(mode) || !mode->valuestring ||
                    room_scenario_reentry_mode_from_str(mode->valuestring,
                                                        &branch->reentry_mode) != ESP_OK) {
                    return ESP_ERR_INVALID_ARG;
                }
            }
        }
        branch->step_start_index = (uint16_t)step_index;
        if (branch->type == ROOM_SCENARIO_BRANCH_REACTIVE &&
            cJSON_GetObjectItemCaseSensitive(branch_obj, "variants")) {
            err = room_scenario_import_reactive_branch_v2_json(branch_obj, scenario, branch);
            if (err != ESP_OK) {
                return err;
            }
            if (branch->policy_mode == ROOM_SCENARIO_REACTIVE_POLICY_SINGLE) {
                branch->max_fire_count = branch->run_once ? 1 : 0;
            }
            branch->step_count = 0;
        } else {
            steps = cJSON_GetObjectItemCaseSensitive(branch_obj, "steps");
            err = room_scenario_import_steps_array(steps, scenario, &step_index, &branch->step_count);
            if (err != ESP_OK) {
                return err;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t room_scenario_import_one_json(const cJSON *obj,
                                               room_scenario_t *scenario)
{
    const cJSON *branches = NULL;
    const cJSON *steps = NULL;
    size_t step_index = 0;
    uint16_t flat_step_count = 0;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(obj)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(scenario, 0, sizeof(*scenario));
    err = json_copy_string_required(obj, "id", scenario->id, sizeof(scenario->id));
    if (err != ESP_OK) {
        return err;
    }
    err = json_copy_string_required(obj, "name", scenario->name, sizeof(scenario->name));
    if (err != ESP_OK) {
        return err;
    }
    err = json_copy_string_required(obj, "room_id", scenario->room_id, sizeof(scenario->room_id));
    if (err != ESP_OK) {
        return err;
    }
    branches = cJSON_GetObjectItemCaseSensitive(obj, "branches");
    if (branches) {
        err = room_scenario_import_branches_json(branches, scenario);
        if (err != ESP_OK) {
            return err;
        }
    } else {
        steps = cJSON_GetObjectItemCaseSensitive(obj, "steps");
        err = room_scenario_import_steps_array(steps, scenario, &step_index, &flat_step_count);
        if (err != ESP_OK) {
            return err;
        }
        scenario->branch_count = 0;
    }
    return room_scenario_validate_structural(scenario);
}

esp_err_t room_scenario_from_json(const cJSON *json, room_scenario_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    return room_scenario_import_one_json(json, out);
}
