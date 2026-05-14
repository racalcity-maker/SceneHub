#include "scenehub_scenario_validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hardware_io.h"
#include "quest_device.h"

static EXT_RAM_BSS_ATTR quest_device_t s_validation_device;
static EXT_RAM_BSS_ATTR quest_device_command_t s_validation_command;
static EXT_RAM_BSS_ATTR quest_device_event_t s_validation_event;
static SemaphoreHandle_t s_validation_scratch_mutex = NULL;
static StaticSemaphore_t s_validation_scratch_mutex_storage;
static portMUX_TYPE s_validation_scratch_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t validation_scratch_lock(void)
{
    if (!s_validation_scratch_mutex) {
        portENTER_CRITICAL(&s_validation_scratch_mutex_init_lock);
        if (!s_validation_scratch_mutex) {
            s_validation_scratch_mutex =
                xSemaphoreCreateMutexStatic(&s_validation_scratch_mutex_storage);
        }
        portEXIT_CRITICAL(&s_validation_scratch_mutex_init_lock);
        if (!s_validation_scratch_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    return xSemaphoreTake(s_validation_scratch_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK
                                                                               : ESP_ERR_TIMEOUT;
}

static void validation_scratch_unlock(void)
{
    if (s_validation_scratch_mutex) {
        xSemaphoreGive(s_validation_scratch_mutex);
    }
}

static void validation_report_init(room_scenario_validation_report_t *report)
{
    if (!report) {
        return;
    }
    memset(report, 0, sizeof(*report));
    report->valid = true;
}

static void validation_add_issue(room_scenario_validation_report_t *report,
                                 room_scenario_validation_level_t level,
                                 uint16_t step_index,
                                 const char *code,
                                 const char *message)
{
    room_scenario_validation_issue_t *issue = NULL;

    if (!report) {
        return;
    }
    if (level == ROOM_SCENARIO_VALIDATION_ERROR) {
        report->valid = false;
    }
    if (report->issue_count >= ROOM_SCENARIO_VALIDATION_MAX_ISSUES) {
        return;
    }
    issue = &report->issues[report->issue_count++];
    issue->level = level;
    issue->step_index = step_index;
    snprintf(issue->code, sizeof(issue->code), "%s", code ? code : "UNKNOWN");
    snprintf(issue->message, sizeof(issue->message), "%s", message ? message : "");
}

static int validation_params_channel(const char *params_json)
{
    int channel = 0;
    cJSON *params = cJSON_Parse(params_json && params_json[0] ? params_json : "{}");
    const cJSON *item = params ? cJSON_GetObjectItemCaseSensitive(params, "channel") : NULL;
    if (cJSON_IsNumber(item)) {
        channel = item->valueint;
    } else if (cJSON_IsString(item) && item->valuestring) {
        channel = atoi(item->valuestring);
    }
    if (params) {
        cJSON_Delete(params);
    }
    return channel;
}

static bool validation_io_channel_has_mode(uint8_t channel, hardware_io_io_mode_t mode)
{
    static hardware_io_io_status_t items[HARDWARE_IO_IO_CHANNEL_COUNT];
    size_t count = 0;

    if (hardware_io_io_get_status(items, HARDWARE_IO_IO_CHANNEL_COUNT, &count) != ESP_OK) {
        return true;
    }
    for (size_t i = 0; i < count; ++i) {
        if (items[i].channel == channel) {
            return items[i].enabled && items[i].mode == mode;
        }
    }
    return false;
}

static void validation_check_io_command_mode(const room_scenario_device_command_t *command_payload,
                                             uint16_t step_index,
                                             const char *step_name,
                                             room_scenario_validation_report_t *report)
{
    char message[ROOM_SCENARIO_VALIDATION_MESSAGE_MAX_LEN] = {0};
    const char *name = step_name && step_name[0] ? step_name : "DEVICE_COMMAND";

    if (!command_payload || strcmp(command_payload->device_id, QUEST_DEVICE_SYSTEM_IO_ID) != 0) {
        return;
    }
    int channel = validation_params_channel(command_payload->params_json);
    if (channel < 1 || channel > HARDWARE_IO_IO_CHANNEL_COUNT) {
        snprintf(message, sizeof(message), "%s uses invalid IO channel %d", name, channel);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "IO_CHANNEL_INVALID",
                             message);
        return;
    }
    if (!validation_io_channel_has_mode((uint8_t)channel, HARDWARE_IO_IO_MODE_OUTPUT)) {
        snprintf(message,
                 sizeof(message),
                 "%s uses IO %d as output, but this channel is disabled or configured as input",
                 name,
                 channel);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "IO_CHANNEL_NOT_OUTPUT",
                             message);
    }
}

static void validation_check_io_event_mode(const room_scenario_wait_device_event_t *wait,
                                           uint16_t step_index,
                                           const char *step_name,
                                           room_scenario_validation_report_t *report)
{
    char message[ROOM_SCENARIO_VALIDATION_MESSAGE_MAX_LEN] = {0};
    const char *name = step_name && step_name[0] ? step_name : "WAIT_DEVICE_EVENT";
    int channel = 0;

    if (!wait || strcmp(wait->device_id, QUEST_DEVICE_SYSTEM_IO_ID) != 0) {
        return;
    }
    if (sscanf(wait->event_id, "ch%d_", &channel) != 1 ||
        channel < 1 ||
        channel > HARDWARE_IO_IO_CHANNEL_COUNT) {
        snprintf(message, sizeof(message), "%s uses invalid IO event '%s'", name, wait->event_id);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "IO_EVENT_INVALID",
                             message);
        return;
    }
    if (!validation_io_channel_has_mode((uint8_t)channel, HARDWARE_IO_IO_MODE_INPUT)) {
        snprintf(message,
                 sizeof(message),
                 "%s waits for IO %d input, but this channel is disabled or configured as output",
                 name,
                 channel);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "IO_CHANNEL_NOT_INPUT",
                             message);
    }
}

