#include "room_scenario_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool room_scenario_valid_device_command(const room_scenario_device_command_t *command)
{
    return command && command->device_id[0] && command->command_id[0];
}

static bool room_scenario_valid_command_ref(const char *device_id, const char *command_id)
{
    return device_id && device_id[0] && command_id && command_id[0];
}

static bool room_scenario_valid_step(const room_scenario_step_t *step)
{
    if (!step || !step->id[0] || !room_scenario_valid_step_type(step->type)) {
        return false;
    }
    switch (step->type) {
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        return step->data.wait_time.duration_ms > 0;
    case ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        return step->data.operator_approval.prompt[0];
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        return room_scenario_valid_device_command(&step->data.device_command);
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        return step->data.wait_device_event.device_id[0] &&
               step->data.wait_device_event.event_id[0];
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        if (step->data.device_command_group.command_count == 0 ||
            step->data.device_command_group.command_count > ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS) {
            return false;
        }
        for (uint8_t i = 0; i < step->data.device_command_group.command_count; ++i) {
            if (!room_scenario_valid_command_ref(step->data.device_command_group.commands[i].device_id,
                                                 step->data.device_command_group.commands[i].command_id)) {
                return false;
            }
        }
        return true;
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        return step->data.operator_message.message[0];
    case ROOM_SCENARIO_STEP_SET_FLAG:
        return step->data.set_flag.name[0];
    case ROOM_SCENARIO_STEP_WAIT_FLAGS:
        if (step->data.wait_flags.flag_count == 0 ||
            step->data.wait_flags.flag_count > ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS) {
            return false;
        }
        for (uint8_t i = 0; i < step->data.wait_flags.flag_count; ++i) {
            if (!step->data.wait_flags.flags[i].name[0]) {
                return false;
            }
        }
        return true;
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        if (step->data.wait_any_device_event.event_count == 0 ||
            step->data.wait_any_device_event.event_count > ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS) {
            return false;
        }
        for (uint8_t i = 0; i < step->data.wait_any_device_event.event_count; ++i) {
            if (!step->data.wait_any_device_event.events[i].device_id[0] ||
                !step->data.wait_any_device_event.events[i].event_id[0]) {
                return false;
            }
        }
        return true;
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        if (step->data.wait_all_device_events.event_count == 0 ||
            step->data.wait_all_device_events.event_count > ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS) {
            return false;
        }
        for (uint8_t i = 0; i < step->data.wait_all_device_events.event_count; ++i) {
            if (!step->data.wait_all_device_events.events[i].device_id[0] ||
                !step->data.wait_all_device_events.events[i].event_id[0]) {
                return false;
            }
        }
        return true;
    case ROOM_SCENARIO_STEP_END_GAME:
        return true;
    default:
        return false;
    }
}

static bool room_scenario_valid_reactive_trigger(const room_scenario_reactive_trigger_t *trigger)
{
    if (!trigger) {
        return false;
    }
    switch (trigger->kind) {
    case ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT:
        return trigger->device_id[0] && trigger->event_id[0];
    case ROOM_SCENARIO_REACTIVE_TRIGGER_FLAG_CHANGED:
        return trigger->flag_name[0];
    case ROOM_SCENARIO_REACTIVE_TRIGGER_OPERATOR_EVENT:
        return trigger->operator_event[0];
    case ROOM_SCENARIO_REACTIVE_TRIGGER_RUNTIME_EVENT:
        return trigger->runtime_event[0];
    case ROOM_SCENARIO_REACTIVE_TRIGGER_NONE:
    default:
        return false;
    }
}

static bool room_scenario_valid_reactive_action(const room_scenario_t *scenario,
                                                const room_scenario_reactive_action_t *action)
{
    if (!scenario || !action) {
        return false;
    }
    switch (action->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        return room_scenario_valid_device_command(&action->data.device_command);
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        if (action->group_command_count == 0 ||
            (size_t)action->group_command_start_index + action->group_command_count >
                scenario->reactive_group_command_count) {
            return false;
        }
        for (uint8_t i = 0; i < action->group_command_count; ++i) {
            const room_scenario_device_command_t *command =
                &scenario->reactive_group_commands[action->group_command_start_index + i];
            if (!room_scenario_valid_device_command(command)) {
                return false;
            }
        }
        return true;
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        return action->data.wait_time.duration_ms > 0;
    case ROOM_SCENARIO_STEP_SET_FLAG:
        return action->data.set_flag.name[0];
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        return action->data.operator_message.message[0];
    default:
        return false;
    }
}

