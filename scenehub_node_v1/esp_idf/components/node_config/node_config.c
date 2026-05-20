#include "node_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "node_config";
static const char *NVS_NS = "node_cfg";
static const char *NVS_KEY = "config";
static const uint32_t NODE_CONFIG_VERSION = 2;

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    bool active_low;
    char label[24];
} node_output_pin_config_v1_t;

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    node_pin_role_t role;
    bool active_low;
    char label[24];
} node_universal_pin_config_v1_t;

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    uint16_t pixel_count;
    char label[24];
} node_led_strip_config_v1_t;

typedef struct {
    uint32_t version;
    char node_id[NODE_ID_MAX_LEN];
    char node_name[NODE_NAME_MAX_LEN];
    char wifi_ssid[NODE_WIFI_SSID_MAX_LEN];
    char wifi_password[NODE_WIFI_PASSWORD_MAX_LEN];
    char controller_host[NODE_HOST_MAX_LEN];
    uint16_t mqtt_port;
    char mqtt_client_id[NODE_MQTT_CLIENT_ID_MAX_LEN];
    int reset_gpio;
    bool pin_config_locked;
    node_output_pin_config_v1_t relays[NODE_RELAY_MAX];
    node_output_pin_config_v1_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_v1_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_v1_t led_strips[NODE_LED_STRIP_MAX];
} node_config_v1_t;

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
        config->led_strips[i].chipset = NODE_LED_CHIPSET_WS2812;
        config->led_strips[i].color_order = NODE_LED_COLOR_ORDER_GRB;
        config->led_strips[i].rgbw = false;
        snprintf(config->led_strips[i].label, sizeof(config->led_strips[i].label), "LED Strip %u", (unsigned)(i + 1));
    }
}

static void node_config_migrate_v1(const node_config_v1_t *legacy, node_config_t *config)
{
    if (!legacy || !config) {
        return;
    }

    node_config_set_factory_defaults(config);
    config->version = NODE_CONFIG_VERSION;
    memcpy(config->node_id, legacy->node_id, sizeof(config->node_id));
    memcpy(config->node_name, legacy->node_name, sizeof(config->node_name));
    memcpy(config->wifi_ssid, legacy->wifi_ssid, sizeof(config->wifi_ssid));
    memcpy(config->wifi_password, legacy->wifi_password, sizeof(config->wifi_password));
    memcpy(config->controller_host, legacy->controller_host, sizeof(config->controller_host));
    config->mqtt_port = legacy->mqtt_port;
    memcpy(config->mqtt_client_id, legacy->mqtt_client_id, sizeof(config->mqtt_client_id));
    config->reset_gpio = legacy->reset_gpio;
    config->pin_config_locked = legacy->pin_config_locked;
    memcpy(config->relays, legacy->relays, sizeof(config->relays));
    memcpy(config->mosfets, legacy->mosfets, sizeof(config->mosfets));
    memcpy(config->universal_io, legacy->universal_io, sizeof(config->universal_io));
    for (uint8_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        config->led_strips[i].enabled = legacy->led_strips[i].enabled;
        config->led_strips[i].channel = legacy->led_strips[i].channel;
        config->led_strips[i].gpio = legacy->led_strips[i].gpio;
        config->led_strips[i].pixel_count = legacy->led_strips[i].pixel_count;
        config->led_strips[i].chipset = NODE_LED_CHIPSET_WS2812;
        config->led_strips[i].color_order = NODE_LED_COLOR_ORDER_GRB;
        config->led_strips[i].rgbw = false;
        memcpy(config->led_strips[i].label,
               legacy->led_strips[i].label,
               sizeof(config->led_strips[i].label));
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
    if (err == ESP_OK && size == sizeof(*config) && config->version == NODE_CONFIG_VERSION) {
        return ESP_OK;
    }

    if (err == ESP_OK && size == sizeof(node_config_v1_t)) {
        node_config_v1_t legacy;
        size = sizeof(legacy);
        err = nvs_open(NVS_NS, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            err = nvs_get_blob(handle, NVS_KEY, &legacy, &size);
            nvs_close(handle);
        }
        if (err == ESP_OK && legacy.version == 1) {
            ESP_LOGW(TAG, "migrating saved config v1 -> v2");
            node_config_migrate_v1(&legacy, config);
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "saved config invalid, using factory defaults");
    node_config_set_factory_defaults(config);
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
