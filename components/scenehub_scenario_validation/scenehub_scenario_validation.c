#include "scenehub_scenario_validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hardware_io.h"
#include "quest_device.h"
#include "scenehub_device_command_resolver.h"

static EXT_RAM_BSS_ATTR quest_device_t s_validation_device;
static EXT_RAM_BSS_ATTR quest_device_command_t s_validation_command;
static EXT_RAM_BSS_ATTR quest_device_event_t s_validation_event;
static SemaphoreHandle_t s_validation_scratch_mutex = NULL;
static StaticSemaphore_t s_validation_scratch_mutex_storage;
static portMUX_TYPE s_validation_scratch_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    esp_err_t err;
    char device_id[ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
    char command_id[ROOM_SCENARIO_DEVICE_COMMAND_ID_MAX_LEN];
    char params_json[QUEST_PAYLOAD_MAX_LEN];
    char error[64];
    quest_device_command_t command;
} validation_command_cache_entry_t;

typedef struct {
    bool io_snapshot_loaded;
    hardware_io_io_status_t io_items[HARDWARE_IO_IO_CHANNEL_COUNT];
    size_t io_count;
    validation_command_cache_entry_t command_cache[16];
    size_t command_cache_count;
} validation_context_t;

static EXT_RAM_BSS_ATTR validation_context_t s_validation_context;
static SemaphoreHandle_t s_validation_context_mutex = NULL;
static StaticSemaphore_t s_validation_context_mutex_storage;
static portMUX_TYPE s_validation_context_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

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

