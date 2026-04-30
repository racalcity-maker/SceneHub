#include "web_ui_handlers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "gm_api.h"
#include "orchestrator_registry.h"
#include "orchestrator_timeline.h"
#include "web_ui_utils.h"

static const char *gm_session_state_str(gm_session_state_t state)
{
    switch (state) {
    case GM_SESSION_RUNNING:
        return "running";
    case GM_SESSION_PAUSED:
        return "paused";
    case GM_SESSION_FINISHED:
        return "finished";
    case GM_SESSION_IDLE:
    default:
        return "idle";
    }
}

static const char *gm_timer_state_str(gm_timer_state_t state)
{
    switch (state) {
    case GM_TIMER_RUNNING:
        return "running";
    case GM_TIMER_PAUSED:
        return "paused";
    case GM_TIMER_FINISHED:
        return "finished";
    case GM_TIMER_IDLE:
    default:
        return "idle";
    }
}

static uint64_t gm_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void *gm_timer_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
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

static esp_err_t gm_send_timer_state(httpd_req_t *req, const char *room_id, const char *status)
{
    gm_room_session_t *session = NULL;
    cJSON *root = NULL;
    if (!req || !room_id || !status) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid state");
    }
    session = gm_timer_alloc(sizeof(*session));
    if (!session) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    if (gm_api_room_session_get(room_id, session) != ESP_OK) {
        heap_caps_free(session);
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "room session not found");
    }
    root = cJSON_CreateObject();
    if (!root) {
        heap_caps_free(session);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddStringToObject(root, "room_id", session->room_id);
    cJSON_AddStringToObject(root, "session_state", gm_session_state_str(session->state));
    cJSON_AddStringToObject(root, "timer_state", gm_timer_state_str(session->timer.state));
    cJSON_AddNumberToObject(root, "timer_duration_ms", session->timer.duration_ms);
    cJSON_AddNumberToObject(root, "timer_remaining_ms", gm_timer_get_remaining(&session->timer, gm_now_ms()));
    cJSON_AddNumberToObject(root, "session_started_at_ms", (double)session->started_at_ms);
    heap_caps_free(session);
    return web_ui_send_json(req, root);
}

static esp_err_t gm_send_timer_error(httpd_req_t *req, esp_err_t err, const char *internal_message)
{
    if (err == ESP_ERR_INVALID_ARG) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid arguments");
    }
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "room session not found");
    }
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "operation not allowed in current state", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, internal_message);
}

esp_err_t gm_timer_start_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    char duration_raw[32] = {0};
    uint32_t duration_ms = 0;
    esp_err_t err = ESP_OK;
    if (!gm_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    if (!gm_read_query_value(req, "duration_ms", duration_raw, sizeof(duration_raw)) ||
        !gm_parse_u32(duration_raw, &duration_ms) || duration_ms == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "duration_ms required");
    }
    err = gm_api_timer_start(room_id, duration_ms);
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "room not found");
    }
    if (err != ESP_OK) {
        return gm_send_timer_error(req, err, "timer start failed");
    }
    orchestrator_registry_invalidate();
    char details[64] = {0};
    snprintf(details, sizeof(details), "duration_ms=%lu", (unsigned long)duration_ms);
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_TIMER_CHANGED,
                                    ORCH_TIMELINE_SEVERITY_INFO,
                                    "http",
                                    room_id,
                                    "",
                                    "Timer started",
                                    details);
    return gm_send_timer_state(req, room_id, "started");
}

esp_err_t gm_timer_pause_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    esp_err_t err;
    if (!gm_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    err = gm_api_timer_pause(room_id);
    if (err != ESP_OK) {
        return gm_send_timer_error(req, err, "timer pause failed");
    }
    orchestrator_registry_invalidate();
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_TIMER_CHANGED,
                                    ORCH_TIMELINE_SEVERITY_INFO,
                                    "http",
                                    room_id,
                                    "",
                                    "Timer paused",
                                    "");
    return gm_send_timer_state(req, room_id, "paused");
}

esp_err_t gm_timer_resume_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    esp_err_t err;
    if (!gm_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    err = gm_api_timer_resume(room_id);
    if (err != ESP_OK) {
        return gm_send_timer_error(req, err, "timer resume failed");
    }
    orchestrator_registry_invalidate();
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_TIMER_CHANGED,
                                    ORCH_TIMELINE_SEVERITY_INFO,
                                    "http",
                                    room_id,
                                    "",
                                    "Timer resumed",
                                    "");
    return gm_send_timer_state(req, room_id, "resumed");
}

esp_err_t gm_timer_reset_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    char duration_raw[32] = {0};
    uint32_t duration_ms = 0;
    bool has_duration = false;
    esp_err_t err;
    if (!gm_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    if (gm_read_query_value(req, "duration_ms", duration_raw, sizeof(duration_raw)) && duration_raw[0]) {
        if (!gm_parse_u32(duration_raw, &duration_ms)) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid duration_ms");
        }
        has_duration = true;
    }
    err = gm_api_timer_reset(room_id, has_duration, duration_ms);
    if (err != ESP_OK) {
        return gm_send_timer_error(req, err, "timer reset failed");
    }
    orchestrator_registry_invalidate();
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_TIMER_CHANGED,
                                    ORCH_TIMELINE_SEVERITY_INFO,
                                    "http",
                                    room_id,
                                    "",
                                    "Timer reset",
                                    has_duration ? duration_raw : "");
    return gm_send_timer_state(req, room_id, "reset");
}

esp_err_t gm_timer_add_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    char delta_raw[32] = {0};
    int32_t delta_ms = 0;
    esp_err_t err;
    if (!gm_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    if (!gm_read_query_value(req, "delta_ms", delta_raw, sizeof(delta_raw)) ||
        !gm_parse_i32(delta_raw, &delta_ms) || delta_ms == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "delta_ms required");
    }
    err = gm_api_timer_add(room_id, delta_ms);
    if (err != ESP_OK) {
        return gm_send_timer_error(req, err, "timer add failed");
    }
    orchestrator_registry_invalidate();
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_TIMER_CHANGED,
                                    ORCH_TIMELINE_SEVERITY_INFO,
                                    "http",
                                    room_id,
                                    "",
                                    "Timer adjusted",
                                    delta_raw);
    return gm_send_timer_state(req, room_id, "adjusted");
}

esp_err_t gm_session_finish_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    esp_err_t err;
    if (!gm_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    err = gm_api_room_session_finish(room_id);
    if (err != ESP_OK) {
        return gm_send_timer_error(req, err, "session finish failed");
    }
    orchestrator_registry_invalidate();
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_TIMER_CHANGED,
                                    ORCH_TIMELINE_SEVERITY_INFO,
                                    "http",
                                    room_id,
                                    "",
                                    "Session finished",
                                    "");
    return gm_send_timer_state(req, room_id, "finished");
}
