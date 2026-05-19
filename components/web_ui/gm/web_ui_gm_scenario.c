#include "web_ui_handlers.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "orch_runtime_view.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "scenehub_control.h"
#include "web_ui_gm_runtime_json_writer.h"
#include "web_ui_utils.h"

static const char *TAG = "web_ui_gm_scenario";
enum { GM_RUNTIME_JSON_CHUNK_CAPACITY = 2048 };

static int64_t gm_scenario_perf_start(void)
{
    return esp_timer_get_time();
}

static void gm_scenario_perf_log(const char *label, int64_t start_us, const char *room_id)
{
    int64_t dt_ms = (esp_timer_get_time() - start_us) / 1000;
    ESP_LOGD(TAG, "PERF %s room=%s took %lld ms", label ? label : "scenario", room_id ? room_id : "", dt_ms);
}

static void *gm_scenario_body_alloc(size_t size)
{
    return web_ui_calloc(1, size);
}

static void *gm_scenario_runtime_alloc(size_t size)
{
    return web_ui_malloc(size);
}

static esp_err_t gm_runtime_json_write_wait_events_field(gm_runtime_json_writer_t *writer,
                                                         bool *first,
                                                         const orch_room_wait_event_entry_t *events,
                                                         uint8_t count)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, "scenario_wait_events");
    if (err != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < count && i < ORCH_ROOM_SCENARIO_MAX_EVENT_REFS; ++i) {
        if (i > 0) {
            err = gm_runtime_json_write_raw(writer, ",");
            if (err != ESP_OK) {
                return err;
            }
        }
        err = gm_runtime_json_write_raw(writer, "{\"event_type\":");
        if (err != ESP_OK ||
            (err = gm_runtime_json_write_string(writer, events[i].event_type)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, ",\"source_id\":")) != ESP_OK ||
            (err = gm_runtime_json_write_string(writer, events[i].source_id)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t gm_runtime_json_write_flag_refs_field(gm_runtime_json_writer_t *writer,
                                                       bool *first,
                                                       const char *field_name,
                                                       const orch_room_scenario_flag_ref_t *flags,
                                                       uint8_t count)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, field_name);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < count && i < ORCH_ROOM_SCENARIO_MAX_FLAG_REFS; ++i) {
        if (i > 0) {
            err = gm_runtime_json_write_raw(writer, ",");
            if (err != ESP_OK) {
                return err;
            }
        }
        err = gm_runtime_json_write_raw(writer, "{\"name\":");
        if (err != ESP_OK ||
            (err = gm_runtime_json_write_string(writer, flags[i].name)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, ",\"value\":")) != ESP_OK ||
            (err = gm_runtime_json_write_bool(writer, flags[i].value)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t gm_runtime_json_write_flag_entries_field(gm_runtime_json_writer_t *writer,
                                                          bool *first,
                                                          const orch_room_scenario_flag_entry_t *flags,
                                                          uint8_t count)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, "scenario_flags");
    if (err != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < count && i < ORCH_ROOM_SCENARIO_MAX_FLAGS; ++i) {
        if (i > 0) {
            err = gm_runtime_json_write_raw(writer, ",");
            if (err != ESP_OK) {
                return err;
            }
        }
        err = gm_runtime_json_write_raw(writer, "{\"name\":");
        if (err != ESP_OK ||
            (err = gm_runtime_json_write_string(writer, flags[i].name)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, ",\"value\":")) != ESP_OK ||
            (err = gm_runtime_json_write_bool(writer, flags[i].value)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t gm_runtime_json_write_string_array_field(gm_runtime_json_writer_t *writer,
                                                          bool *first,
                                                          const char *field_name,
                                                          const char *items,
                                                          size_t item_stride,
                                                          uint8_t count,
                                                          uint8_t limit)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, field_name);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < count && i < limit; ++i) {
        const char *value = items + ((size_t)i * item_stride);
        if (i > 0) {
            err = gm_runtime_json_write_raw(writer, ",");
            if (err != ESP_OK) {
                return err;
            }
        }
        err = gm_runtime_json_write_string(writer, value);
        if (err != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t gm_runtime_json_write_branches_field(gm_runtime_json_writer_t *writer,
                                                      bool *first,
                                                      const orch_room_runtime_detail_view_t *view)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, "scenario_branches");
    if (err != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0;
         i < view->scenario_branch_count && i < ORCH_ROOM_SCENARIO_MAX_BRANCHES;
         ++i) {
        const orch_room_scenario_branch_entry_t *branch = &view->scenario_branches[i];
        bool branch_first = true;
        if (i > 0) {
            err = gm_runtime_json_write_raw(writer, ",");
            if (err != ESP_OK) {
                return err;
            }
        }
        err = gm_runtime_json_write_raw(writer, "{");
        if (err != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer, &branch_first, "index", i)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer, &branch_first, "id", branch->id)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer, &branch_first, "name", branch->name)) != ESP_OK ||
            (err = gm_runtime_json_write_bool_field(writer, &branch_first, "active", branch->active)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer, &branch_first, "type", branch->type_text)) != ESP_OK ||
            (err = gm_runtime_json_write_bool_field(writer,
                                                    &branch_first,
                                                    "required_for_completion",
                                                    branch->required_for_completion)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer, &branch_first, "priority", branch->priority)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer, &branch_first, "cooldown_ms", branch->cooldown_ms)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer,
                                                      &branch_first,
                                                      "cooldown_until_ms",
                                                      branch->cooldown_until_ms)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer,
                                                      &branch_first,
                                                      "max_fire_count",
                                                      branch->max_fire_count)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer, &branch_first, "fire_count", branch->fire_count)) != ESP_OK ||
            (err = gm_runtime_json_write_bool_field(writer, &branch_first, "run_once", branch->run_once)) != ESP_OK ||
            (err = gm_runtime_json_write_bool_field(writer, &branch_first, "fired_once", branch->fired_once)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer,
                                                      &branch_first,
                                                      "reentry_mode",
                                                      branch->reentry_mode_text)) != ESP_OK ||
            (err = gm_runtime_json_write_bool_field(writer,
                                                    &branch_first,
                                                    "pending_trigger",
                                                    branch->pending_trigger)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer,
                                                      &branch_first,
                                                      "step_start_index",
                                                      branch->step_start_index)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer, &branch_first, "step_count", branch->step_count)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer, &branch_first, "total_steps", branch->total_steps)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer,
                                                      &branch_first,
                                                      "current_step_index",
                                                      branch->current_step_index)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer,
                                                      &branch_first,
                                                      "current_step_local_index",
                                                      branch->current_local_step_index)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer, &branch_first, "done_steps", branch->done_steps)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer,
                                                      &branch_first,
                                                      "completed_step_count",
                                                      branch->done_steps)) != ESP_OK ||
            (err = gm_runtime_json_write_int32_field(writer,
                                                     &branch_first,
                                                     "failed_step_index",
                                                     branch->failed_step_index)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer,
                                                      &branch_first,
                                                      "current_step_text",
                                                      branch->current_step_text)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer,
                                                      &branch_first,
                                                      "current_step_state",
                                                      branch->current_step_state_text)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer, &branch_first, "state", branch->state_text)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer,
                                                      &branch_first,
                                                      "wait_type",
                                                      branch->wait_type_text)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer,
                                                      &branch_first,
                                                      "wait_summary",
                                                      branch->wait_summary)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer,
                                                      &branch_first,
                                                      "wait_until_ms",
                                                      branch->wait_until_ms)) != ESP_OK ||
            (err = gm_runtime_json_write_uint64_field(writer,
                                                      &branch_first,
                                                      "wait_started_at_ms",
                                                      branch->wait_started_at_ms)) != ESP_OK ||
            (err = gm_runtime_json_write_bool_field(writer,
                                                    &branch_first,
                                                    "wait_operator_skip_allowed",
                                                    branch->wait_operator_skip_allowed)) != ESP_OK ||
            (err = gm_runtime_json_write_string_field(writer,
                                                      &branch_first,
                                                      "wait_operator_skip_label",
                                                      branch->wait_operator_skip_label)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t gm_runtime_json_write_summary_fields(gm_runtime_json_writer_t *writer,
                                                      bool *first,
                                                      const orch_room_runtime_summary_view_t *summary)
{
    esp_err_t err = ESP_OK;

    if (!writer || !first || !summary) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((err = gm_runtime_json_write_string_field(writer, first, "room_id", summary->room_id)) != ESP_OK ||
        (err = gm_runtime_json_write_bool_field(writer, first, "session_present", summary->session_present)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "session_state", summary->session_state[0] ? summary->session_state : "idle")) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "timer_state", summary->timer_state[0] ? summary->timer_state : "idle")) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "timer_duration_ms", summary->timer_duration_ms)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "timer_remaining_ms", summary->timer_remaining_ms)) != ESP_OK ||
        (err = gm_runtime_json_write_bool_field(writer, first, "hint_active", summary->hint_active)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "hint_sent_count", summary->hint_sent_count)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "hint_message", summary->hint_message)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "selected_profile_id", summary->selected_profile_id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "selected_profile_name", summary->selected_profile_name)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "selected_profile_scenario_id", summary->selected_profile_scenario_id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "selected_scenario_id", summary->selected_scenario_id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "selected_scenario_name", summary->selected_scenario_name)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "running_scenario_id", summary->running_scenario_id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "running_scenario_name", summary->running_scenario_name)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "running_scenario_generation", summary->running_scenario_generation)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "runtime_now_ms", summary->runtime_now_ms)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "scenario_runtime_state", summary->scenario_runtime_state_text)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "scenario_total_steps", summary->scenario_total_steps)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "scenario_done_steps", summary->scenario_done_steps)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "scenario_current_step_text", summary->scenario_current_step_text)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "scenario_wait_type", summary->scenario_wait_type_text)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "scenario_wait_summary", summary->scenario_wait_summary)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "scenario_wait_until_ms", summary->scenario_wait_until_ms)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "scenario_wait_started_at_ms", summary->scenario_wait_started_at_ms)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer,
                                                  first,
                                                  "scenario_wait_operator_prompt",
                                                  summary->scenario_wait_operator_prompt)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer,
                                                  first,
                                                  "scenario_wait_operator_label",
                                                  summary->scenario_wait_operator_label)) != ESP_OK ||
        (err = gm_runtime_json_write_bool_field(writer,
                                                first,
                                                "scenario_wait_operator_skip_allowed",
                                                summary->scenario_wait_operator_skip_allowed)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer,
                                                  first,
                                                  "scenario_wait_operator_skip_label",
                                                  summary->scenario_wait_operator_skip_label)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer,
                                                  first,
                                                  "scenario_operator_message",
                                                  summary->scenario_operator_message)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "scenario_device_count", summary->scenario_device_count)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer, first, "scenario_last_error", summary->scenario_last_error)) != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

