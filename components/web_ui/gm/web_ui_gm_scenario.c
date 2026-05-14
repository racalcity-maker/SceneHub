#include "web_ui_handlers.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "orchestrator_registry.h"
#include "room_scenario.h"
#include "scenehub_control.h"
#include "web_ui_utils.h"

static const char *TAG = "web_ui_gm_scenario";
static EXT_RAM_BSS_ATTR orch_room_runtime_view_t s_room_runtime_view_scratch;
static EXT_RAM_BSS_ATTR char s_room_runtime_json_chunk[2048];
static SemaphoreHandle_t s_room_runtime_view_mutex = NULL;
static StaticSemaphore_t s_room_runtime_view_mutex_storage;

static esp_err_t gm_room_runtime_view_lock(void)
{
    if (!s_room_runtime_view_mutex) {
        s_room_runtime_view_mutex = xSemaphoreCreateMutexStatic(&s_room_runtime_view_mutex_storage);
    }
    if (!s_room_runtime_view_mutex) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_room_runtime_view_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void gm_room_runtime_view_unlock(void)
{
    if (s_room_runtime_view_mutex) {
        xSemaphoreGive(s_room_runtime_view_mutex);
    }
}

static int64_t gm_scenario_perf_start(void)
{
    return esp_timer_get_time();
}

static void gm_scenario_perf_log(const char *label, int64_t start_us, const char *room_id)
{
    int64_t dt_ms = (esp_timer_get_time() - start_us) / 1000;
    ESP_LOGW(TAG, "PERF %s room=%s took %lld ms", label ? label : "scenario", room_id ? room_id : "", dt_ms);
}

static void *gm_scenario_body_alloc(size_t size)
{
    return web_ui_calloc(1, size);
}

typedef struct {
    httpd_req_t *req;
    size_t len;
} gm_runtime_json_writer_t;

static esp_err_t gm_runtime_json_flush(gm_runtime_json_writer_t *writer)
{
    if (!writer || !writer->req) {
        return ESP_ERR_INVALID_ARG;
    }
    if (writer->len == 0) {
        return ESP_OK;
    }
    esp_err_t err = httpd_resp_send_chunk(writer->req, s_room_runtime_json_chunk, writer->len);
    if (err != ESP_OK) {
        return err;
    }
    writer->len = 0;
    return ESP_OK;
}

static esp_err_t gm_runtime_json_write_len(gm_runtime_json_writer_t *writer,
                                           const char *data,
                                           size_t len)
{
    if (!writer || (!data && len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    while (len > 0) {
        size_t space = sizeof(s_room_runtime_json_chunk) - writer->len;
        if (space == 0) {
            esp_err_t err = gm_runtime_json_flush(writer);
            if (err != ESP_OK) {
                return err;
            }
            space = sizeof(s_room_runtime_json_chunk);
        }
        size_t chunk = len < space ? len : space;
        memcpy(s_room_runtime_json_chunk + writer->len, data, chunk);
        writer->len += chunk;
        data += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

static esp_err_t gm_runtime_json_write_raw(gm_runtime_json_writer_t *writer, const char *text)
{
    if (!text) {
        text = "";
    }
    return gm_runtime_json_write_len(writer, text, strlen(text));
}

static esp_err_t gm_runtime_json_write_uint64(gm_runtime_json_writer_t *writer, uint64_t value)
{
    char buf[32];
    int written = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    if (written <= 0 || (size_t)written >= sizeof(buf)) {
        return ESP_FAIL;
    }
    return gm_runtime_json_write_len(writer, buf, (size_t)written);
}

static esp_err_t gm_runtime_json_write_int32(gm_runtime_json_writer_t *writer, int32_t value)
{
    char buf[24];
    int written = snprintf(buf, sizeof(buf), "%ld", (long)value);
    if (written <= 0 || (size_t)written >= sizeof(buf)) {
        return ESP_FAIL;
    }
    return gm_runtime_json_write_len(writer, buf, (size_t)written);
}

static esp_err_t gm_runtime_json_write_bool(gm_runtime_json_writer_t *writer, bool value)
{
    return gm_runtime_json_write_raw(writer, value ? "true" : "false");
}

static esp_err_t gm_runtime_json_write_string(gm_runtime_json_writer_t *writer, const char *value)
{
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    esp_err_t err = gm_runtime_json_write_raw(writer, "\"");
    if (err != ESP_OK) {
        return err;
    }
    for (; *p; ++p) {
        switch (*p) {
            case '\"':
                err = gm_runtime_json_write_raw(writer, "\\\"");
                break;
            case '\\':
                err = gm_runtime_json_write_raw(writer, "\\\\");
                break;
            case '\b':
                err = gm_runtime_json_write_raw(writer, "\\b");
                break;
            case '\f':
                err = gm_runtime_json_write_raw(writer, "\\f");
                break;
            case '\n':
                err = gm_runtime_json_write_raw(writer, "\\n");
                break;
            case '\r':
                err = gm_runtime_json_write_raw(writer, "\\r");
                break;
            case '\t':
                err = gm_runtime_json_write_raw(writer, "\\t");
                break;
            default:
                if (*p < 0x20) {
                    char escaped[7];
                    int written = snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)*p);
                    if (written <= 0 || (size_t)written >= sizeof(escaped)) {
                        return ESP_FAIL;
                    }
                    err = gm_runtime_json_write_len(writer, escaped, (size_t)written);
                } else {
                    char ch = (char)*p;
                    err = gm_runtime_json_write_len(writer, &ch, 1);
                }
                break;
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "\"");
}

static esp_err_t gm_runtime_json_begin_field(gm_runtime_json_writer_t *writer,
                                             bool *first,
                                             const char *key)
{
    esp_err_t err = ESP_OK;
    if (!writer || !first || !key) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!*first) {
        err = gm_runtime_json_write_raw(writer, ",");
        if (err != ESP_OK) {
            return err;
        }
    }
    *first = false;
    err = gm_runtime_json_write_string(writer, key);
    if (err != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_raw(writer, ":");
}

static esp_err_t gm_runtime_json_write_string_field(gm_runtime_json_writer_t *writer,
                                                    bool *first,
                                                    const char *key,
                                                    const char *value)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, key);
    if (err != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_string(writer, value);
}

static esp_err_t gm_runtime_json_write_bool_field(gm_runtime_json_writer_t *writer,
                                                  bool *first,
                                                  const char *key,
                                                  bool value)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, key);
    if (err != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_bool(writer, value);
}

static esp_err_t gm_runtime_json_write_uint64_field(gm_runtime_json_writer_t *writer,
                                                    bool *first,
                                                    const char *key,
                                                    uint64_t value)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, key);
    if (err != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_uint64(writer, value);
}

static esp_err_t gm_runtime_json_write_int32_field(gm_runtime_json_writer_t *writer,
                                                   bool *first,
                                                   const char *key,
                                                   int32_t value)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, key);
    if (err != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_int32(writer, value);
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

static esp_err_t gm_runtime_json_write_branch_steps(gm_runtime_json_writer_t *writer,
                                                    const orch_room_runtime_view_t *view,
                                                    uint8_t branch_index)
{
    esp_err_t err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    if (!view || branch_index >= ORCH_ROOM_SCENARIO_MAX_BRANCHES) {
        return gm_runtime_json_write_raw(writer, "]");
    }
    uint8_t count = view->scenario_branch_steps[branch_index].step_count;
    for (uint8_t step_index = 0;
         step_index < count && step_index < ORCH_ROOM_SCENARIO_MAX_STEPS;
         ++step_index) {
        if (step_index > 0) {
            err = gm_runtime_json_write_raw(writer, ",");
            if (err != ESP_OK) {
                return err;
            }
        }
        err = gm_runtime_json_write_raw(writer, "{\"index\":");
        if (err != ESP_OK ||
            (err = gm_runtime_json_write_uint64(
                 writer,
                 view->scenario_branch_steps[branch_index].steps[step_index].index)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, ",\"global_index\":")) != ESP_OK ||
            (err = gm_runtime_json_write_uint64(
                 writer,
                 view->scenario_branch_steps[branch_index].steps[step_index].global_index)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, ",\"state\":")) != ESP_OK ||
            (err = gm_runtime_json_write_string(
                 writer,
                 view->scenario_branch_steps[branch_index].steps[step_index].state_text)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, ",\"enabled\":")) != ESP_OK ||
            (err = gm_runtime_json_write_bool(
                 writer,
                 view->scenario_branch_steps[branch_index].steps[step_index].enabled)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, ",\"text\":")) != ESP_OK ||
            (err = gm_runtime_json_write_string(
                 writer,
                 view->scenario_branch_steps[branch_index].steps[step_index].text)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t gm_runtime_json_write_branches_field(gm_runtime_json_writer_t *writer,
                                                      bool *first,
                                                      const orch_room_entry_t *room,
                                                      const orch_room_runtime_view_t *view)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, "scenario_branches");
    if (err != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_write_raw(writer, "[");
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < room->scenario_branch_count && i < ORCH_ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        const orch_room_scenario_branch_entry_t *branch = &room->scenario_branches[i];
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
            (err = gm_runtime_json_begin_field(writer, &branch_first, "steps")) != ESP_OK ||
            (err = gm_runtime_json_write_branch_steps(writer, view, i)) != ESP_OK ||
            (err = gm_runtime_json_write_raw(writer, "}")) != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "]");
}

