#include <string.h>

#include "unity.h"

#include "cJSON.h"
#include "device_control_ingest.h"
#include "event_bus.h"
#include "gm_game_profile.h"
#include "gm_room_session.h"
#include "orchestrator_audit.h"
#include "orchestrator_registry.h"
#include "orchestrator_timeline.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "service_status.h"
#include "web_ui_handlers.h"
#include "web_ui_utils.h"

#define HTTP_TEST_BODY_MAX 4096

static web_ui_http_adapter_t s_http_adapter;
static httpd_req_t s_http_req;
static char s_http_query[256];
static const char *s_http_body;
static size_t s_http_body_len;
static size_t s_http_body_offset;
static char s_http_response[HTTP_TEST_BODY_MAX];
static char s_http_status[64];
static char s_http_type[64];
static httpd_err_code_t s_http_error;

static void wh_copy(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static esp_err_t fake_resp_send(httpd_req_t *req, const char *body, ssize_t body_len)
{
    size_t len = 0;
    (void)req;
    if (body && body_len == HTTPD_RESP_USE_STRLEN) {
        len = strlen(body);
    } else if (body && body_len > 0) {
        len = (size_t)body_len;
    }
    if (len >= sizeof(s_http_response)) {
        len = sizeof(s_http_response) - 1;
    }
    if (body && len > 0) {
        memcpy(s_http_response, body, len);
    }
    s_http_response[len] = '\0';
    return ESP_OK;
}

static esp_err_t fake_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *message)
{
    (void)req;
    s_http_error = error;
    switch (error) {
    case HTTPD_400_BAD_REQUEST:
        wh_copy(s_http_status, sizeof(s_http_status), "400 Bad Request");
        break;
    case HTTPD_404_NOT_FOUND:
        wh_copy(s_http_status, sizeof(s_http_status), "404 Not Found");
        break;
    case HTTPD_500_INTERNAL_SERVER_ERROR:
        wh_copy(s_http_status, sizeof(s_http_status), "500 Internal Server Error");
        break;
    default:
        wh_copy(s_http_status, sizeof(s_http_status), "error");
        break;
    }
    return fake_resp_send(req, message ? message : "", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t fake_resp_set_status(httpd_req_t *req, const char *status)
{
    (void)req;
    wh_copy(s_http_status, sizeof(s_http_status), status ? status : "");
    return ESP_OK;
}

static esp_err_t fake_resp_set_type(httpd_req_t *req, const char *type)
{
    (void)req;
    wh_copy(s_http_type, sizeof(s_http_type), type ? type : "");
    return ESP_OK;
}

static esp_err_t fake_resp_set_hdr(httpd_req_t *req, const char *field, const char *value)
{
    (void)req;
    (void)field;
    (void)value;
    return ESP_OK;
}

static int fake_req_recv(httpd_req_t *req, char *buf, size_t buf_len)
{
    size_t remaining = 0;
    size_t to_copy = 0;
    (void)req;
    if (!buf || buf_len == 0 || !s_http_body) {
        return 0;
    }
    if (s_http_body_offset >= s_http_body_len) {
        return 0;
    }
    remaining = s_http_body_len - s_http_body_offset;
    to_copy = remaining < buf_len ? remaining : buf_len;
    memcpy(buf, s_http_body + s_http_body_offset, to_copy);
    s_http_body_offset += to_copy;
    return (int)to_copy;
}

static esp_err_t fake_req_get_url_query_str(httpd_req_t *req, char *buf, size_t buf_len)
{
    (void)req;
    if (!buf || buf_len == 0 || !s_http_query[0]) {
        return ESP_ERR_NOT_FOUND;
    }
    if (strlen(s_http_query) >= buf_len) {
        return ESP_ERR_HTTPD_RESULT_TRUNC;
    }
    wh_copy(buf, buf_len, s_http_query);
    return ESP_OK;
}

static esp_err_t fake_query_key_value(const char *query, const char *key, char *val, size_t val_size)
{
    const char *p = query;
    size_t key_len = key ? strlen(key) : 0;
    if (!query || !key || !val || val_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    while (p && *p) {
        const char *next = strchr(p, '&');
        size_t pair_len = next ? (size_t)(next - p) : strlen(p);
        if (pair_len > key_len && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            size_t value_len = pair_len - key_len - 1;
            if (value_len >= val_size) {
                return ESP_ERR_HTTPD_RESULT_TRUNC;
            }
            memcpy(val, p + key_len + 1, value_len);
            val[value_len] = '\0';
            return ESP_OK;
        }
        p = next ? next + 1 : NULL;
    }
    return ESP_ERR_NOT_FOUND;
}

static size_t fake_req_get_hdr_value_len(httpd_req_t *req, const char *field)
{
    (void)req;
    (void)field;
    return 0;
}

static esp_err_t fake_req_get_hdr_value_str(httpd_req_t *req, const char *field, char *val, size_t val_size)
{
    (void)req;
    (void)field;
    (void)val;
    (void)val_size;
    return ESP_ERR_NOT_FOUND;
}

static void http_test_reset_request(const char *query, const char *body)
{
    memset(&s_http_req, 0, sizeof(s_http_req));
    memset(s_http_query, 0, sizeof(s_http_query));
    memset(s_http_response, 0, sizeof(s_http_response));
    memset(s_http_status, 0, sizeof(s_http_status));
    memset(s_http_type, 0, sizeof(s_http_type));
    s_http_error = 0;
    s_http_body = body;
    s_http_body_len = body ? strlen(body) : 0;
    s_http_body_offset = 0;
    s_http_req.content_len = s_http_body_len;
    if (query) {
        wh_copy(s_http_query, sizeof(s_http_query), query);
    }
}

static cJSON *http_test_parse_response(void)
{
    cJSON *root = cJSON_Parse(s_http_response);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, s_http_response);
    return root;
}

static void http_test_install_adapter(void)
{
    memset(&s_http_adapter, 0, sizeof(s_http_adapter));
    s_http_adapter.resp_send = fake_resp_send;
    s_http_adapter.resp_send_err = fake_resp_send_err;
    s_http_adapter.resp_set_status = fake_resp_set_status;
    s_http_adapter.resp_set_type = fake_resp_set_type;
    s_http_adapter.resp_set_hdr = fake_resp_set_hdr;
    s_http_adapter.req_recv = fake_req_recv;
    s_http_adapter.req_get_url_query_str = fake_req_get_url_query_str;
    s_http_adapter.query_key_value = fake_query_key_value;
    s_http_adapter.req_get_hdr_value_len = fake_req_get_hdr_value_len;
    s_http_adapter.req_get_hdr_value_str = fake_req_get_hdr_value_str;
    web_ui_http_set_adapter_for_test(&s_http_adapter);
}

static void handler_bootstrap(void)
{
    http_test_install_adapter();
    TEST_ASSERT_EQUAL(ESP_OK, service_status_init());
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
    TEST_ASSERT_EQUAL(ESP_OK, device_control_ingest_init());
    TEST_ASSERT_EQUAL(ESP_OK, device_control_ingest_reset());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_init());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_clear());
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_init());
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_clear());
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_init());
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_clear());
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_init());
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_clear());
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_init());
    gm_room_session_reset_all();
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_registry_init());
    orchestrator_registry_invalidate();
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_audit_init());
    orchestrator_audit_reset();
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_timeline_init());
    orchestrator_timeline_reset();
}