static esp_err_t gm_runtime_json_write_detail_fields(gm_runtime_json_writer_t *writer,
                                                     bool *first,
                                                     const orch_room_runtime_detail_view_t *view)
{
    esp_err_t err = ESP_OK;

    if (!writer || !first || !view) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((err = gm_runtime_json_write_wait_events_field(writer,
                                                       first,
                                                       view->scenario_wait_events,
                                                       view->scenario_wait_event_count)) != ESP_OK ||
        (err = gm_runtime_json_write_flag_refs_field(writer,
                                                     first,
                                                     "scenario_wait_flags",
                                                     view->scenario_wait_flags,
                                                     view->scenario_wait_flag_count)) != ESP_OK ||
        (err = gm_runtime_json_write_flag_entries_field(writer,
                                                        first,
                                                        view->scenario_flags,
                                                        view->scenario_flag_count)) != ESP_OK ||
        (err = gm_runtime_json_write_string_array_field(writer,
                                                        first,
                                                        "scenario_device_ids",
                                                        (const char *)view->scenario_device_ids,
                                                        sizeof(view->scenario_device_ids[0]),
                                                        view->summary.scenario_device_count,
                                                        ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS)) != ESP_OK ||
        (err = gm_runtime_json_write_string_array_field(writer,
                                                        first,
                                                        "related_issue_ids",
                                                        (const char *)view->related_issue_ids,
                                                        sizeof(view->related_issue_ids[0]),
                                                        view->related_issue_count,
                                                        ORCH_REGISTRY_MAX_ISSUES)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer,
                                                  first,
                                                  "related_issue_count",
                                                  view->related_issue_count)) != ESP_OK ||
        (err = gm_runtime_json_write_branches_field(writer, first, view)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(writer,
                                                  first,
                                                  "asset_prepare_state",
                                                  view->asset_prepare_state[0]
                                                      ? view->asset_prepare_state
                                                      : "none")) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "asset_audio_total", view->asset_audio_total)) !=
            ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "asset_audio_ready", view->asset_audio_ready)) !=
            ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "asset_audio_missing", view->asset_audio_missing)) !=
            ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer, first, "asset_audio_bad", view->asset_audio_bad)) !=
            ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer,
                                                  first,
                                                  "asset_audio_unsupported",
                                                  view->asset_audio_unsupported)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer,
                                                  first,
                                                  "asset_audio_io_error",
                                                  view->asset_audio_io_error)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(writer,
                                                  first,
                                                  "asset_audio_unknown",
                                                  view->asset_audio_unknown)) != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

