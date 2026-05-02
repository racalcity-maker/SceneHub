#include "unity.h"

#include "service_status.h"

static void ss_bootstrap(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, service_status_init());
}

static void test_service_status_init_resets_entries(void)
{
    service_status_entry_t entry = {0};

    ss_bootstrap();
    service_status_mark_init(SERVICE_STATUS_MQTT, ESP_OK);
    service_status_mark_start(SERVICE_STATUS_MQTT, ESP_OK);
    TEST_ASSERT_TRUE(service_status_get(SERVICE_STATUS_MQTT, &entry));
    TEST_ASSERT_TRUE(entry.init_attempted);
    TEST_ASSERT_TRUE(entry.start_attempted);

    ss_bootstrap();
    TEST_ASSERT_TRUE(service_status_get(SERVICE_STATUS_MQTT, &entry));
    TEST_ASSERT_FALSE(entry.init_attempted);
    TEST_ASSERT_FALSE(entry.init_ok);
    TEST_ASSERT_FALSE(entry.start_attempted);
    TEST_ASSERT_FALSE(entry.start_ok);
}

static void test_service_status_mark_init_failure_clears_start_state(void)
{
    service_status_entry_t entry = {0};

    ss_bootstrap();

    service_status_mark_init(SERVICE_STATUS_AUDIO, ESP_OK);
    service_status_mark_start(SERVICE_STATUS_AUDIO, ESP_OK);
    service_status_mark_init(SERVICE_STATUS_AUDIO, ESP_FAIL);

    TEST_ASSERT_TRUE(service_status_get(SERVICE_STATUS_AUDIO, &entry));
    TEST_ASSERT_TRUE(entry.init_attempted);
    TEST_ASSERT_FALSE(entry.init_ok);
    TEST_ASSERT_FALSE(entry.start_attempted);
    TEST_ASSERT_FALSE(entry.start_ok);
}

static void test_service_status_mark_start_tracks_success_and_failure(void)
{
    service_status_entry_t entry = {0};

    ss_bootstrap();

    service_status_mark_start(SERVICE_STATUS_WEB_UI, ESP_FAIL);
    TEST_ASSERT_TRUE(service_status_get(SERVICE_STATUS_WEB_UI, &entry));
    TEST_ASSERT_TRUE(entry.start_attempted);
    TEST_ASSERT_FALSE(entry.start_ok);

    service_status_mark_start(SERVICE_STATUS_WEB_UI, ESP_OK);
    TEST_ASSERT_TRUE(service_status_get(SERVICE_STATUS_WEB_UI, &entry));
    TEST_ASSERT_TRUE(entry.start_attempted);
    TEST_ASSERT_TRUE(entry.start_ok);
}

static void test_service_status_rejects_invalid_get_and_ignores_invalid_marks(void)
{
    service_status_entry_t entry = {0};

    ss_bootstrap();

    TEST_ASSERT_FALSE(service_status_get(SERVICE_STATUS_COUNT, &entry));
    TEST_ASSERT_FALSE(service_status_get((service_status_id_t)-1, &entry));
    TEST_ASSERT_FALSE(service_status_get(SERVICE_STATUS_MQTT, NULL));

    service_status_mark_init(SERVICE_STATUS_COUNT, ESP_OK);
    service_status_mark_start((service_status_id_t)-1, ESP_OK);
    TEST_ASSERT_TRUE(service_status_get(SERVICE_STATUS_MQTT, &entry));
    TEST_ASSERT_FALSE(entry.init_attempted);
    TEST_ASSERT_FALSE(entry.start_attempted);
}

static void test_service_status_names_are_stable(void)
{
    TEST_ASSERT_EQUAL_STRING("network", service_status_name(SERVICE_STATUS_NETWORK));
    TEST_ASSERT_EQUAL_STRING("mqtt", service_status_name(SERVICE_STATUS_MQTT));
    TEST_ASSERT_EQUAL_STRING("audio", service_status_name(SERVICE_STATUS_AUDIO));
    TEST_ASSERT_EQUAL_STRING("web_ui", service_status_name(SERVICE_STATUS_WEB_UI));
    TEST_ASSERT_EQUAL_STRING("event_bus", service_status_name(SERVICE_STATUS_EVENT_BUS));
    TEST_ASSERT_EQUAL_STRING("unknown", service_status_name(SERVICE_STATUS_COUNT));
    TEST_ASSERT_EQUAL_STRING("unknown", service_status_name((service_status_id_t)-1));
}

static void test_service_status_update_event_bus_counters(void)
{
    service_status_entry_t entry = {0};

    ss_bootstrap();

    service_status_update_event_bus(10, 9, 1, 2, 3, 45, 7, 6, 1, 4);
    TEST_ASSERT_TRUE(service_status_get(SERVICE_STATUS_EVENT_BUS, &entry));
    TEST_ASSERT_EQUAL_UINT32(10, entry.event_posted);
    TEST_ASSERT_EQUAL_UINT32(9, entry.event_dispatched);
    TEST_ASSERT_EQUAL_UINT32(1, entry.event_dropped);
    TEST_ASSERT_EQUAL_UINT32(2, entry.event_queue_waiting);
    TEST_ASSERT_EQUAL_UINT32(3, entry.event_slow_handlers);
    TEST_ASSERT_EQUAL_UINT32(45, entry.event_max_handler_ms);
    TEST_ASSERT_EQUAL_UINT32(7, entry.event_job_posted);
    TEST_ASSERT_EQUAL_UINT32(6, entry.event_job_dispatched);
    TEST_ASSERT_EQUAL_UINT32(1, entry.event_job_dropped);
    TEST_ASSERT_EQUAL_UINT32(4, entry.event_job_queue_waiting);
}

void register_service_status_tests(void)
{
    RUN_TEST(test_service_status_init_resets_entries);
    RUN_TEST(test_service_status_mark_init_failure_clears_start_state);
    RUN_TEST(test_service_status_mark_start_tracks_success_and_failure);
    RUN_TEST(test_service_status_rejects_invalid_get_and_ignores_invalid_marks);
    RUN_TEST(test_service_status_names_are_stable);
    RUN_TEST(test_service_status_update_event_bus_counters);
}
