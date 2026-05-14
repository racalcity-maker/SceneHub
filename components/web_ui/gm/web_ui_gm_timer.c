#include "web_ui_handlers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "orchestrator_registry.h"
#include "scenehub_control.h"
#include "web_ui_utils.h"

static const char *TAG = "web_ui_gm_timer";

static int64_t gm_timer_perf_start(void)
{
    return esp_timer_get_time();
}

static void gm_timer_perf_log(const char *label, int64_t start_us, const char *room_id)
{
    int64_t dt_ms = (esp_timer_get_time() - start_us) / 1000;
    ESP_LOGW(TAG, "PERF %s room=%s took %lld ms", label ? label : "timer", room_id ? room_id : "", dt_ms);
}

static bool gm_parse_i32(const char *text, int32_t *out)
{
    char *end = NULL;
    long value;
    if (!text || !text[0] || !out) {
        return false;
    }
    value = strtol(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    if (value < INT32_MIN || value > INT32_MAX) {
        return false;
    }
    *out = (int32_t)value;
    return true;
}

static bool gm_parse_u32(const char *text, uint32_t *out)
{
    int32_t value = 0;
    if (!out || !gm_parse_i32(text, &value) || value < 0) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool gm_read_query_value(httpd_req_t *req, const char *key, char *out, size_t out_size)
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

esp_err_t gm_timer_start_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    char duration_raw[32] = {0};
    uint32_t duration_ms = 0;
    int64_t t0 = gm_timer_perf_start();
    scenehub_control_result_t result = {0};
    if (!gm_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    if (!gm_read_query_value(req, "duration_ms", duration_raw, sizeof(duration_raw)) ||
        !gm_parse_u32(duration_raw, &duration_ms) || duration_ms == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "duration_ms required");
    }
    esp_err_t err = scenehub_control_timer_start("http", room_id, duration_ms, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return web_ui_send_scenehub_control_error(req, err, &result, "timer start failed");
    }
    gm_timer_perf_log("POST timer start", t0, room_id);
    return web_ui_send_scenehub_control_ack(req);
}

esp_err_t gm_timer_pause_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    int64_t t0 = gm_timer_perf_start();
    scenehub_control_result_t result = {0};
    if (!gm_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    esp_err_t err = scenehub_control_timer_pause("http", room_id, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return web_ui_send_scenehub_control_error(req, err, &result, "timer pause failed");
    }
    gm_timer_perf_log("POST timer pause", t0, room_id);
    return web_ui_send_scenehub_control_ack(req);
}

esp_err_t gm_timer_resume_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    int64_t t0 = gm_timer_perf_start();
    scenehub_control_result_t result = {0};
    if (!gm_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    esp_err_t err = scenehub_control_timer_resume("http", room_id, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return web_ui_send_scenehub_control_error(req, err, &result, "timer resume failed");
    }
    gm_timer_perf_log("POST timer resume", t0, room_id);
    return web_ui_send_scenehub_control_ack(req);
}

esp_err_t gm_timer_reset_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    char duration_raw[32] = {0};
    uint32_t duration_ms = 0;
    bool has_duration = false;
    int64_t t0 = gm_timer_perf_start();
    scenehub_control_result_t result = {0};
    if (!gm_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    if (gm_read_query_value(req, "duration_ms", duration_raw, sizeof(duration_raw)) && duration_raw[0]) {
        if (!gm_parse_u32(duration_raw, &duration_ms)) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid duration_ms");
        }
        has_duration = true;
    }
    esp_err_t err = scenehub_control_timer_reset("http", room_id, has_duration, duration_ms, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return web_ui_send_scenehub_control_error(req, err, &result, "timer reset failed");
    }
    gm_timer_perf_log("POST timer reset", t0, room_id);
    return web_ui_send_scenehub_control_ack(req);
}

esp_err_t gm_timer_add_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    char delta_raw[32] = {0};
    int32_t delta_ms = 0;
    int64_t t0 = gm_timer_perf_start();
    scenehub_control_result_t result = {0};
    if (!gm_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    if (!gm_read_query_value(req, "delta_ms", delta_raw, sizeof(delta_raw)) ||
        !gm_parse_i32(delta_raw, &delta_ms) || delta_ms == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "delta_ms required");
    }
    esp_err_t err = scenehub_control_timer_add("http", room_id, delta_ms, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return web_ui_send_scenehub_control_error(req, err, &result, "timer add failed");
    }
    gm_timer_perf_log("POST timer add", t0, room_id);
    return web_ui_send_scenehub_control_ack(req);
}

esp_err_t gm_session_finish_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    int64_t t0 = gm_timer_perf_start();
    scenehub_control_result_t result = {0};
    if (!gm_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    esp_err_t err = scenehub_control_session_finish("http", room_id, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return web_ui_send_scenehub_control_error(req, err, &result, "session finish failed");
    }
    gm_timer_perf_log("POST session finish", t0, room_id);
    return web_ui_send_scenehub_control_ack(req);
}