static esp_err_t gm_scenario_send_runtime_state_json(httpd_req_t *req,
                                                     const orch_room_runtime_detail_view_t *view,
                                                     char *chunk,
                                                     size_t chunk_capacity)
{
    gm_runtime_json_writer_t writer = {
        .req = req,
        .chunk = chunk,
        .capacity = chunk_capacity,
        .len = 0,
    };
    bool first = true;
    esp_err_t err = ESP_OK;

    if (!req || !view) {
        return ESP_ERR_INVALID_ARG;
    }
    err = httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_write_raw(&writer, "{");
    if (err != ESP_OK ||
        (err = gm_runtime_json_write_bool_field(&writer, &first, "ok", true)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &first, "runtime_schema_version", 1)) != ESP_OK ||
        (err = gm_runtime_json_write_summary_fields(&writer, &first, &view->summary)) != ESP_OK ||
        (err = gm_runtime_json_write_detail_fields(&writer, &first, view)) != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_write_raw(&writer, "}");
    if (err != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_flush(&writer);
    if (err != ESP_OK) {
        return err;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t gm_scenario_send_runtime_summary_json(httpd_req_t *req,
                                                       const orch_room_runtime_summary_view_t *summary,
                                                       char *chunk,
                                                       size_t chunk_capacity)
{
    gm_runtime_json_writer_t writer = {
        .req = req,
        .chunk = chunk,
        .capacity = chunk_capacity,
        .len = 0,
    };
    bool first = true;
    esp_err_t err = ESP_OK;

    if (!req || !summary) {
        return ESP_ERR_INVALID_ARG;
    }
    err = httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_write_raw(&writer, "{");
    if (err != ESP_OK ||
        (err = gm_runtime_json_write_bool_field(&writer, &first, "ok", true)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &first, "runtime_schema_version", 1)) != ESP_OK ||
        (err = gm_runtime_json_write_summary_fields(&writer, &first, summary)) != ESP_OK ||
        (err = gm_runtime_json_write_raw(&writer, "}")) != ESP_OK ||
        (err = gm_runtime_json_flush(&writer)) != ESP_OK) {
        return err;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t gm_scenario_send_rooms_runtime_summary_json(httpd_req_t *req,
                                                             orch_room_runtime_summary_view_t *summary,
                                                             char *chunk,
                                                             size_t chunk_capacity)
{
    gm_runtime_json_writer_t writer = {
        .req = req,
        .chunk = chunk,
        .capacity = chunk_capacity,
        .len = 0,
    };
    bool first = true;
    esp_err_t err = ESP_OK;
    size_t room_count = 0;
    bool emitted_any = false;

    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    err = httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_write_raw(&writer, "{");
    if (err != ESP_OK ||
        (err = gm_runtime_json_write_bool_field(&writer, &first, "ok", true)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &first, "runtime_schema_version", 1)) != ESP_OK ||
        (err = gm_runtime_json_begin_field(&writer, &first, "rooms")) != ESP_OK ||
        (err = gm_runtime_json_write_raw(&writer, "[")) != ESP_OK) {
        return err;
    }
    room_count = room_catalog_count();
    if (room_count > ORCH_REGISTRY_MAX_ROOMS) {
        room_count = ORCH_REGISTRY_MAX_ROOMS;
    }
    for (size_t i = 0; i < room_count; ++i) {
        room_catalog_entry_t room = {0};
        bool room_first = true;

        if (room_catalog_get(i, &room) != ESP_OK) {
            continue;
        }
        memset(summary, 0, sizeof(*summary));
        err = orchestrator_registry_get_room_runtime_summary_view(room.room_id, summary);
        if (err == ESP_ERR_NOT_FOUND) {
            continue;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (emitted_any && (err = gm_runtime_json_write_raw(&writer, ",")) != ESP_OK) {
            return err;
        }
        if ((err = gm_runtime_json_write_raw(&writer, "{")) != ESP_OK ||
            (err = gm_runtime_json_write_summary_fields(&writer,
                                                        &room_first,
                                                        summary)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(&writer, "}")) != ESP_OK) {
            return err;
        }
        emitted_any = true;
    }
    if ((err = gm_runtime_json_write_raw(&writer, "]")) != ESP_OK ||
        (err = gm_runtime_json_write_raw(&writer, "}")) != ESP_OK ||
        (err = gm_runtime_json_flush(&writer)) != ESP_OK) {
        return err;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static bool gm_scenario_read_query_value(httpd_req_t *req, const char *key, char *out, size_t out_size)
{
    char query[256] = {0};
    if (!req || !key || !out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, out_size) == ESP_OK;
}

static esp_err_t gm_scenario_read_body(httpd_req_t *req, char **out_body)
{
    char *body = NULL;
    size_t received = 0;
    if (!req || !out_body) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_body = NULL;
    if (req->content_len <= 0 || req->content_len > 512) {
        return ESP_ERR_INVALID_SIZE;
    }
    body = gm_scenario_body_alloc((size_t)req->content_len + 1);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    while (received < (size_t)req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            web_ui_free(body);
            return ESP_FAIL;
        }
        received += (size_t)r;
    }
    body[received] = '\0';
    *out_body = body;
    return ESP_OK;
}

static esp_err_t gm_scenario_send_error(httpd_req_t *req, esp_err_t err)
{
    return web_ui_send_scenehub_control_error(req, err, NULL, "scenario select failed");
}

static esp_err_t gm_scenario_send_control_error(httpd_req_t *req,
                                                esp_err_t call_err,
                                                const scenehub_control_result_t *result)
{
    return web_ui_send_scenehub_control_error(req, call_err, result, "scenario select failed");
}

static esp_err_t gm_scenario_send_runtime_state(httpd_req_t *req, const char *room_id)
{
    char detail[16] = {0};
    char include_assets_value[8] = {0};
    bool summary_only = false;
    bool include_assets = true;
    char *chunk = NULL;
    esp_err_t err = ESP_OK;

    if (gm_scenario_read_query_value(req, "detail", detail, sizeof(detail)) && detail[0]) {
        summary_only = strcmp(detail, "summary") == 0;
    }
    if (gm_scenario_read_query_value(req, "include_assets", include_assets_value, sizeof(include_assets_value)) &&
        include_assets_value[0]) {
        include_assets = strcmp(include_assets_value, "0") != 0 &&
                         strcmp(include_assets_value, "false") != 0;
    }
    ESP_LOGD(TAG, "room_runtime room_id=%s summary=%d", room_id ? room_id : "", summary_only);

    chunk = gm_scenario_runtime_alloc(GM_RUNTIME_JSON_CHUNK_CAPACITY);
    if (!chunk) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "runtime alloc failed");
    }

    if (summary_only) {
        orch_room_runtime_summary_view_t *summary =
            gm_scenario_runtime_alloc(sizeof(orch_room_runtime_summary_view_t));
        if (!summary) {
            web_ui_free(chunk);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "runtime alloc failed");
        }
        memset(summary, 0, sizeof(*summary));
        err = orchestrator_registry_get_room_runtime_summary_view(room_id, summary);
        if (err != ESP_OK) {
            web_ui_free(summary);
            web_ui_free(chunk);
            return gm_scenario_send_error(req, err);
        }
        err = gm_scenario_send_runtime_summary_json(req,
                                                    summary,
                                                    chunk,
                                                    GM_RUNTIME_JSON_CHUNK_CAPACITY);
        ESP_LOGD(TAG, "room_runtime summary done room_id=%s err=%s", room_id ? room_id : "", esp_err_to_name(err));
        web_ui_free(summary);
        web_ui_free(chunk);
        return err;
    }

    orch_room_runtime_detail_view_t *view =
        gm_scenario_runtime_alloc(sizeof(orch_room_runtime_detail_view_t));
    if (!view) {
        web_ui_free(chunk);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "runtime alloc failed");
    }
    memset(view, 0, sizeof(*view));
    err = orchestrator_registry_get_room_runtime_detail_view(room_id, include_assets, view);
    if (err != ESP_OK) {
        web_ui_free(view);
        web_ui_free(chunk);
        return gm_scenario_send_error(req, err);
    }
    err = gm_scenario_send_runtime_state_json(req, view, chunk, GM_RUNTIME_JSON_CHUNK_CAPACITY);
    ESP_LOGD(TAG, "room_runtime detail done room_id=%s err=%s", room_id ? room_id : "", esp_err_to_name(err));
    web_ui_free(view);
    web_ui_free(chunk);
    return err;
}

typedef esp_err_t (*gm_scenario_control_fn_t)(const char *source,
                                              const char *room_id,
                                              scenehub_control_result_t *out_result);

static esp_err_t gm_scenario_runtime_handler(httpd_req_t *req, gm_scenario_control_fn_t fn)
{
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN] = {0};
    int64_t t0 = gm_scenario_perf_start();
    scenehub_control_result_t result = {0};
    if (!gm_scenario_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    esp_err_t err = fn("http", room_id, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_scenario_send_control_error(req, err, &result);
    }
    gm_scenario_perf_log("POST scenario runtime", t0, room_id);
    return web_ui_send_scenehub_control_ack(req);
}

esp_err_t gm_room_runtime_state_handler(httpd_req_t *req)
{
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN] = {0};
    if (!gm_scenario_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    return gm_scenario_send_runtime_state(req, room_id);
}

esp_err_t gm_rooms_runtime_summary_handler(httpd_req_t *req)
{
    esp_err_t err = ESP_OK;
    orch_room_runtime_summary_view_t *summary = NULL;
    char *chunk = NULL;

    if (room_catalog_init() != ESP_OK || room_catalog_refresh() != ESP_OK) {
        return gm_scenario_send_error(req, ESP_FAIL);
    }

    summary = gm_scenario_runtime_alloc(sizeof(orch_room_runtime_summary_view_t));
    chunk = gm_scenario_runtime_alloc(GM_RUNTIME_JSON_CHUNK_CAPACITY);
    if (!summary || !chunk) {
        web_ui_free(summary);
        web_ui_free(chunk);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "runtime alloc failed");
    }
    err = gm_scenario_send_rooms_runtime_summary_json(req,
                                                      summary,
                                                      chunk,
                                                      GM_RUNTIME_JSON_CHUNK_CAPACITY);
    web_ui_free(summary);
    web_ui_free(chunk);
    if (err != ESP_OK) {
        return gm_scenario_send_error(req, err);
    }
    return err;
}

