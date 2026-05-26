#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "node_board.h"
#include "node_capability.h"
#include "node_config.h"
#include "node_control.h"
#include "node_hardware_io.h"
#include "node_management.h"
#include "node_mqtt_transport.h"
#include "node_provisioning.h"
#include "node_reset_button.h"

static const char *TAG = "scenehub_node";

static node_config_t s_config;
static char s_manifest[NODE_DEVICE_DESCRIPTION_MAX_LEN];
static StaticTask_t s_network_task_storage;
static StackType_t s_network_task_stack[4096];
static bool s_runtime_initialized;

static void log_output_pins(const char *group, const node_output_pin_config_t *pins, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const node_output_pin_config_t *pin = &pins[i];
        if (pin->enabled) {
            ESP_LOGI(TAG,
                     "%s%u enabled gpio=%d active_low=%d label=%s",
                     group,
                     (unsigned)pin->channel,
                     pin->gpio,
                     pin->active_low,
                     pin->label);
        }
    }
}

static void log_pin_config(const node_config_t *config)
{
    log_output_pins("relay", config->relays, NODE_RELAY_MAX);
    log_output_pins("mosfet", config->mosfets, NODE_MOSFET_MAX);
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_universal_pin_config_t *pin = &config->universal_io[i];
        if (pin->enabled) {
            ESP_LOGI(TAG,
                     "io%u enabled gpio=%d role=%d active_low=%d label=%s",
                     (unsigned)pin->channel,
                     pin->gpio,
                     (int)pin->role,
                     pin->active_low,
                     pin->label);
        }
    }
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *pin = &config->led_strips[i];
        if (pin->enabled) {
            ESP_LOGI(TAG,
                     "led%u enabled gpio=%d pixels=%u label=%s",
                     (unsigned)pin->channel,
                     pin->gpio,
                     (unsigned)pin->pixel_count,
                     pin->label);
        }
    }
}

static void init_runtime_after_network(const node_config_t *config)
{
    if (s_runtime_initialized) {
        return;
    }
    esp_err_t err = node_hardware_io_init(config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "hardware init failed: %s", esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(node_control_init(config));

    size_t written = 0;
    err = node_capability_write_device_description(config, s_manifest, sizeof(s_manifest), &written);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "device_description ready bytes=%u", (unsigned)written);
    } else {
        ESP_LOGE(TAG, "device_description failed: %s", esp_err_to_name(err));
    }
    s_runtime_initialized = true;
}

static void on_sta_got_ip(const node_config_t *config, void *ctx)
{
    (void)ctx;
    if (node_config_needs_provisioning(config)) {
        return;
    }
    init_runtime_after_network(config);
    esp_err_t err = node_mqtt_transport_start(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mqtt transport start failed: %s", esp_err_to_name(err));
    }
}

static void network_task(void *arg)
{
    node_config_t *config = (node_config_t *)arg;
    ESP_LOGI(TAG, "network/provisioning start");
    if (node_config_needs_provisioning(config)) {
        init_runtime_after_network(config);
    }
    node_provisioning_callbacks_t callbacks = {
        .got_ip_cb = on_sta_got_ip,
        .got_ip_ctx = NULL,
    };
    esp_err_t err = node_provisioning_start(config, &callbacks);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "network/provisioning start failed: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    node_config_load_or_default(&s_config);
    ESP_ERROR_CHECK(node_board_apply_factory_pin_config(&s_config));
    if (node_board_sanitize_pin_config(&s_config)) {
        ESP_LOGW(TAG, "pin config sanitized; invalid or duplicate pins were disabled");
    }

    const node_board_profile_t *board = node_board_get_profile();

    ESP_LOGI(TAG, "boot node_id=%s name=%s target=%s", s_config.node_id, s_config.node_name, board->target);
    ESP_LOGI(TAG, "pin capacity relay=%d mosfet=%d io=%d led_strip=%d",
             NODE_RELAY_MAX,
             NODE_MOSFET_MAX,
             NODE_UNIVERSAL_IO_MAX,
             NODE_LED_STRIP_MAX);
    ESP_LOGI(TAG, "provisioning_required=%d reset_gpio=%d",
             node_config_needs_provisioning(&s_config),
             s_config.reset_gpio);

    err = node_management_start(&s_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "management start failed: %s", esp_err_to_name(err));
    }

    log_pin_config(&s_config);

    node_reset_button_config_t reset_button = {
        .gpio = s_config.reset_gpio,
        .active_low = true,
        .callback = node_management_handle_reset_button_event,
        .callback_ctx = NULL,
    };
    err = node_reset_button_start(&reset_button);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reset/config button disabled: %s", esp_err_to_name(err));
    }

    TaskHandle_t handle = xTaskCreateStatic(network_task,
                                            "node_network",
                                            sizeof(s_network_task_stack) / sizeof(s_network_task_stack[0]),
                                            &s_config,
                                            tskIDLE_PRIORITY + 2,
                                            s_network_task_stack,
                                            &s_network_task_storage);
    if (!handle) {
        ESP_LOGE(TAG, "network task create failed");
    }
}
