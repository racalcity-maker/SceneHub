#include "web_ui_auth_internal.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

void web_session_generate_token(char *out, size_t len)
{
    static const char *hex = "0123456789abcdef";
    if (!out || len < 33) {
        return;
    }
    for (size_t i = 0; i < (len - 1) / 2; ++i) {
        uint32_t r = esp_random();
        out[i * 2] = hex[(r >> 4) & 0x0F];
        out[i * 2 + 1] = hex[r & 0x0F];
    }
    out[len - 1] = 0;
}

void web_session_store(const char *token, web_user_role_t role, const char *username)
{
    if (!token || !token[0] || !g_web_ui_auth_session_mutex) {
        return;
    }
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(g_web_ui_auth_session_mutex, portMAX_DELAY);
    web_session_entry_t *slot = NULL;
    for (size_t i = 0; i < WEB_SESSION_MAX; ++i) {
        web_session_entry_t *entry = &g_web_ui_auth_sessions[i];
        if (entry->in_use && strcmp(entry->token, token) == 0) {
            slot = entry;
            break;
        }
        if (!entry->in_use || entry->expires_at < now) {
            slot = entry;
        }
    }
    if (slot) {
        memset(slot->token, 0, sizeof(slot->token));
        strncpy(slot->token, token, sizeof(slot->token) - 1);
        slot->expires_at = now + WEB_SESSION_TTL_US;
        slot->role = role;
        memset(slot->username, 0, sizeof(slot->username));
        if (username && username[0]) {
            strncpy(slot->username, username, sizeof(slot->username) - 1);
        }
        slot->in_use = true;
    }
    xSemaphoreGive(g_web_ui_auth_session_mutex);
}

bool web_session_validate(const char *token, web_user_role_t *out_role, char *out_username, size_t username_len)
{
    if (!token || !token[0] || !g_web_ui_auth_session_mutex) {
        return false;
    }
    bool valid = false;
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(g_web_ui_auth_session_mutex, portMAX_DELAY);
    for (size_t i = 0; i < WEB_SESSION_MAX; ++i) {
        web_session_entry_t *entry = &g_web_ui_auth_sessions[i];
        if (!entry->in_use) {
            continue;
        }
        if (entry->expires_at < now) {
            entry->in_use = false;
            continue;
        }
        if (strcmp(entry->token, token) == 0) {
            entry->expires_at = now + WEB_SESSION_TTL_US;
            if (out_role) {
                *out_role = entry->role;
            }
            if (out_username && username_len > 0) {
                strncpy(out_username, entry->username, username_len - 1);
                out_username[username_len - 1] = '\0';
            }
            valid = true;
            break;
        }
    }
    xSemaphoreGive(g_web_ui_auth_session_mutex);
    return valid;
}

void web_session_remove(const char *token)
{
    if (!token || !token[0] || !g_web_ui_auth_session_mutex) {
        return;
    }
    xSemaphoreTake(g_web_ui_auth_session_mutex, portMAX_DELAY);
    for (size_t i = 0; i < WEB_SESSION_MAX; ++i) {
        web_session_entry_t *entry = &g_web_ui_auth_sessions[i];
        if (entry->in_use && strcmp(entry->token, token) == 0) {
            entry->in_use = false;
            break;
        }
    }
    xSemaphoreGive(g_web_ui_auth_session_mutex);
}

bool web_session_get_info(httpd_req_t *req, web_user_role_t *role_out, char *username_out, size_t username_len)
{
    char token[WEB_SESSION_TOKEN_LEN] = {0};
    if (!read_cookie_value(req, "broker_sid", token, sizeof(token))) {
        return false;
    }
    return web_session_validate(token, role_out, username_out, username_len);
}

bool web_auth_get_session_info(httpd_req_t *req, web_user_role_t *role_out, char *username_out, size_t username_len)
{
    return web_session_get_info(req, role_out, username_out, username_len);
}

esp_err_t web_sessions_init(void)
{
    if (!g_web_ui_auth_session_mutex) {
        g_web_ui_auth_session_mutex = xSemaphoreCreateMutex();
    }
    if (!g_web_ui_auth_session_mutex) {
        ESP_LOGE(g_web_ui_auth_tag, "failed to create session mutex");
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(g_web_ui_auth_session_mutex, portMAX_DELAY) == pdTRUE) {
        memset(g_web_ui_auth_sessions, 0, sizeof(g_web_ui_auth_sessions));
        xSemaphoreGive(g_web_ui_auth_session_mutex);
        return ESP_OK;
    }
    ESP_LOGE(g_web_ui_auth_tag, "failed to lock session mutex during init");
    return ESP_ERR_TIMEOUT;
}

void web_sessions_clear(void)
{
    if (!g_web_ui_auth_session_mutex) {
        return;
    }
    xSemaphoreTake(g_web_ui_auth_session_mutex, portMAX_DELAY);
    memset(g_web_ui_auth_sessions, 0, sizeof(g_web_ui_auth_sessions));
    xSemaphoreGive(g_web_ui_auth_session_mutex);
}