static void handler_add_room(const char *room_id)
{
    room_catalog_entry_t room = {0};
    wh_copy(room.room_id, sizeof(room.room_id), room_id);
    wh_copy(room.name, sizeof(room.name), "Room A");
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&room));
}

static void test_web_ui_handler_timer_start_validates_query_and_returns_state_json(void)
{
    cJSON *root = NULL;

    handler_bootstrap();
    handler_add_room("room_a");

    http_test_reset_request("", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_start_handler(&s_http_req));
    TEST_ASSERT_EQUAL(HTTPD_400_BAD_REQUEST, s_http_error);
    TEST_ASSERT_EQUAL_STRING("room_id required", s_http_response);

    http_test_reset_request("room_id=room_a&duration_ms=60000", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_start_handler(&s_http_req));
    TEST_ASSERT_EQUAL_STRING("application/json", s_http_type);
    root = http_test_parse_response();
    TEST_ASSERT_EQUAL_STRING("started", cJSON_GetObjectItem(root, "status")->valuestring);
    TEST_ASSERT_EQUAL_STRING("room_a", cJSON_GetObjectItem(root, "room_id")->valuestring);
    TEST_ASSERT_EQUAL_STRING("running", cJSON_GetObjectItem(root, "session_state")->valuestring);
    TEST_ASSERT_EQUAL_STRING("running", cJSON_GetObjectItem(root, "timer_state")->valuestring);
    TEST_ASSERT_EQUAL(60000, cJSON_GetObjectItem(root, "timer_duration_ms")->valueint);
    cJSON_Delete(root);
}

