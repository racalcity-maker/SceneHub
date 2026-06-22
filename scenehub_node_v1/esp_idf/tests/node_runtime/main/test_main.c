#include "unity.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    // On Xtensa/ESP32-S3 StackType_t is uint8_t, so this value is bytes, not 32-bit words.
    NODE_RUNTIME_TEST_TASK_STACK_BYTES = 32768,
};

static StaticTask_t s_test_task_storage;
static StackType_t *s_test_task_stack;

static StackType_t *alloc_test_task_stack(void)
{
    size_t stack_bytes = NODE_RUNTIME_TEST_TASK_STACK_BYTES * sizeof(StackType_t);
    StackType_t *stack = NULL;

#if CONFIG_SPIRAM && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    stack = (StackType_t *)heap_caps_malloc(stack_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stack) {
        memset(stack, 0, stack_bytes);
        return stack;
    }
#endif

    stack = (StackType_t *)heap_caps_malloc(stack_bytes, MALLOC_CAP_8BIT);
    if (stack) {
        memset(stack, 0, stack_bytes);
    }
    return stack;
}

static void node_runtime_test_task(void *arg)
{
    (void)arg;

    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();

    vTaskDelete(NULL);
}

void app_main(void)
{
    s_test_task_stack = alloc_test_task_stack();
    if (!s_test_task_stack) {
        printf("node_runtime_tests: failed to allocate test task stack\n");
        return;
    }

    TaskHandle_t task = xTaskCreateStaticPinnedToCore(node_runtime_test_task,
                                                      "node_rt_test",
                                                      NODE_RUNTIME_TEST_TASK_STACK_BYTES,
                                                      NULL,
                                                      tskIDLE_PRIORITY + 1,
                                                      s_test_task_stack,
                                                      &s_test_task_storage,
                                                      tskNO_AFFINITY);
    if (!task) {
        printf("node_runtime_tests: failed to start test task\n");
    }
}
