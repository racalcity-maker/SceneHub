#include "node_management.h"

#include <stdint.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_config.h"

#define NODE_MGMT_NOTIFY_WIFI_RESET (1UL << 0)
#define NODE_MGMT_NOTIFY_FACTORY_RESET (1UL << 1)

static const char *TAG = "node_management";

static StaticTask_t s_management_task_storage;
static StackType_t s_management_task_stack[3072];
static TaskHandle_t s_management_task;

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
            esp_restart();
            continue;
        }
        if (notify & NODE_MGMT_NOTIFY_WIFI_RESET) {
            err = node_config_reset_wifi();
            ESP_LOGW(TAG, "wifi settings reset: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(200));
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
    s_management_task = xTaskCreateStatic(management_task,
                                          "node_mgmt",
                                          sizeof(s_management_task_stack) / sizeof(s_management_task_stack[0]),
                                          NULL,
                                          tskIDLE_PRIORITY + 2,
                                          s_management_task_stack,
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
