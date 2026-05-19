#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "event_bus.h"
#include "esp_attr.h"
#include "orchestrator_timeline.h"

EXT_RAM_BSS_ATTR static orchestrator_timeline_entry_t s_timeline_entries[ORCH_TIMELINE_CAPACITY];

static void timeline_bootstrap(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_timeline_init());
    orchestrator_timeline_reset();
}

static void test_orchestrator_timeline_log_uses_defaults_for_empty_fields(void)
{
    orchestrator_timeline_entry_t entry = {0};
    size_t count = 0;

    timeline_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_timeline_log(ORCH_TIMELINE_TYPE_EVENT,
                                                ORCH_TIMELINE_SEVERITY_INFO,
                                                "",
                                                NULL,
                                                NULL,
                                                "",
                                                NULL));

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_timeline_list_recent(1, &entry, &count));
    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_EQUAL(ORCH_TIMELINE_TYPE_EVENT, entry.type);
    TEST_ASSERT_EQUAL(ORCH_TIMELINE_SEVERITY_INFO, entry.severity);
    TEST_ASSERT_EQUAL_STRING("system", entry.source);
    TEST_ASSERT_EQUAL_STRING("", entry.room_id);
    TEST_ASSERT_EQUAL_STRING("", entry.device_id);
    TEST_ASSERT_EQUAL_STRING("Event", entry.title);
    TEST_ASSERT_EQUAL_STRING("", entry.details);
}

static void test_orchestrator_timeline_lists_recent_entries_newest_first(void)
{
    orchestrator_timeline_entry_t entries[3] = {0};
    size_t count = 0;

    timeline_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_timeline_log(ORCH_TIMELINE_TYPE_TIMER_CHANGED,
                                                ORCH_TIMELINE_SEVERITY_INFO,
                                                "gm",
                                                "room_a",
                                                "",
                                                "Timer started",
                                                "10 minutes"));
    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_timeline_log(ORCH_TIMELINE_TYPE_DEVICE_ACTION,
                                                ORCH_TIMELINE_SEVERITY_INFO,
                                                "gm",
                                                "room_a",
                                                "relay",
                                                "Door opened",
                                                "open"));
    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_timeline_log(ORCH_TIMELINE_TYPE_ACTION_FAILED,
                                                ORCH_TIMELINE_SEVERITY_ERROR,
                                                "api",
                                                "room_b",
                                                "audio",
                                                "Audio failed",
                                                "busy"));

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_timeline_list_recent(3, entries, &count));
    TEST_ASSERT_EQUAL_UINT(3, count);
    TEST_ASSERT_EQUAL(ORCH_TIMELINE_TYPE_ACTION_FAILED, entries[0].type);
    TEST_ASSERT_EQUAL(ORCH_TIMELINE_SEVERITY_ERROR, entries[0].severity);
    TEST_ASSERT_EQUAL_STRING("api", entries[0].source);
    TEST_ASSERT_EQUAL_STRING("room_b", entries[0].room_id);
    TEST_ASSERT_EQUAL_STRING("audio", entries[0].device_id);
    TEST_ASSERT_EQUAL_STRING("Audio failed", entries[0].title);
    TEST_ASSERT_EQUAL_STRING("busy", entries[0].details);

    TEST_ASSERT_EQUAL_STRING("Door opened", entries[1].title);
    TEST_ASSERT_EQUAL_STRING("Timer started", entries[2].title);
}

static void test_orchestrator_timeline_list_recent_honors_limit_and_args(void)
{
    orchestrator_timeline_entry_t entries[2] = {0};
    size_t count = 99;

    timeline_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_timeline_list_recent(1, entries, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_timeline_list_recent(1, NULL, &count));

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_timeline_log(ORCH_TIMELINE_TYPE_EVENT,
                                                ORCH_TIMELINE_SEVERITY_INFO,
                                                "system",
                                                "",
                                                "",
                                                "First",
                                                ""));
    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_timeline_log(ORCH_TIMELINE_TYPE_EVENT,
                                                ORCH_TIMELINE_SEVERITY_WARNING,
                                                "system",
                                                "",
                                                "",
                                                "Second",
                                                ""));
    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_timeline_log(ORCH_TIMELINE_TYPE_EVENT,
                                                ORCH_TIMELINE_SEVERITY_ERROR,
                                                "system",
                                                "",
                                                "",
                                                "Third",
                                                ""));

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_timeline_list_recent(2, entries, &count));
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_EQUAL_STRING("Third", entries[0].title);
    TEST_ASSERT_EQUAL_STRING("Second", entries[1].title);
}

static void test_orchestrator_timeline_reset_clears_entries(void)
{
    orchestrator_timeline_entry_t entry = {0};
    size_t count = 99;

    timeline_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_timeline_log(ORCH_TIMELINE_TYPE_EVENT,
                                                ORCH_TIMELINE_SEVERITY_INFO,
                                                "system",
                                                "",
                                                "",
                                                "Before reset",
                                                ""));
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_timeline_list_recent(1, &entry, &count));
    TEST_ASSERT_EQUAL_UINT(1, count);

    orchestrator_timeline_reset();
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_timeline_list_recent(1, &entry, &count));
    TEST_ASSERT_EQUAL_UINT(0, count);
}

static void test_orchestrator_timeline_ring_buffer_keeps_latest_entries(void)
{
    size_t count = 0;
    char title[ORCH_TIMELINE_TITLE_MAX_LEN] = {0};

    timeline_bootstrap();
    memset(s_timeline_entries, 0, sizeof(s_timeline_entries));

    for (size_t i = 0; i < ORCH_TIMELINE_CAPACITY + 3; ++i) {
        snprintf(title, sizeof(title), "Event %03u", (unsigned)i);
        TEST_ASSERT_EQUAL(ESP_OK,
                          orchestrator_timeline_log(ORCH_TIMELINE_TYPE_EVENT,
                                                    ORCH_TIMELINE_SEVERITY_INFO,
                                                    "system",
                                                    "",
                                                    "",
                                                    title,
                                                    ""));
    }

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_timeline_list_recent(ORCH_TIMELINE_CAPACITY, s_timeline_entries, &count));
    TEST_ASSERT_EQUAL_UINT(ORCH_TIMELINE_CAPACITY, count);
    TEST_ASSERT_EQUAL_STRING("Event 130", s_timeline_entries[0].title);
    TEST_ASSERT_EQUAL_STRING("Event 003", s_timeline_entries[ORCH_TIMELINE_CAPACITY - 1].title);
}

void register_orchestrator_timeline_tests(void)
{
    RUN_TEST(test_orchestrator_timeline_log_uses_defaults_for_empty_fields);
    RUN_TEST(test_orchestrator_timeline_lists_recent_entries_newest_first);
    RUN_TEST(test_orchestrator_timeline_list_recent_honors_limit_and_args);
    RUN_TEST(test_orchestrator_timeline_reset_clears_entries);
    RUN_TEST(test_orchestrator_timeline_ring_buffer_keeps_latest_entries);
}
