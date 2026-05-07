#include "gm_room_session_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "quest_common_utils.h"
#include "quest_device.h"

#define GM_REACTIVE_V2_MAX_ACTIONS_PER_TICK 8

static const char *TAG = "gm_room_session";
static EXT_RAM_BSS_ATTR quest_device_t s_reactive_match_device;

static const room_scenario_branch_t *reactive_v2_model_branch(
    const gm_room_session_t *session,
    const gm_room_scenario_branch_runtime_t *branch)
{
    if (!session || !branch ||
        branch->branch_index >= session->running_scenario.branch_count) {
        return NULL;
    }
    const room_scenario_branch_t *model = &session->running_scenario.branches[branch->branch_index];
    if (model->type != ROOM_SCENARIO_BRANCH_REACTIVE ||
        (model->variant_count == 0 &&
         model->trigger.kind == ROOM_SCENARIO_REACTIVE_TRIGGER_NONE)) {
        return NULL;
    }
    return model;
}

bool gm_room_session_branch_is_reactive_v2(const gm_room_session_t *session,
                                           const gm_room_scenario_branch_runtime_t *branch)
{
    return reactive_v2_model_branch(session, branch) != NULL;
}

static bool reactive_v2_get_flag(const gm_room_session_t *session,
                                 const char *name,
                                 bool *out_value)
{
    if (!session || !name || !name[0]) {
        return false;
    }
    for (uint8_t i = 0; i < session->scenario_flag_count; ++i) {
        if (strcmp(session->scenario_flags[i].name, name) == 0) {
            if (out_value) {
                *out_value = session->scenario_flags[i].value;
            }
            return true;
        }
    }
    return false;
}

static bool reactive_v2_guards_match(const gm_room_session_t *session,
                                     const room_scenario_branch_t *model)
{
    for (uint8_t i = 0; i < model->guard_flag_count; ++i) {
        bool actual = false;
        if (!reactive_v2_get_flag(session, model->guard_flags[i].name, &actual) ||
            actual != model->guard_flags[i].value) {
            return false;
        }
    }
    return true;
}

static const char *reactive_v2_event_device_id(const event_bus_message_t *message)
{
    if (!message) {
        return "";
    }
    switch (message->payload_type) {
    case EVENT_BUS_PAYLOAD_DEVICE_STATUS:
        return message->data.device_status.device_id;
    case EVENT_BUS_PAYLOAD_DEVICE_RUNTIME:
        return message->data.device_runtime.device_id;
    case EVENT_BUS_PAYLOAD_DEVICE_CONTROL:
        return message->data.device_control.device_id;
    case EVENT_BUS_PAYLOAD_TEXT:
    default:
        return message->topic;
    }
}

static bool reactive_v2_event_id_matches(const char *expected,
                                         const event_bus_message_t *message)
{
    if (!expected || !expected[0] || !message) {
        return false;
    }
    if (strcmp(expected, message->payload) == 0 ||
        strcmp(expected, message->topic) == 0) {
        return true;
    }
    switch (message->payload_type) {
    case EVENT_BUS_PAYLOAD_DEVICE_CONTROL:
        return strcmp(expected, message->data.device_control.action_id) == 0;
    case EVENT_BUS_PAYLOAD_DEVICE_RUNTIME:
        return strcmp(expected, message->data.device_runtime.runtime_type) == 0 ||
               strcmp(expected, message->data.device_runtime.state) == 0;
    case EVENT_BUS_PAYLOAD_DEVICE_STATUS:
        return strcmp(expected, message->data.device_status.state) == 0 ||
               strcmp(expected, message->data.device_status.health) == 0 ||
               strcmp(expected, message->data.device_status.connectivity) == 0;
    case EVENT_BUS_PAYLOAD_TEXT:
    default:
        return false;
    }
}

static bool reactive_v2_device_event_matches(const room_scenario_branch_t *model,
                                             const event_bus_message_t *message)
{
    quest_device_t *device = &s_reactive_match_device;
    quest_device_event_t event = {0};
    const char *source_id = reactive_v2_event_device_id(message);
    const char *expected_event = model && model->trigger.event_id[0] ? model->trigger.event_id : "";
    bool source_matches = false;
    bool event_matches = false;
    if (!model || !message || !model->trigger.device_id[0] || !expected_event[0]) {
        return false;
    }
    memset(device, 0, sizeof(*device));
    if (device && quest_device_get(model->trigger.device_id, device) == ESP_OK) {
        source_matches = strcmp(model->trigger.device_id, source_id) == 0 ||
                         strcmp(device->client_id, source_id) == 0;
    } else {
        source_matches = strcmp(model->trigger.device_id, source_id) == 0;
    }
    if (quest_device_get_event(model->trigger.device_id, model->trigger.event_id, &event) == ESP_OK) {
        event_matches = reactive_v2_event_id_matches(event.event[0] ? event.event : event.id, message) ||
                        reactive_v2_event_id_matches(event.id, message);
    } else {
        event_matches = reactive_v2_event_id_matches(expected_event, message);
    }
    return source_matches && event_matches;
}

