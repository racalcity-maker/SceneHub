#include <string.h>

#include "unity.h"

#include "cJSON.h"
#include "device_control_ingest.h"
#include "esp_attr.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

#ifndef CONFIG_SCENEHUB_GM_RUNTIME_TICK_MS
#define CONFIG_SCENEHUB_GM_RUNTIME_TICK_MS 100
#endif

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
EXT_RAM_BSS_ATTR static room_scenario_t s_handler_scenario;

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

static esp_err_t fake_resp_send_chunk(httpd_req_t *req, const char *body, ssize_t body_len)
{
    size_t used = strlen(s_http_response);
    size_t len = 0;
    (void)req;
    if (!body) {
        return ESP_OK;
    }
    if (body_len == HTTPD_RESP_USE_STRLEN) {
        len = strlen(body);
    } else if (body_len > 0) {
        len = (size_t)body_len;
    }
    if (used >= sizeof(s_http_response) - 1) {
        return ESP_OK;
    }
    if (len > sizeof(s_http_response) - 1 - used) {
        len = sizeof(s_http_response) - 1 - used;
    }
    if (len > 0) {
        memcpy(s_http_response + used, body, len);
    }
    s_http_response[used + len] = '\0';
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

static void handler_assert_room_runtime_contract(cJSON *root)
{
    static const char *required_fields[] = {
        "ok",
        "runtime_schema_version",
        "room_id",
        "session_present",
        "session_state",
        "timer_state",
        "timer_duration_ms",
        "timer_remaining_ms",
        "hint_active",
        "hint_sent_count",
        "hint_message",
        "selected_profile_id",
        "selected_profile_name",
        "selected_profile_scenario_id",
        "selected_profile_duration_ms",
        "selected_scenario_id",
        "selected_scenario_name",
        "running_scenario_id",
        "running_scenario_name",
        "running_scenario_generation",
        "scenario_runtime_state",
        "scenario_current_step_index",
        "scenario_wait_type",
        "scenario_wait_until_ms",
        "scenario_wait_started_at_ms",
        "scenario_wait_event_type",
        "scenario_wait_source_id",
        "scenario_wait_events",
        "scenario_wait_event_count",
        "scenario_wait_flags",
        "scenario_wait_flag_count",
        "scenario_wait_operator_prompt",
        "scenario_wait_operator_label",
        "scenario_wait_operator_skip_allowed",
        "scenario_wait_operator_skip_label",
        "scenario_operator_message",
        "scenario_flags",
        "scenario_flag_count",
        "scenario_branches",
        "scenario_branch_count",
        "scenario_last_error",
        "asset_prepare_state",
        "asset_audio_total",
        "asset_audio_ready",
        "asset_audio_missing",
        "asset_audio_bad",
        "asset_audio_unsupported",
        "asset_audio_io_error",
        "asset_audio_unknown",
    };
    TEST_ASSERT_NOT_NULL(root);
    for (size_t i = 0; i < sizeof(required_fields) / sizeof(required_fields[0]); ++i) {
        TEST_ASSERT_NOT_NULL_MESSAGE(cJSON_GetObjectItem(root, required_fields[i]), required_fields[i]);
    }
}

static void http_test_install_adapter(void)
{
    memset(&s_http_adapter, 0, sizeof(s_http_adapter));
    s_http_adapter.resp_send = fake_resp_send;
    s_http_adapter.resp_send_chunk = fake_resp_send_chunk;
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

static room_scenario_step_t *handler_add_step(room_scenario_t *scenario,
                                              const char *id,
                                              const char *label,
                                              room_scenario_step_type_t type)
{
    room_scenario_step_t *step = NULL;
    TEST_ASSERT_NOT_NULL(scenario);
    TEST_ASSERT_TRUE(scenario->step_count < ROOM_SCENARIO_MAX_STEPS);
    step = &scenario->steps[scenario->step_count++];
    memset(step, 0, sizeof(*step));
    wh_copy(step->id, sizeof(step->id), id);
    wh_copy(step->label, sizeof(step->label), label);
    step->enabled = true;
    step->type = type;
    return step;
}

static bool handler_response_has_flag(cJSON *root, const char *name, bool expected_value)
{
    cJSON *flags = cJSON_GetObjectItem(root, "scenario_flags");
    cJSON *flag = NULL;
    cJSON_ArrayForEach(flag, flags) {
        cJSON *name_item = cJSON_GetObjectItem(flag, "name");
        cJSON *value_item = cJSON_GetObjectItem(flag, "value");
        if (cJSON_IsString(name_item) &&
            strcmp(name_item->valuestring, name) == 0 &&
            ((expected_value && cJSON_IsTrue(value_item)) ||
             (!expected_value && cJSON_IsFalse(value_item)))) {
            return true;
        }
    }
    return false;
}

static void test_web_ui_handler_timer_start_validates_query_and_returns_accepted_json(void)
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
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "ok")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "accepted")));
    cJSON_Delete(root);
}

