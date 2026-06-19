#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "node_board.h"
#include "node_config.h"
#include "node_control.h"
#include "node_driver_nfc_reader.h"
#include "node_driver_nfc_reader_runtime.h"
#include "node_driver_registry.h"
#include "node_event_router.h"
#include "node_fallback_runtime.h"
#include "node_hardware_io.h"
#include "node_management.h"
#include "node_mqtt_transport.h"
#include "node_provisioning.h"
#include "node_reset_button.h"
#include "node_rule_compile.h"
#include "node_rule_engine.h"
#include "node_runtime_mode.h"

static const char *TAG = "scenehub_node";

static node_config_t s_config;
static StaticTask_t s_network_task_storage;
static StackType_t s_network_task_stack[4096];
static bool s_runtime_initialized;

static node_fallback_runtime_config_t build_fallback_runtime_config(const node_config_t *config)
{
    node_fallback_runtime_config_t fallback = {0};

    if (!config) {
        return fallback;
    }

    fallback.enabled = ((node_operation_mode_t)config->operation_mode) == NODE_OPERATION_MODE_FALLBACK &&
                       config->fallback_timeout_ms > 0;
    fallback.fallback_timeout_ms = config->fallback_timeout_ms;
    fallback.fallback_return_delay_ms = config->fallback_return_delay_ms;
    fallback.return_policy =
        (config->fallback_return_policy == NODE_CONFIG_FALLBACK_RETURN_POLICY_MANUAL_STAY_ACTIVE)
            ? NODE_FALLBACK_RETURN_POLICY_MANUAL_STAY_ACTIVE
            : NODE_FALLBACK_RETURN_POLICY_AUTO_ON_STABLE_MQTT;
    return fallback;
}

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

static void init_local_runtime(const node_config_t *config)
{
    if (s_runtime_initialized) {
        return;
    }
    esp_err_t err = node_hardware_io_init(config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "hardware init failed: %s", esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(node_control_init(config));
    err = node_rule_engine_init(config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "rule engine init failed: %s", esp_err_to_name(err));
    } else {
        err = node_driver_nfc_reader_runtime_start(config);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "nfc reader runtime start failed: %s", esp_err_to_name(err));
        }
    }
    s_runtime_initialized = true;
}

static void on_sta_got_ip(const node_config_t *config, void *ctx)
{
    (void)ctx;
    (void)node_fallback_runtime_note_wifi_state(true);
    if (node_config_needs_provisioning(config)) {
        return;
    }
    if (!node_runtime_mode_should_start_mqtt(config)) {
        ESP_LOGI(TAG,
                 "mqtt transport skipped for operation_mode=%s standalone_mqtt_enabled=%d controller_host=%s",
                 node_runtime_mode_name((node_operation_mode_t)config->operation_mode),
                 config->standalone_mqtt_enabled,
                 config->controller_host);
        return;
    }
    esp_err_t err = node_mqtt_transport_start(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mqtt transport start failed: %s", esp_err_to_name(err));
    }
}

static void on_sta_disconnected(const node_config_t *config, uint16_t reason, void *ctx)
{
    (void)config;
    (void)reason;
    (void)ctx;
    (void)node_fallback_runtime_note_wifi_state(false);
}

static void network_task(void *arg)
{
    node_config_t *config = (node_config_t *)arg;
    ESP_LOGI(TAG, "network/provisioning start");
    init_local_runtime(config);
    esp_err_t err = node_fallback_runtime_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fallback runtime init failed: %s", esp_err_to_name(err));
    } else {
        node_fallback_runtime_config_t fallback = build_fallback_runtime_config(config);
        err = node_fallback_runtime_configure(&fallback);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "fallback runtime configure failed: %s", esp_err_to_name(err));
        }
    }
    node_provisioning_callbacks_t callbacks = {
        .got_ip_cb = on_sta_got_ip,
        .got_ip_ctx = NULL,
        .sta_disconnected_cb = on_sta_disconnected,
        .sta_disconnected_ctx = NULL,
    };
    err = node_provisioning_start(config, &callbacks);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "network/provisioning start failed: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot reset_reason=%d", (int)esp_reset_reason());

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    node_config_load_or_default(&s_config);
    ESP_ERROR_CHECK(node_driver_registry_init());
    ESP_ERROR_CHECK(node_event_router_init());
    ESP_ERROR_CHECK(node_board_apply_factory_pin_config(&s_config));
    if (node_board_sanitize_pin_config(&s_config)) {
        ESP_LOGW(TAG, "pin config sanitized; invalid or duplicate pins were disabled");
    }
    err = node_driver_nfc_reader_register_factory_stub(&s_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "factory driver registration skipped: %s", esp_err_to_name(err));
    }
    err = node_rule_compile_bootstrap(&s_config);
    if (err != ESP_OK) {
        const node_rule_compiled_bundle_t *compiled = node_rule_compile_peek_active();
        ESP_LOGW(TAG,
                 "stored rules compile failed: %s code=%s",
                 esp_err_to_name(err),
                 compiled ? compiled->error_code : "");
    } else {
        const node_rule_compiled_bundle_t *compiled = node_rule_compile_peek_active();
        if (compiled &&
            compiled->status == NODE_RULE_COMPILE_STATUS_READY &&
            compiled->metadata.has_bundle) {
            ESP_LOGI(TAG,
                     "stored rules ready bundle=%s generation=%lu rules=%u actions=%u",
                     compiled->metadata.bundle_id,
                     (unsigned long)compiled->metadata.generation,
                     (unsigned)compiled->rule_count,
                     (unsigned)compiled->total_action_count);
        }
    }

    const node_board_profile_t *board = node_board_get_profile();

    ESP_LOGI(TAG,
             "boot node_id=%s name=%s target=%s operation_mode=%s",
             s_config.node_id,
             s_config.node_name,
             board->target,
             node_runtime_mode_name((node_operation_mode_t)s_config.operation_mode));
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

    if (s_config.reset_gpio >= 0) {
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
    } else {
        ESP_LOGI(TAG, "reset/config button disabled by config");
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
