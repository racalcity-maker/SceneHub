#include "web_ui_auth_internal.h"

#include <stdio.h>
#include <strings.h>

#include "cJSON.h"
#include "config_store.h"
#include "esp_log.h"
#include "web_ui_page.h"
#include "web_ui_utils.h"

static esp_err_t web_same_origin_reject(httpd_req_t *req)
{
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    return WEB_HTTP_CHECK(httpd_resp_send(req,
                                          "{\"error\":\"csrf\",\"message\":\"same-origin check failed\"}",
                                          HTTPD_RESP_USE_STRLEN));
}

esp_err_t login_page_handler(httpd_req_t *req)
{
    char token[WEB_SESSION_TOKEN_LEN] = {0};
    if (read_cookie_value(req, "broker_sid", token, sizeof(token)) &&
        web_session_validate(token, NULL, NULL, 0)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        return WEB_HTTP_CHECK(httpd_resp_send(req, NULL, 0));
    }
    httpd_resp_set_type(req, "text/html");
    return WEB_HTTP_CHECK(httpd_resp_send(req, web_ui_get_login_html(), HTTPD_RESP_USE_STRLEN));
}

esp_err_t auth_login_handler(httpd_req_t *req)
{
    if (!web_ui_is_same_origin_request(req)) {
        return web_same_origin_reject(req);
    }
    char *body = read_request_body(req, 1024);
    if (!body) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body required"));
    }
    cJSON *json = cJSON_Parse(body);
    web_ui_free(body);
    if (!json) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json"));
    }
    const cJSON *username_item = cJSON_GetObjectItem(json, "username");
    const cJSON *password_item = cJSON_GetObjectItem(json, "password");
    if (!cJSON_IsString(username_item) || !cJSON_IsString(password_item)) {
        cJSON_Delete(json);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields"));
    }
    const char *username = username_item->valuestring;
    const char *password = password_item->valuestring;

    uint8_t hash[CONFIG_STORE_AUTH_HASH_LEN] = {0};
    config_store_hash_password(password, hash);
    const app_config_t *cfg = config_store_get();
    if (!cfg) {
        cJSON_Delete(json);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config missing"));
    }

    bool admin_username_match = strcasecmp(cfg->web.username, username) == 0;
    bool admin_password_match = admin_username_match &&
        auth_hash_equal_consttime(cfg->web.password_hash, hash, CONFIG_STORE_AUTH_HASH_LEN);
    bool user_account_exists = config_store_has_web_user(cfg);
    bool user_username_match = user_account_exists &&
        strcasecmp(cfg->web_user.username, username) == 0;
    bool user_password_match = user_username_match &&
        auth_hash_equal_consttime(cfg->web_user.password_hash, hash, CONFIG_STORE_AUTH_HASH_LEN);

    const app_web_auth_t *target = NULL;
    web_user_role_t session_role = WEB_USER_ROLE_ADMIN;

    if (admin_password_match) {
        target = &cfg->web;
        session_role = WEB_USER_ROLE_ADMIN;
    } else if (user_password_match) {
        target = &cfg->web_user;
        session_role = WEB_USER_ROLE_USER;
    } else if (admin_username_match || user_username_match) {
        ESP_LOGW(g_web_ui_auth_tag, "login rejected for username=%s: invalid password", username);
        cJSON_Delete(json);
        return auth_login_reject(req, "Incorrect password.");
    } else {
        ESP_LOGW(g_web_ui_auth_tag, "login rejected for username=%s: unknown username", username);
        cJSON_Delete(json);
        return auth_login_reject(req, "Unknown username.");
    }

    if (!target) {
        ESP_LOGW(g_web_ui_auth_tag, "login rejected for username=%s: account resolution failed", username);
        cJSON_Delete(json);
        return auth_login_reject(req, "Login failed.");
    }
    ESP_LOGI(g_web_ui_auth_tag, "login ok: username=%s role=%s", username, web_role_to_string(session_role));
    cJSON_Delete(json);
    char token[WEB_SESSION_TOKEN_LEN] = {0};
    web_session_generate_token(token, sizeof(token));
    web_session_store(token, session_role, target->username);
    char cookie[WEB_SESSION_TOKEN_LEN + 80];
    snprintf(cookie, sizeof(cookie), "broker_sid=%s; Path=/; HttpOnly; SameSite=Strict", token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    httpd_resp_set_type(req, "application/json");
    return WEB_HTTP_CHECK(httpd_resp_send(req,
                                          session_role == WEB_USER_ROLE_ADMIN
                                              ? "{\"status\":\"ok\",\"role\":\"admin\"}"
                                              : "{\"status\":\"ok\",\"role\":\"user\"}",
                                          HTTPD_RESP_USE_STRLEN));
}