static void test_web_ui_handler_room_runtime_returns_timer_state_json(void)
{
    cJSON *root = NULL;

    handler_bootstrap();
    handler_add_room("room_a");

    http_test_reset_request("room_id=room_a&duration_ms=60000", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_start_handler(&s_http_req));

    http_test_reset_request("room_id=room_a", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_runtime_state_handler(&s_http_req));
    TEST_ASSERT_EQUAL_STRING("application/json", s_http_type);
    root = http_test_parse_response();
    handler_assert_room_runtime_contract(root);
    TEST_ASSERT_EQUAL_STRING("room_a", cJSON_GetObjectItem(root, "room_id")->valuestring);
    TEST_ASSERT_EQUAL(1, cJSON_GetObjectItem(root, "runtime_schema_version")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "session_present")));
    TEST_ASSERT_EQUAL_STRING("running", cJSON_GetObjectItem(root, "session_state")->valuestring);
    TEST_ASSERT_EQUAL_STRING("running", cJSON_GetObjectItem(root, "timer_state")->valuestring);
    TEST_ASSERT_EQUAL(60000, cJSON_GetObjectItem(root, "timer_duration_ms")->valueint);
    TEST_ASSERT_EQUAL_STRING("none", cJSON_GetObjectItem(root, "asset_prepare_state")->valuestring);
    cJSON_Delete(root);
}

static void test_web_ui_handler_room_runtime_returns_idle_for_room_without_session(void)
{
    cJSON *root = NULL;

    handler_bootstrap();
    handler_add_room("room_a");

    http_test_reset_request("room_id=room_a", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_runtime_state_handler(&s_http_req));
    TEST_ASSERT_EQUAL_STRING("application/json", s_http_type);
    root = http_test_parse_response();
    handler_assert_room_runtime_contract(root);
    TEST_ASSERT_EQUAL_STRING("room_a", cJSON_GetObjectItem(root, "room_id")->valuestring);
    TEST_ASSERT_EQUAL(1, cJSON_GetObjectItem(root, "runtime_schema_version")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(root, "session_present")));
    TEST_ASSERT_EQUAL_STRING("idle", cJSON_GetObjectItem(root, "session_state")->valuestring);
    TEST_ASSERT_EQUAL_STRING("idle", cJSON_GetObjectItem(root, "timer_state")->valuestring);
    TEST_ASSERT_EQUAL_STRING("idle", cJSON_GetObjectItem(root, "scenario_runtime_state")->valuestring);
    cJSON_Delete(root);
}