bool gm_room_session_reactive_v2_matches_event(const gm_room_session_t *session,
                                               const gm_room_scenario_branch_runtime_t *branch,
                                               const event_bus_message_t *message)
{
    const room_scenario_branch_t *model = reactive_v2_model_branch(session, branch);
    if (!model || !message || !branch->active ||
        (branch->max_fire_count > 0 && branch->fire_count >= branch->max_fire_count) ||
        !reactive_v2_guards_match(session, model)) {
        return false;
    }
    if (message->payload_type == EVENT_BUS_PAYLOAD_DEVICE_CONTROL &&
        strcmp(message->data.device_control.source, "result") == 0) {
        return false;
    }
    switch (model->trigger.kind) {
    case ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT:
        return reactive_v2_device_event_matches(model, message);
    case ROOM_SCENARIO_REACTIVE_TRIGGER_FLAG_CHANGED:
        return message->type == EVENT_FLAG_CHANGED &&
               reactive_v2_event_id_matches(model->trigger.flag_name, message);
    case ROOM_SCENARIO_REACTIVE_TRIGGER_OPERATOR_EVENT:
        return message->type == EVENT_WEB_COMMAND &&
               reactive_v2_event_id_matches(model->trigger.operator_event, message);
    case ROOM_SCENARIO_REACTIVE_TRIGGER_RUNTIME_EVENT:
        return message->type == EVENT_RUNTIME_CONTROL &&
               reactive_v2_event_id_matches(model->trigger.runtime_event, message);
    case ROOM_SCENARIO_REACTIVE_TRIGGER_NONE:
    default:
        return false;
    }
}

static uint8_t reactive_v2_select_variant(gm_room_scenario_branch_runtime_t *branch,
                                          const room_scenario_branch_t *model)
{
    uint8_t count = model->variant_count;
    if (count == 0) {
        return 0;
    }
    switch (model->policy_mode) {
    case ROOM_SCENARIO_REACTIVE_POLICY_ROTATE: {
        uint8_t selected = branch->policy_cursor % count;
        branch->policy_cursor = (uint8_t)((selected + 1) % count);
        return selected;
    }
    case ROOM_SCENARIO_REACTIVE_POLICY_ESCALATE:
        if (branch->policy_stage >= count) {
            branch->policy_stage = (uint8_t)(count - 1);
        }
        {
            uint8_t selected = branch->policy_stage;
            if (branch->policy_stage + 1 < count) {
                branch->policy_stage++;
            }
            return selected;
        }
    case ROOM_SCENARIO_REACTIVE_POLICY_RANDOM:
        return (uint8_t)(gm_room_session_scenario_now_ms() % count);
    case ROOM_SCENARIO_REACTIVE_POLICY_SINGLE:
    default:
        return 0;
    }
}

