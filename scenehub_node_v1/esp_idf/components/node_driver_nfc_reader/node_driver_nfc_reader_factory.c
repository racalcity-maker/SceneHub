#include "node_driver_nfc_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "node_board.h"
#include "sdkconfig.h"

static const char *TAG = "node_drv_nfc_cfg";

static node_nfc_reader_config_t *alloc_factory_config_buffer(void)
{
    node_nfc_reader_config_t *ptr = NULL;

#if CONFIG_SPIRAM
    ptr = (node_nfc_reader_config_t *)heap_caps_malloc(sizeof(*ptr), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, sizeof(*ptr));
        return ptr;
    }
#endif
    ptr = (node_nfc_reader_config_t *)heap_caps_malloc(sizeof(*ptr), MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, sizeof(*ptr));
    }
    return ptr;
}

void node_driver_nfc_reader_apply_saved_cards(node_nfc_reader_config_t *config);

#if CONFIG_SCENEHUB_NODE_DRIVER_PN532_ENABLED
static bool gpio_conflicts_with_runtime_config(const node_config_t *config, int gpio)
{
    if (!config || gpio < 0) {
        return false;
    }
    if (config->reset_gpio == gpio) {
        return true;
    }
    for (size_t i = 0; i < NODE_RELAY_MAX; ++i) {
        if (config->relays[i].enabled && config->relays[i].gpio == gpio) {
            return true;
        }
    }
    for (size_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        if (config->mosfets[i].enabled && config->mosfets[i].gpio == gpio) {
            return true;
        }
    }
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        if (config->universal_io[i].enabled && config->universal_io[i].gpio == gpio) {
            return true;
        }
    }
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        if (config->led_strips[i].enabled && config->led_strips[i].gpio == gpio) {
            return true;
        }
    }
    return false;
}
#endif

esp_err_t node_driver_nfc_reader_load_factory_config(const node_config_t *node_config,
                                                     node_nfc_reader_config_t *out_config)
{
#if !CONFIG_SCENEHUB_NODE_DRIVER_PN532_ENABLED
    (void)node_config;
    (void)out_config;
    return ESP_ERR_NOT_FOUND;
#else
    if (!node_config || !out_config) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_config, 0, sizeof(*out_config));
    out_config->enabled = true;
    snprintf(out_config->id, sizeof(out_config->id), "%s", "reader_1");
    snprintf(out_config->driver_impl, sizeof(out_config->driver_impl), "%s", "pn532");
    snprintf(out_config->bus, sizeof(out_config->bus), "%s", "i2c_1");
    out_config->i2c_sda_gpio = CONFIG_SCENEHUB_NODE_DRIVER_PN532_I2C_SDA_GPIO;
    out_config->i2c_scl_gpio = CONFIG_SCENEHUB_NODE_DRIVER_PN532_I2C_SCL_GPIO;
    out_config->reset_gpio = CONFIG_SCENEHUB_NODE_DRIVER_PN532_RESET_GPIO;
    out_config->i2c_address = (uint8_t)CONFIG_SCENEHUB_NODE_DRIVER_PN532_I2C_ADDRESS;
    out_config->poll_interval_ms = CONFIG_SCENEHUB_NODE_DRIVER_PN532_POLL_INTERVAL_MS;
    out_config->debounce_ms = CONFIG_SCENEHUB_NODE_DRIVER_PN532_DEBOUNCE_MS;

    if (!node_board_gpio_is_allowed(out_config->i2c_sda_gpio) ||
        !node_board_gpio_is_allowed(out_config->i2c_scl_gpio) ||
        (out_config->reset_gpio >= 0 && !node_board_gpio_is_allowed(out_config->reset_gpio))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (gpio_conflicts_with_runtime_config(node_config, out_config->i2c_sda_gpio) ||
        gpio_conflicts_with_runtime_config(node_config, out_config->i2c_scl_gpio) ||
        gpio_conflicts_with_runtime_config(node_config, out_config->reset_gpio)) {
        return ESP_ERR_INVALID_STATE;
    }

    node_driver_nfc_reader_apply_saved_cards(out_config);
    return ESP_OK;
#endif
}

esp_err_t node_driver_nfc_reader_register_factory_stub(const node_config_t *node_config)
{
    node_nfc_reader_config_t *config = NULL;
    esp_err_t err = ESP_OK;

    config = alloc_factory_config_buffer();
    if (!config) {
        return ESP_ERR_NO_MEM;
    }
    err = node_driver_nfc_reader_load_factory_config(node_config, config);

    if (err == ESP_ERR_NOT_FOUND) {
        free(config);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "pn532 factory config rejected: %s", esp_err_to_name(err));
        free(config);
        return err;
    }

    err = node_driver_nfc_reader_register_stub(config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "pn532 stub register failed: %s", esp_err_to_name(err));
        free(config);
        return err;
    }

    ESP_LOGI(TAG,
             "pn532 stub enabled id=%s bus=%s sda=%d scl=%d rst=%d addr=0x%02x",
             config->id,
             config->bus,
             config->i2c_sda_gpio,
             config->i2c_scl_gpio,
             config->reset_gpio,
             (unsigned)config->i2c_address);
    free(config);
    return ESP_OK;
}