static void test_web_ui_handler_room_runtime_ticks_wait_flags_without_gm_state_refresh(void)
{
    room_scenario_step_t *step = NULL;
    cJSON *root = NULL;

    handler_bootstrap();
    handler_add_room("room_a");

    memset(&s_handler_scenario, 0, sizeof(s_handler_scenario));
    wh_copy(s_handler_scenario.id, sizeof(s_handler_scenario.id), "scenario_flags");
    wh_copy(s_handler_scenario.name, sizeof(s_handler_scenario.name), "Scenario Flags");
    wh_copy(s_handler_scenario.room_id, sizeof(s_handler_scenario.room_id), "room_a");

    s_handler_scenario.branch_count = 2;
    wh_copy(s_handler_scenario.branches[0].id, sizeof(s_handler_scenario.branches[0].id), "main");
    wh_copy(s_handler_scenario.branches[0].name, sizeof(s_handler_scenario.branches[0].name), "Main");
    s_handler_scenario.branches[0].type = ROOM_SCENARIO_BRANCH_NORMAL;
    s_handler_scenario.branches[0].enabled = true;
    s_handler_scenario.branches[0].required_for_completion = true;
    s_handler_scenario.branches[0].step_start_index = 0;
    s_handler_scenario.branches[0].step_count = 3;

    wh_copy(s_handler_scenario.branches[1].id, sizeof(s_handler_scenario.branches[1].id), "branch_2");
    wh_copy(s_handler_scenario.branches[1].name, sizeof(s_handler_scenario.branches[1].name), "Branch 2");
    s_handler_scenario.branches[1].type = ROOM_SCENARIO_BRANCH_NORMAL;
    s_handler_scenario.branches[1].enabled = true;
    s_handler_scenario.branches[1].required_for_completion = true;
    s_handler_scenario.branches[1].step_start_index = 3;
    s_handler_scenario.branches[1].step_count = 3;

    step = handler_add_step(&s_handler_scenario, "main_gate", "Operator approval", ROOM_SCENARIO_STEP_OPERATOR_APPROVAL);
    wh_copy(step->data.operator_approval.prompt, sizeof(step->data.operator_approval.prompt), "Continue?");
    wh_copy(step->data.operator_approval.approve_label, sizeof(step->data.operator_approval.approve_label), "Continue");
    step = handler_add_step(&s_handler_scenario, "main_flag", "Set done1", ROOM_SCENARIO_STEP_SET_FLAG);
    wh_copy(step->data.set_flag.name, sizeof(step->data.set_flag.name), "done1");
    step->data.set_flag.value = true;
    step = handler_add_step(&s_handler_scenario, "main_stop", "Main stop", ROOM_SCENARIO_STEP_OPERATOR_APPROVAL);
    wh_copy(step->data.operator_approval.prompt, sizeof(step->data.operator_approval.prompt), "Stop?");
    wh_copy(step->data.operator_approval.approve_label, sizeof(step->data.operator_approval.approve_label), "Continue");

    step = handler_add_step(&s_handler_scenario, "branch_wait", "Wait done1", ROOM_SCENARIO_STEP_WAIT_FLAGS);
    step->data.wait_flags.flag_count = 1;
    wh_copy(step->data.wait_flags.flags[0].name, sizeof(step->data.wait_flags.flags[0].name), "done1");
    step->data.wait_flags.flags[0].value = true;
    step = handler_add_step(&s_handler_scenario, "branch_flag", "Set done2", ROOM_SCENARIO_STEP_SET_FLAG);
    wh_copy(step->data.set_flag.name, sizeof(step->data.set_flag.name), "done2");
    step->data.set_flag.value = true;
    step = handler_add_step(&s_handler_scenario, "branch_stop", "Branch stop", ROOM_SCENARIO_STEP_OPERATOR_APPROVAL);
    wh_copy(step->data.operator_approval.prompt, sizeof(step->data.operator_approval.prompt), "Branch?");
    wh_copy(step->data.operator_approval.approve_label, sizeof(step->data.operator_approval.approve_label), "Continue");

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&s_handler_scenario));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_select_scenario("room_a", "scenario_flags"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_start("room_a"));

    http_test_reset_request("room_id=room_a", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_scenario_approve_handler(&s_http_req));

    vTaskDelay(pdMS_TO_TICKS(CONFIG_SCENEHUB_GM_RUNTIME_TICK_MS + 20));

    http_test_reset_request("room_id=room_a", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_runtime_state_handler(&s_http_req));
    TEST_ASSERT_EQUAL_STRING("application/json", s_http_type);
    root = http_test_parse_response();
    TEST_ASSERT_TRUE(handler_response_has_flag(root, "done1", true));
    TEST_ASSERT_TRUE(handler_response_has_flag(root, "done2", true));
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
    RUN_TEST(test_web_ui_handler_timer_start_validates_query_and_returns_accepted_json);
    RUN_TEST(test_web_ui_handler_room_runtime_returns_timer_state_json);
    RUN_TEST(test_web_ui_handler_room_runtime_returns_idle_for_room_without_session);
    RUN_TEST(test_web_ui_handler_room_runtime_ticks_wait_flags_without_gm_state_refresh);
    RUN_TEST(test_web_ui_handler_gm_state_returns_stable_json_shape);
    RUN_TEST(test_web_ui_handler_scenario_validate_rejects_bad_body_and_reports_valid_json);
    RUN_TEST(test_web_ui_handler_game_action_maps_missing_room_to_json_error);
    RUN_TEST(test_web_ui_profile_handlers_reject_bad_body_before_persistence);
    RUN_TEST(test_web_ui_device_handlers_reject_bad_body_before_persistence);
    web_ui_http_reset_adapter_for_test();
}