static esp_err_t reactive_v2_execute_command_group(
    gm_room_session_t *session,
    const room_scenario_reactive_action_t *action,
    char *error,
    size_t error_size)
{
    if (!session || !action ||
        action->group_mode == ROOM_SCENARIO_COMMAND_GROUP_PARALLEL ||
        action->group_command_count == 0 ||
        (size_t)action->group_command_start_index + action->group_command_count >
            session->running_scenario.reactive_group_command_count) {
        scenario_set_error_locked(session, "reactive_group_unsupported");
        return ESP_ERR_NOT_SUPPORTED;
    }
    for (uint8_t i = 0; i < action->group_command_count; ++i) {
        const room_scenario_device_command_t *command =
            &session->running_scenario.reactive_group_commands[action->group_command_start_index + i];
        gm_room_session_command_dispatch_t dispatch = {0};
        esp_err_t err = gm_room_session_execute_quest_device_command_internal(command,
                                                                              error,
                                                                              error_size,
                                                                              &dispatch);
        if (err != ESP_OK) {
            return err;
        }
        if (dispatch.result_required) {
            scenario_set_error_locked(session, "reactive_group_result_required_unsupported");
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    return ESP_OK;
}

static esp_err_t reactive_v2_apply_completion_locked(gm_room_session_t *session,
                                                     const room_scenario_branch_t *model)
{
    if (!session || !model ||
        (size_t)model->on_complete_action_start_index + model->on_complete_action_count >
            session->running_scenario.reactive_action_count) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < model->on_complete_action_count; ++i) {
        const room_scenario_reactive_action_t *action =
            &session->running_scenario.reactive_actions[model->on_complete_action_start_index + i];
        if (action->type == ROOM_SCENARIO_STEP_SET_FLAG) {
            esp_err_t err = scenario_set_flag_locked(session,
                                                     action->data.set_flag.name,
                                                     action->data.set_flag.value);
            if (err != ESP_OK) {
                return err;
            }
        } else if (action->type == ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE) {
            quest_str_copy(session->scenario_operator_message,
                           sizeof(session->scenario_operator_message),
                           action->data.operator_message.message);
        }
    }
    return ESP_OK;
}

static esp_err_t reactive_v2_finish_locked(gm_room_session_t *session,
                                           gm_room_scenario_branch_runtime_t *branch,
                                           const room_scenario_branch_t *model,
                                           uint32_t now_ms)
{
    esp_err_t err = reactive_v2_apply_completion_locked(session, model);
    bool run_pending = false;
    if (err != ESP_OK) {
        scenario_set_error_locked(session, "reactive_on_complete_failed");
        return err;
    }
    branch->fire_count++;
    branch->fired_once = true;
    branch->reactive_action_start_index = 0;
    branch->reactive_action_count = 0;
    branch->reactive_current_action = 0;
    if (branch->max_fire_count > 0 && branch->fire_count >= branch->max_fire_count) {
        branch->scenario_state = GM_ROOM_SCENARIO_DONE;
        branch->pending_trigger = false;
        scenario_branch_clear_wait_fields(branch);
        gm_room_session_mark_session_changed_locked(session);
        return ESP_OK;
    }

    run_pending = branch->pending_trigger &&
                  (branch->cooldown_until_ms == 0 ||
                   scenario_time_reached(now_ms, branch->cooldown_until_ms));
    branch->pending_trigger = false;

    if (run_pending) {
        branch->scenario_state = GM_ROOM_SCENARIO_WAITING;
        branch->cooldown_until_ms = 0;
        scenario_branch_clear_wait_fields(branch);
        gm_room_session_mark_session_changed_locked(session);
        return gm_room_session_reactive_v2_fire_locked(session, branch, now_ms);
    }

    if (branch->cooldown_until_ms > 0 && !scenario_time_reached(now_ms, branch->cooldown_until_ms)) {
        branch->scenario_state = GM_ROOM_SCENARIO_COOLDOWN;
    } else {
        branch->scenario_state = GM_ROOM_SCENARIO_WAITING;
        branch->cooldown_until_ms = 0;
    }
    scenario_branch_clear_wait_fields(branch);
    gm_room_session_mark_session_changed_locked(session);
    return ESP_OK;
}

static esp_err_t reactive_v2_fail_reaction_locked(gm_room_session_t *session,
                                                  gm_room_scenario_branch_runtime_t *branch,
                                                  const char *message)
{
    if (!session || !branch) {
        return ESP_ERR_INVALID_ARG;
    }
    branch->scenario_state = GM_ROOM_SCENARIO_ERROR;
    branch->reactive_action_start_index = 0;
    branch->reactive_action_count = 0;
    branch->reactive_current_action = 0;
    scenario_branch_clear_wait_fields(branch);
    quest_str_copy(session->scenario_last_error,
                   sizeof(session->scenario_last_error),
                   message ? message : "reactive_failed");
    gm_room_session_mark_session_changed_locked(session);
    return ESP_OK;
}

static esp_err_t reactive_v2_apply_result_policy_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    const room_scenario_branch_t *model,
    room_scenario_reactive_result_action_t action,
    uint32_t now_ms,
    const char *message)
{
    esp_err_t err = ESP_OK;
    if (!session || !branch || !model) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (action) {
    case ROOM_SCENARIO_REACTIVE_RESULT_CONTINUE:
        return reactive_v2_finish_locked(session, branch, model, now_ms);
    case ROOM_SCENARIO_REACTIVE_RESULT_SET_FLAG:
        if (!model->result_flag[0]) {
            return reactive_v2_fail_reaction_locked(session, branch, "reactive_result_flag_empty");
        }
        err = scenario_set_flag_locked(session, model->result_flag, true);
        if (err != ESP_OK) {
            return reactive_v2_fail_reaction_locked(session, branch, "reactive_result_set_flag_failed");
        }
        return reactive_v2_finish_locked(session, branch, model, now_ms);
    case ROOM_SCENARIO_REACTIVE_RESULT_FAIL_SCENARIO:
        scenario_set_error_locked(session, message ? message : "reactive_result_failed");
        branch->scenario_state = GM_ROOM_SCENARIO_ERROR;
        branch->reactive_action_start_index = 0;
        branch->reactive_action_count = 0;
        branch->reactive_current_action = 0;
        scenario_branch_clear_wait_fields(branch);
        gm_room_session_mark_session_changed_locked(session);
        return ESP_OK;
    case ROOM_SCENARIO_REACTIVE_RESULT_RETRY:
        return reactive_v2_fail_reaction_locked(session, branch, "reactive_retry_unsupported");
    case ROOM_SCENARIO_REACTIVE_RESULT_FAIL_REACTION:
    default:
        return reactive_v2_fail_reaction_locked(session, branch, message);
    }
}

