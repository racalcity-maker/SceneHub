#include <limits.h>
#include <string.h>

#include "unity.h"

#include "gm_hint.h"
#include "gm_timer.h"

static void test_gm_timer_start_pause_resume_and_finish(void)
{
    gm_timer_t timer = {0};

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_timer_start(NULL, 1000, 0));
    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_start(&timer, 10000, 1000));
    TEST_ASSERT_EQUAL(GM_TIMER_RUNNING, timer.state);
    TEST_ASSERT_EQUAL_UINT32(10000, timer.duration_ms);
    TEST_ASSERT_EQUAL_UINT32(10000, gm_timer_get_remaining(&timer, 1000));
    TEST_ASSERT_EQUAL_UINT32(7500, gm_timer_get_remaining(&timer, 3500));
    TEST_ASSERT_TRUE(gm_timer_is_active(&timer, 3500));

    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_pause(&timer, 4000));
    TEST_ASSERT_EQUAL(GM_TIMER_PAUSED, timer.state);
    TEST_ASSERT_EQUAL_UINT32(7000, timer.remaining_ms);
    TEST_ASSERT_EQUAL_UINT32(7000, gm_timer_get_remaining(&timer, 9000));
    TEST_ASSERT_FALSE(gm_timer_is_active(&timer, 9000));

    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_resume(&timer, 10000));
    TEST_ASSERT_EQUAL(GM_TIMER_RUNNING, timer.state);
    TEST_ASSERT_EQUAL_UINT32(5000, gm_timer_get_remaining(&timer, 12000));

    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_finish(&timer, 13000));
    TEST_ASSERT_EQUAL(GM_TIMER_FINISHED, timer.state);
    TEST_ASSERT_EQUAL_UINT32(4000, gm_timer_get_remaining(&timer, 20000));
    TEST_ASSERT_FALSE(gm_timer_is_active(&timer, 20000));
}

static void test_gm_timer_rejects_invalid_state_transitions(void)
{
    gm_timer_t timer = {0};

    gm_timer_reset(&timer, 5000, 0);
    TEST_ASSERT_EQUAL(GM_TIMER_IDLE, timer.state);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, gm_timer_pause(&timer, 100));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, gm_timer_resume(&timer, 100));

    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_start(&timer, 5000, 1000));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, gm_timer_resume(&timer, 1500));
    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_pause(&timer, 2000));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, gm_timer_pause(&timer, 2500));
}

static void test_gm_timer_add_time_clamps_and_reopens_finished_timer_paused(void)
{
    gm_timer_t timer = {0};

    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_start(&timer, 1000, 0));
    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_add_time(&timer, -2000, 100));
    TEST_ASSERT_EQUAL(GM_TIMER_FINISHED, timer.state);
    TEST_ASSERT_EQUAL_UINT32(0, timer.remaining_ms);

    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_add_time(&timer, 3000, 200));
    TEST_ASSERT_EQUAL(GM_TIMER_PAUSED, timer.state);
    TEST_ASSERT_EQUAL_UINT32(3000, timer.remaining_ms);

    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_add_time(&timer, INT32_MAX, 300));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)INT32_MAX + 3000u, timer.remaining_ms);
}

static void test_gm_timer_set_remaining_zero_finishes(void)
{
    gm_timer_t timer = {0};

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_timer_set_remaining(NULL, 1000, 0));
    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_start(&timer, 5000, 0));
    TEST_ASSERT_EQUAL(ESP_OK, gm_timer_set_remaining(&timer, 0, 100));
    TEST_ASSERT_EQUAL(GM_TIMER_FINISHED, timer.state);
    TEST_ASSERT_EQUAL_UINT32(0, timer.remaining_ms);
}

static void test_gm_hint_send_clear_and_reset(void)
{
    gm_hint_state_t hint = {0};

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_hint_send(NULL, "hint", 10));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_hint_send(&hint, NULL, 10));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_hint_send(&hint, "", 10));

    TEST_ASSERT_EQUAL(ESP_OK, gm_hint_send(&hint, "Look under the altar", 1000));
    TEST_ASSERT_TRUE(hint.active);
    TEST_ASSERT_EQUAL_UINT32(1, hint.sent_count);
    TEST_ASSERT_EQUAL_UINT32(1000, (uint32_t)hint.last_changed_ms);
    TEST_ASSERT_EQUAL_STRING("Look under the altar", hint.message);

    TEST_ASSERT_EQUAL(ESP_OK, gm_hint_send(&hint, "Try the blue key", 2000));
    TEST_ASSERT_TRUE(hint.active);
    TEST_ASSERT_EQUAL_UINT32(2, hint.sent_count);
    TEST_ASSERT_EQUAL_UINT32(2000, (uint32_t)hint.last_changed_ms);
    TEST_ASSERT_EQUAL_STRING("Try the blue key", hint.message);

    TEST_ASSERT_EQUAL(ESP_OK, gm_hint_clear(&hint, 3000));
    TEST_ASSERT_FALSE(hint.active);
    TEST_ASSERT_EQUAL_UINT32(2, hint.sent_count);
    TEST_ASSERT_EQUAL_UINT32(3000, (uint32_t)hint.last_changed_ms);
    TEST_ASSERT_EQUAL_STRING("", hint.message);

    gm_hint_reset(&hint);
    TEST_ASSERT_FALSE(hint.active);
    TEST_ASSERT_EQUAL_UINT32(0, hint.sent_count);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)hint.last_changed_ms);
    TEST_ASSERT_EQUAL_STRING("", hint.message);
}

static void test_gm_hint_truncates_long_message_safely(void)
{
    gm_hint_state_t hint = {0};
    char message[sizeof(hint.message) + 16] = {0};

    memset(message, 'x', sizeof(message) - 1);
    TEST_ASSERT_EQUAL(ESP_OK, gm_hint_send(&hint, message, 10));
    TEST_ASSERT_TRUE(hint.active);
    TEST_ASSERT_EQUAL_UINT32(1, hint.sent_count);
    TEST_ASSERT_EQUAL_UINT(sizeof(hint.message) - 1, strlen(hint.message));
    TEST_ASSERT_EQUAL_CHAR('\0', hint.message[sizeof(hint.message) - 1]);
}

void register_gm_core_primitive_tests(void)
{
    RUN_TEST(test_gm_timer_start_pause_resume_and_finish);
    RUN_TEST(test_gm_timer_rejects_invalid_state_transitions);
    RUN_TEST(test_gm_timer_add_time_clamps_and_reopens_finished_timer_paused);
    RUN_TEST(test_gm_timer_set_remaining_zero_finishes);
    RUN_TEST(test_gm_hint_send_clear_and_reset);
    RUN_TEST(test_gm_hint_truncates_long_message_safely);
}