static bool room_scenario_valid_reactive_branch_v2(const room_scenario_t *scenario,
                                                   const room_scenario_branch_t *branch)
{
    if (!scenario || !branch || !room_scenario_valid_reactive_trigger(&branch->trigger) ||
        branch->variant_count == 0 ||
        (size_t)branch->variant_start_index + branch->variant_count >
            scenario->reactive_variant_count) {
        return false;
    }
    for (uint8_t i = 0; i < branch->guard_flag_count; ++i) {
        if (!branch->guard_flags[i].name[0]) {
            return false;
        }
    }
    for (uint8_t i = 0; i < branch->variant_count; ++i) {
        const room_scenario_reactive_variant_t *variant =
            &scenario->reactive_variants[branch->variant_start_index + i];
        if (!variant->id[0] || variant->action_count == 0 ||
            (size_t)variant->action_start_index + variant->action_count >
                scenario->reactive_action_count) {
            return false;
        }
        for (uint8_t action_index = 0; action_index < variant->action_count; ++action_index) {
            if (!room_scenario_valid_reactive_action(
                    scenario,
                    &scenario->reactive_actions[variant->action_start_index + action_index])) {
                return false;
            }
        }
    }
    if ((size_t)branch->on_complete_action_start_index + branch->on_complete_action_count >
        scenario->reactive_action_count) {
        return false;
    }
    for (uint8_t i = 0; i < branch->on_complete_action_count; ++i) {
        if (!room_scenario_valid_reactive_action(
                scenario,
                &scenario->reactive_actions[branch->on_complete_action_start_index + i])) {
            return false;
        }
    }
    return true;
}

static bool room_scenario_step_is_reactive_trigger(room_scenario_step_type_t type)
{
    switch (type) {
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
    case ROOM_SCENARIO_STEP_WAIT_FLAGS:
        return true;
    default:
        return false;
    }
}

static bool room_scenario_branch_first_step(const room_scenario_t *scenario,
                                            const room_scenario_branch_t *branch,
                                            const room_scenario_step_t **out_step)
{
    uint32_t end_index = 0;
    if (out_step) {
        *out_step = NULL;
    }
    if (!scenario || !branch || !out_step || branch->step_count == 0) {
        return false;
    }
    end_index = (uint32_t)branch->step_start_index + branch->step_count;
    if (end_index > scenario->step_count) {
        return false;
    }
    *out_step = &scenario->steps[branch->step_start_index];
    return true;
}

static bool room_scenario_has_duplicate_branch_ids(const room_scenario_t *scenario)
{
    if (!scenario) {
        return true;
    }
    for (size_t i = 0; i < scenario->branch_count; ++i) {
        for (size_t j = i + 1; j < scenario->branch_count; ++j) {
            if (strcmp(scenario->branches[i].id, scenario->branches[j].id) == 0) {
                return true;
            }
        }
    }
    return false;
}

static bool room_scenario_steps_share_branch(const room_scenario_t *scenario, size_t a, size_t b)
{
    if (!scenario || scenario->branch_count == 0) {
        return true;
    }
    for (size_t i = 0; i < scenario->branch_count; ++i) {
        const room_scenario_branch_t *branch = &scenario->branches[i];
        size_t start = branch->step_start_index;
        size_t end = start + branch->step_count;
        if (a >= start && a < end) {
            return b >= start && b < end;
        }
    }
    return true;
}

static bool room_scenario_has_duplicate_step_ids(const room_scenario_t *scenario)
{
    if (!scenario) {
        return true;
    }
    for (size_t i = 0; i < scenario->step_count; ++i) {
        for (size_t j = i + 1; j < scenario->step_count; ++j) {
            if (room_scenario_steps_share_branch(scenario, i, j) &&
                strcmp(scenario->steps[i].id, scenario->steps[j].id) == 0) {
                return true;
            }
        }
    }
    return false;
}