esp_err_t gm_room_session_reactive_v2_handle_result_failure_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    const char *status,
    uint32_t now_ms)
{
    const room_scenario_branch_t *model = reactive_v2_model_branch(session, branch);
    room_scenario_reactive_result_action_t action = ROOM_SCENARIO_REACTIVE_RESULT_FAIL_REACTION;
    if (!session || !branch || !model) {
        return ESP_ERR_INVALID_ARG;
    }
    action = (status && strcmp(status, "timeout") == 0) ? model->result_on_timeout
                                                        : model->result_on_fail;
    return reactive_v2_apply_result_policy_locked(session,
                                                  branch,
                                                  model,
                                                  action,
                                                  now_ms,
                                                  status && status[0] ? status : "reactive_result_failed");
}

esp_err_t gm_room_session_reactive_v2_handle_result_success_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    uint32_t now_ms)
{
    const room_scenario_branch_t *model = reactive_v2_model_branch(session, branch);
    if (!session || !branch || !model) {
        return ESP_ERR_INVALID_ARG;
    }
    if (model->result_on_done != ROOM_SCENARIO_REACTIVE_RESULT_CONTINUE) {
        return reactive_v2_apply_result_policy_locked(session,
                                                      branch,
                                                      model,
                                                      model->result_on_done,
                                                      now_ms,
                                                      "reactive_result_done");
    }
    branch->reactive_current_action++;
    branch->scenario_state = GM_ROOM_SCENARIO_RUNNING;
    scenario_branch_clear_wait_fields(branch);
    return gm_room_session_reactive_v2_continue_locked(session, branch, now_ms);
}