static void validation_check_device_command_payload_runtime(
    const room_scenario_device_command_t *command_payload,
    uint16_t step_index,
    const char *step_name,
    room_scenario_validation_report_t *report)
{
    char message[ROOM_SCENARIO_VALIDATION_MESSAGE_MAX_LEN] = {0};
    quest_device_t *device = &s_validation_device;
    quest_device_command_t *command = &s_validation_command;
    const char *name = step_name && step_name[0] ? step_name : "DEVICE_COMMAND";
    esp_err_t err = ESP_OK;

    if (!command_payload || !command_payload->device_id[0] || !command_payload->command_id[0]) {
        return;
    }
    err = validation_scratch_lock();
    if (err != ESP_OK) {
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "VALIDATION_SCRATCH_UNAVAILABLE",
                             "Validation scratch is unavailable");
        return;
    }
    memset(device, 0, sizeof(*device));
    memset(command, 0, sizeof(*command));
    err = quest_device_get(command_payload->device_id, device);
    if (err == ESP_ERR_NOT_FOUND) {
        validation_scratch_unlock();
        snprintf(message,
                 sizeof(message),
                 "Quest device '%s' not found",
                 command_payload->device_id);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "QUEST_DEVICE_NOT_FOUND",
                             message);
        return;
    }
    if (err != ESP_OK) {
        validation_scratch_unlock();
        snprintf(message,
                 sizeof(message),
                 "Quest device '%s' is unavailable",
                 command_payload->device_id);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "QUEST_DEVICE_UNAVAILABLE",
                             message);
        return;
    }
    if (!device->enabled) {
        validation_scratch_unlock();
        snprintf(message,
                 sizeof(message),
                 "Quest device '%s' is disabled",
                 command_payload->device_id);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "QUEST_DEVICE_DISABLED",
                             message);
        return;
    }
    err = quest_device_get_command(command_payload->device_id,
                                   command_payload->command_id,
                                   command);
    if (err == ESP_ERR_NOT_FOUND) {
        validation_scratch_unlock();
        snprintf(message,
                 sizeof(message),
                 "Command '%s' not found on quest device '%s'",
                 command_payload->command_id,
                 command_payload->device_id);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "DEVICE_COMMAND_NOT_FOUND",
                             message);
        return;
    }
    if (err != ESP_OK) {
        validation_scratch_unlock();
        snprintf(message,
                 sizeof(message),
                 "Quest device '%s' is unavailable",
                 command_payload->device_id);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "QUEST_DEVICE_UNAVAILABLE",
                             message);
        return;
    }
    validation_scratch_unlock();
    validation_check_io_command_mode(command_payload, step_index, name, report);
}

