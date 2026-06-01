#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "config_store.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "scenehub_config.h"

#include "web_ui_auth.h"

#define WEB_SESSION_MAX        6
#define WEB_SESSION_TOKEN_LEN  64
#define WEB_SESSION_TTL_US     (12LL * 60 * 60 * 1000000)
#define WEB_AUTH_TRACE_HTTP    0

typedef struct {
    bool in_use;
    char token[WEB_SESSION_TOKEN_LEN];
    int64_t expires_at;
    web_user_role_t role;
    char username[CONFIG_STORE_USERNAME_MAX];
} web_session_entry_t;

extern const char *g_web_ui_auth_tag;
extern SemaphoreHandle_t g_web_ui_auth_session_mutex;
extern web_session_entry_t g_web_ui_auth_sessions[WEB_SESSION_MAX];

esp_err_t web_http_check(esp_err_t err, const char *context);
#define WEB_HTTP_CHECK(call) web_http_check((call), __func__)

bool auth_hash_equal_consttime(const uint8_t *a, const uint8_t *b, size_t len);
void web_session_generate_token(char *out, size_t len);
void web_session_store(const char *token, web_user_role_t role, const char *username);
bool web_session_validate(const char *token, web_user_role_t *out_role, char *out_username, size_t username_len);
void web_session_remove(const char *token);
bool read_cookie_value(httpd_req_t *req, const char *name, char *out, size_t out_len);
bool web_session_get_info(httpd_req_t *req, web_user_role_t *role_out, char *username_out, size_t username_len);
const char *web_role_to_string(web_user_role_t role);
bool web_role_allows(web_user_role_t have, web_user_role_t required);
bool web_ui_require_session(httpd_req_t *req, bool redirect_on_fail, web_user_role_t *role_out);
char *read_request_body(httpd_req_t *req, size_t max_len);
esp_err_t auth_login_reject(httpd_req_t *req, const char *message);
