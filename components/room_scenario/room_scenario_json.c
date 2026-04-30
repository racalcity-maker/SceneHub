#include "room_scenario_internal.h"

#include <stdio.h>
#include <string.h>

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
    if (branch->type == ROOM_SCENARIO_BRANCH_REACTIVE) {
        if (branch->cooldown_ms > 0) {
            cJSON_AddNumberToObject(obj, "cooldown_ms", branch->cooldown_ms);
        }
        if (branch->run_once) {
            cJSON_AddBoolToObject(obj, "run_once", true);
        }
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
        run_once = cJSON_GetObjectItemCaseSensitive(branch_obj, "run_once");
        if (run_once) {
            if (!cJSON_IsBool(run_once)) {
                return ESP_ERR_INVALID_ARG;
            }
            branch->run_once = cJSON_IsTrue(run_once);
        }
        branch->step_start_index = (uint16_t)step_index;
        steps = cJSON_GetObjectItemCaseSensitive(branch_obj, "steps");
        err = room_scenario_import_steps_array(steps, scenario, &step_index, &branch->step_count);
        if (err != ESP_OK) {
            return err;
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