esp_err_t gm_room_scenario_select_handler(httpd_req_t *req)
{
    char *body = NULL;
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN] = {0};
    char scenario_id[ROOM_SCENARIO_ID_MAX_LEN] = {0};
    cJSON *json = NULL;
    const cJSON *room_id_item = NULL;
    const cJSON *scenario_id_item = NULL;
    esp_err_t err = gm_scenario_read_body(req, &body);

    if (err != ESP_OK) {
        return gm_scenario_send_error(req, err);
    }
    json = cJSON_Parse(body);
    web_ui_free(body);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    room_id_item = cJSON_GetObjectItem(json, "room_id");
    scenario_id_item = cJSON_GetObjectItem(json, "scenario_id");
    if (!cJSON_IsString(room_id_item) || !room_id_item->valuestring || !room_id_item->valuestring[0] ||
        !cJSON_IsString(scenario_id_item) || !scenario_id_item->valuestring ||
        !scenario_id_item->valuestring[0]) {
        cJSON_Delete(json);
        return gm_scenario_send_error(req, ESP_ERR_INVALID_ARG);
    }
    snprintf(room_id, sizeof(room_id), "%s", room_id_item->valuestring);
    snprintf(scenario_id, sizeof(scenario_id), "%s", scenario_id_item->valuestring);

    scenehub_control_result_t result = {0};
    err = scenehub_control_select_scenario("http", room_id, scenario_id, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        cJSON_Delete(json);
        return gm_scenario_send_control_error(req, err, &result);
    }
    cJSON_Delete(json);
    return web_ui_send_selection_result_json(req,
                                             "room_id",
                                             room_id,
                                             "selected_scenario_id",
                                             scenario_id);
}

