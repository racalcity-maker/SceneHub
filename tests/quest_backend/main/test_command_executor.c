#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "command_executor.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_core.h"
#include "quest_device.h"
#include "scenehub_command_result.h"

static void ce_test_copy(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static void ce_test_bootstrap(void)
{
    static bool initialized = false;
    if (!initialized) {
        TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
        TEST_ASSERT_EQUAL(ESP_OK, mqtt_core_init());
        TEST_ASSERT_EQUAL(ESP_OK, quest_device_init());
        initialized = true;
    }
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_clear());
    command_executor_reset_pending();
}

static void ce_add_relay_device(bool manual_allowed,
                                bool scenario_allowed,
                                bool result_required,
                                uint32_t timeout_ms)
{
    quest_device_t device = {0};

    ce_test_copy(device.id, sizeof(device.id), "relay");
    ce_test_copy(device.client_id, sizeof(device.client_id), "relay_client");
    ce_test_copy(device.name, sizeof(device.name), "Relay");
    device.enabled = true;

    device.command_count = 1;
    ce_test_copy(device.commands[0].id, sizeof(device.commands[0].id), "pulse");
    ce_test_copy(device.commands[0].label, sizeof(device.commands[0].label), "Pulse");
    ce_test_copy(device.commands[0].capability, sizeof(device.commands[0].capability), "relay");
    ce_test_copy(device.commands[0].command, sizeof(device.commands[0].command), "relay.pulse");
    ce_test_copy(device.commands[0].default_args_json,
                 sizeof(device.commands[0].default_args_json),
                 "{\"channel\":1,\"duration_ms\":100}");
    device.commands[0].manual_allowed = manual_allowed;
    device.commands[0].scenario_allowed = scenario_allowed;
    device.commands[0].result_required = result_required;
    device.commands[0].timeout_ms = timeout_ms;
    ce_test_copy(device.commands[0].danger_level, sizeof(device.commands[0].danger_level), "normal");

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&device));
}

static void ce_make_result_event(event_bus_message_t *msg,
                                 const command_executor_dispatch_t *dispatch,
                                 const char *status)
{
    memset(msg, 0, sizeof(*msg));
    msg->type = EVENT_DEVICE_CONTROL;
    msg->payload_type = EVENT_BUS_PAYLOAD_DEVICE_CONTROL;
    ce_test_copy(msg->payload, sizeof(msg->payload), status);
    ce_test_copy(msg->data.device_control.device_id,
                 sizeof(msg->data.device_control.device_id),
                 dispatch->source_id);
    ce_test_copy(msg->data.device_control.action_id,
                 sizeof(msg->data.device_control.action_id),
                 dispatch->request_id);
    ce_test_copy(msg->data.device_control.source,
                 sizeof(msg->data.device_control.source),
                 "result");
}

static void test_command_executor_dispatches_mqtt_and_tracks_result_required(void)
{
    command_executor_request_t request = {0};
    command_executor_dispatch_t dispatch = {0};

    ce_test_bootstrap();
    ce_add_relay_device(true, true, true, 1000);

    ce_test_copy(request.source, sizeof(request.source), "scenario");
    ce_test_copy(request.device_id, sizeof(request.device_id), "relay");
    ce_test_copy(request.command_id, sizeof(request.command_id), "pulse");
    ce_test_copy(request.params_json, sizeof(request.params_json), "{\"duration_ms\":250}");
    request.require_scenario_allowed = true;

    TEST_ASSERT_EQUAL(ESP_OK, command_executor_execute(&request, &dispatch, NULL, 0));
    TEST_ASSERT_TRUE(dispatch.result_required);
    TEST_ASSERT_EQUAL_UINT32(1000, dispatch.timeout_ms);
    TEST_ASSERT_TRUE(dispatch.request_id[0] != '\0');
    TEST_ASSERT_EQUAL_STRING("relay_client", dispatch.source_id);
    TEST_ASSERT_EQUAL_STRING("relay.pulse", dispatch.command);
}