esp_err_t room_scenario_validate_structural(const room_scenario_t *scenario)
{
    if (!scenario || !scenario->id[0] || !scenario->name[0] || !scenario->room_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scenario->step_count > ROOM_SCENARIO_MAX_STEPS) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (scenario->reactive_variant_count > ROOM_SCENARIO_MAX_REACTIVE_VARIANTS ||
        scenario->reactive_action_count > ROOM_SCENARIO_MAX_REACTIVE_ACTIONS ||
        scenario->reactive_group_command_count > ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (scenario->branch_count > ROOM_SCENARIO_MAX_BRANCHES) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (room_scenario_has_duplicate_step_ids(scenario)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (room_scenario_has_duplicate_branch_ids(scenario)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < scenario->branch_count; ++i) {
        const room_scenario_branch_t *branch = &scenario->branches[i];
        uint32_t end_index = (uint32_t)branch->step_start_index + branch->step_count;
        if (!branch->id[0] || !branch->name[0]) {
            return ESP_ERR_INVALID_ARG;
        }
        if (branch->type != ROOM_SCENARIO_BRANCH_NORMAL &&
            branch->type != ROOM_SCENARIO_BRANCH_REACTIVE) {
            return ESP_ERR_INVALID_ARG;
        }
        if (branch->reentry_mode != ROOM_SCENARIO_REENTRY_IGNORE &&
            branch->reentry_mode != ROOM_SCENARIO_REENTRY_QUEUE_ONE &&
            branch->reentry_mode != ROOM_SCENARIO_REENTRY_RESTART &&
            branch->reentry_mode != ROOM_SCENARIO_REENTRY_PARALLEL) {
            return ESP_ERR_INVALID_ARG;
        }
        if (end_index > scenario->step_count) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (branch->type == ROOM_SCENARIO_BRANCH_REACTIVE) {
            const room_scenario_step_t *first_step = NULL;
            if (branch->variant_count > 0 || branch->trigger.kind != ROOM_SCENARIO_REACTIVE_TRIGGER_NONE) {
                if (!room_scenario_valid_reactive_branch_v2(scenario, branch)) {
                    return ESP_ERR_INVALID_ARG;
                }
            } else {
                if (!room_scenario_branch_first_step(scenario, branch, &first_step) ||
                    !room_scenario_step_is_reactive_trigger(first_step->type)) {
                    return ESP_ERR_INVALID_ARG;
                }
                if (first_step->type == ROOM_SCENARIO_STEP_WAIT_FLAGS &&
                    !branch->run_once &&
                    branch->max_fire_count == 0 &&
                    branch->cooldown_ms == 0) {
                    return ESP_ERR_INVALID_ARG;
                }
            }
        }
    }
    for (size_t i = 0; i < scenario->step_count; ++i) {
        if (!room_scenario_valid_step(&scenario->steps[i])) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
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

static void validation_add_reactive_issue(room_scenario_validation_report_t *report,
                                          room_scenario_validation_level_t level,
                                          const room_scenario_branch_t *branch,
                                          int16_t variant_index,
                                          int16_t action_index,
                                          uint16_t step_index,
                                          const char *code,
                                          const char *message)
{
    validation_add_issue(report, level, step_index, code, message);
    if (!report || report->issue_count == 0) {
        return;
    }
    room_scenario_validation_issue_t *issue = &report->issues[report->issue_count - 1];
    if (branch) {
        snprintf(issue->branch_id, sizeof(issue->branch_id), "%s", branch->id);
    }
    issue->variant_index = variant_index;
    issue->action_index = action_index;
}

static void validation_check_device_command_payload_static(const room_scenario_device_command_t *command_payload,
                                                           uint16_t step_index,
                                                           const char *step_name,
                                                           room_scenario_validation_report_t *report)
{
    char message[ROOM_SCENARIO_VALIDATION_MESSAGE_MAX_LEN] = {0};
    const char *name = step_name && step_name[0] ? step_name : "DEVICE_COMMAND";
    if (!command_payload || !command_payload->device_id[0]) {
        snprintf(message, sizeof(message), "%s has empty device_id", name);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "DEVICE_ID_EMPTY",
                             message);
        return;
    }
    if (!command_payload->command_id[0]) {
        snprintf(message, sizeof(message), "%s has empty command_id", name);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "DEVICE_COMMAND_ID_EMPTY",
                             message);
    }
}

static void validation_check_device_command_payload_runtime(const room_scenario_device_command_t *command_payload,
                                                            uint16_t step_index,
                                                            const char *step_name,
                                                            room_scenario_validation_report_t *report)
{
    (void)command_payload;
    (void)step_index;
    (void)step_name;
    (void)report;
}

static void validation_check_device_command_step_static(const room_scenario_step_t *step,
                                                        uint16_t step_index,
                                                        room_scenario_validation_report_t *report)
{
    validation_check_device_command_payload_static(&step->data.device_command,
                                                   step_index,
                                                   "DEVICE_COMMAND",
                                                   report);
}

static void validation_check_device_command_step_runtime(const room_scenario_step_t *step,
                                                         uint16_t step_index,
                                                         room_scenario_validation_report_t *report)
{
    validation_check_device_command_payload_runtime(&step->data.device_command,
                                                    step_index,
                                                    "DEVICE_COMMAND",
                                                    report);
}

static void validation_check_device_command_group_step_static(const room_scenario_step_t *step,
                                                              uint16_t step_index,
                                                              room_scenario_validation_report_t *report)
{
    if (step->data.device_command_group.command_count == 0) {
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "DEVICE_COMMAND_GROUP_EMPTY",
                             "DEVICE_COMMAND_GROUP has no commands");
        return;
    }
    if (step->data.device_command_group.command_count > ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS) {
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "DEVICE_COMMAND_GROUP_LIMIT",
                             "DEVICE_COMMAND_GROUP has too many commands");
        return;
    }
    for (uint8_t i = 0; i < step->data.device_command_group.command_count; ++i) {
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
        validation_check_device_command_payload_static(&command, step_index, name, report);
    }
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

static void validation_check_wait_device_event_payload_static(const room_scenario_wait_device_event_t *wait,
                                                              uint16_t step_index,
                                                              const char *step_name,
                                                              room_scenario_validation_report_t *report)
{
    char message[ROOM_SCENARIO_VALIDATION_MESSAGE_MAX_LEN] = {0};
    const char *name = step_name && step_name[0] ? step_name : "WAIT_DEVICE_EVENT";
    if (!wait || !wait->device_id[0]) {
        snprintf(message, sizeof(message), "%s has empty device_id", name);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "DEVICE_ID_EMPTY",
                             message);
        return;
    }
    if (!wait->event_id[0]) {
        snprintf(message, sizeof(message), "%s has empty event_id", name);
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "DEVICE_EVENT_ID_EMPTY",
                             message);
    }
}