static void test_web_ui_handler_gm_state_returns_stable_json_shape(void)
{
    cJSON *root = NULL;
    cJSON *summary = NULL;
    cJSON *rooms = NULL;

    handler_bootstrap();
    handler_add_room("room_a");

    http_test_reset_request(NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gm_state_handler(&s_http_req));
    TEST_ASSERT_EQUAL_STRING("application/json", s_http_type);
    root = http_test_parse_response();
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "ok")));
    summary = cJSON_GetObjectItem(root, "summary");
    TEST_ASSERT_TRUE(cJSON_IsObject(summary));
    TEST_ASSERT_EQUAL(1, cJSON_GetObjectItem(summary, "rooms_total")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsArray(cJSON_GetObjectItem(root, "devices")));
    TEST_ASSERT_TRUE(cJSON_IsArray(cJSON_GetObjectItem(root, "issues")));
    rooms = cJSON_GetObjectItem(root, "rooms");
    TEST_ASSERT_TRUE(cJSON_IsArray(rooms));
    TEST_ASSERT_EQUAL_STRING("room_a",
                             cJSON_GetObjectItem(cJSON_GetArrayItem(rooms, 0), "room_id")->valuestring);
    cJSON_Delete(root);
}

static void test_web_ui_handler_scenario_validate_rejects_bad_body_and_reports_valid_json(void)
{
    cJSON *root = NULL;
    const char *valid_body =
        "{\"id\":\"scenario_a\",\"name\":\"Scenario A\",\"room_id\":\"room_a\","
        "\"steps\":[{\"id\":\"wait\",\"label\":\"Wait\",\"enabled\":true,"
        "\"type\":\"WAIT_TIME\",\"duration_ms\":1000}]}";

    handler_bootstrap();
    handler_add_room("room_a");

    http_test_reset_request(NULL, "");
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_scenario_validate_handler(&s_http_req));
    TEST_ASSERT_EQUAL(HTTPD_400_BAD_REQUEST, s_http_error);

    http_test_reset_request(NULL, "{");
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_scenario_validate_handler(&s_http_req));
    TEST_ASSERT_EQUAL(HTTPD_400_BAD_REQUEST, s_http_error);

    http_test_reset_request(NULL, valid_body);
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_scenario_validate_handler(&s_http_req));
    TEST_ASSERT_EQUAL_STRING("application/json", s_http_type);
    root = http_test_parse_response();
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "ok")));
    TEST_ASSERT_EQUAL_STRING("scenario_a", cJSON_GetObjectItem(root, "scenario_id")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "valid")));
    TEST_ASSERT_TRUE(cJSON_IsArray(cJSON_GetObjectItem(root, "issues")));
    cJSON_Delete(root);
}

