#include "device_control_ingest_internal.h"

#include <string.h>

#include "esp_log.h"
#include "event_bus.h"
#include "quest_common_utils.h"

static const char *TAG = "control_ingest";

void dci_post_status_event(const dci_event_snapshot_t *state)
{
    scenehub_event_t msg = {0};
    esp_err_t err = ESP_OK;
    if (!state || !state->device_id[0]) {
        return;
    }

    err = scenehub_event_make_device_status(&msg,
                                            state->device_id,
                                            "online",
                                            state->status_health[0] ? state->status_health : "unknown",
                                            state->status_state[0] ? state->status_state : "unknown",
                                            state->last_seen_ms);
    if (err != ESP_OK) {
        return;
    }
    (void)event_bus_post_priority(&msg, EVENT_BUS_PRIORITY_HIGH, 0);
}

void dci_post_runtime_event(const dci_event_snapshot_t *state)
{
    scenehub_event_t msg = {0};
    esp_err_t err = ESP_OK;
    if (!state || !state->device_id[0] || !state->has_status) {
        return;
    }

    err = scenehub_event_make_device_runtime(&msg,
                                             state->device_id,
                                             "control_status",
                                             state->status_state[0] ? state->status_state : "unknown",
                                             state->status_runtime_active,
                                             state->last_seen_ms);
    if (err != ESP_OK) {
        return;
    }
    (void)event_bus_post(&msg, 0);
}

void dci_post_control_event(const dci_event_snapshot_t *state)
{
    scenehub_event_t msg = {0};
    esp_err_t err = ESP_OK;
    if (!state || !state->device_id[0]) {
        return;
    }

    if (state->control_is_event) {
        if (!state->event_name[0]) {
            ESP_LOGW(TAG, "dropping device event without parsed action: device=%s args=%s",
                     state->device_id,
                     state->event_args_json);
            return;
        }
        err = scenehub_event_make_device_control_event(&msg,
                                                       state->device_id,
                                                       state->event_name,
                                                       state->event_args_json,
                                                       state->last_seen_ms);
        if (err != ESP_OK) {
            return;
        }
        ESP_LOGD(TAG,
                 "publish device event: device=%s action=%s args=%s",
                 state->device_id,
                 msg.data.device_control.action_id,
                 msg.data.device_control.args_json);
    } else {
        if (!state->has_result) {
            return;
        }
        err = scenehub_event_make_device_control_result(&msg,
                                                        state->device_id,
                                                        state->result_request_id[0] ? state->result_request_id : state->result_command,
                                                        state->result_status,
                                                        state->last_seen_ms);
        if (err != ESP_OK) {
            return;
        }
    }
    (void)event_bus_post_priority(&msg, EVENT_BUS_PRIORITY_HIGH, 0);
}

void dci_capture_event_snapshot(const device_control_ingest_device_t *state,
                                bool control_is_event,
                                dci_event_snapshot_t *out)
{
    if (!state || !out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    quest_str_copy(out->device_id, sizeof(out->device_id), state->device_id);
    out->last_seen_ms = state->last_seen_ms;
    out->has_status = state->has_status;
    quest_str_copy(out->status_state, sizeof(out->status_state), state->status_state);
    quest_str_copy(out->status_health, sizeof(out->status_health), state->status_health);
    out->status_runtime_active = state->status_runtime_active;
    out->control_is_event = control_is_event;
    out->has_result = state->has_result;
    quest_str_copy(out->result_request_id,
                   sizeof(out->result_request_id),
                   state->result_request_id);
    quest_str_copy(out->result_command,
                   sizeof(out->result_command),
                   state->result_command);
    quest_str_copy(out->result_status,
                   sizeof(out->result_status),
                   state->result_status);
    if (state->has_event) {
        quest_str_copy(out->event_name,
                       sizeof(out->event_name),
                       state->event_name);
        quest_str_copy(out->event_args_json,
                       sizeof(out->event_args_json),
                       state->event_args_json);
    }
}