static void validation_check_wait_device_event_payload_runtime(const room_scenario_wait_device_event_t *wait,
                                                               uint16_t step_index,
                                                               const char *step_name,
                                                               room_scenario_validation_report_t *report)
{
    (void)wait;
    (void)step_index;
    (void)step_name;
    (void)report;
}

static void validation_check_wait_device_event_step_static(const room_scenario_step_t *step,
                                                           uint16_t step_index,
                                                           room_scenario_validation_report_t *report)
{
    validation_check_wait_device_event_payload_static(&step->data.wait_device_event,
                                                      step_index,
                                                      "WAIT_DEVICE_EVENT",
                                                      report);
}

static void validation_check_wait_device_event_step_runtime(const room_scenario_step_t *step,
                                                            uint16_t step_index,
                                                            room_scenario_validation_report_t *report)
{
    validation_check_wait_device_event_payload_runtime(&step->data.wait_device_event,
                                                       step_index,
                                                       "WAIT_DEVICE_EVENT",
                                                       report);
}

static void validation_check_wait_any_device_event_step_static(const room_scenario_step_t *step,
                                                               uint16_t step_index,
                                                               room_scenario_validation_report_t *report)
{
    if (step->data.wait_any_device_event.event_count == 0) {
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "WAIT_ANY_DEVICE_EVENT_EMPTY",
                             "WAIT_ANY_DEVICE_EVENT has no events");
        return;
    }
    if (step->data.wait_any_device_event.event_count > ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS) {
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "WAIT_ANY_DEVICE_EVENT_LIMIT",
                             "WAIT_ANY_DEVICE_EVENT has too many events");
        return;
    }
    for (uint8_t i = 0; i < step->data.wait_any_device_event.event_count; ++i) {
        char name[40] = {0};
        snprintf(name, sizeof(name), "WAIT_ANY_EVENT_%u", (unsigned)(i + 1));
        validation_check_wait_device_event_payload_static(&step->data.wait_any_device_event.events[i],
                                                          step_index,
                                                          name,
                                                          report);
    }
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

