#include "node_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "node_config";
static const char *NVS_NS = "node_cfg";
static const char *NVS_KEY = "config";
static const uint32_t NODE_CONFIG_VERSION = 1;

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    snprintf(dst, dst_size, "%s", src);
}

void node_config_set_factory_defaults(node_config_t *config)
{
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->version = NODE_CONFIG_VERSION;
    copy_text(config->node_id, sizeof(config->node_id), "scenehub_node_s3");
    copy_text(config->node_name, sizeof(config->node_name), "SceneHub Node S3");
    copy_text(config->mqtt_client_id, sizeof(config->mqtt_client_id), "dcc-scenehub-node-s3");
    config->mqtt_port = 1883;
    config->reset_gpio = 0;

    for (uint8_t i = 0; i < NODE_RELAY_MAX; ++i) {
        config->relays[i].channel = i + 1;
        config->relays[i].gpio = -1;
        snprintf(config->relays[i].label, sizeof(config->relays[i].label), "Relay %u", (unsigned)(i + 1));
    }
    for (uint8_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        config->mosfets[i].channel = i + 1;
        config->mosfets[i].gpio = -1;
        snprintf(config->mosfets[i].label, sizeof(config->mosfets[i].label), "MOSFET %u", (unsigned)(i + 1));
    }
    for (uint8_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        config->universal_io[i].channel = i + 1;
        config->universal_io[i].gpio = -1;
        config->universal_io[i].role = NODE_PIN_DISABLED;
        snprintf(config->universal_io[i].label, sizeof(config->universal_io[i].label), "IO %u", (unsigned)(i + 1));
    }
    for (uint8_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        config->led_strips[i].channel = i + 1;
        config->led_strips[i].gpio = -1;
        config->led_strips[i].pixel_count = 30;
        snprintf(config->led_strips[i].label, sizeof(config->led_strips[i].label), "LED Strip %u", (unsigned)(i + 1));
    }
}

esp_err_t node_config_load_or_default(node_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    node_config_set_factory_defaults(config);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "no saved config, using factory defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs open failed: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    size_t size = sizeof(*config);
    err = nvs_get_blob(handle, NVS_KEY, config, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(*config) || config->version != NODE_CONFIG_VERSION) {
        ESP_LOGW(TAG, "saved config invalid, using factory defaults");
        node_config_set_factory_defaults(config);
        return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t node_config_save(const node_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, NVS_KEY, config, sizeof(*config));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t node_config_reset_wifi(void)
{
    node_config_t config;
    esp_err_t err = node_config_load_or_default(&config);
    if (err != ESP_OK) {
        return err;
    }
    config.wifi_ssid[0] = '\0';
    config.wifi_password[0] = '\0';
    return node_config_save(&config);
}

esp_err_t node_config_factory_reset(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(handle, NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

bool node_config_needs_provisioning(const node_config_t *config)
{
    if (!config) {
        return true;
    }
    return config->wifi_ssid[0] == '\0' || config->controller_host[0] == '\0';
}
