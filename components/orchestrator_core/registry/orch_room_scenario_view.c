#include "orchestrator_registry_internal.h"

#include <string.h>

static size_t orch_room_scenario_min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

static orch_room_scenario_step_type_t orch_room_scenario_map_step_type(room_scenario_step_type_t type)
{
    switch (type) {
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        return ORCH_ROOM_SCENARIO_STEP_WAIT_TIME;
    case ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        return ORCH_ROOM_SCENARIO_STEP_OPERATOR_APPROVAL;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        return ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND;
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        return ORCH_ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        return ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP;
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        return ORCH_ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE;
    case ROOM_SCENARIO_STEP_SET_FLAG:
        return ORCH_ROOM_SCENARIO_STEP_SET_FLAG;
    case ROOM_SCENARIO_STEP_WAIT_FLAGS:
        return ORCH_ROOM_SCENARIO_STEP_WAIT_FLAGS;
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        return ORCH_ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT;
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        return ORCH_ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS;
    case ROOM_SCENARIO_STEP_END_GAME:
        return ORCH_ROOM_SCENARIO_STEP_END_GAME;
    default:
        return ORCH_ROOM_SCENARIO_STEP_OPERATOR_APPROVAL;
    }
}

static void orch_room_scenario_copy_summary(const room_scenario_t *src,
                                            orch_room_scenario_entry_t *dst)
{
    if (!src || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    quest_str_copy(dst->room_id, sizeof(dst->room_id), src->room_id);
    quest_str_copy(dst->id, sizeof(dst->id), src->id);
    quest_str_copy(dst->name, sizeof(dst->name), src->name);
    dst->step_count = src->step_count;
    dst->valid = true;
}

static void orch_room_scenario_copy_step(const room_scenario_step_t *src,
                                         orch_room_scenario_step_entry_t *dst)
{
    if (!src || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    quest_str_copy(dst->id, sizeof(dst->id), src->id);
    quest_str_copy(dst->label, sizeof(dst->label), src->label);
    dst->type = orch_room_scenario_map_step_type(src->type);
    dst->enabled = src->enabled;
    switch (src->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        quest_str_copy(dst->device_id,
                    sizeof(dst->device_id),
                    src->data.device_command.device_id);
        quest_str_copy(dst->command_id,
                    sizeof(dst->command_id),
                    src->data.device_command.command_id);
        quest_str_copy(dst->params_json,
                    sizeof(dst->params_json),
                    src->data.device_command.params_json);
        break;
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        dst->duration_ms = src->data.wait_time.duration_ms;
        break;
    case ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        quest_str_copy(dst->operator_prompt,
                    sizeof(dst->operator_prompt),
                    src->data.operator_approval.prompt);
        quest_str_copy(dst->operator_approve_label,
                    sizeof(dst->operator_approve_label),
                    src->data.operator_approval.approve_label);
        break;
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        quest_str_copy(dst->device_id,
                    sizeof(dst->device_id),
                    src->data.wait_device_event.device_id);
        quest_str_copy(dst->event_id,
                    sizeof(dst->event_id),
                    src->data.wait_device_event.event_id);
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        dst->command_count = src->data.device_command_group.command_count;
        if (src->data.device_command_group.command_count > 0) {
            quest_str_copy(dst->device_id,
                        sizeof(dst->device_id),
                        src->data.device_command_group.commands[0].device_id);
            quest_str_copy(dst->command_id,
                        sizeof(dst->command_id),
                        src->data.device_command_group.commands[0].command_id);
        }
        break;
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        quest_str_copy(dst->operator_message,
                    sizeof(dst->operator_message),
                    src->data.operator_message.message);
        break;
    case ROOM_SCENARIO_STEP_SET_FLAG:
        quest_str_copy(dst->flag_name,
                    sizeof(dst->flag_name),
                    src->data.set_flag.name);
        dst->flag_value = src->data.set_flag.value;
        break;
    case ROOM_SCENARIO_STEP_WAIT_FLAGS:
        dst->flag_count = src->data.wait_flags.flag_count;
        for (uint8_t i = 0;
             i < src->data.wait_flags.flag_count &&
             i < ORCH_ROOM_SCENARIO_MAX_FLAG_REFS;
             ++i) {
            quest_str_copy(dst->flags[i].name,
                        sizeof(dst->flags[i].name),
                        src->data.wait_flags.flags[i].name);
            dst->flags[i].value = src->data.wait_flags.flags[i].value;
        }
        break;
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        dst->event_count = src->data.wait_any_device_event.event_count;
        for (uint8_t i = 0;
             i < src->data.wait_any_device_event.event_count &&
             i < ORCH_ROOM_SCENARIO_MAX_EVENT_REFS;
             ++i) {
            quest_str_copy(dst->events[i].device_id,
                        sizeof(dst->events[i].device_id),
                        src->data.wait_any_device_event.events[i].device_id);
            quest_str_copy(dst->events[i].event_id,
                        sizeof(dst->events[i].event_id),
                        src->data.wait_any_device_event.events[i].event_id);
        }
        break;
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        dst->event_count = src->data.wait_all_device_events.event_count;
        for (uint8_t i = 0;
             i < src->data.wait_all_device_events.event_count &&
             i < ORCH_ROOM_SCENARIO_MAX_EVENT_REFS;
             ++i) {
            quest_str_copy(dst->events[i].device_id,
                        sizeof(dst->events[i].device_id),
                        src->data.wait_all_device_events.events[i].device_id);
            quest_str_copy(dst->events[i].event_id,
                        sizeof(dst->events[i].event_id),
                        src->data.wait_all_device_events.events[i].event_id);
        }
        break;
    case ROOM_SCENARIO_STEP_END_GAME:
        break;
    default:
        break;
    }
}

static void orch_room_scenario_copy_detail(const room_scenario_t *src,
                                           orch_room_scenario_detail_t *dst)
{
    room_scenario_validation_report_t *report = NULL;
    size_t step_count = 0;
    if (!src || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    orch_room_scenario_copy_summary(src, &dst->summary);
    step_count = orch_room_scenario_min_size(src->step_count, ORCH_ROOM_SCENARIO_MAX_STEPS);
    dst->summary.step_count = step_count;
    for (size_t i = 0; i < step_count; ++i) {
        orch_room_scenario_copy_step(&src->steps[i], &dst->steps[i]);
    }
    report = orch_scratch_validation_report();
    if (!report) {
        return;
    }
    if (room_scenario_validate(src, report) == ESP_OK) {
        dst->summary.valid = report->valid;
        dst->summary.validation_issue_count = report->issue_count;
        for (size_t i = 0; i < report->issue_count && i < ROOM_SCENARIO_VALIDATION_MAX_ISSUES; ++i) {
            dst->validation_issues[i] = report->issues[i];
        }
    }
}

void orch_room_scenario_view_collect_all(orch_registry_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }
    snapshot->room_scenario_count = 0;
    for (uint8_t room_index = 0; room_index < snapshot->room_count; ++room_index) {
        for (size_t i = 0; snapshot->room_scenario_count < ORCH_REGISTRY_MAX_ROOM_SCENARIOS; ++i) {
            room_scenario_t *scenario = orch_scratch_room_scenario();
            size_t total = 0;
            esp_err_t err = room_scenario_get_by_room_index(snapshot->rooms[room_index].room_id,
                                                            i,
                                                            scenario,
                                                            &total);
            if (err == ESP_ERR_NOT_FOUND) {
                break;
            }
            if (err != ESP_OK) {
                break;
            }
            orch_room_scenario_copy_summary(scenario, &snapshot->room_scenarios[snapshot->room_scenario_count++]);
        }
        if (snapshot->room_scenario_count >= ORCH_REGISTRY_MAX_ROOM_SCENARIOS) {
            return;
        }
    }
}

esp_err_t orch_room_scenario_view_list(const orch_registry_snapshot_t *snapshot,
                                       const char *room_id,
                                       orch_room_scenario_entry_t *out_scenarios,
                                       size_t max_scenarios,
                                       size_t *out_count)
{
    size_t total = 0;
    size_t emitted = 0;
    bool room_exists = false;
    if (!snapshot || !room_id || !room_id[0] || !out_scenarios || !out_count || max_scenarios == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    for (uint8_t i = 0; i < snapshot->room_count; ++i) {
        if (strcmp(snapshot->rooms[i].room_id, room_id) == 0) {
            room_exists = true;
            break;
        }
    }
    if (!room_exists) {
        return ESP_ERR_NOT_FOUND;
    }
    for (size_t i = 0; i < snapshot->room_scenario_count; ++i) {
        const orch_room_scenario_entry_t *src = &snapshot->room_scenarios[i];
        if (strcmp(src->room_id, room_id) != 0) {
            continue;
        }
        if (emitted < max_scenarios) {
            out_scenarios[emitted] = *src;
            emitted++;
        }
        total++;
    }
    *out_count = total;
    return total > max_scenarios ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

esp_err_t orch_room_scenario_view_list_details(const char *room_id,
                                               orch_room_scenario_detail_t *out_scenarios,
                                               size_t max_scenarios,
                                               size_t *out_count)
{
    room_scenario_t *scenario = NULL;
    size_t count = 0;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0] || !out_scenarios || !out_count || max_scenarios == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    err = orch_scratch_lock();
    if (err != ESP_OK) {
        return err;
    }
    scenario = orch_scratch_room_scenario();
    for (size_t i = 0; i < max_scenarios; ++i) {
        size_t total = 0;
        err = room_scenario_get_by_room_index(room_id, i, scenario, &total);
        if (err == ESP_ERR_NOT_FOUND) {
            count = total;
            err = ESP_OK;
            break;
        }
        if (err != ESP_OK) {
            orch_scratch_unlock();
            return err;
        }
        count = total;
        orch_room_scenario_copy_detail(scenario, &out_scenarios[i]);
    }
    *out_count = count;
    err = count > max_scenarios ? ESP_ERR_INVALID_SIZE : ESP_OK;
    orch_scratch_unlock();
    return err;
}
