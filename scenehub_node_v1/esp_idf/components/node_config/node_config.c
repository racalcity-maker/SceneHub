#include "node_config_legacy.h"
#include "node_config_migrations.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "node_config";
static const char *NVS_NS = "node_cfg";
static const char *NVS_KEY = "config";

const uint32_t g_node_config_version = 9;
const uint32_t g_node_led_editor_version = 2;

static node_config_v8_t s_legacy_v8_config;
static node_config_v7_t s_legacy_v7_config;
static node_config_v6_t s_legacy_v6_config;
static node_config_v5_t s_legacy_v5_config;
static node_config_v4_t s_legacy_v4_config;
static node_config_v3_t s_legacy_v3_config;
static node_config_v2_t s_legacy_v2_config;
static node_config_v1_t s_legacy_v1_config;
static uint8_t s_unknown_config_blob[sizeof(node_config_t)];

static bool node_config_recover_common_prefix(size_t blob_size, node_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err;
    const node_config_common_prefix_t *prefix = NULL;

    if (!config || blob_size < sizeof(node_config_common_prefix_t) || blob_size > sizeof(s_unknown_config_blob)) {
        return false;
    }

    memset(s_unknown_config_blob, 0, sizeof(s_unknown_config_blob));
    err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }
    err = nvs_get_blob(handle, NVS_KEY, s_unknown_config_blob, &blob_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        return false;
    }

    prefix = (const node_config_common_prefix_t *)s_unknown_config_blob;
    if (prefix->version == 0 || prefix->version > g_node_config_version) {
        return false;
    }

    node_config_copy_text(config->node_id, sizeof(config->node_id), prefix->node_id);
    node_config_copy_text(config->node_name, sizeof(config->node_name), prefix->node_name);
    node_config_copy_text(config->wifi_ssid, sizeof(config->wifi_ssid), prefix->wifi_ssid);
    node_config_copy_text(config->wifi_password, sizeof(config->wifi_password), prefix->wifi_password);
    node_config_copy_text(config->controller_host, sizeof(config->controller_host), prefix->controller_host);
    config->mqtt_port = prefix->mqtt_port ? prefix->mqtt_port : 1883;
    node_config_copy_text(config->mqtt_client_id, sizeof(config->mqtt_client_id), prefix->mqtt_client_id);
    config->reset_gpio = prefix->reset_gpio;
    config->pin_config_locked = prefix->pin_config_locked;

    ESP_LOGW(TAG,
             "recovered common config prefix from unknown blob version=%lu size=%u",
             (unsigned long)prefix->version,
             (unsigned)blob_size);
    return true;
}

