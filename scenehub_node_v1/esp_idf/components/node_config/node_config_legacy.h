#pragma once

#include "node_config_internal.h"

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    bool active_low;
    char label[24];
} node_output_pin_config_v1_t;

typedef node_output_pin_config_v1_t node_output_pin_config_v2_t;
typedef node_output_pin_config_v1_t node_output_pin_config_v3_t;

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    node_pin_role_t role;
    bool active_low;
    char label[24];
} node_universal_pin_config_v1_t;

typedef node_universal_pin_config_v1_t node_universal_pin_config_v11_t;

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

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    uint16_t pixel_count;
    node_led_chipset_t chipset;
    node_led_color_order_t color_order;
    bool rgbw;
    char label[24];
} node_led_strip_config_v2_t;

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    uint16_t pixel_count;
    node_led_chipset_t chipset;
    node_led_color_order_t color_order;
    bool rgbw;
    uint32_t effect_duration_ms;
    uint32_t effect_step_ms;
    uint16_t effect_repeat_count;
    char label[24];
} node_led_strip_config_v3_t;

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
    node_output_pin_config_v2_t relays[NODE_RELAY_MAX];
    node_output_pin_config_v2_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_v11_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_v2_t led_strips[NODE_LED_STRIP_MAX];
} node_config_v2_t;

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
    node_output_pin_config_v3_t relays[NODE_RELAY_MAX];
    node_output_pin_config_v3_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_v11_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_v3_t led_strips[NODE_LED_STRIP_MAX];
} node_config_v3_t;

typedef node_led_strip_config_v3_t node_led_strip_config_v4_t;

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    uint16_t pixel_count;
    node_led_chipset_t chipset;
    node_led_color_order_t color_order;
    bool rgbw;
    uint32_t blink_on_ms;
    uint32_t blink_off_ms;
    uint16_t blink_repeat_count;
    uint32_t breathe_cycle_ms;
    uint32_t breathe_step_ms;
    uint16_t breathe_repeat_count;
    uint32_t effect_duration_ms;
    uint32_t effect_step_ms;
    uint16_t effect_repeat_count;
    uint8_t effect_red;
    uint8_t effect_green;
    uint8_t effect_blue;
    uint8_t effect_white;
    uint8_t effect_red2;
    uint8_t effect_green2;
    uint8_t effect_blue2;
    uint8_t effect_white2;
    char label[24];
} node_led_strip_config_v5_t;

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
    node_output_pin_config_v3_t relays[NODE_RELAY_MAX];
    node_output_pin_config_v3_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_v11_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_v4_t led_strips[NODE_LED_STRIP_MAX];
} node_config_v4_t;

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
    node_output_pin_config_v3_t relays[NODE_RELAY_MAX];
    node_output_pin_config_v3_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_v11_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_v5_t led_strips[NODE_LED_STRIP_MAX];
} node_config_v5_t;

typedef struct {
    uint32_t on_ms;
    uint32_t off_ms;
    uint16_t repeat_count;
} node_led_blink_preset_v6_t;

typedef struct {
    uint32_t cycle_ms;
    uint32_t step_ms;
    uint16_t repeat_count;
} node_led_breathe_preset_v6_t;

typedef struct {
    uint32_t duration_ms;
    uint32_t step_ms;
    uint16_t repeat_count;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t white;
    uint8_t red2;
    uint8_t green2;
    uint8_t blue2;
    uint8_t white2;
} node_led_effect_preset_v7_t;

typedef struct {
    node_led_effect_preset_v7_t rainbow;
    node_led_effect_preset_v7_t color_wipe;
    node_led_effect_preset_v7_t scanner;
    node_led_effect_preset_v7_t theater;
    node_led_effect_preset_v7_t strobe;
} node_led_effect_presets_v7_t;

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    uint16_t pixel_count;
    node_led_chipset_t chipset;
    node_led_color_order_t color_order;
    bool rgbw;
    node_led_blink_preset_v6_t blink;
    node_led_breathe_preset_v6_t breathe;
    node_led_effect_presets_v7_t effects;
    char label[24];
} node_led_strip_config_v6_t;

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
    node_output_pin_config_v3_t relays[NODE_RELAY_MAX];
    node_output_pin_config_v3_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_v11_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_v6_t led_strips[NODE_LED_STRIP_MAX];
} node_config_v6_t;

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    uint16_t pixel_count;
    node_led_chipset_t chipset;
    node_led_color_order_t color_order;
    bool rgbw;
    node_led_blink_preset_t blink;
    node_led_breathe_preset_t breathe;
    node_led_effect_presets_v7_t effects;
    char label[24];
} node_led_strip_config_v7_t;

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
    node_output_pin_config_v3_t relays[NODE_RELAY_MAX];
    node_output_pin_config_v3_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_v11_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_v7_t led_strips[NODE_LED_STRIP_MAX];
} node_config_v7_t;

typedef struct {
    bool enabled;
    uint8_t channel;
    int gpio;
    uint16_t pixel_count;
    node_led_chipset_t chipset;
    node_led_color_order_t color_order;
    bool rgbw;
    node_led_blink_preset_t blink;
    node_led_breathe_preset_t breathe;
    node_led_effect_presets_v8_t effects;
    char label[24];
} node_led_strip_config_v8_t;

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
    node_output_pin_config_t relays[NODE_RELAY_MAX];
    node_output_pin_config_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_v11_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_v8_t led_strips[NODE_LED_STRIP_MAX];
} node_config_v8_t;

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
    node_output_pin_config_t relays[NODE_RELAY_MAX];
    node_output_pin_config_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_v11_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_t led_strips[NODE_LED_STRIP_MAX];
} node_config_v9_t;

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
    uint8_t operation_mode;
    node_output_pin_config_t relays[NODE_RELAY_MAX];
    node_output_pin_config_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_v11_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_t led_strips[NODE_LED_STRIP_MAX];
} node_config_v10_t;

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
    uint8_t operation_mode;
    bool standalone_mqtt_enabled;
    node_output_pin_config_t relays[NODE_RELAY_MAX];
    node_output_pin_config_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_v11_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_t led_strips[NODE_LED_STRIP_MAX];
} node_config_v11_t;

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
    uint8_t operation_mode;
    bool standalone_mqtt_enabled;
    node_output_pin_config_t relays[NODE_RELAY_MAX];
    node_output_pin_config_t mosfets[NODE_MOSFET_MAX];
    node_universal_pin_config_v11_t universal_io[NODE_UNIVERSAL_IO_MAX];
    node_led_strip_config_t led_strips[NODE_LED_STRIP_MAX];
} node_config_v12_t;

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
} node_config_common_prefix_t;
