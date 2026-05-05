#include "room_scenario_internal.h"

#include <strings.h>

bool room_scenario_valid_step_type(room_scenario_step_type_t type)
{
    switch (type) {
    case ROOM_SCENARIO_STEP_WAIT_TIME:
    case ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
    case ROOM_SCENARIO_STEP_SET_FLAG:
    case ROOM_SCENARIO_STEP_WAIT_FLAGS:
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
    case ROOM_SCENARIO_STEP_END_GAME:
        return true;
    default:
        return false;
    }
}

const char *room_scenario_step_type_to_str(room_scenario_step_type_t type)
{
    switch (type) {
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        return "WAIT_TIME";
    case ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        return "OPERATOR_APPROVAL";
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        return "DEVICE_COMMAND";
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        return "WAIT_DEVICE_EVENT";
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        return "DEVICE_COMMAND_GROUP";
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        return "SHOW_OPERATOR_MESSAGE";
    case ROOM_SCENARIO_STEP_SET_FLAG:
        return "SET_FLAG";
    case ROOM_SCENARIO_STEP_WAIT_FLAGS:
        return "WAIT_FLAGS";
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        return "WAIT_ANY_DEVICE_EVENT";
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        return "WAIT_ALL_DEVICE_EVENTS";
    case ROOM_SCENARIO_STEP_END_GAME:
        return "END_GAME";
    default:
        return "UNKNOWN";
    }
}

const char *room_scenario_branch_type_to_str(room_scenario_branch_type_t type)
{
    switch (type) {
    case ROOM_SCENARIO_BRANCH_REACTIVE:
        return "reactive";
    case ROOM_SCENARIO_BRANCH_NORMAL:
    default:
        return "normal";
    }
}

const char *room_scenario_reentry_mode_to_str(room_scenario_reentry_mode_t mode)
{
    switch (mode) {
    case ROOM_SCENARIO_REENTRY_QUEUE_ONE:
        return "queue_one";
    case ROOM_SCENARIO_REENTRY_RESTART:
        return "restart";
    case ROOM_SCENARIO_REENTRY_PARALLEL:
        return "parallel";
    case ROOM_SCENARIO_REENTRY_IGNORE:
    default:
        return "ignore";
    }
}

esp_err_t room_scenario_reentry_mode_from_str(const char *s,
                                              room_scenario_reentry_mode_t *out)
{
    if (!s || !s[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcasecmp(s, "ignore") == 0) {
        *out = ROOM_SCENARIO_REENTRY_IGNORE;
        return ESP_OK;
    }
    if (strcasecmp(s, "queue_one") == 0 ||
        strcasecmp(s, "queue-one") == 0 ||
        strcasecmp(s, "queueOne") == 0) {
        *out = ROOM_SCENARIO_REENTRY_QUEUE_ONE;
        return ESP_OK;
    }
    if (strcasecmp(s, "restart") == 0) {
        *out = ROOM_SCENARIO_REENTRY_RESTART;
        return ESP_OK;
    }
    if (strcasecmp(s, "parallel") == 0) {
        *out = ROOM_SCENARIO_REENTRY_PARALLEL;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t room_scenario_branch_type_from_str(const char *s,
                                             room_scenario_branch_type_t *out)
{
    if (!s || !s[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcasecmp(s, "normal") == 0 ||
        strcasecmp(s, "flow") == 0 ||
        strcasecmp(s, "scenario") == 0) {
        *out = ROOM_SCENARIO_BRANCH_NORMAL;
        return ESP_OK;
    }
    if (strcasecmp(s, "reactive") == 0 ||
        strcasecmp(s, "reaction") == 0) {
        *out = ROOM_SCENARIO_BRANCH_REACTIVE;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t room_scenario_step_type_from_str(const char *s,
                                           room_scenario_step_type_t *out)
{
    if (!s || !s[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcasecmp(s, "WAIT_TIME") == 0 ||
        strcasecmp(s, "wait_time") == 0) {
        *out = ROOM_SCENARIO_STEP_WAIT_TIME;
        return ESP_OK;
    }
    if (strcasecmp(s, "OPERATOR_APPROVAL") == 0 ||
        strcasecmp(s, "operator_approval") == 0 ||
        strcasecmp(s, "MANUAL_GATE") == 0) {
        *out = ROOM_SCENARIO_STEP_OPERATOR_APPROVAL;
        return ESP_OK;
    }
    if (strcasecmp(s, "DEVICE_COMMAND") == 0 ||
        strcasecmp(s, "device_command") == 0 ||
        strcasecmp(s, "SEND_DEVICE_COMMAND") == 0) {
        *out = ROOM_SCENARIO_STEP_DEVICE_COMMAND;
        return ESP_OK;
    }
    if (strcasecmp(s, "WAIT_DEVICE_EVENT") == 0 ||
        strcasecmp(s, "wait_device_event") == 0) {
        *out = ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT;
        return ESP_OK;
    }
    if (strcasecmp(s, "DEVICE_COMMAND_GROUP") == 0 ||
        strcasecmp(s, "device_command_group") == 0) {
        *out = ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP;
        return ESP_OK;
    }
    if (strcasecmp(s, "SHOW_OPERATOR_MESSAGE") == 0 ||
        strcasecmp(s, "show_operator_message") == 0 ||
        strcasecmp(s, "OPERATOR_MESSAGE") == 0) {
        *out = ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE;
        return ESP_OK;
    }
    if (strcasecmp(s, "SET_FLAG") == 0 ||
        strcasecmp(s, "set_flag") == 0) {
        *out = ROOM_SCENARIO_STEP_SET_FLAG;
        return ESP_OK;
    }
    if (strcasecmp(s, "WAIT_FLAGS") == 0 ||
        strcasecmp(s, "wait_flags") == 0) {
        *out = ROOM_SCENARIO_STEP_WAIT_FLAGS;
        return ESP_OK;
    }
    if (strcasecmp(s, "WAIT_ANY_DEVICE_EVENT") == 0 ||
        strcasecmp(s, "wait_any_device_event") == 0) {
        *out = ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT;
        return ESP_OK;
    }
    if (strcasecmp(s, "WAIT_ALL_DEVICE_EVENTS") == 0 ||
        strcasecmp(s, "wait_all_device_events") == 0) {
        *out = ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS;
        return ESP_OK;
    }
    if (strcasecmp(s, "END_GAME") == 0 ||
        strcasecmp(s, "end_game") == 0 ||
        strcasecmp(s, "FINISH_GAME") == 0 ||
        strcasecmp(s, "finish_game") == 0) {
        *out = ROOM_SCENARIO_STEP_END_GAME;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}
