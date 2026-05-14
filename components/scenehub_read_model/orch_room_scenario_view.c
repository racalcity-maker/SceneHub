#include "orchestrator_registry_internal.h"

#include <string.h>

#include "scenehub_scenario_validation.h"

static size_t orch_room_scenario_min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

static uint8_t orch_room_scenario_min_u8(size_t value, size_t limit)
{
    size_t bounded = orch_room_scenario_min_size(value, limit);
    return (uint8_t)bounded;
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

static void orch_room_scenario_copy_command(const room_scenario_device_command_t *src,
                                            orch_room_scenario_command_entry_t *dst)
{
    if (!src || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    quest_str_copy(dst->device_id, sizeof(dst->device_id), src->device_id);
    quest_str_copy(dst->command_id, sizeof(dst->command_id), src->command_id);
    quest_str_copy(dst->params_json, sizeof(dst->params_json), src->params_json);
}

static void orch_room_scenario_copy_group_command(
    const room_scenario_device_command_group_t *group,
    uint8_t index,
    orch_room_scenario_command_entry_t *dst)
{
    if (!group || !dst || index >= ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    quest_str_copy(dst->device_id, sizeof(dst->device_id), group->commands[index].device_id);
    quest_str_copy(dst->command_id, sizeof(dst->command_id), group->commands[index].command_id);
    quest_str_copy(dst->params_json, sizeof(dst->params_json), group->commands[index].params_json);
}

static void orch_room_scenario_copy_flag_refs(const room_scenario_flag_condition_t *src,
                                              uint8_t count,
                                              orch_room_scenario_flag_ref_t *dst,
                                              uint8_t *out_count)
{
    uint8_t copied = 0;
    if (!dst || !out_count) {
        return;
    }
    memset(dst, 0, sizeof(*dst) * ORCH_ROOM_SCENARIO_MAX_FLAG_REFS);
    if (!src) {
        *out_count = 0;
        return;
    }
    copied = orch_room_scenario_min_u8(count, ORCH_ROOM_SCENARIO_MAX_FLAG_REFS);
    for (uint8_t i = 0; i < copied; ++i) {
        quest_str_copy(dst[i].name, sizeof(dst[i].name), src[i].name);
        dst[i].value = src[i].value;
    }
    *out_count = copied;
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

static void orch_room_scenario_copy_reactive_trigger(
    const room_scenario_reactive_trigger_t *src,
    orch_room_scenario_reactive_trigger_entry_t *dst)
{
    if (!src || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    dst->kind = src->kind;
    quest_str_copy(dst->kind_text, sizeof(dst->kind_text), orch_room_scenario_trigger_kind_str(dst->kind));
    quest_str_copy(dst->device_id, sizeof(dst->device_id), src->device_id);
    quest_str_copy(dst->event_id, sizeof(dst->event_id), src->event_id);
    quest_str_copy(dst->flag_name, sizeof(dst->flag_name), src->flag_name);
    quest_str_copy(dst->runtime_event, sizeof(dst->runtime_event), src->runtime_event);
    quest_str_copy(dst->operator_event, sizeof(dst->operator_event), src->operator_event);
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
    quest_str_copy(dst->type_text, sizeof(dst->type_text), orch_room_scenario_step_type_str(dst->type));
    dst->enabled = src->enabled;
    dst->allow_operator_skip = src->allow_operator_skip;
    quest_str_copy(dst->operator_skip_label,
                   sizeof(dst->operator_skip_label),
                   src->operator_skip_label);
    switch (src->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        orch_room_scenario_copy_command(&src->data.device_command, &dst->commands[0]);
        dst->command_count = 1;
        quest_str_copy(dst->device_id, sizeof(dst->device_id), dst->commands[0].device_id);
        quest_str_copy(dst->command_id, sizeof(dst->command_id), dst->commands[0].command_id);
        quest_str_copy(dst->params_json, sizeof(dst->params_json), dst->commands[0].params_json);
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
        dst->timeout_ms = src->data.wait_device_event.timeout_ms;
        quest_str_copy(dst->timeout_message,
                       sizeof(dst->timeout_message),
                       src->data.wait_device_event.timeout_message);
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        dst->command_count = orch_room_scenario_min_u8(
            src->data.device_command_group.command_count,
            ORCH_ROOM_SCENARIO_MAX_COMMAND_GROUP_COMMANDS);
        if (dst->command_count > 0) {
            for (uint8_t i = 0; i < dst->command_count; ++i) {
                orch_room_scenario_copy_group_command(&src->data.device_command_group,
                                                      i,
                                                      &dst->commands[i]);
            }
            quest_str_copy(dst->device_id, sizeof(dst->device_id), dst->commands[0].device_id);
            quest_str_copy(dst->command_id, sizeof(dst->command_id), dst->commands[0].command_id);
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
        orch_room_scenario_copy_flag_refs(src->data.wait_flags.flags,
                                          src->data.wait_flags.flag_count,
                                          dst->flags,
                                          &dst->flag_count);
        dst->timeout_ms = src->data.wait_flags.timeout_ms;
        quest_str_copy(dst->timeout_message,
                       sizeof(dst->timeout_message),
                       src->data.wait_flags.timeout_message);
        break;
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        dst->event_count = orch_room_scenario_min_u8(src->data.wait_any_device_event.event_count,
                                                     ORCH_ROOM_SCENARIO_MAX_EVENT_REFS);
        for (uint8_t i = 0;
             i < dst->event_count;
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
        dst->event_count = orch_room_scenario_min_u8(src->data.wait_all_device_events.event_count,
                                                     ORCH_ROOM_SCENARIO_MAX_EVENT_REFS);
        for (uint8_t i = 0;
             i < dst->event_count;
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

static void orch_room_scenario_copy_reactive_action(
    const room_scenario_reactive_action_t *src,
    orch_room_scenario_reactive_action_entry_t *dst)
{
    if (!src || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    quest_str_copy(dst->id, sizeof(dst->id), src->id);
    quest_str_copy(dst->label, sizeof(dst->label), src->label);
    dst->type = orch_room_scenario_map_step_type(src->type);
    quest_str_copy(dst->type_text, sizeof(dst->type_text), orch_room_scenario_step_type_str(dst->type));
    dst->group_mode = src->group_mode;
    quest_str_copy(dst->group_mode_text,
                   sizeof(dst->group_mode_text),
                   orch_room_scenario_group_mode_str(dst->group_mode));
    dst->group_command_start_index = src->group_command_start_index;
    dst->group_command_count = src->group_command_count;
    switch (src->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        quest_str_copy(dst->device_id, sizeof(dst->device_id), src->data.device_command.device_id);
        quest_str_copy(dst->command_id,
                       sizeof(dst->command_id),
                       src->data.device_command.command_id);
        quest_str_copy(dst->params_json,
                       sizeof(dst->params_json),
                       src->data.device_command.params_json);
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        break;
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        dst->duration_ms = src->data.wait_time.duration_ms;
        break;
    case ROOM_SCENARIO_STEP_SET_FLAG:
        quest_str_copy(dst->flag_name, sizeof(dst->flag_name), src->data.set_flag.name);
        dst->flag_value = src->data.set_flag.value;
        break;
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        quest_str_copy(dst->operator_message,
                       sizeof(dst->operator_message),
                       src->data.operator_message.message);
        break;
    default:
        break;
    }
}

static void orch_room_scenario_copy_reactive_variant(
    const room_scenario_reactive_variant_t *src,
    orch_room_scenario_reactive_variant_entry_t *dst)
{
    if (!src || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    quest_str_copy(dst->id, sizeof(dst->id), src->id);
    quest_str_copy(dst->label, sizeof(dst->label), src->label);
    dst->action_start_index = src->action_start_index;
    dst->action_count = src->action_count;
}

static void orch_room_scenario_copy_result_policy(const room_scenario_branch_t *src,
                                                  orch_room_scenario_result_policy_entry_t *dst)
{
    if (!src || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    dst->on_done = src->result_on_done;
    quest_str_copy(dst->on_done_text,
                   sizeof(dst->on_done_text),
                   orch_room_scenario_result_action_str(dst->on_done));
    dst->on_fail = src->result_on_fail;
    quest_str_copy(dst->on_fail_text,
                   sizeof(dst->on_fail_text),
                   orch_room_scenario_result_action_str(dst->on_fail));
    dst->on_timeout = src->result_on_timeout;
    quest_str_copy(dst->on_timeout_text,
                   sizeof(dst->on_timeout_text),
                   orch_room_scenario_result_action_str(dst->on_timeout));
    quest_str_copy(dst->flag, sizeof(dst->flag), src->result_flag);
}

static void orch_room_scenario_copy_branch_detail(const room_scenario_branch_t *src,
                                                  orch_room_scenario_branch_detail_t *dst)
{
    if (!src || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    quest_str_copy(dst->id, sizeof(dst->id), src->id);
    quest_str_copy(dst->name, sizeof(dst->name), src->name);
    dst->type = src->type;
    quest_str_copy(dst->type_text, sizeof(dst->type_text), room_scenario_branch_type_to_str(dst->type));
    dst->enabled = src->enabled;
    dst->required_for_completion = src->required_for_completion;
    dst->priority = src->priority;
    dst->policy_mode = src->policy_mode;
    quest_str_copy(dst->policy_mode_text,
                   sizeof(dst->policy_mode_text),
                   orch_room_scenario_policy_mode_str(dst->policy_mode));
    dst->cooldown_ms = src->cooldown_ms;
    dst->max_fire_count = src->max_fire_count;
    dst->run_once = src->run_once;
    dst->reentry_mode = src->reentry_mode;
    quest_str_copy(dst->reentry_mode_text,
                   sizeof(dst->reentry_mode_text),
                   room_scenario_reentry_mode_to_str(dst->reentry_mode));
    orch_room_scenario_copy_reactive_trigger(&src->trigger, &dst->trigger);
    orch_room_scenario_copy_flag_refs(src->guard_flags,
                                      src->guard_flag_count,
                                      dst->guard_flags,
                                      &dst->guard_flag_count);
    orch_room_scenario_copy_result_policy(src, &dst->result_policy);
    dst->variant_start_index = src->variant_start_index;
    dst->variant_count = src->variant_count;
    dst->on_complete_action_start_index = src->on_complete_action_start_index;
    dst->on_complete_action_count = src->on_complete_action_count;
    dst->step_start_index = src->step_start_index;
    dst->step_count = src->step_count;
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
    dst->branch_count = orch_room_scenario_min_u8(src->branch_count, ORCH_ROOM_SCENARIO_MAX_BRANCHES);
    dst->reactive_variant_count =
        orch_room_scenario_min_u8(src->reactive_variant_count, ORCH_ROOM_SCENARIO_MAX_REACTIVE_VARIANTS);
    dst->reactive_action_count =
        orch_room_scenario_min_u8(src->reactive_action_count, ORCH_ROOM_SCENARIO_MAX_REACTIVE_ACTIONS);
    dst->reactive_group_command_count = orch_room_scenario_min_u8(
        src->reactive_group_command_count,
        ORCH_ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS);
    for (uint8_t i = 0; i < dst->branch_count; ++i) {
        orch_room_scenario_copy_branch_detail(&src->branches[i], &dst->branches[i]);
    }
    for (uint8_t i = 0; i < dst->reactive_variant_count; ++i) {
        orch_room_scenario_copy_reactive_variant(&src->reactive_variants[i],
                                                 &dst->reactive_variants[i]);
    }
    for (uint8_t i = 0; i < dst->reactive_action_count; ++i) {
        orch_room_scenario_copy_reactive_action(&src->reactive_actions[i], &dst->reactive_actions[i]);
    }
    for (uint8_t i = 0; i < dst->reactive_group_command_count; ++i) {
        orch_room_scenario_copy_command(&src->reactive_group_commands[i],
                                        &dst->reactive_group_commands[i]);
    }
    report = orch_scratch_validation_report();
    if (!report) {
        return;
    }
    if (scenehub_scenario_validate(src, report) == ESP_OK) {
        dst->summary.valid = report->valid;
        dst->summary.validation_issue_count = report->issue_count;
        for (size_t i = 0; i < report->issue_count && i < ROOM_SCENARIO_VALIDATION_MAX_ISSUES; ++i) {
            dst->validation_issues[i].level = report->issues[i].level;
            quest_str_copy(dst->validation_issues[i].level_text,
                           sizeof(dst->validation_issues[i].level_text),
                           orch_room_scenario_validation_level_str(report->issues[i].level));
            dst->validation_issues[i].step_index = report->issues[i].step_index;
            quest_str_copy(dst->validation_issues[i].code,
                           sizeof(dst->validation_issues[i].code),
                           report->issues[i].code);
            quest_str_copy(dst->validation_issues[i].message,
                           sizeof(dst->validation_issues[i].message),
                           report->issues[i].message);
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
