#include "node_provisioning_internal.h"
#include "node_provisioning_config_api_internal.h"

#include <stdio.h>

#include "node_driver_nfc_api.h"
#include "node_runtime_snapshot.h"

esp_err_t node_provisioning_status_get(httpd_req_t *req)
{
    char ap_ssid_json[sizeof(g_node_prov.status.ap_ssid) * 2 + 1];
    char ap_password_json[sizeof(g_node_prov.status.ap_password) * 2 + 1];
    char reader_id_json[(NODE_DRIVER_ID_MAX_LEN + 1) * 2 + 1];
    char uid_json[NODE_DRIVER_UID_TEXT_MAX_LEN * 2 + 1];
    char last_seen_uid_json[NODE_DRIVER_UID_TEXT_MAX_LEN * 2 + 1];
    char health_json[16 * 2 + 1];
    char state_json[16 * 2 + 1];
    char error_code_json[32 * 2 + 1];
    node_nfc_driver_status_t nfc_status = {0};
    node_runtime_snapshot_t *runtime_snapshot = NULL;
    int n = 0;

    if (!ensure_provisioning_scratch()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status no mem");
    }
    if (!lock_config_json()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status busy");
    }
    if (!ensure_config_json_buffer()) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status no mem");
    }
    node_driver_nfc_api_get_status(&nfc_status);
    runtime_snapshot = &s_scratch->runtime_snapshot_scratch;
    if (node_runtime_snapshot_capture(runtime_snapshot) != ESP_OK) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status unavailable");
    }
    if (!json_escape_string(ap_ssid_json, sizeof(ap_ssid_json), g_node_prov.status.ap_ssid) ||
        !json_escape_string(ap_password_json, sizeof(ap_password_json), g_node_prov.status.ap_password) ||
        !json_escape_string(reader_id_json, sizeof(reader_id_json), nfc_status.reader_id) ||
        !json_escape_string(uid_json, sizeof(uid_json), nfc_status.uid) ||
        !json_escape_string(last_seen_uid_json, sizeof(last_seen_uid_json), nfc_status.last_seen_uid) ||
        !json_escape_string(health_json, sizeof(health_json), nfc_status.health) ||
        !json_escape_string(state_json, sizeof(state_json), nfc_status.state) ||
        !json_escape_string(error_code_json, sizeof(error_code_json), nfc_status.error_code)) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status too large");
    }

    n = snprintf(s_config_json,
                 NODE_PROVISIONING_CONFIG_JSON_CAPACITY,
                 "{\"ok\":true,\"mode\":\"%s\",\"ap_started\":%s,\"web_started\":%s,"
                 "\"ap_ssid\":\"%s\",\"ap_password\":\"%s\","
                 "\"sta_got_ip\":%s,\"sta_disconnected\":%s,\"sta_disconnect_reason\":%u,"
                 "\"auto_close_supported\":%s,\"auto_close_running\":%s,"
                 "\"auto_close_keep_open\":%s,\"auto_close_timeout_sec\":%u,"
                 "\"auto_close_remaining_sec\":%u,"
                 "\"fallback\":{\"initialized\":%s,\"enabled\":%s,\"state\":\"%s\","
                 "\"wifi_ready\":%s,\"mqtt_connected\":%s,\"fallback_rules_active\":%s,"
                 "\"fallback_timeout_ms\":%lu,\"fallback_return_delay_ms\":%lu,"
                 "\"return_policy\":\"%s\"},"
                 "\"nfc_reader\":{\"initialized\":%s,\"started\":%s,\"enabled\":%s,\"driver_ready\":%s,"
                 "\"health\":\"%s\",\"state\":\"%s\",\"error_code\":\"%s\","
                 "\"reader_id\":\"%s\",\"card_present\":%s,\"seen_count\":%lu,"
                 "\"token_id\":%ld,\"uid\":\"%s\",\"last_seen_uid\":\"%s\"}}",
                 g_node_prov.status.mode == NODE_PROVISIONING_MODE_AP ? "ap" : "sta",
                 g_node_prov.status.ap_started ? "true" : "false",
                 g_node_prov.status.web_started ? "true" : "false",
                 ap_ssid_json,
                 ap_password_json,
                 g_node_prov.status.sta_got_ip ? "true" : "false",
                 g_node_prov.status.sta_disconnected ? "true" : "false",
                 (unsigned)g_node_prov.status.sta_disconnect_reason,
                 g_node_prov.status.auto_close_supported ? "true" : "false",
                 g_node_prov.status.auto_close_running ? "true" : "false",
                 g_node_prov.status.auto_close_keep_open ? "true" : "false",
                 (unsigned)g_node_prov.status.auto_close_timeout_sec,
                 (unsigned)g_node_prov.status.auto_close_remaining_sec,
                 runtime_snapshot->fallback_initialized ? "true" : "false",
                 runtime_snapshot->fallback_enabled ? "true" : "false",
                 runtime_snapshot->fallback_state,
                 runtime_snapshot->fallback_wifi_ready ? "true" : "false",
                 runtime_snapshot->fallback_mqtt_connected ? "true" : "false",
                 runtime_snapshot->fallback_rules_active ? "true" : "false",
                 (unsigned long)runtime_snapshot->fallback_timeout_ms,
                 (unsigned long)runtime_snapshot->fallback_return_delay_ms,
                 runtime_snapshot->fallback_return_policy,
                 nfc_status.initialized ? "true" : "false",
                 nfc_status.started ? "true" : "false",
                 nfc_status.enabled ? "true" : "false",
                 nfc_status.driver_ready ? "true" : "false",
                 health_json,
                 state_json,
                 error_code_json,
                 reader_id_json,
                 nfc_status.card_present ? "true" : "false",
                 (unsigned long)nfc_status.seen_count,
                 (long)nfc_status.token_id,
                 uid_json,
                 last_seen_uid_json);
    if (n < 0 || n >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status too large");
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t resp = httpd_resp_send(req, s_config_json, (ssize_t)n);
    unlock_config_json();
    return resp;
}
