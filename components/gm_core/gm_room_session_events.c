#include "gm_room_session_internal.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define GM_SCENARIO_MAX_STEPS_PER_TICK 8

static const char *TAG = "gm_room_session";

static const char *event_type_name(event_bus_type_t type)
{
    switch (type) {
    case EVENT_CARD_OK:
        return "card_ok";
    case EVENT_CARD_BAD:
        return "card_bad";
    case EVENT_RELAY_CMD:
        return "relay_cmd";
    case EVENT_AUDIO_PLAY:
        return "audio_play";
    case EVENT_AUDIO_FINISHED:
        return "audio_finished";
    case EVENT_VOLUME_SET:
        return "volume_set";
    case EVENT_WEB_COMMAND:
        return "web_command";
    case EVENT_SYSTEM_STATUS:
        return "system_status";
    case EVENT_SCENARIO_TRIGGER:
        return "scenario_trigger";
    case EVENT_DEVICE_CONFIG_CHANGED:
        return "device_config_changed";
    case EVENT_MQTT_MESSAGE:
        return "mqtt_message";
    case EVENT_FLAG_CHANGED:
        return "flag_changed";
    case EVENT_DEVICE_STATUS:
        return "device_status";
    case EVENT_DEVICE_RUNTIME:
        return "device_runtime";
    case EVENT_DEVICE_CONTROL:
        return "device_control";
    case EVENT_RUNTIME_CONTROL:
        return "runtime_control";
    case EVENT_NONE:
    default:
        return "";
    }
}

static const char *event_source_id(const event_bus_message_t *message)
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

static bool match_field(const char *expected, const char *actual)
{
    if (!expected || expected[0] == '\0') {
        return true;
    }
    return actual && strcmp(expected, actual) == 0;
}

static bool wait_event_type_matches(const char *expected, const event_bus_message_t *message)
{
    if (!message) {
        return false;
    }
    if (match_field(expected, event_type_name(message->type)) ||
        match_field(expected, message->topic) ||
        match_field(expected, message->payload)) {
        return true;
    }
    switch (message->payload_type) {
    case EVENT_BUS_PAYLOAD_DEVICE_STATUS:
        return match_field(expected, message->data.device_status.state) ||
               match_field(expected, message->data.device_status.health) ||
               match_field(expected, message->data.device_status.connectivity);
    case EVENT_BUS_PAYLOAD_DEVICE_RUNTIME:
        return match_field(expected, message->data.device_runtime.runtime_type) ||
               match_field(expected, message->data.device_runtime.state);
    case EVENT_BUS_PAYLOAD_DEVICE_CONTROL:
        return match_field(expected, message->data.device_control.action_id) ||
               match_field(expected, message->data.device_control.source);
    case EVENT_BUS_PAYLOAD_TEXT:
    default:
        return false;
    }
}

static int wait_event_match_index(const gm_room_session_t *session, const event_bus_message_t *message)
{
    const char *source_id = event_source_id(message);
    if (!session || !message) {
        return -1;
    }
    if (session->wait_event_count > 0) {
        for (uint8_t i = 0; i < session->wait_event_count &&
                            i < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS; ++i) {
            if (wait_event_type_matches(session->wait_events[i].event_type, message) &&
                match_field(session->wait_events[i].source_id, source_id)) {
                return i;
            }
        }
        return -1;
    }
    return (wait_event_type_matches(session->wait_event_type, message) &&
            match_field(session->wait_source_id, source_id))
               ? 0
               : -1;
}

static bool wait_all_device_events_met(const gm_room_session_t *session)
{
    if (!session || session->wait_event_count == 0) {
        return false;
    }
    for (uint8_t i = 0; i < session->wait_event_count &&
                        i < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS; ++i) {
        if (!session->wait_event_matched[i]) {
            return false;
        }
    }
    return true;
}

esp_err_t gm_room_session_scenario_on_event(const event_bus_message_t *message)
{
    uint32_t now_ms = gm_room_session_scenario_now_ms();
    bool matched_any = false;
    if (!message) {
        return ESP_ERR_INVALID_ARG;
    }
    if (gm_room_session_sessions_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < GM_SESSION_MAX_ROOMS; ++i) {
        gm_room_session_t *session = &g_gm_room_sessions[i];
        if (!session->in_use || !session->running_scenario_valid) {
            continue;
        }
        for (uint8_t branch_index = 0;
             branch_index < session->branch_runtime_count &&
             branch_index < ROOM_SCENARIO_MAX_BRANCHES;
             ++branch_index) {
            gm_room_scenario_branch_runtime_t *branch = &session->branch_runtimes[branch_index];
            if (!branch->active ||
                branch->scenario_state != GM_ROOM_SCENARIO_WAITING ||
                (branch->wait_type != GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT &&
                 branch->wait_type != GM_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT &&
                 branch->wait_type != GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS)) {
                continue;
            }
            gm_room_session_scenario_branch_load_into_session(session, branch);
            int match_index = wait_event_match_index(session, message);
            if (match_index < 0) {
                gm_room_session_scenario_branch_save_from_session(branch, session);
                continue;
            }

            ESP_LOGI(TAG,
                     "scenario event matched: room=%s branch=%u event=%s source=%s",
                     session->room_id,
                     (unsigned)branch->branch_index,
                     session->wait_event_type[0] ? session->wait_event_type : "*",
                     session->wait_source_id[0] ? session->wait_source_id : "*");
            if (session->wait_type == GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS) {
                const char *source_id = event_source_id(message);
                for (uint8_t event_index = 0;
                     event_index < session->wait_event_count &&
                     event_index < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS;
                     ++event_index) {
                    if (wait_event_type_matches(session->wait_events[event_index].event_type, message) &&
                        match_field(session->wait_events[event_index].source_id, source_id)) {
                        session->wait_event_matched[event_index] = true;
                    }
                }
                gm_room_session_mark_session_changed_locked(session);
                matched_any = true;
                if (!wait_all_device_events_met(session)) {
                    gm_room_session_scenario_branch_save_from_session(branch, session);
                    continue;
                }
            }
            session->current_step_index++;
            session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
            gm_room_session_scenario_clear_wait_locked(session);
            gm_room_session_mark_session_changed_locked(session);
            matched_any = true;
            gm_room_session_scenario_branch_save_from_session(branch, session);
            (void)gm_room_session_execute_branch_locked(session,
                                                        branch,
                                                        now_ms,
                                                        GM_SCENARIO_MAX_STEPS_PER_TICK);
        }
        gm_room_session_scenario_update_summary_from_branches_locked(session);
    }
    gm_room_session_sessions_unlock();
    return matched_any ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void gm_room_session_event_handler(const event_bus_message_t *message)
{
    event_bus_message_t copy = {0};
    if (!message) {
        return;
    }
    copy = *message;
    if (!s_event_queue) {
        return;
    }
    (void)xQueueSend(s_event_queue, &copy, 0);
}

void gm_room_session_event_task(void *ctx)
{
    (void)ctx;

    event_bus_message_t message = {0};
    while (true) {
        if (xQueueReceive(s_event_queue, &message, portMAX_DELAY) == pdTRUE) {
            (void)gm_room_session_scenario_on_event(&message);
        }
    }
}
