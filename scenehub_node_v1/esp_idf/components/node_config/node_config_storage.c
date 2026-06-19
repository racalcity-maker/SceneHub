#include "node_config_internal.h"

#include "nvs.h"

static const char *NVS_NS = "node_cfg";
static const char *NVS_KEY = "config";
static const char *NVS_LED_KEY = "led_cfg";

esp_err_t node_config_save(const node_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
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
    static node_config_t s_reset_wifi_config;
    esp_err_t err = node_config_load_or_default(&s_reset_wifi_config);
    if (err != ESP_OK) {
        return err;
    }
    s_reset_wifi_config.wifi_ssid[0] = '\0';
    s_reset_wifi_config.wifi_password[0] = '\0';
    return node_config_save(&s_reset_wifi_config);
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
        esp_err_t led_err = nvs_erase_key(handle, NVS_LED_KEY);
        if (led_err != ESP_OK && led_err != ESP_ERR_NVS_NOT_FOUND) {
            err = led_err;
        }
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
    if (config->wifi_ssid[0] == '\0') {
        return true;
    }
    if (config->operation_mode == NODE_OPERATION_MODE_STANDALONE) {
        return false;
    }
    return config->controller_host[0] == '\0';
}