static void validation_check_wait_all_device_events_step_static(const room_scenario_step_t *step,
                                                                uint16_t step_index,
                                                                room_scenario_validation_report_t *report)
{
    if (step->data.wait_all_device_events.event_count == 0) {
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "WAIT_ALL_DEVICE_EVENTS_EMPTY",
                             "WAIT_ALL_DEVICE_EVENTS has no events");
        return;
    }
    if (step->data.wait_all_device_events.event_count > ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS) {
        validation_add_issue(report,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             step_index,
                             "WAIT_ALL_DEVICE_EVENTS_LIMIT",
                             "WAIT_ALL_DEVICE_EVENTS has too many events");
        return;
    }
    for (uint8_t i = 0; i < step->data.wait_all_device_events.event_count; ++i) {
        char name[40] = {0};
        snprintf(name, sizeof(name), "WAIT_ALL_EVENT_%u", (unsigned)(i + 1));
        validation_check_wait_device_event_payload_static(&step->data.wait_all_device_events.events[i],
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

static void validation_check_reactive_action_v2_static(const room_scenario_t *scenario,
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
        validation_check_device_command_payload_static(&action->data.device_command,
                                                       branch_step_index,
                                                       "REACTIVE_DEVICE_COMMAND",
                                                       report);
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        if (action->group_command_count == 0 ||
            (size_t)action->group_command_start_index + action->group_command_count >
                scenario->reactive_group_command_count) {
            validation_add_reactive_issue(report,
                                          ROOM_SCENARIO_VALIDATION_ERROR,
                                          branch,
                                          variant_index,
                                          action_index,
                                          branch_step_index,
                                          "REACTIVE_GROUP_COMMAND_RANGE_INVALID",
                                          "Reactive DEVICE_COMMAND_GROUP command range is invalid");
            return;
        }
        for (uint8_t i = 0; i < action->group_command_count; ++i) {
            validation_check_device_command_payload_static(
                &scenario->reactive_group_commands[action->group_command_start_index + i],
                branch_step_index,
                "REACTIVE_GROUP_COMMAND",
                report);
        }
        break;
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        if (action->data.wait_time.duration_ms == 0) {
            validation_add_reactive_issue(report,
                                          ROOM_SCENARIO_VALIDATION_ERROR,
                                          branch,
                                          variant_index,
                                          action_index,
                                          branch_step_index,
                                          "REACTIVE_WAIT_TIME_ZERO",
                                          "Reactive WAIT_TIME duration_ms must be greater than zero");
        }
        break;
    case ROOM_SCENARIO_STEP_SET_FLAG:
        if (!action->data.set_flag.name[0]) {
            validation_add_reactive_issue(report,
                                          ROOM_SCENARIO_VALIDATION_ERROR,
                                          branch,
                                          variant_index,
                                          action_index,
                                          branch_step_index,
                                          "REACTIVE_FLAG_NAME_EMPTY",
                                          "Reactive SET_FLAG has empty flag name");
        }
        break;
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        if (!action->data.operator_message.message[0]) {
            validation_add_reactive_issue(report,
                                          ROOM_SCENARIO_VALIDATION_ERROR,
                                          branch,
                                          variant_index,
                                          action_index,
                                          branch_step_index,
                                          "REACTIVE_OPERATOR_MESSAGE_EMPTY",
                                          "Reactive SHOW_OPERATOR_MESSAGE message is empty");
        }
        break;
    default:
        validation_add_reactive_issue(report,
                                      ROOM_SCENARIO_VALIDATION_ERROR,
                                      branch,
                                      variant_index,
                                      action_index,
                                      branch_step_index,
                                      "REACTIVE_ACTION_TYPE_UNSUPPORTED",
                                      "Reactive action type is not allowed in v2");
        break;
    }
    for (size_t i = issue_base; report && i < report->issue_count; ++i) {
        room_scenario_validation_issue_t *issue = &report->issues[i];
        if (issue->branch_id[0]) {
            continue;
        }
        snprintf(issue->branch_id, sizeof(issue->branch_id), "%s", branch ? branch->id : "");
        issue->variant_index = variant_index;
        issue->action_index = action_index;
    }
}

static void validation_check_reactive_action_v2_runtime(const room_scenario_t *scenario,
                                                        const room_scenario_reactive_action_t *action,
                                                        const room_scenario_branch_t *branch,
                                                        int16_t variant_index,
                                                        int16_t action_index,
                                                        uint16_t branch_step_index,
                                                        room_scenario_validation_report_t *report)
{
    if (!scenario || !action || !report) {
        return;
    }
    switch (action->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        if (action->group_mode == ROOM_SCENARIO_COMMAND_GROUP_PARALLEL) {
            validation_add_reactive_issue(report,
                                          ROOM_SCENARIO_VALIDATION_WARNING,
                                          branch,
                                          variant_index,
                                          action_index,
                                          branch_step_index,
                                          "REACTIVE_GROUP_PARALLEL_UNSUPPORTED",
                                          "Reactive DEVICE_COMMAND_GROUP parallel mode is reserved for future runtime support");
        }
        break;
    default:
        break;
    }
}

static void validation_check_reactive_branch_v2_static(const room_scenario_t *scenario,
                                                       const room_scenario_branch_t *branch,
                                                       room_scenario_validation_report_t *report)
{
    uint16_t branch_step_index = branch ? branch->step_start_index : 0;
    if (!scenario || !branch || !report) {
        return;
    }
    if (!room_scenario_valid_reactive_trigger(&branch->trigger)) {
        validation_add_reactive_issue(report,
                                      ROOM_SCENARIO_VALIDATION_ERROR,
                                      branch,
                                      -1,
                                      -1,
                                      branch_step_index,
                                      "REACTIVE_TRIGGER_INVALID",
                                      "Reactive v2 branch has invalid trigger");
    }
    if (branch->variant_count == 0 ||
        (size_t)branch->variant_start_index + branch->variant_count >
            scenario->reactive_variant_count) {
        validation_add_reactive_issue(report,
                                      ROOM_SCENARIO_VALIDATION_ERROR,
                                      branch,
                                      -1,
                                      -1,
                                      branch_step_index,
                                      "REACTIVE_VARIANTS_INVALID",
                                      "Reactive v2 branch must have at least one valid variant");
        return;
    }
    for (uint8_t i = 0; i < branch->guard_flag_count; ++i) {
        if (!branch->guard_flags[i].name[0]) {
            validation_add_reactive_issue(report,
                                          ROOM_SCENARIO_VALIDATION_ERROR,
                                          branch,
                                          -1,
                                          -1,
                                          branch_step_index,
                                          "REACTIVE_GUARD_FLAG_EMPTY",
                                          "Reactive guard flag name is empty");
        }
    }
    for (uint8_t i = 0; i < branch->variant_count; ++i) {
        const room_scenario_reactive_variant_t *variant =
            &scenario->reactive_variants[branch->variant_start_index + i];
        if (!variant->id[0]) {
            validation_add_reactive_issue(report,
                                          ROOM_SCENARIO_VALIDATION_ERROR,
                                          branch,
                                          (int16_t)i,
                                          -1,
                                          branch_step_index,
                                          "REACTIVE_VARIANT_ID_EMPTY",
                                          "Reactive variant id is empty");
        }
        if (variant->action_count == 0 ||
            (size_t)variant->action_start_index + variant->action_count >
                scenario->reactive_action_count) {
            validation_add_reactive_issue(report,
                                          ROOM_SCENARIO_VALIDATION_ERROR,
                                          branch,
                                          (int16_t)i,
                                          -1,
                                          branch_step_index,
                                          "REACTIVE_VARIANT_ACTIONS_INVALID",
                                          "Reactive variant must have at least one valid action");
            continue;
        }
        for (uint8_t action_index = 0; action_index < variant->action_count; ++action_index) {
            validation_check_reactive_action_v2_static(
                scenario,
                &scenario->reactive_actions[variant->action_start_index + action_index],
                branch,
                (int16_t)i,
                (int16_t)action_index,
                branch_step_index,
                report);
        }
    }
    if ((size_t)branch->on_complete_action_start_index + branch->on_complete_action_count >
        scenario->reactive_action_count) {
        validation_add_reactive_issue(report,
                                      ROOM_SCENARIO_VALIDATION_ERROR,
                                      branch,
                                      -1,
                                      -1,
                                      branch_step_index,
                                      "REACTIVE_ON_COMPLETE_RANGE_INVALID",
                                      "Reactive on_complete action range is invalid");
    }
}

static void validation_check_reactive_branch_v2_runtime(const room_scenario_t *scenario,
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
            validation_check_reactive_action_v2_runtime(
                scenario,
                &scenario->reactive_actions[variant->action_start_index + action_index],
                branch,
                (int16_t)i,
                (int16_t)action_index,
                branch_step_index,
                report);
        }
    }
    if (branch->reentry_mode == ROOM_SCENARIO_REENTRY_RESTART ||
        branch->reentry_mode == ROOM_SCENARIO_REENTRY_PARALLEL) {
        validation_add_reactive_issue(report,
                                      ROOM_SCENARIO_VALIDATION_WARNING,
                                      branch,
                                      -1,
                                      -1,
                                      branch_step_index,
                                      "REACTIVE_REENTRY_UNSUPPORTED",
                                      "Reactive branch reentry mode is reserved for future runtime support");
    }
}

