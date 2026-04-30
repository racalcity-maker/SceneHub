#include "web_ui_handlers.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "gm_control.h"
#include "orchestrator_registry.h"
#include "web_ui_utils.h"

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

static esp_err_t gm_action_send_error(httpd_req_t *req,
                                      const char *status,
                                      const char *error_code,
                                      const char *room_id,
                                      const char *action_id)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", error_code ? error_code : "execution_failed");
    cJSON_AddStringToObject(root, "room_id", room_id ? room_id : "");
    cJSON_AddStringToObject(root, "action_id", action_id ? action_id : "");
    if (status && status[0]) {
        httpd_resp_set_status(req, status);
    }
    return web_ui_send_json(req, root);
}

static esp_err_t gm_action_send_ok(httpd_req_t *req, const char *room_id, const char *action_id)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "room_id", room_id ? room_id : "");
    cJSON_AddStringToObject(root, "action_id", action_id ? action_id : "");
    return web_ui_send_json(req, root);
}

static esp_err_t gm_room_game_action_query_handler(httpd_req_t *req, const char *action_id)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    esp_err_t err = ESP_OK;
    if (!gm_action_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return gm_action_send_error(req, "400 Bad Request", "invalid_request", "", action_id);
    }
    err = gm_control_execute_room_action_with_source("http", room_id, action_id);
    if (err == ESP_OK) {
        orchestrator_registry_invalidate();
        return gm_action_send_ok(req, room_id, action_id);
    }
    if (err == GM_CTRL_ERR_ROOM_NOT_FOUND) {
        return gm_action_send_error(req, "404 Not Found", "room_not_found", room_id, action_id);
    }
    if (err == GM_CTRL_ERR_ACTION_NOT_FOUND) {
        return gm_action_send_error(req, "404 Not Found", "action_not_found", room_id, action_id);
    }
    if (err == GM_CTRL_ERR_ACTION_DISABLED) {
        return gm_action_send_error(req, "409 Conflict", "action_disabled", room_id, action_id);
    }
    if (err == GM_CTRL_ERR_NOT_SUPPORTED) {
        return gm_action_send_error(req, "422 Unprocessable Entity", "not_supported", room_id, action_id);
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return gm_action_send_error(req, "400 Bad Request", "invalid_request", room_id, action_id);
    }
    return gm_action_send_error(req, "500 Internal Server Error", "execution_failed", room_id, action_id);
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