esp_err_t gm_room_scenario_start_handler(httpd_req_t *req)
{
    return gm_scenario_runtime_handler(req, scenehub_control_scenario_start);
}

esp_err_t gm_room_scenario_stop_handler(httpd_req_t *req)
{
    return gm_scenario_runtime_handler(req, scenehub_control_scenario_stop);
}

esp_err_t gm_room_scenario_next_handler(httpd_req_t *req)
{
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN] = {0};
    char branch_id[ROOM_SCENARIO_BRANCH_ID_MAX_LEN] = {0};
    int64_t t0 = gm_scenario_perf_start();
    scenehub_control_result_t result = {0};
    if (!gm_scenario_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    (void)gm_scenario_read_query_value(req, "branch_id", branch_id, sizeof(branch_id));
    esp_err_t err = scenehub_control_scenario_next("http", room_id, branch_id, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_scenario_send_control_error(req, err, &result);
    }
    gm_scenario_perf_log("POST scenario next", t0, room_id);
    return web_ui_send_scenehub_control_ack(req);
}

esp_err_t gm_room_scenario_approve_handler(httpd_req_t *req)
{
    return gm_scenario_runtime_handler(req, scenehub_control_scenario_approve);
}

esp_err_t gm_room_scenario_reset_handler(httpd_req_t *req)
{
    return gm_scenario_runtime_handler(req, scenehub_control_scenario_reset);
}