esp_err_t session_info_handler(httpd_req_t *req)
{
    web_user_role_t role = WEB_USER_ROLE_ADMIN;
    char username[CONFIG_STORE_USERNAME_MAX] = {0};
    if (!web_session_get_info(req, &role, username, sizeof(username))) {
        httpd_resp_set_status(req, "401 Unauthorized");
        WEB_HTTP_CHECK(httpd_resp_send(req, "{}", HTTPD_RESP_USE_STRLEN));
        return ESP_OK;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }
    cJSON_AddStringToObject(root, "role", web_role_to_string(role));
    cJSON_AddStringToObject(root, "username", username);
    return WEB_HTTP_CHECK(web_ui_send_json(req, root));
}

esp_err_t auth_logout_handler(httpd_req_t *req)
{
    char token[WEB_SESSION_TOKEN_LEN] = {0};
    if (read_cookie_value(req, "broker_sid", token, sizeof(token))) {
        web_session_remove(token);
    }
    httpd_resp_set_hdr(req, "Set-Cookie", "broker_sid=deleted; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    httpd_resp_set_type(req, "application/json");
    return WEB_HTTP_CHECK(httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN));
}

esp_err_t auth_password_handler(httpd_req_t *req)
{
    char *body = read_request_body(req, 1024);
    if (!body) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body required"));
    }
    cJSON *json = cJSON_Parse(body);
    web_ui_free(body);
    if (!json) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json"));
    }
    const cJSON *new_user_item = cJSON_GetObjectItem(json, "username");
    const cJSON *current_item = cJSON_GetObjectItem(json, "current_password");
    const cJSON *next_item = cJSON_GetObjectItem(json, "new_password");
    const cJSON *role_item = cJSON_GetObjectItem(json, "role");
    const char *new_user = cJSON_IsString(new_user_item) ? new_user_item->valuestring : NULL;
    const char *current = cJSON_IsString(current_item) ? current_item->valuestring : NULL;
    const char *next = cJSON_IsString(next_item) ? next_item->valuestring : NULL;
    const char *role_str = cJSON_IsString(role_item) ? role_item->valuestring : NULL;
    bool target_user = role_str && strcasecmp(role_str, "user") == 0;
    if (!current || !current[0]) {
        cJSON_Delete(json);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing current password"));
    }
    if (!next || !next[0]) {
        cJSON_Delete(json);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing new password"));
    }
    if (target_user && (!new_user || !new_user[0])) {
        cJSON_Delete(json);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "username required"));
    }
    const app_config_t *cfg = config_store_get();
    if (!cfg) {
        cJSON_Delete(json);
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config missing"));
    }
    uint8_t hash[CONFIG_STORE_AUTH_HASH_LEN] = {0};
    config_store_hash_password(current, hash);
    if (!auth_hash_equal_consttime(cfg->web.password_hash, hash, CONFIG_STORE_AUTH_HASH_LEN)) {
        ESP_LOGW(g_web_ui_auth_tag, "password change rejected: invalid admin confirmation for role=%s",
                 target_user ? "user" : "admin");
        cJSON_Delete(json);
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        WEB_HTTP_CHECK(httpd_resp_send(req, "{\"error\":\"invalid\"}", HTTPD_RESP_USE_STRLEN));
        return ESP_OK;
    }
    esp_err_t err = ESP_OK;
    if (target_user) {
        uint8_t user_hash[CONFIG_STORE_AUTH_HASH_LEN];
        config_store_hash_password(next, user_hash);
        err = config_store_set_web_user(new_user, user_hash, true);
        if (err != ESP_OK) {
            cJSON_Delete(json);
            ESP_LOGE(g_web_ui_auth_tag, "failed to update user auth: %s", esp_err_to_name(err));
            return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed"));
        }
        cJSON_Delete(json);
        web_sessions_clear();
        httpd_resp_set_hdr(req, "Set-Cookie", "broker_sid=deleted; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
        httpd_resp_set_type(req, "application/json");
        return WEB_HTTP_CHECK(httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN));
    }
    const char *username = (new_user && new_user[0]) ? new_user : cfg->web.username;
    uint8_t new_hash[CONFIG_STORE_AUTH_HASH_LEN];
    config_store_hash_password(next, new_hash);
    err = config_store_set_web_auth(username, new_hash);
    if (err != ESP_OK) {
        cJSON_Delete(json);
        ESP_LOGE(g_web_ui_auth_tag, "failed to update web auth: %s", esp_err_to_name(err));
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed"));
    }
    cJSON_Delete(json);
    web_sessions_clear();
    httpd_resp_set_hdr(req, "Set-Cookie", "broker_sid=deleted; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    httpd_resp_set_type(req, "application/json");
    return WEB_HTTP_CHECK(httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN));
}