esp_err_t gm_room_session_reactive_v2_continue_locked(gm_room_session_t *session,
                                                      gm_room_scenario_branch_runtime_t *branch,
                                                      uint32_t now_ms)
{
    const room_scenario_branch_t *model = reactive_v2_model_branch(session, branch);
    uint8_t budget = GM_REACTIVE_V2_MAX_ACTIONS_PER_TICK;
    if (!session || !branch || !model || !branch->active) {
        return ESP_ERR_INVALID_ARG;
    }
    while (budget-- > 0) {
        if (branch->reactive_current_action >= branch->reactive_action_count) {
            return reactive_v2_finish_locked(session, branch, model, now_ms);
        }
        uint16_t index = branch->reactive_action_start_index + branch->reactive_current_action;
        char command_error[96] = {0};
        if (index >= session->running_scenario.reactive_action_count) {
            scenario_set_error_locked(session, "reactive_action_range_invalid");
            return ESP_ERR_INVALID_ARG;
        }
        const room_scenario_reactive_action_t *action =
            &session->running_scenario.reactive_actions[index];
        switch (action->type) {
        case ROOM_SCENARIO_STEP_DEVICE_COMMAND: {
            gm_room_session_command_dispatch_t dispatch = {0};
            esp_err_t err = gm_room_session_execute_quest_device_command_internal(
                &action->data.device_command,
                command_error,
                sizeof(command_error),
                &dispatch);
            if (err != ESP_OK) {
                ESP_LOGW(TAG,
                         "reactive v2 action command failed: room=%s branch=%u action=%u device=%s command=%s err=0x%x reason=%s",
                         session->room_id,
                         (unsigned)branch->branch_index,
                         (unsigned)branch->reactive_current_action,
                         action->data.device_command.device_id,
                         action->data.device_command.command_id,
                         (unsigned)err,
                         command_error[0] ? command_error : "reactive_device_command_failed");
                scenario_set_error_locked(session,
                                          command_error[0] ? command_error
                                                           : "reactive_device_command_failed");
                return err;
            }
            if (dispatch.result_required) {
                scenario_enter_wait_command_result_locked(session, &dispatch, now_ms);
                gm_room_session_scenario_branch_save_from_session(branch, session);
                return ESP_OK;
            }
            branch->reactive_current_action++;
            break;
        }
        case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP: {
            esp_err_t err = reactive_v2_execute_command_group(session,
                                                              action,
                                                              command_error,
                                                              sizeof(command_error));
            if (err != ESP_OK) {
                scenario_set_error_locked(session,
                                          command_error[0] ? command_error
                                                           : "reactive_device_command_group_failed");
                return err;
            }
            branch->reactive_current_action++;
            break;
        }
        case ROOM_SCENARIO_STEP_WAIT_TIME:
            branch->scenario_state = GM_ROOM_SCENARIO_WAITING;
            branch->wait_type = GM_ROOM_SCENARIO_WAIT_TIME;
            branch->wait_started_at_ms = now_ms;
            branch->wait_until_ms = now_ms + action->data.wait_time.duration_ms;
            gm_room_session_mark_session_changed_locked(session);
            return ESP_OK;
        case ROOM_SCENARIO_STEP_SET_FLAG: {
            esp_err_t err = scenario_set_flag_locked(session,
                                                     action->data.set_flag.name,
                                                     action->data.set_flag.value);
            if (err != ESP_OK) {
                scenario_set_error_locked(session, "reactive_set_flag_failed");
                return err;
            }
            branch->reactive_current_action++;
            break;
        }
        case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
            quest_str_copy(session->scenario_operator_message,
                           sizeof(session->scenario_operator_message),
                           action->data.operator_message.message);
            branch->reactive_current_action++;
            break;
        default:
            scenario_set_error_locked(session, "reactive_action_unsupported");
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    gm_room_session_mark_session_changed_locked(session);
    return ESP_OK;
}

esp_err_t gm_room_session_reactive_v2_fire_locked(gm_room_session_t *session,
                                                  gm_room_scenario_branch_runtime_t *branch,
                                                  uint32_t now_ms)
{
    const room_scenario_branch_t *model = reactive_v2_model_branch(session, branch);
    const room_scenario_reactive_variant_t *variant = NULL;
    uint8_t variant_index = 0;
    if (!session || !branch || !model || !branch->active || model->variant_count == 0 ||
        (size_t)model->variant_start_index + model->variant_count >
            session->running_scenario.reactive_variant_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (branch->max_fire_count > 0 && branch->fire_count >= branch->max_fire_count) {
        return ESP_ERR_INVALID_STATE;
    }
    if (branch->scenario_state != GM_ROOM_SCENARIO_WAITING ||
        branch->wait_type != GM_ROOM_SCENARIO_WAIT_NONE ||
        (branch->cooldown_until_ms > 0 && !scenario_time_reached(now_ms, branch->cooldown_until_ms))) {
        if (branch->scenario_state != GM_ROOM_SCENARIO_COOLDOWN &&
            branch->reentry_mode == ROOM_SCENARIO_REENTRY_QUEUE_ONE) {
            branch->pending_trigger = true;
            gm_room_session_mark_session_changed_locked(session);
        }
        return ESP_OK;
    }
    variant_index = reactive_v2_select_variant(branch, model);
    variant = &session->running_scenario.reactive_variants[model->variant_start_index + variant_index];
    branch->last_variant_index = variant_index;
    branch->reactive_action_start_index = variant->action_start_index;
    branch->reactive_action_count = variant->action_count;
    branch->reactive_current_action = 0;
    branch->pending_trigger = false;
    branch->cooldown_until_ms = branch->cooldown_ms > 0 ? now_ms + branch->cooldown_ms : 0;
    branch->scenario_state = GM_ROOM_SCENARIO_RUNNING;
    scenario_branch_clear_wait_fields(branch);
    gm_room_session_mark_session_changed_locked(session);
    return gm_room_session_reactive_v2_continue_locked(session, branch, now_ms);
}
