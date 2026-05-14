#include "web_ui_auth_internal.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#if CONFIG_SCENEHUB_WEB_AUTH_RESET_GPIO >= 0
static void web_auth_reset_task(void *param)
{
    const int pin = CONFIG_SCENEHUB_WEB_AUTH_RESET_GPIO;
    int64_t low_since = 0;
    const TickType_t delay_ticks = pdMS_TO_TICKS(100);
    ESP_LOGI(g_web_ui_auth_tag, "web auth reset monitor on GPIO%d", pin);
    while (1) {
        int level = gpio_get_level(pin);
        int64_t now = esp_timer_get_time();
        if (level == 0) {
            if (low_since == 0) {
                low_since = now;
            } else if (now - low_since >= WEB_AUTH_RESET_HOLD_US) {
                ESP_LOGW(g_web_ui_auth_tag, "web auth reset pin triggered, restoring defaults");
                config_store_reset_web_auth_defaults();
                web_sessions_clear();
                low_since = 0;
            }
        } else {
            low_since = 0;
        }
        vTaskDelay(delay_ticks);
    }
}

void web_auth_start_reset_monitor(void)
{
    if (g_web_ui_auth_reset_task) {
        return;
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << CONFIG_SCENEHUB_WEB_AUTH_RESET_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(g_web_ui_auth_tag, "web auth reset gpio config failed: %s", esp_err_to_name(err));
        return;
    }
    BaseType_t ok = xTaskCreate(web_auth_reset_task, "web_auth_reset", 2048, NULL, 5, &g_web_ui_auth_reset_task);
    if (ok != pdPASS) {
        g_web_ui_auth_reset_task = NULL;
        ESP_LOGE(g_web_ui_auth_tag, "failed to create web auth reset monitor task");
        return;
    }
    ESP_LOGI(g_web_ui_auth_tag, "web auth reset monitor task started");
}
#else
void web_auth_start_reset_monitor(void)
{
    // feature disabled
}
#endif