esp_err_t node_config_load_or_default(node_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err;
    size_t size = 0;

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    node_config_set_factory_defaults(config);

    err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "no saved config, using factory defaults");
        node_config_overlay_led_editor(config);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs open failed: %s", esp_err_to_name(err));
        node_config_overlay_led_editor(config);
        return ESP_OK;
    }

    err = nvs_get_blob(handle, NVS_KEY, NULL, &size);
    nvs_close(handle);
    if (err == ESP_OK && size == sizeof(*config)) {
        size = sizeof(*config);
        err = nvs_open(NVS_NS, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            err = nvs_get_blob(handle, NVS_KEY, config, &size);
            nvs_close(handle);
        }
    }
    if (err == ESP_OK && size == sizeof(*config) && config->version == g_node_config_version) {
        node_config_overlay_led_editor(config);
        return ESP_OK;
    }

    if (err == ESP_OK && size == sizeof(node_config_v8_t)) {
        memset(&s_legacy_v8_config, 0, sizeof(s_legacy_v8_config));
        size = sizeof(s_legacy_v8_config);
        err = nvs_open(NVS_NS, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            err = nvs_get_blob(handle, NVS_KEY, &s_legacy_v8_config, &size);
            nvs_close(handle);
        }
        if (err == ESP_OK && s_legacy_v8_config.version == 8) {
            ESP_LOGW(TAG, "migrating saved config v8 -> v9");
            node_config_migrate_v8(&s_legacy_v8_config, config);
            node_config_overlay_led_editor(config);
            return ESP_OK;
        }
    }

    if (err == ESP_OK && size == sizeof(node_config_v7_t)) {
        memset(&s_legacy_v7_config, 0, sizeof(s_legacy_v7_config));
        size = sizeof(s_legacy_v7_config);
        err = nvs_open(NVS_NS, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            err = nvs_get_blob(handle, NVS_KEY, &s_legacy_v7_config, &size);
            nvs_close(handle);
        }
        if (err == ESP_OK && s_legacy_v7_config.version == 7) {
            ESP_LOGW(TAG, "migrating saved config v7 -> v9");
            node_config_migrate_v7(&s_legacy_v7_config, config);
            node_config_overlay_led_editor(config);
            return ESP_OK;
        }
    }

    if (err == ESP_OK && size == sizeof(node_config_v6_t)) {
        memset(&s_legacy_v6_config, 0, sizeof(s_legacy_v6_config));
        size = sizeof(s_legacy_v6_config);
        err = nvs_open(NVS_NS, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            err = nvs_get_blob(handle, NVS_KEY, &s_legacy_v6_config, &size);
            nvs_close(handle);
        }
        if (err == ESP_OK && s_legacy_v6_config.version == 6) {
            ESP_LOGW(TAG, "migrating saved config v6 -> v9");
            node_config_migrate_v6(&s_legacy_v6_config, config);
            node_config_overlay_led_editor(config);
            return ESP_OK;
        }
    }

    if (err == ESP_OK && size == sizeof(node_config_v5_t)) {
        memset(&s_legacy_v5_config, 0, sizeof(s_legacy_v5_config));
        size = sizeof(s_legacy_v5_config);
        err = nvs_open(NVS_NS, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            err = nvs_get_blob(handle, NVS_KEY, &s_legacy_v5_config, &size);
            nvs_close(handle);
        }
        if (err == ESP_OK && s_legacy_v5_config.version == 5) {
            ESP_LOGW(TAG, "migrating saved config v5 -> v9");
            node_config_migrate_v5(&s_legacy_v5_config, config);
            node_config_overlay_led_editor(config);
            return ESP_OK;
        }
    }

    if (err == ESP_OK && size == sizeof(node_config_v4_t)) {
        memset(&s_legacy_v4_config, 0, sizeof(s_legacy_v4_config));
        size = sizeof(s_legacy_v4_config);
        err = nvs_open(NVS_NS, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            err = nvs_get_blob(handle, NVS_KEY, &s_legacy_v4_config, &size);
            nvs_close(handle);
        }
        if (err == ESP_OK && s_legacy_v4_config.version == 4) {
            ESP_LOGW(TAG, "migrating saved config v4 -> v9");
            node_config_migrate_v4(&s_legacy_v4_config, config);
            node_config_overlay_led_editor(config);
            return ESP_OK;
        }
    }

    if (err == ESP_OK && size == sizeof(node_config_v3_t)) {
        memset(&s_legacy_v3_config, 0, sizeof(s_legacy_v3_config));
        size = sizeof(s_legacy_v3_config);
        err = nvs_open(NVS_NS, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            err = nvs_get_blob(handle, NVS_KEY, &s_legacy_v3_config, &size);
            nvs_close(handle);
        }
        if (err == ESP_OK && s_legacy_v3_config.version == 3) {
            ESP_LOGW(TAG, "migrating saved config v3 -> v9");
            node_config_migrate_v3(&s_legacy_v3_config, config);
            node_config_overlay_led_editor(config);
            return ESP_OK;
        }
    }

    if (err == ESP_OK && size == sizeof(node_config_v2_t)) {
        memset(&s_legacy_v2_config, 0, sizeof(s_legacy_v2_config));
        size = sizeof(s_legacy_v2_config);
        err = nvs_open(NVS_NS, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            err = nvs_get_blob(handle, NVS_KEY, &s_legacy_v2_config, &size);
            nvs_close(handle);
        }
        if (err == ESP_OK && s_legacy_v2_config.version == 2) {
            ESP_LOGW(TAG, "migrating saved config v2 -> v8");
            node_config_migrate_v2(&s_legacy_v2_config, config);
            node_config_overlay_led_editor(config);
            return ESP_OK;
        }
    }

    if (err == ESP_OK && size == sizeof(node_config_v1_t)) {
        memset(&s_legacy_v1_config, 0, sizeof(s_legacy_v1_config));
        size = sizeof(s_legacy_v1_config);
        err = nvs_open(NVS_NS, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            err = nvs_get_blob(handle, NVS_KEY, &s_legacy_v1_config, &size);
            nvs_close(handle);
        }
        if (err == ESP_OK && s_legacy_v1_config.version == 1) {
            ESP_LOGW(TAG, "migrating saved config v1 -> v8");
            node_config_migrate_v1(&s_legacy_v1_config, config);
            node_config_overlay_led_editor(config);
            return ESP_OK;
        }
    }

    if (node_config_recover_common_prefix(size, config)) {
        node_config_overlay_led_editor(config);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "saved config invalid size=%u err=%s, using factory defaults",
             (unsigned)size,
             esp_err_to_name(err));
    node_config_set_factory_defaults(config);
    node_config_overlay_led_editor(config);
    return ESP_OK;
}
