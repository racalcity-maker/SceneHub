#include "scenehub_control_internal.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "hardware_io.h"
#include "quest_device.h"
#include "room_scenario.h"

static int scenehub_control_hw_io_event_channel(const char *event_id)
{
    int channel = 0;
    if (!event_id || sscanf(event_id, "ch%d_", &channel) != 1) {
        return 0;
    }
    return channel;
}

static int scenehub_control_hw_io_params_channel(const cJSON *obj)
{
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(obj, "params");
    const cJSON *channel = params ? cJSON_GetObjectItemCaseSensitive(params, "channel") : NULL;
    if (!cJSON_IsNumber(channel)) {
        return 0;
    }
    return channel->valueint;
}

static bool scenehub_control_hw_io_command_uses_output(const char *command_id)
{
    return command_id &&
           (strcmp(command_id, "set") == 0 ||
            strcmp(command_id, "pulse") == 0 ||
            strcmp(command_id, "blink") == 0 ||
            strcmp(command_id, "toggle") == 0);
}

static bool scenehub_control_hw_io_mode_blocks_input(hardware_io_io_mode_t requested)
{
    return requested != HARDWARE_IO_IO_MODE_INPUT;
}

static bool scenehub_control_hw_io_mode_blocks_output(hardware_io_io_mode_t requested)
{
    return requested != HARDWARE_IO_IO_MODE_OUTPUT;
}

static bool scenehub_control_hw_io_scan_refs(const cJSON *node,
                                             uint8_t channel,
                                             hardware_io_io_mode_t requested,
                                             const char *scenario_id,
                                             char *message,
                                             size_t message_size)
{
    if (!node) {
        return false;
    }
    if (cJSON_IsObject(node)) {
        const cJSON *device_id = cJSON_GetObjectItemCaseSensitive(node, "device_id");
        if (cJSON_IsString(device_id) &&
            device_id->valuestring &&
            strcmp(device_id->valuestring, QUEST_DEVICE_SYSTEM_IO_ID) == 0) {
            const cJSON *event_id = cJSON_GetObjectItemCaseSensitive(node, "event_id");
            const cJSON *command_id = cJSON_GetObjectItemCaseSensitive(node, "command_id");
            int ref_channel = 0;
            if (cJSON_IsString(event_id) && event_id->valuestring) {
                ref_channel = scenehub_control_hw_io_event_channel(event_id->valuestring);
                if (ref_channel == channel && scenehub_control_hw_io_mode_blocks_input(requested)) {
                    snprintf(message,
                             message_size,
                             "IO %u is used as input in scenario %s",
                             (unsigned)channel,
                             scenario_id && scenario_id[0] ? scenario_id : "?");
                    return true;
                }
            }
            if (cJSON_IsString(command_id) &&
                command_id->valuestring &&
                scenehub_control_hw_io_command_uses_output(command_id->valuestring)) {
                ref_channel = scenehub_control_hw_io_params_channel(node);
                if (ref_channel == channel && scenehub_control_hw_io_mode_blocks_output(requested)) {
                    snprintf(message,
                             message_size,
                             "IO %u is used as output in scenario %s",
                             (unsigned)channel,
                             scenario_id && scenario_id[0] ? scenario_id : "?");
                    return true;
                }
            }
        }
        for (const cJSON *child = node->child; child; child = child->next) {
            if (scenehub_control_hw_io_scan_refs(child, channel, requested, scenario_id, message, message_size)) {
                return true;
            }
        }
        return false;
    }
    if (cJSON_IsArray(node)) {
        const cJSON *child = NULL;
        cJSON_ArrayForEach(child, node) {
            if (scenehub_control_hw_io_scan_refs(child, channel, requested, scenario_id, message, message_size)) {
                return true;
            }
        }
    }
    return false;
}

static esp_err_t scenehub_control_hw_io_check_mode_conflict(uint8_t channel,
                                                            hardware_io_io_mode_t requested,
                                                            char *message,
                                                            size_t message_size)
{
    cJSON *root = NULL;
    const cJSON *scenarios = NULL;
    const cJSON *scenario = NULL;
    esp_err_t err = room_scenario_store_export_json(&root);
    if (err != ESP_OK) {
        return err;
    }
    scenarios = cJSON_GetObjectItemCaseSensitive(root, "room_scenarios");
    cJSON_ArrayForEach(scenario, scenarios) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(scenario, "id");
        const char *scenario_id = cJSON_IsString(id) ? id->valuestring : "?";
        if (scenehub_control_hw_io_scan_refs(scenario,
                                             channel,
                                             requested,
                                             scenario_id,
                                             message,
                                             message_size)) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_STATE;
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t scenehub_control_hardware_io_set_mode(const char *source,
                                                uint8_t channel,
                                                hardware_io_io_mode_t mode,
                                                scenehub_control_result_t *out_result)
{
    char conflict[SCENEHUB_CONTROL_MESSAGE_MAX_LEN] = {0};
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "hardware_io_set_mode", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (channel < 1 || channel > HARDWARE_IO_IO_CHANNEL_COUNT) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }
    if (!hardware_io_is_available()) {
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_FAILED,
                                    ESP_ERR_INVALID_STATE,
                                    false,
                                    "hardware_io_unavailable",
                                    "hardware_io_unavailable");
        return ESP_OK;
    }
    err = scenehub_control_hw_io_check_mode_conflict(channel, mode, conflict, sizeof(conflict));
    if (err == ESP_ERR_INVALID_STATE) {
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "invalid_state",
                                    conflict);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }
    err = hardware_io_io_set_mode(channel, mode);
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }
    scenehub_control_finish_success_with_invalidation(out_result,
                                                      SCENEHUB_STATE_SLICE_SYSTEM_SUMMARY,
                                                      "",
                                                      "hardware_io_set_mode");
    return ESP_OK;
}
