#include "unity.h"
#include "unity_internals.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

extern void register_device_control_ingest_tests(void);
extern void register_quest_device_tests(void);
extern void register_room_scenario_tests(void);
extern void register_gm_game_profile_tests(void);

#define FAILED_TESTS_MAX       128
#define FAILED_TEST_NAME_MAX   96
#define FAILED_TEST_FILE_MAX   128
#define UNITY_RUNNER_STACK_SIZE 65536

typedef struct {
    char name[FAILED_TEST_NAME_MAX];
    char file[FAILED_TEST_FILE_MAX];
    UNITY_LINE_TYPE line;
} failed_test_detail_t;

static failed_test_detail_t s_failed_tests[FAILED_TESTS_MAX];
static size_t s_failed_count = 0;

static void runner_str_copy(char *dst, size_t dst_len, const char *src)
{
    size_t i = 0;
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    while (i + 1 < dst_len && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void setUp(void)
{
}

void tearDown(void)
{
    if (!Unity.CurrentTestFailed || !Unity.CurrentTestName) {
        return;
    }
    if (s_failed_count >= FAILED_TESTS_MAX) {
        return;
    }
    runner_str_copy(s_failed_tests[s_failed_count].name,
                    sizeof(s_failed_tests[s_failed_count].name),
                    Unity.CurrentTestName);
    runner_str_copy(s_failed_tests[s_failed_count].file,
                    sizeof(s_failed_tests[s_failed_count].file),
                    Unity.TestFile ? Unity.TestFile : "unknown");
    s_failed_tests[s_failed_count].line = Unity.CurrentTestLineNumber;
    s_failed_count++;
}

static void unity_runner_task(void *arg)
{
    (void)arg;
    s_failed_count = 0;
    UNITY_BEGIN();
    register_device_control_ingest_tests();
    register_quest_device_tests();
    register_room_scenario_tests();
    register_gm_game_profile_tests();
    UNITY_END();
    if (s_failed_count > 0) {
        printf("\nFailed tests detail (%u):\n", (unsigned)s_failed_count);
        for (size_t i = 0; i < s_failed_count; ++i) {
            printf(" - %s (registered at %s:%u; see Unity assertion above for expected/actual)\n",
                   s_failed_tests[i].name,
                   s_failed_tests[i].file,
                   (unsigned)s_failed_tests[i].line);
        }
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    BaseType_t ok = xTaskCreate(unity_runner_task,
                                "unity_runner",
                                UNITY_RUNNER_STACK_SIZE,
                                NULL,
                                tskIDLE_PRIORITY + 1,
                                NULL);
    TEST_ASSERT_EQUAL(pdPASS, ok);
}