static esp_err_t validation_context_lock(validation_context_t **out_ctx)
{
    if (!out_ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_ctx = NULL;
    if (!s_validation_context_mutex) {
        portENTER_CRITICAL(&s_validation_context_mutex_init_lock);
        if (!s_validation_context_mutex) {
            s_validation_context_mutex =
                xSemaphoreCreateMutexStatic(&s_validation_context_mutex_storage);
        }
        portEXIT_CRITICAL(&s_validation_context_mutex_init_lock);
        if (!s_validation_context_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xSemaphoreTake(s_validation_context_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *out_ctx = &s_validation_context;
    return ESP_OK;
}

static void validation_context_unlock(void)
{
    if (s_validation_context_mutex) {
        xSemaphoreGive(s_validation_context_mutex);
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
    issue->branch_id[0] = '\0';
    issue->variant_index = -1;
    issue->action_index = -1;
    snprintf(issue->code, sizeof(issue->code), "%s", code ? code : "UNKNOWN");
    snprintf(issue->message, sizeof(issue->message), "%s", message ? message : "");
}

static const char *validation_skip_ws(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        ++p;
    }
    return p;
}

static int validation_params_channel(const char *params_json)
{
    const char *p = params_json && params_json[0] ? params_json : "{}";
    const char *key = "\"channel\"";
    const char *match = strstr(p, key);

    if (!match) {
        return 0;
    }
    p = validation_skip_ws(match + strlen(key));
    if (!p || *p != ':') {
        return 0;
    }
    p = validation_skip_ws(p + 1);
    if (!p || !*p) {
        return 0;
    }
    if (*p == '"') {
        char value[16] = {0};
        size_t written = 0;
        ++p;
        while (*p && *p != '"' && written + 1 < sizeof(value)) {
            value[written++] = *p++;
        }
        value[written] = '\0';
        return atoi(value);
    }
    return atoi(p);
}

static void validation_context_init(validation_context_t *ctx)
{
    if (ctx) {
        memset(ctx, 0, sizeof(*ctx));
    }
}

static bool validation_context_load_io_snapshot(validation_context_t *ctx)
{
    if (!ctx) {
        return true;
    }
    if (!ctx->io_snapshot_loaded) {
        ctx->io_count = 0;
        if (hardware_io_io_get_status(ctx->io_items,
                                      HARDWARE_IO_IO_CHANNEL_COUNT,
                                      &ctx->io_count) != ESP_OK) {
            return true;
        }
        ctx->io_snapshot_loaded = true;
    }
    return true;
}

static bool validation_io_channel_has_mode(validation_context_t *ctx,
                                           uint8_t channel,
                                           hardware_io_io_mode_t mode)
{
    if (!validation_context_load_io_snapshot(ctx)) {
        return true;
    }
    for (size_t i = 0; i < ctx->io_count; ++i) {
        if (ctx->io_items[i].channel == channel) {
            return ctx->io_items[i].enabled && ctx->io_items[i].mode == mode;
        }
    }
    return false;
}

static esp_err_t validation_lookup_command(validation_context_t *ctx,
                                           const char *device_id,
                                           const char *command_id,
                                           const char *params_json,
                                           char *error,
                                           size_t error_size,
                                           quest_device_command_t *out)
{
    scenehub_resolved_device_command_t resolved = {0};
    if (!ctx || !device_id || !command_id || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < ctx->command_cache_count; ++i) {
        const validation_command_cache_entry_t *entry = &ctx->command_cache[i];
        if (strcmp(entry->device_id, device_id) == 0 &&
            strcmp(entry->command_id, command_id) == 0 &&
            strcmp(entry->params_json, params_json ? params_json : "") == 0) {
            if (entry->err == ESP_OK) {
                *out = entry->command;
            } else if (error && error_size > 0) {
                snprintf(error, error_size, "%s", entry->error);
            }
            return entry->err;
        }
    }

    esp_err_t err = scenehub_device_command_resolve(device_id,
                                                    command_id,
                                                    params_json,
                                                    true,
                                                    &resolved,
                                                    error,
                                                    error_size);
    validation_command_cache_entry_t *slot = &ctx->command_cache[
        ctx->command_cache_count < (sizeof(ctx->command_cache) / sizeof(ctx->command_cache[0]))
            ? ctx->command_cache_count++
            : (ctx->command_cache_count - 1)];
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->device_id, sizeof(slot->device_id), "%s", device_id);
    snprintf(slot->command_id, sizeof(slot->command_id), "%s", command_id);
    snprintf(slot->params_json, sizeof(slot->params_json), "%s", params_json ? params_json : "");
    slot->err = err;
    if (err == ESP_OK) {
        slot->command = resolved.command;
        *out = resolved.command;
    } else if (error && error_size > 0) {
        snprintf(slot->error, sizeof(slot->error), "%s", error);
    }
    return err;
}

static void validation_check_io_command_mode(validation_context_t *ctx,
                                             const room_scenario_device_command_t *command_payload,
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
    if (!validation_io_channel_has_mode(ctx, (uint8_t)channel, HARDWARE_IO_IO_MODE_OUTPUT)) {
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

static void validation_check_io_event_mode(validation_context_t *ctx,
                                           const room_scenario_wait_device_event_t *wait,
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
    if (!validation_io_channel_has_mode(ctx, (uint8_t)channel, HARDWARE_IO_IO_MODE_INPUT)) {
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
    validation_context_t *ctx,
    const room_scenario_device_command_t *command_payload,
    uint16_t step_index,
    const char *step_name,
    room_scenario_validation_report_t *report)
{
    char message[ROOM_SCENARIO_VALIDATION_MESSAGE_MAX_LEN] = {0};
    quest_device_command_t *command = &s_validation_command;
    char lookup_error[64] = {0};
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
    memset(command, 0, sizeof(*command));
    err = validation_lookup_command(ctx,
                                    command_payload->device_id,
                                    command_payload->command_id,
                                    command_payload->params_json,
                                    lookup_error,
                                    sizeof(lookup_error),
                                    command);
    if (err == ESP_ERR_NOT_FOUND &&
        (strcmp(lookup_error, "device_not_found") == 0 || !lookup_error[0])) {
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
    if (err == ESP_ERR_INVALID_STATE && strcmp(lookup_error, "device_disabled") == 0) {
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
        if (lookup_error[0]) {
            snprintf(message,
                     sizeof(message),
                     "%s failed validation: %s",
                     name,
                     lookup_error);
        } else {
            snprintf(message,
                     sizeof(message),
                     "Quest device '%s' is unavailable",
                     command_payload->device_id);
        }
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "DEVICE_COMMAND_INVALID",
                             message);
        return;
    }
    validation_scratch_unlock();
    validation_check_io_command_mode(ctx, command_payload, step_index, name, report);
}

static void validation_check_device_command_group_step_runtime(validation_context_t *ctx,
                                                               const room_scenario_step_t *step,
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
        validation_check_device_command_payload_runtime(ctx, &command, step_index, name, report);
    }
}

static void validation_check_wait_device_event_payload_runtime(
    validation_context_t *ctx,
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
    validation_check_io_event_mode(ctx, wait, step_index, name, report);
}

static void validation_check_wait_any_device_event_step_runtime(validation_context_t *ctx,
                                                                const room_scenario_step_t *step,
                                                                uint16_t step_index,
                                                                room_scenario_validation_report_t *report)
{
    for (uint8_t i = 0; i < step->data.wait_any_device_event.event_count &&
                        i < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS;
         ++i) {
        char name[40] = {0};
        snprintf(name, sizeof(name), "WAIT_ANY_EVENT_%u", (unsigned)(i + 1));
        validation_check_wait_device_event_payload_runtime(ctx,
                                                           &step->data.wait_any_device_event.events[i],
                                                           step_index,
                                                           name,
                                                           report);
    }
}

static void validation_check_wait_all_device_events_step_runtime(validation_context_t *ctx,
                                                                 const room_scenario_step_t *step,
                                                                 uint16_t step_index,
                                                                 room_scenario_validation_report_t *report)
{
    for (uint8_t i = 0; i < step->data.wait_all_device_events.event_count &&
                        i < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS;
         ++i) {
        char name[40] = {0};
        snprintf(name, sizeof(name), "WAIT_ALL_EVENT_%u", (unsigned)(i + 1));
        validation_check_wait_device_event_payload_runtime(ctx,
                                                           &step->data.wait_all_device_events.events[i],
                                                           step_index,
                                                           name,
                                                           report);
    }
}

static void validation_check_reactive_action_environment(validation_context_t *ctx,
                                                         const room_scenario_t *scenario,
                                                         const room_scenario_reactive_action_t *action,
                                                         const room_scenario_branch_t *branch,
                                                         int16_t variant_index,
                                                         int16_t action_index,
                                                         uint16_t branch_step_index,
                                                         room_scenario_validation_report_t *report)
{
    size_t issue_base = report ? report->issue_count : 0;

    if (!scenario || !action || !report) {
        return;
    }
    switch (action->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        validation_check_device_command_payload_runtime(ctx,
                                                        &action->data.device_command,
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
                ctx,
                &scenario->reactive_group_commands[action->group_command_start_index + i],
                branch_step_index,
                "REACTIVE_GROUP_COMMAND",
                report);
        }
        break;
    default:
        break;
    }
    for (size_t i = issue_base; report && i < report->issue_count; ++i) {
        room_scenario_validation_issue_t *issue = &report->issues[i];
        snprintf(issue->branch_id, sizeof(issue->branch_id), "%s", branch ? branch->id : "");
        issue->variant_index = variant_index;
        issue->action_index = action_index;
    }
}

static void validation_check_reactive_branch_environment(validation_context_t *ctx,
                                                         const room_scenario_t *scenario,
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
                ctx,
                scenario,
                &scenario->reactive_actions[variant->action_start_index + action_index],
                branch,
                (int16_t)i,
                (int16_t)action_index,
                branch_step_index,
                report);
        }
    }
}

static esp_err_t scenehub_scenario_validate_environment_append(
    const room_scenario_t *scenario,
    room_scenario_validation_report_t *out)
{
    validation_context_t *ctx = NULL;
    esp_err_t err = ESP_OK;

    err = validation_context_lock(&ctx);
    if (err != ESP_OK) {
        validation_add_issue(out,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             0,
                             "VALIDATION_CONTEXT_UNAVAILABLE",
                             "Validation context is unavailable");
        return err;
    }
    validation_context_init(ctx);
    if (!scenario) {
        validation_add_issue(out,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             0,
                             "SCENARIO_NULL",
                             "Scenario is null");
        validation_context_unlock();
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < scenario->branch_count; ++i) {
        const room_scenario_branch_t *branch = &scenario->branches[i];
        if (branch->type == ROOM_SCENARIO_BRANCH_REACTIVE &&
            (branch->variant_count > 0 || branch->trigger.kind != ROOM_SCENARIO_REACTIVE_TRIGGER_NONE)) {
            validation_check_reactive_branch_environment(ctx, scenario, branch, out);
        }
    }
    for (size_t i = 0; i < scenario->step_count; ++i) {
        const room_scenario_step_t *step = &scenario->steps[i];
        uint16_t step_index = (uint16_t)i;
        switch (step->type) {
        case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
            validation_check_device_command_payload_runtime(ctx,
                                                            &step->data.device_command,
                                                            step_index,
                                                            "DEVICE_COMMAND",
                                                            out);
            break;
        case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
            validation_check_wait_device_event_payload_runtime(ctx,
                                                               &step->data.wait_device_event,
                                                               step_index,
                                                               "WAIT_DEVICE_EVENT",
                                                               out);
            break;
        case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
            validation_check_device_command_group_step_runtime(ctx, step, step_index, out);
            break;
        case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
            validation_check_wait_any_device_event_step_runtime(ctx, step, step_index, out);
            break;
        case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
            validation_check_wait_all_device_events_step_runtime(ctx, step, step_index, out);
            break;
        default:
            break;
        }
    }
    validation_context_unlock();
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
