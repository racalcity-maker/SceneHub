#define WEB_UI_UTILS_NO_HTTP_MACROS
#include "web_ui_utils.h"

#include <string.h>

static const char *web_ui_scenehub_control_status_text(esp_err_t err,
                                                       scenehub_control_status_t status,
                                                       const scenehub_control_result_t *result)
{
    if (result && result->error_code[0]) {
        if (strcmp(result->error_code, "invalid_request") == 0) {
            return "400 Bad Request";
        }
        if (strcmp(result->error_code, "invalid_device_manifest") == 0 ||
            strcmp(result->error_code, "invalid_device_manifest_identity") == 0 ||
            strcmp(result->error_code, "invalid_device_manifest_shape") == 0 ||
            strcmp(result->error_code, "invalid_device_manifest_gpio") == 0) {
            return "400 Bad Request";
        }
        if (strcmp(result->error_code, "scenario_invalid") == 0) {
            return "409 Conflict";
        }
        if (strcmp(result->error_code, "room_not_found") == 0 ||
            strcmp(result->error_code, "action_not_found") == 0 ||
            strcmp(result->error_code, "profile_not_found") == 0 ||
            strcmp(result->error_code, "scenario_not_found") == 0 ||
            strcmp(result->error_code, "device_not_found") == 0 ||
            strcmp(result->error_code, "resource_not_found") == 0) {
            return "404 Not Found";
        }
        if (strcmp(result->error_code, "action_disabled") == 0 ||
            strcmp(result->error_code, "invalid_state") == 0 ||
            strcmp(result->error_code, "room_unhealthy") == 0) {
            return "409 Conflict";
        }
        if (strcmp(result->error_code, "not_supported") == 0) {
            return "422 Unprocessable Entity";
        }
    }
    if (status == SCENEHUB_CONTROL_STATUS_TIMEOUT) {
        return "504 Gateway Timeout";
    }
    switch (err) {
    case ESP_ERR_INVALID_ARG:
    case ESP_ERR_INVALID_SIZE:
    case ESP_ERR_INVALID_VERSION:
        return "400 Bad Request";
    case ESP_ERR_NOT_FOUND:
        return "404 Not Found";
    case ESP_ERR_INVALID_STATE:
        return "409 Conflict";
    case ESP_ERR_NOT_SUPPORTED:
        return "422 Unprocessable Entity";
    default:
        return "500 Internal Server Error";
    }
}

static const char *web_ui_scenehub_control_message(esp_err_t err,
                                                   const scenehub_control_result_t *result,
                                                   const char *fallback_message)
{
    if (result && result->message[0]) {
        return result->message;
    }
    switch (err) {
    case ESP_ERR_INVALID_ARG:
    case ESP_ERR_INVALID_SIZE:
    case ESP_ERR_INVALID_VERSION:
        return "invalid request";
    case ESP_ERR_NOT_FOUND:
        return "resource not found";
    case ESP_ERR_INVALID_STATE:
        return "operation not allowed in current state";
    case ESP_ERR_NOT_SUPPORTED:
        return "operation not supported";
    case ESP_ERR_NO_MEM:
        return "no memory";
    default:
        return (fallback_message && fallback_message[0]) ? fallback_message : "scenehub control failed";
    }
}

static const char *web_ui_scenehub_control_error_code(esp_err_t err,
                                                      const scenehub_control_result_t *result,
                                                      const char *fallback_error_code)
{
    if (result && result->error_code[0]) {
        return result->error_code;
    }
    switch (err) {
    case ESP_ERR_INVALID_ARG:
    case ESP_ERR_INVALID_SIZE:
    case ESP_ERR_INVALID_VERSION:
        return "invalid_request";
    case ESP_ERR_NOT_FOUND:
        return "resource_not_found";
    case ESP_ERR_INVALID_STATE:
        return "invalid_state";
    case ESP_ERR_NOT_SUPPORTED:
        return "not_supported";
    case ESP_ERR_NO_MEM:
        return "no_memory";
    default:
        return (fallback_error_code && fallback_error_code[0]) ? fallback_error_code
                                                               : "execution_failed";
    }
}

bool web_ui_scenehub_control_is_done(esp_err_t call_err, const scenehub_control_result_t *result)
{
    return call_err == ESP_OK &&
           result &&
           result->status == SCENEHUB_CONTROL_STATUS_DONE;
}

bool web_ui_scenehub_control_is_success(esp_err_t call_err, const scenehub_control_result_t *result)
{
    return call_err == ESP_OK &&
           result &&
           (result->status == SCENEHUB_CONTROL_STATUS_DONE ||
            result->status == SCENEHUB_CONTROL_STATUS_ACCEPTED);
}

esp_err_t web_ui_send_scenehub_control_ack(httpd_req_t *req)
{
    return web_ui_send_ok(req, "application/json", "{\"ok\":true,\"accepted\":true}");
}

esp_err_t web_ui_send_scenehub_control_error(httpd_req_t *req,
                                             esp_err_t call_err,
                                             const scenehub_control_result_t *result,
                                             const char *fallback_message)
{
    const scenehub_control_status_t status = result ? result->status : SCENEHUB_CONTROL_STATUS_FAILED;
    const esp_err_t err = (call_err != ESP_OK) ? call_err : (result ? result->err : ESP_FAIL);
    const char *status_text = web_ui_scenehub_control_status_text(err, status, result);
    const char *message = web_ui_scenehub_control_message(err, result, fallback_message);

    web_ui_http_resp_set_status(req, status_text);
    return web_ui_http_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
}

esp_err_t web_ui_send_scenehub_control_error_json(httpd_req_t *req,
                                                  esp_err_t call_err,
                                                  const scenehub_control_result_t *result,
                                                  const char *fallback_error_code,
                                                  const char *room_id,
                                                  const char *action_id)
{
    const scenehub_control_status_t status = result ? result->status : SCENEHUB_CONTROL_STATUS_FAILED;
    const esp_err_t err = (call_err != ESP_OK) ? call_err : (result ? result->err : ESP_FAIL);
    const char *status_text = web_ui_scenehub_control_status_text(err, status, result);
    const char *error_code =
        web_ui_scenehub_control_error_code(err, result, fallback_error_code);
    const char *message = web_ui_scenehub_control_message(err, result, NULL);
    cJSON *root = cJSON_CreateObject();

    if (!root) {
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", error_code);
    cJSON_AddStringToObject(root, "message", message ? message : "");
    cJSON_AddStringToObject(root, "room_id", room_id ? room_id : "");
    cJSON_AddStringToObject(root, "action_id", action_id ? action_id : "");
    web_ui_http_resp_set_status(req, status_text);
    return web_ui_send_json(req, root);
}