static esp_err_t room_scenario_validate_static_report(const room_scenario_t *scenario,
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
    if (!scenario->id[0]) {
        validation_add_issue(out,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             0,
                             "SCENARIO_ID_EMPTY",
                             "Scenario id is empty");
    }
    if (!scenario->room_id[0]) {
        validation_add_issue(out,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             0,
                             "ROOM_ID_EMPTY",
                             "Scenario room_id is empty");
    }
    if (!scenario->name[0]) {
        validation_add_issue(out,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             0,
                             "SCENARIO_NAME_EMPTY",
                             "Scenario name is empty");
    }
    if (scenario->step_count > ROOM_SCENARIO_MAX_STEPS) {
        validation_add_issue(out,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             ROOM_SCENARIO_MAX_STEPS,
                             "STEP_COUNT_LIMIT",
                             "Scenario step_count exceeds limit");
        return ESP_OK;
    }
    if (scenario->branch_count > ROOM_SCENARIO_MAX_BRANCHES) {
        validation_add_issue(out,
                             ROOM_SCENARIO_VALIDATION_ERROR,
                             0,
                             "BRANCH_COUNT_LIMIT",
                             "Scenario branch_count exceeds limit");
        return ESP_OK;
    }
    for (size_t i = 0; i < scenario->branch_count; ++i) {
        const room_scenario_branch_t *branch = &scenario->branches[i];
        uint32_t end_index = (uint32_t)branch->step_start_index + branch->step_count;
        if (!branch->id[0]) {
            validation_add_issue(out,
                                 ROOM_SCENARIO_VALIDATION_ERROR,
                                 branch->step_start_index,
                                 "BRANCH_ID_EMPTY",
                                 "Branch id is empty");
        }
        if (!branch->name[0]) {
            validation_add_issue(out,
                                 ROOM_SCENARIO_VALIDATION_ERROR,
                                 branch->step_start_index,
                                 "BRANCH_NAME_EMPTY",
                                 "Branch name is empty");
        }
        for (size_t j = i + 1; branch->id[0] && j < scenario->branch_count; ++j) {
            if (strcmp(branch->id, scenario->branches[j].id) == 0) {
                validation_add_issue(out,
                                     ROOM_SCENARIO_VALIDATION_ERROR,
                                     branch->step_start_index,
                                     "BRANCH_ID_DUPLICATE",
                                     "Branch id is duplicated");
                break;
            }
        }
        if (end_index > scenario->step_count) {
            validation_add_issue(out,
                                 ROOM_SCENARIO_VALIDATION_ERROR,
                                 branch->step_start_index,
                                 "BRANCH_STEP_RANGE_INVALID",
                                 "Branch step range is outside scenario steps");
        }
        if (branch->type == ROOM_SCENARIO_BRANCH_REACTIVE) {
            const room_scenario_step_t *first_step = NULL;
            if (branch->required_for_completion) {
                validation_add_issue(out,
                                     ROOM_SCENARIO_VALIDATION_WARNING,
                                     branch->step_start_index,
                                     "REACTIVE_BRANCH_REQUIRED_IGNORED",
                                     "Reactive branch is ignored for scenario completion");
            }
            if (branch->variant_count > 0 || branch->trigger.kind != ROOM_SCENARIO_REACTIVE_TRIGGER_NONE) {
                validation_check_reactive_branch_v2_static(scenario, branch, out);
            } else if (!room_scenario_branch_first_step(scenario, branch, &first_step)) {
                validation_add_issue(out,
                                     ROOM_SCENARIO_VALIDATION_ERROR,
                                     branch->step_start_index,
                                     "REACTIVE_BRANCH_EMPTY",
                                     "Reactive branch must start with a wait/listen step");
            } else if (!room_scenario_step_is_reactive_trigger(first_step->type)) {
                validation_add_issue(out,
                                     ROOM_SCENARIO_VALIDATION_ERROR,
                                     branch->step_start_index,
                                     "REACTIVE_BRANCH_TRIGGER_REQUIRED",
                                     "Reactive branch first enabled step must wait for a device event or flag");
            } else if (first_step->type == ROOM_SCENARIO_STEP_WAIT_FLAGS &&
                       !branch->run_once &&
                       branch->max_fire_count == 0 &&
                       branch->cooldown_ms == 0) {
                validation_add_issue(out,
                                     ROOM_SCENARIO_VALIDATION_ERROR,
                                     branch->step_start_index,
                                     "REACTIVE_FLAGS_NEEDS_GUARD",
                                     "Reactive branch triggered by flags must run once or have cooldown");
            }
        }
    }

    for (size_t i = 0; i < scenario->step_count; ++i) {
        const room_scenario_step_t *step = &scenario->steps[i];
        uint16_t step_index = (uint16_t)i;
        if (!step->id[0]) {
            validation_add_issue(out,
                                 ROOM_SCENARIO_VALIDATION_ERROR,
                                 step_index,
                                 "STEP_ID_EMPTY",
                                 "Step id is empty");
        }
        for (size_t j = i + 1; step->id[0] && j < scenario->step_count; ++j) {
            if (!room_scenario_steps_share_branch(scenario, i, j)) {
                continue;
            }
            if (strcmp(step->id, scenario->steps[j].id) == 0) {
                char message[ROOM_SCENARIO_VALIDATION_MESSAGE_MAX_LEN] = {0};
                snprintf(message,
                         sizeof(message),
                         "Step id '%s' is duplicated",
                         step->id);
                validation_add_issue(out,
                                     ROOM_SCENARIO_VALIDATION_ERROR,
                                     step_index,
                                     "STEP_ID_DUPLICATE",
                                     message);
                break;
            }
        }
        if (!room_scenario_valid_step_type(step->type)) {
            validation_add_issue(out,
                                 ROOM_SCENARIO_VALIDATION_ERROR,
                                 step_index,
                                 "STEP_TYPE_INVALID",
                                 "Step type is invalid");
            continue;
        }
        switch (step->type) {
        case ROOM_SCENARIO_STEP_WAIT_TIME:
            if (step->data.wait_time.duration_ms == 0) {
                validation_add_issue(out,
                                     ROOM_SCENARIO_VALIDATION_ERROR,
                                     step_index,
                                     "WAIT_TIME_ZERO",
                                     "WAIT_TIME duration_ms must be greater than zero");
            }
            break;
        case ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
            if (!step->data.operator_approval.prompt[0]) {
                validation_add_issue(out,
                                     ROOM_SCENARIO_VALIDATION_ERROR,
                                     step_index,
                                     "OPERATOR_PROMPT_EMPTY",
                                     "OPERATOR_APPROVAL prompt is empty");
            }
            break;
        case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
            validation_check_device_command_step_static(step, step_index, out);
            break;
        case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
            validation_check_wait_device_event_step_static(step, step_index, out);
            break;
        case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
            validation_check_device_command_group_step_static(step, step_index, out);
            break;
        case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
            if (!step->data.operator_message.message[0]) {
                validation_add_issue(out,
                                     ROOM_SCENARIO_VALIDATION_ERROR,
                                     step_index,
                                     "OPERATOR_MESSAGE_EMPTY",
                                     "SHOW_OPERATOR_MESSAGE message is empty");
            }
            break;
        case ROOM_SCENARIO_STEP_SET_FLAG:
            if (!step->data.set_flag.name[0]) {
                validation_add_issue(out,
                                     ROOM_SCENARIO_VALIDATION_ERROR,
                                     step_index,
                                     "FLAG_NAME_EMPTY",
                                     "SET_FLAG name is empty");
            }
            break;
        case ROOM_SCENARIO_STEP_WAIT_FLAGS:
            if (step->data.wait_flags.flag_count == 0) {
                validation_add_issue(out,
                                     ROOM_SCENARIO_VALIDATION_ERROR,
                                     step_index,
                                     "WAIT_FLAGS_EMPTY",
                                     "WAIT_FLAGS has no flags");
                break;
            }
            if (step->data.wait_flags.flag_count > ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS) {
                validation_add_issue(out,
                                     ROOM_SCENARIO_VALIDATION_ERROR,
                                     step_index,
                                     "WAIT_FLAGS_LIMIT",
                                     "WAIT_FLAGS has too many flags");
                break;
            }
            for (uint8_t flag_index = 0; flag_index < step->data.wait_flags.flag_count; ++flag_index) {
                if (!step->data.wait_flags.flags[flag_index].name[0]) {
                    validation_add_issue(out,
                                         ROOM_SCENARIO_VALIDATION_ERROR,
                                         step_index,
                                         "FLAG_NAME_EMPTY",
                                         "WAIT_FLAGS has empty flag name");
                    break;
                }
            }
            break;
        case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
            validation_check_wait_any_device_event_step_static(step, step_index, out);
            break;
        case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
            validation_check_wait_all_device_events_step_static(step, step_index, out);
            break;
        case ROOM_SCENARIO_STEP_END_GAME:
            break;
        default:
            break;
        }
    }
    return ESP_OK;
}

