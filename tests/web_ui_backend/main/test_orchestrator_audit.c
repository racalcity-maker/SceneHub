#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "esp_attr.h"
#include "orchestrator_audit.h"

EXT_RAM_BSS_ATTR static orchestrator_audit_entry_t s_audit_entries[ORCH_AUDIT_CAPACITY];

static void audit_bootstrap(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_audit_init());
    orchestrator_audit_reset();
}

static void test_orchestrator_audit_rejects_invalid_device_actions(void)
{
    audit_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_audit_log_device_action(NULL, "device", "open", true, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_audit_log_device_action("", "device", "open", true, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_audit_log_device_action("gm", NULL, "open", true, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_audit_log_device_action("gm", "", "open", true, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_audit_log_device_action("gm", "device", NULL, true, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_audit_log_device_action("gm", "device", "", true, NULL));
}

static void test_orchestrator_audit_lists_recent_entries_newest_first(void)
{
    orchestrator_audit_entry_t entries[3] = {0};
    size_t count = 0;

    audit_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_audit_log_device_action("gm", "relay", "open", true, NULL));
    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_audit_log_device_action("gm", "audio", "play", false, "busy"));
    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_audit_log_device_action("api", "light", "toggle", true, ""));

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_audit_list_recent(3, entries, &count));
    TEST_ASSERT_EQUAL_UINT(3, count);
    TEST_ASSERT_EQUAL_STRING("light", entries[0].device_id);
    TEST_ASSERT_EQUAL_STRING("toggle", entries[0].action_id);
    TEST_ASSERT_TRUE(entries[0].success);
    TEST_ASSERT_EQUAL_STRING("", entries[0].error_code);

    TEST_ASSERT_EQUAL_STRING("audio", entries[1].device_id);
    TEST_ASSERT_EQUAL_STRING("play", entries[1].action_id);
    TEST_ASSERT_FALSE(entries[1].success);
    TEST_ASSERT_EQUAL_STRING("busy", entries[1].error_code);

    TEST_ASSERT_EQUAL_STRING("relay", entries[2].device_id);
    TEST_ASSERT_EQUAL_STRING("open", entries[2].action_id);
}

static void test_orchestrator_audit_list_recent_honors_limit_and_args(void)
{
    orchestrator_audit_entry_t entries[2] = {0};
    size_t count = 99;

    audit_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_audit_list_recent(1, entries, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_audit_list_recent(1, NULL, &count));

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_audit_log_device_action("gm", "a", "one", true, NULL));
    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_audit_log_device_action("gm", "b", "two", true, NULL));
    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_audit_log_device_action("gm", "c", "three", true, NULL));

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_audit_list_recent(2, entries, &count));
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_EQUAL_STRING("c", entries[0].device_id);
    TEST_ASSERT_EQUAL_STRING("b", entries[1].device_id);
}

static void test_orchestrator_audit_ring_buffer_keeps_latest_entries(void)
{
    orchestrator_audit_entry_t first = {0};
    size_t count = 0;
    char device_id[16] = {0};

    audit_bootstrap();
    memset(s_audit_entries, 0, sizeof(s_audit_entries));

    for (size_t i = 0; i < ORCH_AUDIT_CAPACITY + 3; ++i) {
        snprintf(device_id, sizeof(device_id), "dev_%02u", (unsigned)i);
        TEST_ASSERT_EQUAL(ESP_OK,
                          orchestrator_audit_log_device_action("gm", device_id, "run", true, NULL));
    }

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_audit_list_recent(1, &first, &count));
    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_EQUAL_STRING("dev_66", first.device_id);

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_audit_list_recent(ORCH_AUDIT_CAPACITY, s_audit_entries, &count));
    TEST_ASSERT_EQUAL_UINT(ORCH_AUDIT_CAPACITY, count);
    TEST_ASSERT_EQUAL_STRING("dev_66", s_audit_entries[0].device_id);
    TEST_ASSERT_EQUAL_STRING("dev_03", s_audit_entries[ORCH_AUDIT_CAPACITY - 1].device_id);
}

void register_orchestrator_audit_tests(void)
{
    RUN_TEST(test_orchestrator_audit_rejects_invalid_device_actions);
    RUN_TEST(test_orchestrator_audit_lists_recent_entries_newest_first);
    RUN_TEST(test_orchestrator_audit_list_recent_honors_limit_and_args);
    RUN_TEST(test_orchestrator_audit_ring_buffer_keeps_latest_entries);
}
