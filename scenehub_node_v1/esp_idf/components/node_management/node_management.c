#include "node_management.h"

#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_config.h"
#include "sdkconfig.h"

#define NODE_MGMT_NOTIFY_WIFI_RESET (1UL << 0)
#define NODE_MGMT_NOTIFY_FACTORY_RESET (1UL << 1)

static const char *TAG = "node_management";

static StaticTask_t s_management_task_storage;
static StackType_t *s_management_task_stack;
static TaskHandle_t s_management_task;

static StackType_t *allocate_management_task_stack(void)
{
    const size_t stack_words = 3072U;
    const size_t stack_bytes = stack_words * sizeof(StackType_t);

    if (s_management_task_stack) {
        return s_management_task_stack;
    }
#if CONFIG_SPIRAM
    s_management_task_stack = (StackType_t *)heap_caps_malloc(stack_bytes,
                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_management_task_stack) {
        return s_management_task_stack;
    }
#endif
    s_management_task_stack = (StackType_t *)heap_caps_malloc(stack_bytes, MALLOC_CAP_8BIT);
    return s_management_task_stack;
}

static void management_task(void *arg)
{
    (void)arg;
    uint32_t notify = 0;
    while (true) {
        if (xTaskNotifyWait(0, UINT32_MAX, &notify, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp_err_t err = ESP_OK;
        if (notify & NODE_MGMT_NOTIFY_FACTORY_RESET) {
            err = node_config_factory_reset();
            ESP_LOGW(TAG, "factory reset: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(200));
            ESP_LOGW(TAG, "restart source=reset_button reason=factory_reset");
            esp_restart();
            continue;
        }
        if (notify & NODE_MGMT_NOTIFY_WIFI_RESET) {
            err = node_config_reset_wifi();
            ESP_LOGW(TAG, "wifi settings reset: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(200));
            ESP_LOGW(TAG, "restart source=reset_button reason=wifi_reset");
            esp_restart();
        }
    }
}

esp_err_t node_management_start(const node_config_t *config)
{
    (void)config;
    if (s_management_task) {
        return ESP_OK;
    }
    StackType_t *task_stack = allocate_management_task_stack();
    if (!task_stack) {
        return ESP_ERR_NO_MEM;
    }
    s_management_task = xTaskCreateStatic(management_task,
                                          "node_mgmt",
                                          3072U,
                                          NULL,
                                          tskIDLE_PRIORITY + 2,
                                          task_stack,
                                          &s_management_task_storage);
    return s_management_task ? ESP_OK : ESP_ERR_NO_MEM;
}

void node_management_handle_reset_button_event(node_reset_button_event_t event, void *ctx)
{
    (void)ctx;
    if (!s_management_task) {
        return;
    }

    uint32_t notify = 0;
    switch (event) {
    case NODE_RESET_BUTTON_EVENT_WIFI_RESET:
        notify = NODE_MGMT_NOTIFY_WIFI_RESET;
        break;
    case NODE_RESET_BUTTON_EVENT_FACTORY_RESET:
        notify = NODE_MGMT_NOTIFY_FACTORY_RESET;
        break;
    default:
        return;
    }
    xTaskNotify(s_management_task, notify, eSetBits);
}