static esp_err_t room_scenario_validate_runtime_report(const room_scenario_t *scenario,
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
            validation_check_reactive_branch_v2_runtime(scenario, branch, out);
        }
    }
    for (size_t i = 0; i < scenario->step_count; ++i) {
        const room_scenario_step_t *step = &scenario->steps[i];
        uint16_t step_index = (uint16_t)i;
        switch (step->type) {
        case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
            validation_check_device_command_step_runtime(step, step_index, out);
            break;
        case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
            validation_check_wait_device_event_step_runtime(step, step_index, out);
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

esp_err_t room_scenario_validate_static(const room_scenario_t *scenario,
                                        room_scenario_validation_report_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    validation_report_init(out);
    return room_scenario_validate_static_report(scenario, out);
}

esp_err_t room_scenario_validate_runtime(const room_scenario_t *scenario,
                                         room_scenario_validation_report_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    validation_report_init(out);
    return room_scenario_validate_runtime_report(scenario, out);
}

esp_err_t room_scenario_validate(const room_scenario_t *scenario,
                                 room_scenario_validation_report_t *out)
{
    esp_err_t err = ESP_OK;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    validation_report_init(out);
    err = room_scenario_validate_static_report(scenario, out);
    if (err != ESP_OK || !out->valid) {
        return err;
    }
    return room_scenario_validate_runtime_report(scenario, out);
}

esp_err_t room_scenario_validate_by_id(const char *scenario_id,
                                       room_scenario_validation_report_t *out)
{
    room_scenario_t *scenario = NULL;
    esp_err_t err = ESP_OK;

    if (!scenario_id || !scenario_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_scenario_acquire_scratch(&scenario, NULL);
    if (err != ESP_OK) {
        return err;
    }
    memset(scenario, 0, sizeof(*scenario));
    err = room_scenario_get(scenario_id, scenario);
    if (err != ESP_OK) {
        room_scenario_release_scratch();
        return err;
    }
    err = room_scenario_validate(scenario, out);
    room_scenario_release_scratch();
    return err;
}
