#include "web_ui_handlers.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "scenehub_control.h"
#include "web_ui_utils.h"

static const char *TAG = "web_ui_gm_action";

static int64_t gm_action_perf_start(void)
{
    return esp_timer_get_time();
}

static void gm_action_perf_log(const char *label, int64_t start_us, const char *room_id, const char *action_id)
{
    int64_t dt_ms = (esp_timer_get_time() - start_us) / 1000;
    ESP_LOGD(TAG,
             "PERF %s room=%s action=%s took %lld ms",
             label ? label : "room action",
             room_id ? room_id : "",
             action_id ? action_id : "",
             dt_ms);
}

static bool gm_action_read_query_value(httpd_req_t *req, const char *key, char *out, size_t out_size)
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

static esp_err_t gm_action_send_ok(httpd_req_t *req, const char *room_id, const char *action_id)
{
    return web_ui_send_room_action_result_json(req, room_id, action_id);
}

static esp_err_t gm_room_game_action_query_handler(httpd_req_t *req, const char *action_id)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    int64_t t0 = gm_action_perf_start();
    scenehub_control_result_t result = {0};
    if (!gm_action_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return web_ui_send_scenehub_control_error_json(req,
                                                       ESP_ERR_INVALID_ARG,
                                                       NULL,
                                                       "invalid_request",
                                                       "",
                                                       action_id);
    }
    esp_err_t err = scenehub_control_execute_room_action("http", room_id, action_id, &result);
    if (web_ui_scenehub_control_is_done(err, &result)) {
        gm_action_perf_log("POST room game action", t0, room_id, action_id);
        return gm_action_send_ok(req, room_id, action_id);
    }
    return web_ui_send_scenehub_control_error_json(req,
                                                   err,
                                                   &result,
                                                   "execution_failed",
                                                   room_id,
                                                   action_id);
}

esp_err_t gm_room_game_start_handler(httpd_req_t *req)
{
    return gm_room_game_action_query_handler(req, "start_game");
}

esp_err_t gm_room_game_stop_handler(httpd_req_t *req)
{
    return gm_room_game_action_query_handler(req, "stop_game");
}

esp_err_t gm_room_game_reset_handler(httpd_req_t *req)
{
    return gm_room_game_action_query_handler(req, "reset_game");
}
