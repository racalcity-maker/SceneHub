#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_config.h"

typedef struct {
    const char *target;
    const char *default_node_id;
    const char *default_node_name;
    const char *default_mqtt_client_id;
    int default_reset_gpio;
    uint8_t max_relays;
    uint8_t max_mosfets;
    uint8_t max_universal_io;
    uint8_t max_led_strips;
} node_board_profile_t;

const node_board_profile_t *node_board_get_profile(void);
esp_err_t node_board_apply_factory_pin_config(node_config_t *config);
bool node_board_sanitize_pin_config(node_config_t *config);
bool node_board_gpio_is_allowed(int gpio);
bool node_board_gpio_is_strapping_or_reserved(int gpio);
