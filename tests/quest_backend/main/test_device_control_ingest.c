#include <string.h>

#include "unity.h"

#include "device_control_ingest.h"

static void dci_test_bootstrap(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, device_control_ingest_init());
    TEST_ASSERT_EQUAL(ESP_OK, device_control_ingest_reset());
}

static void test_control_ingest_parses_heartbeat_status_diag_result(void)
{
    device_control_ingest_device_t state = {0};

    dci_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK,
                      device_control_ingest_handle_mqtt("cp/v1/dev/pipa/heartbeat",
                                                        "{\"ts_ms\":1000,\"boot_id\":\"boot-a\",\"uptime_ms\":77,\"status_seq\":9}"));
    TEST_ASSERT_EQUAL(ESP_OK,
                      device_control_ingest_handle_mqtt("cp/v1/dev/pipa/status",
                                                        "{\"ts_ms\":1010,\"boot_id\":\"boot-a\",\"fw_version\":\"1.0.3\",\"mode\":\"normal\",\"state\":\"idle\",\"health\":\"ok\",\"runtime\":{\"active\":false}}"));
    TEST_ASSERT_EQUAL(ESP_OK,
                      device_control_ingest_handle_mqtt("cp/v1/dev/pipa/diag",
                                                        "{\"ts_ms\":1020,\"level\":\"warn\",\"code\":\"sensor_timeout\",\"message\":\"No pulse\"}"));
    TEST_ASSERT_EQUAL(ESP_OK,
                      device_control_ingest_handle_mqtt("cp/v1/dev/pipa/result",
                                                        "{\"ts_ms\":1030,\"request_id\":\"req-1\",\"command\":\"refresh_status\",\"status\":\"error\",\"error\":{\"code\":\"busy\",\"message\":\"still running\"}}"));

    TEST_ASSERT_EQUAL(ESP_OK, device_control_ingest_get_device("pipa", &state));
    TEST_ASSERT_EQUAL_STRING("pipa", state.device_id);
    TEST_ASSERT_TRUE(state.has_heartbeat);
    TEST_ASSERT_TRUE(state.heartbeat_ts_ms == 1000ULL);
    TEST_ASSERT_EQUAL_STRING("boot-a", state.heartbeat_boot_id);
    TEST_ASSERT_TRUE(state.heartbeat_uptime_ms == 77ULL);
    TEST_ASSERT_EQUAL_UINT32(9, state.heartbeat_status_seq);
    TEST_ASSERT_TRUE(state.has_status);
    TEST_ASSERT_EQUAL_STRING("1.0.3", state.status_fw_version);
    TEST_ASSERT_EQUAL_STRING("idle", state.status_state);
    TEST_ASSERT_EQUAL_STRING("ok", state.status_health);
    TEST_ASSERT_FALSE(state.status_runtime_active);
    TEST_ASSERT_TRUE(state.has_diag);
    TEST_ASSERT_EQUAL_STRING("warn", state.diag_level);
    TEST_ASSERT_EQUAL_STRING("sensor_timeout", state.diag_code);
    TEST_ASSERT_EQUAL_STRING("No pulse", state.diag_message);
    TEST_ASSERT_TRUE(state.has_result);
    TEST_ASSERT_EQUAL_STRING("req-1", state.result_request_id);
    TEST_ASSERT_EQUAL_STRING("refresh_status", state.result_command);
    TEST_ASSERT_EQUAL_STRING("failed", state.result_status);
    TEST_ASSERT_EQUAL_STRING("busy", state.result_error_code);
    TEST_ASSERT_EQUAL_STRING("still running", state.result_message);
    TEST_ASSERT_EQUAL_UINT32(1, state.heartbeat_count);
    TEST_ASSERT_EQUAL_UINT32(1, state.status_count);
    TEST_ASSERT_EQUAL_UINT32(1, state.diag_count);
    TEST_ASSERT_EQUAL_UINT32(1, state.result_count);
}

static void test_control_ingest_ignores_non_contract_topics(void)
{
    device_control_ingest_device_t state = {0};
    dci_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                      device_control_ingest_handle_mqtt("quest/relay/1", "{\"x\":1}"));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                      device_control_ingest_get_device("quest", &state));
}

static void test_control_ingest_online_window_uses_last_seen(void)
{
    device_control_ingest_device_t state = {0};
    dci_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK,
                      device_control_ingest_handle_mqtt("cp/v1/dev/pipa/heartbeat",
                                                        "{\"ts_ms\":1000,\"boot_id\":\"boot-a\"}"));
    TEST_ASSERT_EQUAL(ESP_OK, device_control_ingest_get_device("pipa", &state));
    TEST_ASSERT_TRUE(state.last_seen_ms > 0);
    TEST_ASSERT_TRUE(device_control_ingest_is_online(&state, state.last_seen_ms + 1000, 5000));
    TEST_ASSERT_FALSE(device_control_ingest_is_online(&state, state.last_seen_ms + 6000, 5000));
}

static void test_control_ingest_parses_native_device_event(void)
{
    device_control_ingest_device_t state = {0};
    dci_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK,
                      device_control_ingest_handle_mqtt("cp/v1/dev/node_1/event",
                                                        "{\"ts_ms\":2000,\"event\":\"input.pressed\",\"args\":{\"channel\":1}}"));
    TEST_ASSERT_EQUAL(ESP_OK, device_control_ingest_get_device("node_1", &state));
    TEST_ASSERT_TRUE(state.has_result);
    TEST_ASSERT_EQUAL_STRING("input.pressed", state.result_command);
    TEST_ASSERT_EQUAL_STRING("event", state.result_status);
    TEST_ASSERT_EQUAL_STRING("{\"channel\":1}", state.result_data_json);
}

void register_device_control_ingest_tests(void)
{
    RUN_TEST(test_control_ingest_parses_heartbeat_status_diag_result);
    RUN_TEST(test_control_ingest_ignores_non_contract_topics);
    RUN_TEST(test_control_ingest_online_window_uses_last_seen);
    RUN_TEST(test_control_ingest_parses_native_device_event);
}
