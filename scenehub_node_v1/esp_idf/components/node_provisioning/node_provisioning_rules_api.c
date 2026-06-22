#include "node_provisioning_config_api_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

static esp_err_t send_rules_get_chunk(httpd_req_t *req, const char *data, ssize_t len)
{
    if (!req || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    return httpd_resp_send_chunk(req, data, len >= 0 ? len : HTTPD_RESP_USE_STRLEN);
}

esp_err_t node_provisioning_rules_get(httpd_req_t *req)
{
    int written = 0;
    esp_err_t err = ESP_OK;
    node_rule_store_entry_t *rule_entry = NULL;
    char head[256];

    rule_entry = (node_rule_store_entry_t *)alloc_provisioning_buffer(sizeof(*rule_entry));
    if (!rule_entry) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules no mem");
    }

    err = node_rule_api_get_bundle(rule_entry);
    if (err != ESP_OK) {
        free(rule_entry);
        ESP_LOGE(g_node_provisioning_tag, "rules get failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules unavailable");
    }

    if (rule_entry->metadata.raw_size > NODE_RULE_BUNDLE_GET_RESPONSE_MAX_LEN) {
        free(rule_entry);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules response too large");
    }

    written = snprintf(head,
                       sizeof(head),
                       "{\"ok\":true,\"has_bundle\":%s,\"metadata\":{"
                       "\"version\":%lu,\"generation\":%lu,\"bundle_id\":\"%s\","
                       "\"mode\":\"%s\",\"raw_size\":%lu},\"bundle\":",
                       rule_entry->metadata.has_bundle ? "true" : "false",
                       (unsigned long)rule_entry->metadata.version,
                       (unsigned long)rule_entry->metadata.generation,
                       rule_entry->metadata.bundle_id,
                       rule_entry->metadata.mode,
                       (unsigned long)rule_entry->metadata.raw_size);
    if (written < 0 || written >= (int)sizeof(head)) {
        free(rule_entry);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules response too large");
    }

    httpd_resp_set_type(req, "application/json");
    err = send_rules_get_chunk(req, head, written);
    if (err == ESP_OK) {
        if (rule_entry->metadata.has_bundle) {
            err = send_rules_get_chunk(req, rule_entry->raw_json, (ssize_t)rule_entry->metadata.raw_size);
        } else {
            err = send_rules_get_chunk(req, "null", -1);
        }
    }
    if (err == ESP_OK) {
        err = send_rules_get_chunk(req, "}", -1);
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    free(rule_entry);
    return err;
}

esp_err_t node_provisioning_rules_validate_post(httpd_req_t *req)
{
    node_rule_bundle_metadata_t metadata = {0};
    node_admin_control_result_t admin_result = {0};
    char *raw_json = NULL;
    char error_code[NODE_RULE_API_ERROR_MAX_LEN] = {0};
    esp_err_t err = ESP_OK;

    if (req->content_len <= 0 || req->content_len > NODE_RULE_BUNDLE_HTTP_MAX_LEN) {
        drain_request_body(req);
        return send_rule_result_json(req, HTTPD_400_BAD_REQUEST, false, "bundle_too_large", NULL, NULL);
    }
    if (!lock_post_body()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules busy");
    }
    if (!ensure_post_body_buffer()) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules no mem");
    }
    if (!read_request_body(req, s_post_body, NODE_PROVISIONING_POST_BODY_CAPACITY)) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
    }
    raw_json = dup_request_json(s_post_body, (size_t)req->content_len);
    unlock_post_body();
    if (!raw_json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules no mem");
    }

    err = node_admin_control_validate_rules(raw_json,
                                            &metadata,
                                            error_code,
                                            sizeof(error_code),
                                            &admin_result);
    free(raw_json);
    if (err != ESP_OK) {
        return send_rule_result_json(req, HTTPD_400_BAD_REQUEST, false, error_code, &metadata, &admin_result);
    }
    return send_rule_result_json(req, 200, true, "", &metadata, &admin_result);
}

esp_err_t node_provisioning_rules_apply_post(httpd_req_t *req)
{
    node_rule_bundle_metadata_t metadata = {0};
    node_admin_control_result_t admin_result = {0};
    char *raw_json = NULL;
    char error_code[NODE_RULE_API_ERROR_MAX_LEN] = {0};
    esp_err_t err = ESP_OK;

    if (req->content_len <= 0 || req->content_len > NODE_RULE_BUNDLE_HTTP_MAX_LEN) {
        drain_request_body(req);
        return send_rule_result_json(req, HTTPD_400_BAD_REQUEST, false, "bundle_too_large", NULL, NULL);
    }
    if (!lock_post_body()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules busy");
    }
    if (!ensure_post_body_buffer()) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules no mem");
    }
    if (!read_request_body(req, s_post_body, NODE_PROVISIONING_POST_BODY_CAPACITY)) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
    }
    raw_json = dup_request_json(s_post_body, (size_t)req->content_len);
    unlock_post_body();
    if (!raw_json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules no mem");
    }

    err = node_admin_control_apply_rules(raw_json,
                                         &metadata,
                                         error_code,
                                         sizeof(error_code),
                                         &admin_result);
    free(raw_json);
    if (err != ESP_OK) {
        int http_status = (strcmp(error_code, "store_failed") == 0)
                              ? HTTPD_500_INTERNAL_SERVER_ERROR
                              : HTTPD_400_BAD_REQUEST;
        ESP_LOGW(g_node_provisioning_tag, "rules apply failed: %s code=%s", esp_err_to_name(err), error_code);
        return send_rule_result_json(req, http_status, false, error_code, &metadata, &admin_result);
    }

    return send_rule_result_json(req, 200, true, "", &metadata, &admin_result);
}

esp_err_t node_provisioning_rules_clear_post(httpd_req_t *req)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_clear_rules(&admin_result);

    if (err != ESP_OK) {
        ESP_LOGE(g_node_provisioning_tag, "rules clear failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules clear failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"cleared\":true,\"restart_required\":true}");
}

esp_err_t node_provisioning_rules_pause_post(httpd_req_t *req)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_pause_rules(&admin_result);

    if (err != ESP_OK) {
        ESP_LOGE(g_node_provisioning_tag, "rules pause failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules pause failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"paused\":true}");
}

esp_err_t node_provisioning_rules_resume_post(httpd_req_t *req)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_resume_rules(&admin_result);

    if (err != ESP_OK) {
        ESP_LOGE(g_node_provisioning_tag, "rules resume failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules resume failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"resumed\":true}");
}
