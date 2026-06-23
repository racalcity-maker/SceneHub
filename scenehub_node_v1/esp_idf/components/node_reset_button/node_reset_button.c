#include "node_reset_button.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_limits.h"
#include "sdkconfig.h"

static const char *TAG = "node_reset_button";

typedef struct {
    int gpio;
    bool active_low;
    node_reset_button_callback_t callback;
    void *callback_ctx;
    StaticTask_t task_storage;
    StackType_t *task_stack;
    TaskHandle_t task_handle;
} reset_button_state_t;

static reset_button_state_t s_button;

static StackType_t *allocate_reset_button_task_stack(void)
{
    const size_t stack_words = 2048U;
    const size_t stack_bytes = stack_words * sizeof(StackType_t);

    if (s_button.task_stack) {
        return s_button.task_stack;
    }
#if CONFIG_SPIRAM
    s_button.task_stack = (StackType_t *)heap_caps_malloc(stack_bytes,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_button.task_stack) {
        return s_button.task_stack;
    }
#endif
    s_button.task_stack = (StackType_t *)heap_caps_malloc(stack_bytes, MALLOC_CAP_8BIT);
    return s_button.task_stack;
}

static bool button_is_pressed(void)
{
    int level = gpio_get_level(s_button.gpio);
    return s_button.active_low ? level == 0 : level != 0;
}

static void fire_event_once(node_reset_button_event_t event)
{
    if (s_button.callback) {
        s_button.callback(event, s_button.callback_ctx);
    }
}

static void reset_button_task(void *arg)
{
    (void)arg;
    bool was_pressed = false;
    bool wifi_fired = false;
    bool factory_fired = false;
    int64_t press_start_us = 0;

    while (true) {
        bool pressed = button_is_pressed();
        int64_t now_us = esp_timer_get_time();

        if (pressed && !was_pressed) {
            press_start_us = now_us;
            wifi_fired = false;
            factory_fired = false;
            ESP_LOGI(TAG, "reset/config button pressed");
        } else if (!pressed && was_pressed) {
            ESP_LOGI(TAG, "reset/config button released");
            press_start_us = 0;
        }

        if (pressed && press_start_us > 0) {
            int64_t held_ms = (now_us - press_start_us) / 1000;
            if (!factory_fired && held_ms >= NODE_FACTORY_RESET_HOLD_MS) {
                factory_fired = true;
                wifi_fired = true;
                ESP_LOGW(TAG, "factory reset threshold reached");
                fire_event_once(NODE_RESET_BUTTON_EVENT_FACTORY_RESET);
            } else if (!wifi_fired && held_ms >= NODE_RESET_WIFI_HOLD_MS) {
                wifi_fired = true;
                ESP_LOGW(TAG, "wifi reset threshold reached");
                fire_event_once(NODE_RESET_BUTTON_EVENT_WIFI_RESET);
            }
        }

        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t node_reset_button_start(const node_reset_button_config_t *config)
{
    if (!config || config->gpio < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_button.task_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    s_button.gpio = config->gpio;
    s_button.active_low = config->active_low;
    s_button.callback = config->callback;
    s_button.callback_ctx = config->callback_ctx;

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << config->gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = config->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    StackType_t *task_stack = allocate_reset_button_task_stack();
    if (!task_stack) {
        return ESP_ERR_NO_MEM;
    }
    s_button.task_handle = xTaskCreateStatic(reset_button_task,
                                             "node_reset_btn",
                                             2048U,
                                             NULL,
                                             tskIDLE_PRIORITY + 1,
                                             task_stack,
                                             &s_button.task_storage);
    if (!s_button.task_handle) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "reset/config button started gpio=%d active_low=%d", config->gpio, config->active_low);
    return ESP_OK;
}