static void test_command_executor_rejects_manual_or_scenario_disabled_policy(void)
{
    command_executor_request_t request = {0};
    char error[64] = {0};

    ce_test_bootstrap();
    ce_add_relay_device(false, true, false, 0);

    ce_test_copy(request.source, sizeof(request.source), "manual");
    ce_test_copy(request.device_id, sizeof(request.device_id), "relay");
    ce_test_copy(request.command_id, sizeof(request.command_id), "pulse");
    request.require_manual_allowed = true;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      command_executor_execute(&request, NULL, error, sizeof(error)));
    TEST_ASSERT_EQUAL_STRING("device_command_manual_disabled", error);

    memset(&request, 0, sizeof(request));
    memset(error, 0, sizeof(error));
    ce_test_copy(request.source, sizeof(request.source), "scenario");
    ce_test_copy(request.device_id, sizeof(request.device_id), "relay");
    ce_test_copy(request.command_id, sizeof(request.command_id), "pulse");
    request.require_scenario_allowed = true;
    TEST_ASSERT_EQUAL(ESP_OK, command_executor_execute(&request, NULL, error, sizeof(error)));

    ce_test_bootstrap();
    ce_add_relay_device(true, false, false, 0);
    memset(error, 0, sizeof(error));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      command_executor_execute(&request, NULL, error, sizeof(error)));
    TEST_ASSERT_EQUAL_STRING("device_command_scenario_disabled", error);
}

static void test_command_executor_device_command_helper_requires_manual_policy(void)
{
    ce_test_bootstrap();
    ce_add_relay_device(false, true, false, 0);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      command_executor_execute_device_command("relay", "pulse", "{\"duration_ms\":100}"));
}

static void test_command_executor_accepted_result_keeps_pending_until_timeout(void)
{
    command_executor_request_t request = {0};
    command_executor_dispatch_t dispatch = {0};
    event_bus_message_t event = {0};
    event_bus_message_t timeout_event = {0};

    ce_test_bootstrap();
    ce_add_relay_device(true, true, true, 1);

    ce_test_copy(request.source, sizeof(request.source), "scenario");
    ce_test_copy(request.device_id, sizeof(request.device_id), "relay");
    ce_test_copy(request.command_id, sizeof(request.command_id), "pulse");
    request.require_scenario_allowed = true;
    TEST_ASSERT_EQUAL(ESP_OK, command_executor_execute(&request, &dispatch, NULL, 0));

    ce_make_result_event(&event, &dispatch, SCENEHUB_COMMAND_RESULT_ACCEPTED);
    command_executor_on_event(&event);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(1, command_executor_poll_timeouts(&timeout_event, 1));
    TEST_ASSERT_EQUAL(EVENT_DEVICE_CONTROL, timeout_event.type);
    TEST_ASSERT_EQUAL_STRING(SCENEHUB_COMMAND_RESULT_TIMEOUT, timeout_event.payload);
    TEST_ASSERT_EQUAL_STRING(dispatch.request_id, timeout_event.data.device_control.action_id);
}

static void test_command_executor_terminal_result_clears_pending(void)
{
    command_executor_request_t request = {0};
    command_executor_dispatch_t dispatch = {0};
    event_bus_message_t event = {0};
    event_bus_message_t timeout_event = {0};

    ce_test_bootstrap();
    ce_add_relay_device(true, true, true, 1);

    ce_test_copy(request.source, sizeof(request.source), "scenario");
    ce_test_copy(request.device_id, sizeof(request.device_id), "relay");
    ce_test_copy(request.command_id, sizeof(request.command_id), "pulse");
    request.require_scenario_allowed = true;
    TEST_ASSERT_EQUAL(ESP_OK, command_executor_execute(&request, &dispatch, NULL, 0));

    ce_make_result_event(&event, &dispatch, SCENEHUB_COMMAND_RESULT_DONE);
    command_executor_on_event(&event);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(0, command_executor_poll_timeouts(&timeout_event, 1));
}

static void test_command_executor_cancel_request_clears_pending(void)
{
    command_executor_request_t request = {0};
    command_executor_dispatch_t dispatch = {0};
    event_bus_message_t timeout_event = {0};

    ce_test_bootstrap();
    ce_add_relay_device(true, true, true, 1);

    ce_test_copy(request.source, sizeof(request.source), "scenario");
    ce_test_copy(request.device_id, sizeof(request.device_id), "relay");
    ce_test_copy(request.command_id, sizeof(request.command_id), "pulse");
    request.require_scenario_allowed = true;
    TEST_ASSERT_EQUAL(ESP_OK, command_executor_execute(&request, &dispatch, NULL, 0));
    TEST_ASSERT_TRUE(dispatch.request_id[0] != '\0');

    command_executor_cancel_request(dispatch.request_id);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(0, command_executor_poll_timeouts(&timeout_event, 1));
}

void register_command_executor_tests(void)
{
    RUN_TEST(test_command_executor_dispatches_mqtt_and_tracks_result_required);
    RUN_TEST(test_command_executor_rejects_manual_or_scenario_disabled_policy);
    RUN_TEST(test_command_executor_device_command_helper_requires_manual_policy);
    RUN_TEST(test_command_executor_accepted_result_keeps_pending_until_timeout);
    RUN_TEST(test_command_executor_terminal_result_clears_pending);
    RUN_TEST(test_command_executor_cancel_request_clears_pending);
}