static void test_web_ui_handler_game_action_maps_missing_room_to_json_error(void)
{
    cJSON *root = NULL;

    handler_bootstrap();

    http_test_reset_request("", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_game_start_handler(&s_http_req));
    TEST_ASSERT_EQUAL_STRING("400 Bad Request", s_http_status);
    root = http_test_parse_response();
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(root, "ok")));
    TEST_ASSERT_EQUAL_STRING("invalid_request", cJSON_GetObjectItem(root, "error")->valuestring);
    cJSON_Delete(root);

    http_test_reset_request("room_id=missing", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_game_start_handler(&s_http_req));
    TEST_ASSERT_EQUAL_STRING("404 Not Found", s_http_status);
    root = http_test_parse_response();
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(root, "ok")));
    TEST_ASSERT_EQUAL_STRING("room_not_found", cJSON_GetObjectItem(root, "error")->valuestring);
    TEST_ASSERT_EQUAL_STRING("missing", cJSON_GetObjectItem(root, "room_id")->valuestring);
    TEST_ASSERT_EQUAL_STRING("start_game", cJSON_GetObjectItem(root, "action_id")->valuestring);
    cJSON_Delete(root);
}

static void test_web_ui_profile_handlers_reject_bad_body_before_persistence(void)
{
    handler_bootstrap();

    http_test_reset_request(NULL, "");
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_profile_select_handler(&s_http_req));
    TEST_ASSERT_EQUAL(HTTPD_400_BAD_REQUEST, s_http_error);
    TEST_ASSERT_EQUAL_STRING("invalid game profile request", s_http_response);

    http_test_reset_request(NULL, "{\"room_id\":\"room_a\"}");
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_profile_select_handler(&s_http_req));
    TEST_ASSERT_EQUAL(HTTPD_400_BAD_REQUEST, s_http_error);
    TEST_ASSERT_EQUAL_STRING("invalid game profile request", s_http_response);

    http_test_reset_request(NULL, "{");
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_profile_save_handler(&s_http_req));
    TEST_ASSERT_EQUAL(HTTPD_400_BAD_REQUEST, s_http_error);
    TEST_ASSERT_EQUAL_STRING("invalid game profile request", s_http_response);

    http_test_reset_request(NULL, "{}");
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_profile_delete_handler(&s_http_req));
    TEST_ASSERT_EQUAL(HTTPD_400_BAD_REQUEST, s_http_error);
    TEST_ASSERT_EQUAL_STRING("invalid game profile request", s_http_response);
}

static void test_web_ui_device_handlers_reject_bad_body_before_persistence(void)
{
    handler_bootstrap();

    http_test_reset_request(NULL, "");
    TEST_ASSERT_EQUAL(ESP_OK, gm_quest_device_save_handler(&s_http_req));
    TEST_ASSERT_EQUAL(HTTPD_400_BAD_REQUEST, s_http_error);
    TEST_ASSERT_EQUAL_STRING("invalid quest device request", s_http_response);

    http_test_reset_request(NULL, "{");
    TEST_ASSERT_EQUAL(ESP_OK, gm_quest_device_save_handler(&s_http_req));
    TEST_ASSERT_EQUAL(HTTPD_400_BAD_REQUEST, s_http_error);
    TEST_ASSERT_EQUAL_STRING("invalid quest device request", s_http_response);

    http_test_reset_request(NULL, "{}");
    TEST_ASSERT_EQUAL(ESP_OK, gm_quest_device_delete_handler(&s_http_req));
    TEST_ASSERT_EQUAL(HTTPD_400_BAD_REQUEST, s_http_error);
    TEST_ASSERT_EQUAL_STRING("invalid quest device request", s_http_response);
}

void register_web_ui_handler_tests(void)
{
    RUN_TEST(test_web_ui_handler_timer_start_validates_query_and_returns_state_json);
    RUN_TEST(test_web_ui_handler_gm_state_returns_stable_json_shape);
    RUN_TEST(test_web_ui_handler_scenario_validate_rejects_bad_body_and_reports_valid_json);
    RUN_TEST(test_web_ui_handler_game_action_maps_missing_room_to_json_error);
    RUN_TEST(test_web_ui_profile_handlers_reject_bad_body_before_persistence);
    RUN_TEST(test_web_ui_device_handlers_reject_bad_body_before_persistence);
    web_ui_http_reset_adapter_for_test();
}
