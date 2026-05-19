#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_limits.h"

typedef enum {
    NODE_PIN_DISABLED = 0,
    NODE_PIN_RELAY,
    NODE_PIN_MOSFET,
    NODE_PIN_UNIVERSAL_INPUT,
    NODE_PIN_UNIVERSAL_OUTPUT,
    NODE_PIN_LED_STRIP,
} node_pin_role_t;

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    bool active_low;
    char label[24];
} node_output_pin_config_t;

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    node_pin_role_t role;
    bool active_low;
    char label[24];
} node_universal_pin_config_t;

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    uint16_t pixel_count;
    char label[24];
} node_led_strip_config_t;

typedef struct node_config_t {
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
    node_output_pin_config_t relays[NODE_RELAY_MAX];
    node_output_pin_config_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_t led_strips[NODE_LED_STRIP_MAX];
} node_config_t;

void node_config_set_factory_defaults(node_config_t *config);
esp_err_t node_config_load_or_default(node_config_t *config);
esp_err_t node_config_save(const node_config_t *config);
esp_err_t node_config_reset_wifi(void);
esp_err_t node_config_factory_reset(void);
bool node_config_needs_provisioning(const node_config_t *config);