static void validation_check_device_command_group_step_runtime(const room_scenario_step_t *step,
                                                               uint16_t step_index,
                                                               room_scenario_validation_report_t *report)
{
    for (uint8_t i = 0; i < step->data.device_command_group.command_count &&
                        i < ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS;
         ++i) {
        char name[32] = {0};
        room_scenario_device_command_t command = {0};
        snprintf(name, sizeof(name), "GROUP_COMMAND_%u", (unsigned)(i + 1));
        snprintf(command.device_id,
                 sizeof(command.device_id),
                 "%s",
                 step->data.device_command_group.commands[i].device_id);
        snprintf(command.command_id,
                 sizeof(command.command_id),
                 "%s",
                 step->data.device_command_group.commands[i].command_id);
        snprintf(command.params_json,
                 sizeof(command.params_json),
                 "%s",
                 step->data.device_command_group.commands[i].params_json);
        validation_check_device_command_payload_runtime(&command, step_index, name, report);
    }
}

static void validation_check_wait_device_event_payload_runtime(
    const room_scenario_wait_device_event_t *wait,
    uint16_t step_index,
    const char *step_name,
    room_scenario_validation_report_t *report)
{
    char message[ROOM_SCENARIO_VALIDATION_MESSAGE_MAX_LEN] = {0};
    quest_device_t *device = &s_validation_device;
    quest_device_event_t *event = &s_validation_event;
    const char *name = step_name && step_name[0] ? step_name : "WAIT_DEVICE_EVENT";
    esp_err_t err = ESP_OK;

    if (!wait || !wait->device_id[0] || !wait->event_id[0]) {
        return;
    }
    err = validation_scratch_lock();
    if (err != ESP_OK) {
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "VALIDATION_SCRATCH_UNAVAILABLE",
                             "Validation scratch is unavailable");
        return;
    }
    memset(device, 0, sizeof(*device));
    memset(event, 0, sizeof(*event));
    err = quest_device_get(wait->device_id, device);
    if (err == ESP_ERR_NOT_FOUND) {
        validation_scratch_unlock();
        snprintf(message,
                 sizeof(message),
                 "Quest device '%s' not found",
                 wait->device_id);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "QUEST_DEVICE_NOT_FOUND",
                             message);
        return;
    }
    if (err != ESP_OK) {
        validation_scratch_unlock();
        snprintf(message,
                 sizeof(message),
                 "Quest device '%s' is unavailable",
                 wait->device_id);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "QUEST_DEVICE_UNAVAILABLE",
                             message);
        return;
    }
    if (!device->enabled) {
        validation_scratch_unlock();
        snprintf(message,
                 sizeof(message),
                 "Quest device '%s' is disabled",
                 wait->device_id);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "QUEST_DEVICE_DISABLED",
                             message);
        return;
    }
    err = quest_device_get_event(wait->device_id, wait->event_id, event);
    if (err == ESP_ERR_NOT_FOUND) {
        validation_scratch_unlock();
        snprintf(message,
                 sizeof(message),
                 "Event '%s' not found on quest device '%s'",
                 wait->event_id,
                 wait->device_id);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "DEVICE_EVENT_NOT_FOUND",
                             message);
        return;
    }
    if (err != ESP_OK) {
        validation_scratch_unlock();
        snprintf(message,
                 sizeof(message),
                 "Quest device '%s' is unavailable",
                 wait->device_id);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "QUEST_DEVICE_UNAVAILABLE",
                             message);
        return;
    }
    validation_scratch_unlock();
    validation_check_io_event_mode(wait, step_index, name, report);
}

static void validation_check_wait_any_device_event_step_runtime(const room_scenario_step_t *step,
                                                                uint16_t step_index,
                                                                room_scenario_validation_report_t *report)
{
    for (uint8_t i = 0; i < step->data.wait_any_device_event.event_count &&
                        i < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS;
         ++i) {
        char name[40] = {0};
        snprintf(name, sizeof(name), "WAIT_ANY_EVENT_%u", (unsigned)(i + 1));
        validation_check_wait_device_event_payload_runtime(&step->data.wait_any_device_event.events[i],
                                                           step_index,
                                                           name,
                                                           report);
    }
}

static void validation_check_wait_all_device_events_step_runtime(const room_scenario_step_t *step,
                                                                 uint16_t step_index,
                                                                 room_scenario_validation_report_t *report)
{
    for (uint8_t i = 0; i < step->data.wait_all_device_events.event_count &&
                        i < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS;
         ++i) {
        char name[40] = {0};
        snprintf(name, sizeof(name), "WAIT_ALL_EVENT_%u", (unsigned)(i + 1));
        validation_check_wait_device_event_payload_runtime(&step->data.wait_all_device_events.events[i],
                                                           step_index,
                                                           name,
                                                           report);
    }
}

