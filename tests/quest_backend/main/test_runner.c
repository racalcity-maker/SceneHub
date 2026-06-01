#include "unity.h"
#include "unity_internals.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void register_device_control_ingest_tests(void);
extern void register_service_status_tests(void);
extern void register_event_bus_tests(void);
extern void register_config_store_utils_tests(void);
extern void register_audio_player_state_tests(void);
extern void register_command_executor_tests(void);
extern void register_ota_manager_tests(void);
extern void register_quest_device_tests(void);
extern void register_room_catalog_tests(void);
extern void register_room_scenario_tests(void);
extern void register_gm_core_primitive_tests(void);
extern void register_gm_game_profile_tests(void);
extern void register_gm_room_session_tests(void);
extern void register_integration_quest_flow_tests(void);
extern void register_web_ui_contract_tests(void);
extern void register_web_ui_handler_tests(void);
extern void register_orchestrator_registry_tests(void);
extern void register_orchestrator_audit_tests(void);
extern void register_orchestrator_timeline_tests(void);

#define FAILED_TESTS_MAX       128
#define FAILED_TEST_NAME_MAX   96
#define FAILED_TEST_FILE_MAX   128
#define FAILED_TEST_REASON_MAX 256
#define UNITY_OUTPUT_LINE_MAX 512
#define UNITY_RUNNER_STACK_SIZE 65536

typedef struct {
    char name[FAILED_TEST_NAME_MAX];
    char file[FAILED_TEST_FILE_MAX];
    char reason[FAILED_TEST_REASON_MAX];
    UNITY_LINE_TYPE line;
} failed_test_detail_t;

EXT_RAM_BSS_ATTR static failed_test_detail_t s_failed_tests[FAILED_TESTS_MAX];
static char s_unity_output_line[UNITY_OUTPUT_LINE_MAX];
static size_t s_unity_output_line_len = 0;
static char s_last_failure_reason[FAILED_TEST_REASON_MAX];
static char s_last_failure_file[FAILED_TEST_FILE_MAX];
static UNITY_LINE_TYPE s_last_failure_line = 0;
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

static void runner_capture_failure_line(void)
{
    const char *marker = strstr(s_unity_output_line, ":FAIL:");
    const char *line_sep = NULL;
    const char *test_sep = NULL;
    char line_buf[16] = {0};
    size_t line_len = 0;

    if (!marker) {
        return;
    }

    s_last_failure_file[0] = '\0';
    s_last_failure_reason[0] = '\0';
    s_last_failure_line = 0;

    test_sep = marker - 1;
    while (test_sep > s_unity_output_line && *test_sep != ':') {
        --test_sep;
    }
    if (test_sep > s_unity_output_line && *test_sep == ':') {
        line_sep = test_sep - 1;
        while (line_sep > s_unity_output_line && *line_sep != ':') {
            --line_sep;
        }
        if (line_sep > s_unity_output_line && *line_sep == ':') {
            size_t file_len = (size_t)(line_sep - s_unity_output_line);
            if (file_len >= sizeof(s_last_failure_file)) {
                file_len = sizeof(s_last_failure_file) - 1;
            }
            memcpy(s_last_failure_file, s_unity_output_line, file_len);
            s_last_failure_file[file_len] = '\0';

            line_len = (size_t)(test_sep - line_sep - 1);
            if (line_len >= sizeof(line_buf)) {
                line_len = sizeof(line_buf) - 1;
            }
            memcpy(line_buf, line_sep + 1, line_len);
            line_buf[line_len] = '\0';
            s_last_failure_line = (UNITY_LINE_TYPE)strtoul(line_buf, NULL, 10);
        }
    }

    marker += strlen(":FAIL:");
    while (*marker == ' ') {
        ++marker;
    }
    runner_str_copy(s_last_failure_reason,
                    sizeof(s_last_failure_reason),
                    marker[0] ? marker : "assertion failed");
}

static void runner_capture_unity_output_char(int c)
{
    if (c == '\r') {
        return;
    }
    if (c == '\n') {
        s_unity_output_line[s_unity_output_line_len] = '\0';
        runner_capture_failure_line();
        s_unity_output_line_len = 0;
        s_unity_output_line[0] = '\0';
        return;
    }
    if (s_unity_output_line_len + 1 < sizeof(s_unity_output_line)) {
        s_unity_output_line[s_unity_output_line_len++] = (char)c;
        s_unity_output_line[s_unity_output_line_len] = '\0';
    }
}

void __real_unity_putc(int c);

void __wrap_unity_putc(int c)
{
    runner_capture_unity_output_char(c);
    __real_unity_putc(c);
}

void setUp(void)
{
    s_last_failure_reason[0] = '\0';
    s_last_failure_file[0] = '\0';
    s_last_failure_line = 0;
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
                    s_last_failure_file[0] ? s_last_failure_file
                                           : (Unity.TestFile ? Unity.TestFile : "unknown"));
    runner_str_copy(s_failed_tests[s_failed_count].reason,
                    sizeof(s_failed_tests[s_failed_count].reason),
                    s_last_failure_reason[0] ? s_last_failure_reason : "assertion failed");
    s_failed_tests[s_failed_count].line = s_last_failure_line ? s_last_failure_line
                                                              : Unity.CurrentTestLineNumber;
    s_failed_count++;
}

static void unity_runner_task(void *arg)
{
    (void)arg;
    s_failed_count = 0;
    s_unity_output_line[0] = '\0';
    s_unity_output_line_len = 0;
    s_last_failure_reason[0] = '\0';
    s_last_failure_file[0] = '\0';
    s_last_failure_line = 0;
    UNITY_BEGIN();
    register_service_status_tests();
    register_event_bus_tests();
    register_config_store_utils_tests();
    register_audio_player_state_tests();
    register_command_executor_tests();
    register_ota_manager_tests();
    register_device_control_ingest_tests();
    register_quest_device_tests();
    register_room_catalog_tests();
    register_room_scenario_tests();
    register_gm_core_primitive_tests();
    register_gm_game_profile_tests();
    register_gm_room_session_tests();
    register_integration_quest_flow_tests();
    register_web_ui_contract_tests();
    register_web_ui_handler_tests();
    register_orchestrator_registry_tests();
    register_orchestrator_audit_tests();
    register_orchestrator_timeline_tests();
    UNITY_END();
    if (s_failed_count > 0) {
        printf("\nFailed tests (%u):\n", (unsigned)s_failed_count);
        for (size_t i = 0; i < s_failed_count; ++i) {
            if (s_failed_tests[i].file[0] && s_failed_tests[i].line > 0) {
                printf(" - %s: %s (%s:%u)\n",
                       s_failed_tests[i].name,
                       s_failed_tests[i].reason,
                       s_failed_tests[i].file,
                       (unsigned)s_failed_tests[i].line);
            } else {
                printf(" - %s: %s\n",
                       s_failed_tests[i].name,
                       s_failed_tests[i].reason);
            }
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
