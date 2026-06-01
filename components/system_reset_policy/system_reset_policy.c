#include "system_reset_policy.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config_store.h"
#include "scenehub_config.h"

static const char *TAG = "sys_reset_policy";
static const int64_t BOOT_SETUP_HOLD_US = 2LL * 1000 * 1000;
static const int64_t RUNTIME_RESET_HOLD_US = 15LL * 1000 * 1000;

typedef struct {
    bool initialized;
    bool boot_setup_requested;
    bool ignore_until_release;
    TaskHandle_t task_handle;
} system_reset_policy_state_t;

static system_reset_policy_state_t s_state;

static bool pin_is_pressed(void)
{
#if CONFIG_SCENEHUB_WEB_AUTH_RESET_GPIO >= 0
    return gpio_get_level(CONFIG_SCENEHUB_WEB_AUTH_RESET_GPIO) == 0;
#else
    return false;
#endif
}

static esp_err_t configure_reset_pin(void)
{
#if CONFIG_SCENEHUB_WEB_AUTH_RESET_GPIO >= 0
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << CONFIG_SCENEHUB_WEB_AUTH_RESET_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io_conf);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static bool detect_boot_setup_request(void)
{
#if CONFIG_SCENEHUB_WEB_AUTH_RESET_GPIO >= 0
    if (!pin_is_pressed()) {
        return false;
    }
    int64_t start_us = esp_timer_get_time();
    while ((esp_timer_get_time() - start_us) < BOOT_SETUP_HOLD_US) {
        if (!pin_is_pressed()) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGW(TAG, "reset/setup pin held on boot; setup AP requested");
    return true;
#else
    return false;
#endif
}

static void reset_policy_task(void *param)
{
    (void)param;
    int64_t low_since = 0;
    bool reset_fired = false;
    const TickType_t delay_ticks = pdMS_TO_TICKS(100);

    ESP_LOGI(TAG, "runtime reset/setup monitor started on GPIO%d", CONFIG_SCENEHUB_WEB_AUTH_RESET_GPIO);

    while (1) {
        bool pressed = pin_is_pressed();
        int64_t now = esp_timer_get_time();

        if (s_state.ignore_until_release) {
            if (!pressed) {
                s_state.ignore_until_release = false;
                ESP_LOGI(TAG, "reset/setup pin released after boot-hold setup request");
            }
            vTaskDelay(delay_ticks);
            continue;
        }

        if (pressed) {
            if (low_since == 0) {
                low_since = now;
                reset_fired = false;
            } else if (!reset_fired && now - low_since >= RUNTIME_RESET_HOLD_US) {
                reset_fired = true;
                ESP_LOGW(TAG, "runtime reset/setup pin threshold reached; restoring defaults");
                (void)config_store_reset_defaults();
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }
        } else {
            low_since = 0;
            reset_fired = false;
        }

        vTaskDelay(delay_ticks);
    }
}

esp_err_t system_reset_policy_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }
#if CONFIG_SCENEHUB_WEB_AUTH_RESET_GPIO < 0
    s_state.initialized = true;
    return ESP_OK;
#else
    esp_err_t err = configure_reset_pin();
    if (err != ESP_OK) {
        return err;
    }

    s_state.boot_setup_requested = detect_boot_setup_request();
    s_state.ignore_until_release = s_state.boot_setup_requested;

    BaseType_t ok = xTaskCreate(reset_policy_task, "sys_reset_policy", 2048, NULL, 5, &s_state.task_handle);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_state.initialized = true;
    return ESP_OK;
#endif
}

bool system_reset_policy_boot_setup_requested(void)
{
    return s_state.boot_setup_requested;
}