static void validation_check_reactive_action_environment(const room_scenario_t *scenario,
                                                         const room_scenario_reactive_action_t *action,
                                                         uint16_t branch_step_index,
                                                         room_scenario_validation_report_t *report)
{
    if (!scenario || !action || !report) {
        return;
    }
    switch (action->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        validation_check_device_command_payload_runtime(&action->data.device_command,
                                                        branch_step_index,
                                                        "REACTIVE_DEVICE_COMMAND",
                                                        report);
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        for (uint8_t i = 0; i < action->group_command_count &&
                            (size_t)action->group_command_start_index + i <
                                scenario->reactive_group_command_count;
             ++i) {
            validation_check_device_command_payload_runtime(
                &scenario->reactive_group_commands[action->group_command_start_index + i],
                branch_step_index,
                "REACTIVE_GROUP_COMMAND",
                report);
        }
        break;
    default:
        break;
    }
}

static void validation_check_reactive_branch_environment(const room_scenario_t *scenario,
                                                         const room_scenario_branch_t *branch,
                                                         room_scenario_validation_report_t *report)
{
    uint16_t branch_step_index = branch ? branch->step_start_index : 0;

    if (!scenario || !branch || !report) {
        return;
    }
    for (uint8_t i = 0;
         i < branch->variant_count &&
         (size_t)branch->variant_start_index + i < scenario->reactive_variant_count;
         ++i) {
        const room_scenario_reactive_variant_t *variant =
            &scenario->reactive_variants[branch->variant_start_index + i];
        for (uint8_t action_index = 0;
             action_index < variant->action_count &&
             (size_t)variant->action_start_index + action_index < scenario->reactive_action_count;
             ++action_index) {
            validation_check_reactive_action_environment(
                scenario,
                &scenario->reactive_actions[variant->action_start_index + action_index],
                branch_step_index,
                report);
        }
    }
}

static esp_err_t scenehub_scenario_validate_environment_append(
    const room_scenario_t *scenario,
    room_scenario_validation_report_t *out)
{
    if (!scenario) {
        validation_add_issue(out,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             0,
                             "SCENARIO_NULL",
                             "Scenario is null");
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < scenario->branch_count; ++i) {
        const room_scenario_branch_t *branch = &scenario->branches[i];
        if (branch->type == ROOM_SCENARIO_BRANCH_REACTIVE &&
            (branch->variant_count > 0 || branch->trigger.kind != ROOM_SCENARIO_REACTIVE_TRIGGER_NONE)) {
            validation_check_reactive_branch_environment(scenario, branch, out);
        }
    }
    for (size_t i = 0; i < scenario->step_count; ++i) {
        const room_scenario_step_t *step = &scenario->steps[i];
        uint16_t step_index = (uint16_t)i;
        switch (step->type) {
        case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
            validation_check_device_command_payload_runtime(&step->data.device_command,
                                                            step_index,
                                                            "DEVICE_COMMAND",
                                                            out);
            break;
        case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
            validation_check_wait_device_event_payload_runtime(&step->data.wait_device_event,
                                                               step_index,
                                                               "WAIT_DEVICE_EVENT",
                                                               out);
            break;
        case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
            validation_check_device_command_group_step_runtime(step, step_index, out);
            break;
        case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
            validation_check_wait_any_device_event_step_runtime(step, step_index, out);
            break;
        case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
            validation_check_wait_all_device_events_step_runtime(step, step_index, out);
            break;
        default:
            break;
        }
    }
    return ESP_OK;
}

esp_err_t scenehub_scenario_validate_environment(const room_scenario_t *scenario,
                                                 room_scenario_validation_report_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    validation_report_init(out);
    return scenehub_scenario_validate_environment_append(scenario, out);
}

esp_err_t scenehub_scenario_validate(const room_scenario_t *scenario,
                                     room_scenario_validation_report_t *out)
{
    esp_err_t err = ESP_OK;

    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_scenario_validate(scenario, out);
    if (err != ESP_OK || !out->valid) {
        return err;
    }
    return scenehub_scenario_validate_environment_append(scenario, out);
}
