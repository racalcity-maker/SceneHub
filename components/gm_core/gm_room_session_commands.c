#include "gm_room_session_internal.h"

#include <string.h>

#include "command_executor.h"
#include "quest_common_utils.h"
#include "quest_device.h"

static void dispatch_from_executor(gm_room_session_command_dispatch_t *out,
                                   const command_executor_dispatch_t *in)
{
    if (!out || !in) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->result_required = in->result_required;
    out->timeout_ms = in->timeout_ms;
    quest_str_copy(out->request_id, sizeof(out->request_id), in->request_id);
    quest_str_copy(out->source_id, sizeof(out->source_id), in->source_id);
    quest_str_copy(out->command, sizeof(out->command), in->command);
}

esp_err_t gm_room_session_execute_quest_device_command_internal(
    const room_scenario_device_command_t *step_command,
    char *error,
    size_t error_size,
    gm_room_session_command_dispatch_t *out_dispatch)
{
    command_executor_request_t request = {0};
    command_executor_dispatch_t dispatch = {0};
    esp_err_t err = ESP_OK;
    if (!step_command || !step_command->device_id[0] || !step_command->command_id[0]) {
        if (error && error_size > 0) {
            quest_str_copy(error, error_size, "device_command_invalid");
        }
        return ESP_ERR_INVALID_ARG;
    }
    quest_str_copy(request.source, sizeof(request.source), "scenario");
    quest_str_copy(request.device_id, sizeof(request.device_id), step_command->device_id);
    quest_str_copy(request.command_id, sizeof(request.command_id), step_command->command_id);
    quest_str_copy(request.params_json, sizeof(request.params_json), step_command->params_json);
    request.require_scenario_allowed = true;
    err = command_executor_execute(&request, &dispatch, error, error_size);
    if (err == ESP_OK && out_dispatch) {
        dispatch_from_executor(out_dispatch, &dispatch);
    }
    return err;
}

esp_err_t gm_room_session_execute_device_command(const char *device_id,
                                                 const char *command_id,
                                                 const char *params_json)
{
    return command_executor_execute_device_command(device_id, command_id, params_json);
}

void gm_room_session_stop_audio(void)
{
    (void)command_executor_execute_device_command(QUEST_DEVICE_SYSTEM_AUDIO_ID,
                                                  "stop",
                                                  "{\"channel\":\"all\"}");
}