static esp_err_t gm_scenario_send_runtime_state_json(httpd_req_t *req,
                                                     const orch_room_runtime_view_t *view,
                                                     bool summary_only)
{
    const orch_room_entry_t *room = NULL;
    gm_runtime_json_writer_t writer = {
        .req = req,
        .len = 0,
    };
    bool first = true;
    esp_err_t err = ESP_OK;

    if (!req || !view) {
        return ESP_ERR_INVALID_ARG;
    }
    room = &view->room;
    err = httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        return err;
    }
    err = gm_runtime_json_write_raw(&writer, "{");
    if (err != ESP_OK ||
        (err = gm_runtime_json_write_bool_field(&writer, &first, "ok", true)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &first, "runtime_schema_version", 1)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer, &first, "room_id", room->room_id)) != ESP_OK ||
        (err = gm_runtime_json_write_bool_field(&writer, &first, "session_present", room->session_present)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer,
                                                  &first,
                                                  "session_state",
                                                  room->session_state[0] ? room->session_state : "idle")) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer,
                                                  &first,
                                                  "timer_state",
                                                  room->timer_state[0] ? room->timer_state : "idle")) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &first, "timer_duration_ms", room->timer_duration_ms)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &first, "timer_remaining_ms", room->timer_remaining_ms)) != ESP_OK ||
        (err = gm_runtime_json_write_bool_field(&writer, &first, "hint_active", room->hint_active)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &first, "hint_sent_count", room->hint_sent_count)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer, &first, "hint_message", room->hint_message)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer, &first, "selected_profile_id", room->selected_profile_id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer, &first, "selected_profile_name", room->selected_profile_name)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer,
                                                  &first,
                                                  "selected_profile_scenario_id",
                                                  room->selected_profile_scenario_id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer, &first, "selected_scenario_id", room->selected_scenario_id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer,
                                                  &first,
                                                  "selected_scenario_name",
                                                  room->selected_scenario_name)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer, &first, "running_scenario_id", room->running_scenario_id)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer,
                                                  &first,
                                                  "running_scenario_name",
                                                  room->running_scenario_name)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer,
                                                  &first,
                                                  "running_scenario_generation",
                                                  room->running_scenario_generation)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &first, "runtime_now_ms", view->runtime_now_ms)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer,
                                                  &first,
                                                  "scenario_runtime_state",
                                                  room->scenario_runtime_state_text)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &first, "scenario_total_steps", room->scenario_total_steps)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer, &first, "scenario_done_steps", room->scenario_done_steps)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer,
                                                  &first,
                                                  "scenario_current_step_text",
                                                  room->scenario_current_step_text)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer,
                                                  &first,
                                                  "scenario_wait_type",
                                                  room->scenario_wait_type_text)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer,
                                                  &first,
                                                  "scenario_wait_summary",
                                                  room->scenario_wait_summary)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer,
                                                  &first,
                                                  "scenario_wait_until_ms",
                                                  room->scenario_wait_until_ms)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer,
                                                  &first,
                                                  "scenario_wait_started_at_ms",
                                                  room->scenario_wait_started_at_ms)) != ESP_OK ||
        (err = gm_runtime_json_write_uint64_field(&writer,
                                                  &first,
                                                  "scenario_device_count",
                                                  room->scenario_device_count)) != ESP_OK ||
        (err = gm_runtime_json_write_string_field(&writer,
                                                  &first,
                                                  "scenario_last_error",
                                                  room->scenario_last_error)) != ESP_OK) {
        return err;
    }
    if (!summary_only &&
        ((err = gm_runtime_json_write_wait_events_field(&writer,
                                                        &first,
                                                        room->scenario_wait_events,
                                                        room->scenario_wait_event_count)) != ESP_OK ||
         (err = gm_runtime_json_write_flag_refs_field(&writer,
                                                      &first,
                                                      "scenario_wait_flags",
                                                      room->scenario_wait_flags,
                                                      room->scenario_wait_flag_count)) != ESP_OK ||
         (err = gm_runtime_json_write_string_field(&writer,
                                                   &first,
                                                   "scenario_wait_operator_prompt",
                                                   room->scenario_wait_operator_prompt)) != ESP_OK ||
         (err = gm_runtime_json_write_string_field(&writer,
                                                   &first,
                                                   "scenario_wait_operator_label",
                                                   room->scenario_wait_operator_label)) != ESP_OK ||
         (err = gm_runtime_json_write_bool_field(&writer,
                                                 &first,
                                                 "scenario_wait_operator_skip_allowed",
                                                 room->scenario_wait_operator_skip_allowed)) != ESP_OK ||
         (err = gm_runtime_json_write_string_field(&writer,
                                                   &first,
                                                   "scenario_wait_operator_skip_label",
                                                   room->scenario_wait_operator_skip_label)) != ESP_OK ||
         (err = gm_runtime_json_write_string_field(&writer,
                                                   &first,
                                                   "scenario_operator_message",
                                                   room->scenario_operator_message)) != ESP_OK ||
         (err = gm_runtime_json_write_flag_entries_field(&writer,
                                                         &first,
                                                         room->scenario_flags,
                                                         room->scenario_flag_count)) != ESP_OK ||
         (err = gm_runtime_json_write_string_array_field(&writer,
                                                         &first,
                                                         "scenario_device_ids",
                                                         (const char *)room->scenario_device_ids,
                                                         sizeof(room->scenario_device_ids[0]),
                                                         room->scenario_device_count,
                                                         ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS)) != ESP_OK ||
         (err = gm_runtime_json_write_string_array_field(&writer,
                                                         &first,
                                                         "related_issue_ids",
                                                         (const char *)room->related_issue_ids,
                                                         sizeof(room->related_issue_ids[0]),
                                                         room->related_issue_count,
                                                         ORCH_REGISTRY_MAX_ISSUES)) != ESP_OK ||
         (err = gm_runtime_json_write_uint64_field(&writer,
                                                   &first,
                                                   "related_issue_count",
                                                   room->related_issue_count)) != ESP_OK ||
         (err = gm_runtime_json_write_branches_field(&writer, &first, room, view)) != ESP_OK ||
         (err = gm_runtime_json_write_string_field(&writer,
                                                   &first,
                                                   "asset_prepare_state",
                                                   view->asset_prepare_state[0] ? view->asset_prepare_state : "none")) != ESP_OK ||
         (err = gm_runtime_json_write_uint64_field(&writer, &first, "asset_audio_total", view->asset_audio_total)) != ESP_OK ||
         (err = gm_runtime_json_write_uint64_field(&writer, &first, "asset_audio_ready", view->asset_audio_ready)) != ESP_OK ||
         (err = gm_runtime_json_write_uint64_field(&writer, &first, "asset_audio_missing", view->asset_audio_missing)) != ESP_OK ||
         (err = gm_runtime_json_write_uint64_field(&writer, &first, "asset_audio_bad", view->asset_audio_bad)) != ESP_OK ||
         (err = gm_runtime_json_write_uint64_field(&writer,
                                                   &first,
                                                   "asset_audio_unsupported",
                                                   view->asset_audio_unsupported)) != ESP_OK ||
         (err = gm_runtime_json_write_uint64_field(&writer,
                                                   &first,
                                                   "asset_audio_io_error",
                                                   view->asset_audio_io_error)) != ESP_OK ||
         (err = gm_runtime_json_write_uint64_field(&writer,
                                                   &first,
                                                   "asset_audio_unknown",
                                                   view->asset_audio_unknown)) != ESP_OK)) {
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
    bool summary_only = false;
    esp_err_t err = ESP_OK;

    if (gm_scenario_read_query_value(req, "detail", detail, sizeof(detail)) && detail[0]) {
        summary_only = strcmp(detail, "summary") == 0;
    }

    err = gm_room_runtime_view_lock();
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "runtime busy");
    }
    memset(&s_room_runtime_view_scratch, 0, sizeof(s_room_runtime_view_scratch));
    err = orchestrator_registry_get_room_runtime_view(room_id, &s_room_runtime_view_scratch);
    if (err != ESP_OK) {
        gm_room_runtime_view_unlock();
        return gm_scenario_send_error(req, err);
    }
    err = gm_scenario_send_runtime_state_json(req, &s_room_runtime_view_scratch, summary_only);
    gm_room_runtime_view_unlock();
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
