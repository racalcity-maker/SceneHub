#include "node_provisioning_config_api_internal.h"

#include <stdio.h>

#include "node_json.h"

esp_err_t send_preview_json(httpd_req_t *req,
                            bool ok,
                            const char *status,
                            const char *error_code,
                            const char *command)
{
    char response[192];
    char status_json[32];
    char error_code_json[64];
    char command_json[48];
    int written = 0;

    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_json_escape_string(status_json, sizeof(status_json), status) ||
        !node_json_escape_string(error_code_json, sizeof(error_code_json), error_code) ||
        !node_json_escape_string(command_json, sizeof(command_json), command)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "preview response too large");
    }

    written = snprintf(response,
                       sizeof(response),
                       "{\"ok\":%s,\"status\":\"%s\",\"error_code\":\"%s\",\"command\":\"%s\"}",
                       ok ? "true" : "false",
                       status_json,
                       error_code_json,
                       command_json);
    if (written < 0 || written >= (int)sizeof(response)) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, written);
}

esp_err_t send_admin_result_json(httpd_req_t *req,
                                 int http_status,
                                 bool ok,
                                 const char *error_code,
                                 const node_admin_control_result_t *result)
{
    char response[224];
    char error_code_json[64];
    int written = 0;

    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_json_escape_string(error_code_json, sizeof(error_code_json), error_code)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "admin response too large");
    }

    if (http_status >= 400) {
        httpd_resp_set_status(req,
                              http_status == HTTPD_400_BAD_REQUEST ? "400 Bad Request"
                              : "500 Internal Server Error");
    }

    written = snprintf(response,
                       sizeof(response),
                       "{\"ok\":%s,\"applied\":%s,\"restart_required\":%s,"
                       "\"restarting\":%s,\"error_code\":\"%s\"}",
                       ok ? "true" : "false",
                       result && result->applied ? "true" : "false",
                       result && result->restart_required ? "true" : "false",
                       result && result->restarting ? "true" : "false",
                       error_code_json);
    if (written < 0 || written >= (int)sizeof(response)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "admin response too large");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, written);
}

esp_err_t send_rule_result_json(httpd_req_t *req,
                                int http_status,
                                bool ok,
                                const char *error_code,
                                const node_rule_bundle_metadata_t *metadata,
                                const node_admin_control_result_t *result)
{
    char response[320];
    char error_code_json[64];
    char bundle_id_json[NODE_RULE_BUNDLE_ID_MAX_LEN * 2];
    char mode_json[24];
    int written = 0;

    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_json_escape_string(error_code_json, sizeof(error_code_json), error_code) ||
        !node_json_escape_string(bundle_id_json,
                                 sizeof(bundle_id_json),
                                 metadata ? metadata->bundle_id : "") ||
        !node_json_escape_string(mode_json, sizeof(mode_json), metadata ? metadata->mode : "")) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules response too large");
    }

    if (http_status >= 400) {
        httpd_resp_set_status(req,
                              http_status == HTTPD_400_BAD_REQUEST ? "400 Bad Request"
                              : "500 Internal Server Error");
    }

    written = snprintf(response,
                       sizeof(response),
                       "{\"ok\":%s,\"applied\":%s,\"restart_required\":%s,\"error_code\":\"%s\",\"metadata\":{"
                       "\"has_bundle\":%s,\"version\":%lu,\"generation\":%lu,"
                       "\"bundle_id\":\"%s\",\"mode\":\"%s\",\"raw_size\":%lu}}",
                       ok ? "true" : "false",
                       result && result->applied ? "true" : "false",
                       result && result->restart_required ? "true" : "false",
                       error_code_json,
                       metadata && metadata->has_bundle ? "true" : "false",
                       metadata ? (unsigned long)metadata->version : 0UL,
                       metadata ? (unsigned long)metadata->generation : 0UL,
                       bundle_id_json,
                       mode_json,
                       metadata ? (unsigned long)metadata->raw_size : 0UL);
    if (written < 0 || written >= (int)sizeof(response)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules response too large");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, written);
}
