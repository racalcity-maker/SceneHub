#include "unity.h"

#include <string.h>

#include "node_config.h"
#include "node_control.h"

static node_config_t s_control_config;

static void init_control_with_defaults(void)
{
    memset(&s_control_config, 0, sizeof(s_control_config));
    node_config_set_factory_defaults(&s_control_config);
    TEST_ASSERT_EQUAL(ESP_OK, node_control_init(&s_control_config));
}

static void assert_rejected_result(const node_control_result_t *result, const char *error_code)
{
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("rejected", result->status);
    TEST_ASSERT_EQUAL_STRING(error_code, result->error_code);
    TEST_ASSERT_EQUAL_UINT32(0, strlen(result->data_json));
}

TEST_CASE("node control local rule source does not redispatch exported commands",
          "[node_runtime][control]")
{
    node_control_command_t command = {
        .request_id = "test_local_rule",
        .command = "open_secret_door",
        .args_json = NULL,
        .source = NODE_CONTROL_SOURCE_LOCAL_RULE,
    };
    node_control_result_t result = {0};

    init_control_with_defaults();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, node_control_submit(&command, &result));
    assert_rejected_result(&result, "not_supported");
}

TEST_CASE("node control hub source falls back to not supported without active exported bundle",
          "[node_runtime][control]")
{
    node_control_command_t command = {
        .request_id = "test_hub",
        .command = "open_secret_door",
        .args_json = NULL,
        .source = NODE_CONTROL_SOURCE_HUB,
    };
    node_control_result_t result = {0};

    init_control_with_defaults();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, node_control_submit(&command, &result));
    assert_rejected_result(&result, "not_supported");
}

TEST_CASE("node control local ui source falls back to not supported without active exported bundle",
          "[node_runtime][control]")
{
    node_control_command_t command = {
        .request_id = "test_local_ui",
        .command = "open_secret_door",
        .args_json = NULL,
        .source = NODE_CONTROL_SOURCE_LOCAL_UI,
    };
    node_control_result_t result = {0};

    init_control_with_defaults();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, node_control_submit(&command, &result));
    assert_rejected_result(&result, "not_supported");
}
