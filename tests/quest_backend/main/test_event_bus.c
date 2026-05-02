#include "unity.h"

#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static volatile uint32_t s_event_first_seen = 0;
static volatile uint32_t s_event_second_seen = 0;
static volatile uint32_t s_event_sequence = 0;
static volatile uint32_t s_event_last_type = 0;
static volatile uint32_t s_event_job_value = 0;

static void event_bus_test_handler(const event_bus_message_t *message)
{
    (void)message;
}

static void event_bus_test_job(void *ctx)
{
    (void)ctx;
}

static void event_bus_order_handler_first(const event_bus_message_t *message)
{
    s_event_first_seen = ++s_event_sequence;
    s_event_last_type = message ? (uint32_t)message->type : 0;
}

static void event_bus_order_handler_second(const event_bus_message_t *message)
{
    (void)message;
    s_event_second_seen = ++s_event_sequence;
}

static void event_bus_job_sets_value(void *ctx)
{
    uint32_t *value = (uint32_t *)ctx;
    s_event_job_value = value ? *value : 0;
}

static void test_event_bus_rejects_invalid_args_before_init(void)
{
    event_bus_stats_t stats = {0};
    event_bus_message_t message = {
        .type = EVENT_SYSTEM_STATUS,
    };

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_bus_get_stats(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_bus_post(NULL, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_bus_post_priority(NULL, EVENT_BUS_PRIORITY_HIGH, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_bus_post_job(NULL, NULL, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_bus_register_handler(NULL));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, event_bus_post(&message, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, event_bus_post_job(event_bus_test_job, NULL, 0));
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(0, stats.posted);
    TEST_ASSERT_EQUAL_UINT32(0, stats.handler_count);
}

static void test_event_bus_init_resets_stats_and_registers_unique_handlers(void)
{
    event_bus_stats_t stats = {0};

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(0, stats.posted);
    TEST_ASSERT_EQUAL_UINT32(0, stats.dispatched);
    TEST_ASSERT_EQUAL_UINT32(0, stats.dropped);
    TEST_ASSERT_EQUAL_UINT32(0, stats.queue_waiting);
    TEST_ASSERT_EQUAL_UINT32(0, stats.job_queue_waiting);
    TEST_ASSERT_EQUAL_UINT32(0, stats.handler_count);

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_register_handler(event_bus_test_handler));
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(1, stats.handler_count);

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_register_handler(event_bus_test_handler));
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(1, stats.handler_count);
}

static void test_event_bus_dispatches_handlers_in_registration_order(void)
{
    event_bus_stats_t stats = {0};
    event_bus_message_t message = {
        .type = EVENT_WEB_COMMAND,
    };

    s_event_first_seen = 0;
    s_event_second_seen = 0;
    s_event_sequence = 0;
    s_event_last_type = 0;

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_register_handler(event_bus_order_handler_first));
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_register_handler(event_bus_order_handler_second));
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_start());
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_post(&message, 0));

    for (int i = 0; i < 20 && s_event_second_seen == 0; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    TEST_ASSERT_EQUAL_UINT32(1, s_event_first_seen);
    TEST_ASSERT_EQUAL_UINT32(2, s_event_second_seen);
    TEST_ASSERT_EQUAL_UINT32(EVENT_WEB_COMMAND, s_event_last_type);
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_TRUE(stats.posted >= 1);
    TEST_ASSERT_TRUE(stats.dispatched >= 1);
}

static void test_event_bus_dispatches_posted_jobs(void)
{
    uint32_t value = 0xA5A5;
    event_bus_stats_t stats = {0};

    s_event_job_value = 0;

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_start());
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_post_job(event_bus_job_sets_value, &value, 0));

    for (int i = 0; i < 20 && s_event_job_value == 0; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    TEST_ASSERT_EQUAL_UINT32(value, s_event_job_value);
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_TRUE(stats.job_posted >= 1);
    TEST_ASSERT_TRUE(stats.job_dispatched >= 1);
}

void register_event_bus_tests(void)
{
    RUN_TEST(test_event_bus_rejects_invalid_args_before_init);
    RUN_TEST(test_event_bus_init_resets_stats_and_registers_unique_handlers);
    RUN_TEST(test_event_bus_dispatches_handlers_in_registration_order);
    RUN_TEST(test_event_bus_dispatches_posted_jobs);
}
