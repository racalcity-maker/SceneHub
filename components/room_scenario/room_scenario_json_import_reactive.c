#include "room_scenario_internal.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"

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

static esp_err_t reactive_trigger_kind_from_str(const char *s,
                                                room_scenario_reactive_trigger_kind_t *out)
{
    if (!s || !s[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcasecmp(s, "device_event") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
        return ESP_OK;
    }
    if (strcasecmp(s, "flag_changed") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_TRIGGER_FLAG_CHANGED;
        return ESP_OK;
    }
    if (strcasecmp(s, "operator_event") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_TRIGGER_OPERATOR_EVENT;
        return ESP_OK;
    }
    if (strcasecmp(s, "runtime_event") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_TRIGGER_RUNTIME_EVENT;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}


static esp_err_t reactive_result_action_from_str(const char *s,
                                                 room_scenario_reactive_result_action_t *out)
{
    if (!s || !s[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcasecmp(s, "continue") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_RESULT_CONTINUE;
        return ESP_OK;
    }
    if (strcasecmp(s, "set_flag") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_RESULT_SET_FLAG;
        return ESP_OK;
    }
    if (strcasecmp(s, "fail_reaction") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_RESULT_FAIL_REACTION;
        return ESP_OK;
    }
    if (strcasecmp(s, "fail_scenario") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_RESULT_FAIL_SCENARIO;
        return ESP_OK;
    }
    if (strcasecmp(s, "retry") == 0) {
        *out = ROOM_SCENARIO_REACTIVE_RESULT_RETRY;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t command_group_mode_from_str(const char *s,
                                             room_scenario_command_group_mode_t *out)
{
    if (!s || !s[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcasecmp(s, "sequential") == 0) {
        *out = ROOM_SCENARIO_COMMAND_GROUP_SEQUENTIAL;
        return ESP_OK;
    }
    if (strcasecmp(s, "parallel") == 0) {
        *out = ROOM_SCENARIO_COMMAND_GROUP_PARALLEL;
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

static esp_err_t room_scenario_import_reactive_action_json(const cJSON *obj,
                                                           room_scenario_t *scenario,
                                                           room_scenario_reactive_action_t *action)
{
    const cJSON *type = NULL;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(obj) || !scenario || !action) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(action, 0, sizeof(*action));
    err = json_copy_string_optional(obj, "id", action->id, sizeof(action->id));
    if (err != ESP_OK) {
        return err;
    }
    err = json_copy_string_optional(obj, "label", action->label, sizeof(action->label));
    if (err != ESP_OK) {
        return err;
    }
    type = cJSON_GetObjectItemCaseSensitive(obj, "type");
    if (!cJSON_IsString(type) || !type->valuestring ||
        room_scenario_step_type_from_str(type->valuestring, &action->type) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (action->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        return room_scenario_import_command_json(obj, &action->data.device_command);
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP: {
        const cJSON *commands = cJSON_GetObjectItemCaseSensitive(obj, "commands");
        const cJSON *mode = cJSON_GetObjectItemCaseSensitive(obj, "mode");
        int count = cJSON_IsArray(commands) ? cJSON_GetArraySize(commands) : 0;
        if (count <= 0 ||
            scenario->reactive_group_command_count + (size_t)count >
                ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS) {
            return ESP_ERR_INVALID_ARG;
        }
        action->group_mode = ROOM_SCENARIO_COMMAND_GROUP_SEQUENTIAL;
        if (mode) {
            if (!cJSON_IsString(mode) || !mode->valuestring ||
                command_group_mode_from_str(mode->valuestring, &action->group_mode) != ESP_OK) {
                return ESP_ERR_INVALID_ARG;
            }
        }
        action->group_command_start_index = (uint16_t)scenario->reactive_group_command_count;
        action->group_command_count = (uint8_t)count;
        for (int i = 0; i < count; ++i) {
            err = room_scenario_import_command_json(
                cJSON_GetArrayItem(commands, i),
                &scenario->reactive_group_commands[scenario->reactive_group_command_count]);
            if (err != ESP_OK) {
                return err;
            }
            scenario->reactive_group_command_count++;
        }
        return ESP_OK;
    }
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        return json_get_uint32_required(obj, "duration_ms", &action->data.wait_time.duration_ms);
    case ROOM_SCENARIO_STEP_SET_FLAG: {
        const cJSON *value = NULL;
        err = json_copy_string_optional(obj,
                                        "flag",
                                        action->data.set_flag.name,
                                        sizeof(action->data.set_flag.name));
        if (err == ESP_OK && !action->data.set_flag.name[0]) {
            err = json_copy_string_required(obj,
                                            "flag_name",
                                            action->data.set_flag.name,
                                            sizeof(action->data.set_flag.name));
        }
        if (err != ESP_OK) {
            return err;
        }
        value = cJSON_GetObjectItemCaseSensitive(obj, "value");
        if (!cJSON_IsBool(value)) {
            return ESP_ERR_INVALID_ARG;
        }
        action->data.set_flag.value = cJSON_IsTrue(value);
        return ESP_OK;
    }
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        return json_copy_string_required(obj,
                                         "message",
                                         action->data.operator_message.message,
                                         sizeof(action->data.operator_message.message));
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t room_scenario_import_reactive_actions_array(
    const cJSON *actions,
    room_scenario_t *scenario,
    uint16_t *out_start,
    uint8_t *out_count)
{
    int count = cJSON_IsArray(actions) ? cJSON_GetArraySize(actions) : 0;
    esp_err_t err = ESP_OK;
    if (!scenario || !out_start || !out_count || count <= 0 ||
        scenario->reactive_action_count + (size_t)count > ROOM_SCENARIO_MAX_REACTIVE_ACTIONS) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_start = (uint16_t)scenario->reactive_action_count;
    *out_count = (uint8_t)count;
    for (int i = 0; i < count; ++i) {
        err = room_scenario_import_reactive_action_json(
            cJSON_GetArrayItem(actions, i),
            scenario,
            &scenario->reactive_actions[scenario->reactive_action_count]);
        if (err != ESP_OK) {
            return err;
        }
        scenario->reactive_action_count++;
    }
    return ESP_OK;
}

esp_err_t room_scenario_import_reactive_branch_v2_json(const cJSON *branch_obj,
                                                              room_scenario_t *scenario,
                                                              room_scenario_branch_t *branch)
{
    const cJSON *trigger = cJSON_GetObjectItemCaseSensitive(branch_obj, "trigger");
    const cJSON *guards = cJSON_GetObjectItemCaseSensitive(branch_obj, "guard_flags");
    const cJSON *variants = cJSON_GetObjectItemCaseSensitive(branch_obj, "variants");
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(branch_obj, "result_policy");
    const cJSON *complete = cJSON_GetObjectItemCaseSensitive(branch_obj, "on_complete");
    const cJSON *kind = NULL;
    int guard_count = 0;
    int variant_count = 0;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(trigger) || !cJSON_IsArray(variants) || !scenario || !branch) {
        return ESP_ERR_INVALID_ARG;
    }
    kind = cJSON_GetObjectItemCaseSensitive(trigger, "kind");
    if (!cJSON_IsString(kind) || !kind->valuestring ||
        reactive_trigger_kind_from_str(kind->valuestring, &branch->trigger.kind) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (branch->trigger.kind) {
    case ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT:
        err = json_copy_string_required(trigger,
                                        "device_id",
                                        branch->trigger.device_id,
                                        sizeof(branch->trigger.device_id));
        if (err == ESP_OK) {
            err = json_copy_string_required(trigger,
                                            "event_id",
                                            branch->trigger.event_id,
                                            sizeof(branch->trigger.event_id));
        }
        break;
    case ROOM_SCENARIO_REACTIVE_TRIGGER_FLAG_CHANGED:
        err = json_copy_string_required(trigger,
                                        "flag_name",
                                        branch->trigger.flag_name,
                                        sizeof(branch->trigger.flag_name));
        break;
    case ROOM_SCENARIO_REACTIVE_TRIGGER_OPERATOR_EVENT:
        err = json_copy_string_required(trigger,
                                        "event_id",
                                        branch->trigger.operator_event,
                                        sizeof(branch->trigger.operator_event));
        break;
    case ROOM_SCENARIO_REACTIVE_TRIGGER_RUNTIME_EVENT:
        err = json_copy_string_required(trigger,
                                        "event_id",
                                        branch->trigger.runtime_event,
                                        sizeof(branch->trigger.runtime_event));
        break;
    case ROOM_SCENARIO_REACTIVE_TRIGGER_NONE:
    default:
        err = ESP_ERR_INVALID_ARG;
        break;
    }
    if (err != ESP_OK) {
        return err;
    }
    guard_count = cJSON_IsArray(guards) ? cJSON_GetArraySize(guards) : 0;
    if (guard_count < 0 || guard_count > ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS) {
        return ESP_ERR_INVALID_ARG;
    }
    branch->guard_flag_count = (uint8_t)guard_count;
    for (int i = 0; i < guard_count; ++i) {
        const cJSON *guard = cJSON_GetArrayItem(guards, i);
        const cJSON *value = NULL;
        err = json_copy_string_required(guard,
                                        "flag",
                                        branch->guard_flags[i].name,
                                        sizeof(branch->guard_flags[i].name));
        if (err != ESP_OK) {
            return err;
        }
        value = cJSON_GetObjectItemCaseSensitive(guard, "value");
        if (!cJSON_IsBool(value)) {
            return ESP_ERR_INVALID_ARG;
        }
        branch->guard_flags[i].value = cJSON_IsTrue(value);
    }
    variant_count = cJSON_GetArraySize(variants);
    if (variant_count <= 0 ||
        scenario->reactive_variant_count + (size_t)variant_count >
            ROOM_SCENARIO_MAX_REACTIVE_VARIANTS) {
        return ESP_ERR_INVALID_ARG;
    }
    branch->variant_start_index = (uint16_t)scenario->reactive_variant_count;
    branch->variant_count = (uint8_t)variant_count;
    for (int i = 0; i < variant_count; ++i) {
        const cJSON *variant_obj = cJSON_GetArrayItem(variants, i);
        const cJSON *actions = NULL;
        room_scenario_reactive_variant_t *variant =
            &scenario->reactive_variants[scenario->reactive_variant_count];
        if (!cJSON_IsObject(variant_obj)) {
            return ESP_ERR_INVALID_ARG;
        }
        err = json_copy_string_required(variant_obj, "id", variant->id, sizeof(variant->id));
        if (err != ESP_OK) {
            return err;
        }
        err = json_copy_string_optional(variant_obj, "label", variant->label, sizeof(variant->label));
        if (err != ESP_OK) {
            return err;
        }
        actions = cJSON_GetObjectItemCaseSensitive(variant_obj, "actions");
        err = room_scenario_import_reactive_actions_array(actions,
                                                          scenario,
                                                          &variant->action_start_index,
                                                          &variant->action_count);
        if (err != ESP_OK) {
            return err;
        }
        scenario->reactive_variant_count++;
    }
    branch->result_on_done = ROOM_SCENARIO_REACTIVE_RESULT_CONTINUE;
    branch->result_on_fail = ROOM_SCENARIO_REACTIVE_RESULT_FAIL_REACTION;
    branch->result_on_timeout = ROOM_SCENARIO_REACTIVE_RESULT_FAIL_REACTION;
    if (result) {
        const cJSON *on_done = NULL;
        const cJSON *on_fail = NULL;
        const cJSON *on_timeout = NULL;
        if (!cJSON_IsObject(result)) {
            return ESP_ERR_INVALID_ARG;
        }
        on_done = cJSON_GetObjectItemCaseSensitive(result, "on_done");
        on_fail = cJSON_GetObjectItemCaseSensitive(result, "on_fail");
        on_timeout = cJSON_GetObjectItemCaseSensitive(result, "on_timeout");
        if (on_done && (!cJSON_IsString(on_done) || !on_done->valuestring ||
                        reactive_result_action_from_str(on_done->valuestring,
                                                        &branch->result_on_done) != ESP_OK)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (on_fail && (!cJSON_IsString(on_fail) || !on_fail->valuestring ||
                        reactive_result_action_from_str(on_fail->valuestring,
                                                        &branch->result_on_fail) != ESP_OK)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (on_timeout && (!cJSON_IsString(on_timeout) || !on_timeout->valuestring ||
                           reactive_result_action_from_str(on_timeout->valuestring,
                                                           &branch->result_on_timeout) != ESP_OK)) {
            return ESP_ERR_INVALID_ARG;
        }
        err = json_copy_string_optional(result,
                                        "flag",
                                        branch->result_flag,
                                        sizeof(branch->result_flag));
        if (err == ESP_OK && !branch->result_flag[0]) {
            err = json_copy_string_optional(result,
                                            "timeout_flag",
                                            branch->result_flag,
                                            sizeof(branch->result_flag));
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    if (complete) {
        if (cJSON_IsArray(complete) && cJSON_GetArraySize(complete) == 0) {
            branch->on_complete_action_start_index = 0;
            branch->on_complete_action_count = 0;
            return ESP_OK;
        }
        return room_scenario_import_reactive_actions_array(complete,
                                                           scenario,
                                                           &branch->on_complete_action_start_index,
                                                           &branch->on_complete_action_count);
    }
    return ESP_OK;
}
