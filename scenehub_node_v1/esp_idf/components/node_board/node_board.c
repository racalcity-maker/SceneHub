#include "node_board.h"

#include <stdbool.h>
#include <stdio.h>

#include "node_config.h"
#include "node_limits.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3
static const node_board_profile_t s_profile = {
    .target = "esp32s3",
    .default_node_id = "scenehub_node_s3",
    .default_node_name = "SceneHub Node S3",
    .default_mqtt_client_id = "dcc-scenehub-node-s3",
    .default_reset_gpio = 0,
    .max_relays = NODE_RELAY_MAX,
    .max_mosfets = NODE_MOSFET_MAX,
    .max_universal_io = NODE_UNIVERSAL_IO_MAX,
    .max_led_strips = NODE_LED_STRIP_MAX,
};
#else
static const node_board_profile_t s_profile = {
    .target = "unknown",
    .default_node_id = "scenehub_node",
    .default_node_name = "SceneHub Node",
    .default_mqtt_client_id = "dcc-scenehub-node",
    .default_reset_gpio = 0,
    .max_relays = NODE_RELAY_MAX,
    .max_mosfets = NODE_MOSFET_MAX,
    .max_universal_io = NODE_UNIVERSAL_IO_MAX,
    .max_led_strips = NODE_LED_STRIP_MAX,
};
#endif

const node_board_profile_t *node_board_get_profile(void)
{
    return &s_profile;
}

#if CONFIG_SCENEHUB_NODE_FACTORY_PIN_PROFILE
static void board_copy_label(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void apply_relay(node_config_t *config, size_t idx, int gpio, const char *label)
{
    if (!config || idx >= NODE_RELAY_MAX || gpio < 0) {
        return;
    }
    config->relays[idx].enabled = true;
    config->relays[idx].gpio = gpio;
#if CONFIG_SCENEHUB_NODE_FACTORY_PIN_PROFILE
    config->relays[idx].active_low = CONFIG_SCENEHUB_NODE_FACTORY_RELAY_ACTIVE_LOW;
#else
    config->relays[idx].active_low = true;
#endif
    board_copy_label(config->relays[idx].label, sizeof(config->relays[idx].label), label);
}

static void apply_mosfet(node_config_t *config, size_t idx, int gpio, const char *label)
{
    if (!config || idx >= NODE_MOSFET_MAX || gpio < 0) {
        return;
    }
    config->mosfets[idx].enabled = true;
    config->mosfets[idx].gpio = gpio;
#if CONFIG_SCENEHUB_NODE_FACTORY_PIN_PROFILE
    config->mosfets[idx].active_low = CONFIG_SCENEHUB_NODE_FACTORY_MOSFET_ACTIVE_LOW;
#else
    config->mosfets[idx].active_low = false;
#endif
    board_copy_label(config->mosfets[idx].label, sizeof(config->mosfets[idx].label), label);
}

static node_pin_role_t io_role_from_config(int role)
{
    switch (role) {
    case 1:
        return NODE_PIN_UNIVERSAL_INPUT;
    case 2:
        return NODE_PIN_UNIVERSAL_OUTPUT;
    default:
        return NODE_PIN_DISABLED;
    }
}

static void apply_io(node_config_t *config, size_t idx, int gpio, int role, const char *label)
{
    if (!config || idx >= NODE_UNIVERSAL_IO_MAX || gpio < 0) {
        return;
    }
    node_pin_role_t pin_role = io_role_from_config(role);
    if (pin_role == NODE_PIN_DISABLED) {
        return;
    }
    config->universal_io[idx].enabled = true;
    config->universal_io[idx].gpio = gpio;
    config->universal_io[idx].role = pin_role;
    board_copy_label(config->universal_io[idx].label, sizeof(config->universal_io[idx].label), label);
}

static void apply_led(node_config_t *config, size_t idx, int gpio, int pixels, const char *label)
{
    if (!config || idx >= NODE_LED_STRIP_MAX || gpio < 0) {
        return;
    }
    config->led_strips[idx].enabled = true;
    config->led_strips[idx].gpio = gpio;
    config->led_strips[idx].pixel_count = pixels > 0 ? (uint16_t)pixels : 30;
    board_copy_label(config->led_strips[idx].label, sizeof(config->led_strips[idx].label), label);
}
#endif

esp_err_t node_board_apply_factory_pin_config(node_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_SCENEHUB_NODE_FACTORY_PIN_PROFILE
    config->pin_config_locked = CONFIG_SCENEHUB_NODE_FACTORY_PIN_LOCKED;
    apply_relay(config, 0, CONFIG_SCENEHUB_NODE_FACTORY_RELAY1_GPIO, "Relay 1");
    apply_relay(config, 1, CONFIG_SCENEHUB_NODE_FACTORY_RELAY2_GPIO, "Relay 2");
    apply_relay(config, 2, CONFIG_SCENEHUB_NODE_FACTORY_RELAY3_GPIO, "Relay 3");
    apply_relay(config, 3, CONFIG_SCENEHUB_NODE_FACTORY_RELAY4_GPIO, "Relay 4");
    apply_relay(config, 4, CONFIG_SCENEHUB_NODE_FACTORY_RELAY5_GPIO, "Relay 5");
    apply_relay(config, 5, CONFIG_SCENEHUB_NODE_FACTORY_RELAY6_GPIO, "Relay 6");
    apply_relay(config, 6, CONFIG_SCENEHUB_NODE_FACTORY_RELAY7_GPIO, "Relay 7");
    apply_relay(config, 7, CONFIG_SCENEHUB_NODE_FACTORY_RELAY8_GPIO, "Relay 8");
    apply_mosfet(config, 0, CONFIG_SCENEHUB_NODE_FACTORY_MOSFET1_GPIO, "MOSFET 1");
    apply_mosfet(config, 1, CONFIG_SCENEHUB_NODE_FACTORY_MOSFET2_GPIO, "MOSFET 2");
    apply_mosfet(config, 2, CONFIG_SCENEHUB_NODE_FACTORY_MOSFET3_GPIO, "MOSFET 3");
    apply_mosfet(config, 3, CONFIG_SCENEHUB_NODE_FACTORY_MOSFET4_GPIO, "MOSFET 4");
    apply_mosfet(config, 4, CONFIG_SCENEHUB_NODE_FACTORY_MOSFET5_GPIO, "MOSFET 5");
    apply_mosfet(config, 5, CONFIG_SCENEHUB_NODE_FACTORY_MOSFET6_GPIO, "MOSFET 6");
    apply_mosfet(config, 6, CONFIG_SCENEHUB_NODE_FACTORY_MOSFET7_GPIO, "MOSFET 7");
    apply_mosfet(config, 7, CONFIG_SCENEHUB_NODE_FACTORY_MOSFET8_GPIO, "MOSFET 8");
    apply_io(config, 0, CONFIG_SCENEHUB_NODE_FACTORY_IO1_GPIO, CONFIG_SCENEHUB_NODE_FACTORY_IO1_ROLE, "IO 1");
    apply_io(config, 1, CONFIG_SCENEHUB_NODE_FACTORY_IO2_GPIO, CONFIG_SCENEHUB_NODE_FACTORY_IO2_ROLE, "IO 2");
    apply_io(config, 2, CONFIG_SCENEHUB_NODE_FACTORY_IO3_GPIO, CONFIG_SCENEHUB_NODE_FACTORY_IO3_ROLE, "IO 3");
    apply_io(config, 3, CONFIG_SCENEHUB_NODE_FACTORY_IO4_GPIO, CONFIG_SCENEHUB_NODE_FACTORY_IO4_ROLE, "IO 4");
    apply_io(config, 4, CONFIG_SCENEHUB_NODE_FACTORY_IO5_GPIO, CONFIG_SCENEHUB_NODE_FACTORY_IO5_ROLE, "IO 5");
    apply_io(config, 5, CONFIG_SCENEHUB_NODE_FACTORY_IO6_GPIO, CONFIG_SCENEHUB_NODE_FACTORY_IO6_ROLE, "IO 6");
    apply_io(config, 6, CONFIG_SCENEHUB_NODE_FACTORY_IO7_GPIO, CONFIG_SCENEHUB_NODE_FACTORY_IO7_ROLE, "IO 7");
    apply_io(config, 7, CONFIG_SCENEHUB_NODE_FACTORY_IO8_GPIO, CONFIG_SCENEHUB_NODE_FACTORY_IO8_ROLE, "IO 8");
    apply_led(config, 0, CONFIG_SCENEHUB_NODE_FACTORY_LED1_GPIO, CONFIG_SCENEHUB_NODE_FACTORY_LED1_PIXELS, "LED Strip 1");
    apply_led(config, 1, CONFIG_SCENEHUB_NODE_FACTORY_LED2_GPIO, CONFIG_SCENEHUB_NODE_FACTORY_LED2_PIXELS, "LED Strip 2");
#else
    config->pin_config_locked = false;
#endif
    return ESP_OK;
}

bool node_board_gpio_is_strapping_or_reserved(int gpio)
{
#if CONFIG_IDF_TARGET_ESP32S3
    switch (gpio) {
    case 0:  // boot/config button on many dev boards; allowed only for reset/config by policy.
    case 19: // native USB D-
    case 20: // native USB D+
    case 26: // commonly tied to flash/PSRAM on some modules.
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
        return true;
    default:
        return false;
    }
#else
    return gpio == 0;
#endif
}

bool node_board_gpio_is_allowed(int gpio)
{
#if CONFIG_IDF_TARGET_ESP32S3
    if (gpio < 0 || gpio > 48) {
        return false;
    }
    return !node_board_gpio_is_strapping_or_reserved(gpio);
#else
    if (gpio < 0 || gpio > 39) {
        return false;
    }
    return !node_board_gpio_is_strapping_or_reserved(gpio);
#endif
}

static bool gpio_mark_used(bool used[49], int gpio)
{
    if (!node_board_gpio_is_allowed(gpio)) {
        return false;
    }
    if (gpio >= 0 && gpio < 49 && used[gpio]) {
        return false;
    }
    if (gpio >= 0 && gpio < 49) {
        used[gpio] = true;
    }
    return true;
}

static bool sanitize_output_pins(node_output_pin_config_t *pins, size_t count, bool used[49], int reset_gpio)
{
    bool changed = false;
    for (size_t i = 0; i < count; ++i) {
        node_output_pin_config_t *pin = &pins[i];
        if (!pin->enabled) {
            continue;
        }
        if (pin->gpio == reset_gpio || !gpio_mark_used(used, pin->gpio)) {
            pin->enabled = false;
            pin->gpio = -1;
            changed = true;
        }
    }
    return changed;
}

bool node_board_sanitize_pin_config(node_config_t *config)
{
    if (!config) {
        return false;
    }
    bool changed = false;
    bool used[49] = {0};

    if (!node_board_gpio_is_allowed(config->reset_gpio) && config->reset_gpio != 0) {
        config->reset_gpio = s_profile.default_reset_gpio;
        changed = true;
    }
    if (config->reset_gpio >= 0 && config->reset_gpio < 49) {
        used[config->reset_gpio] = true;
    }

    changed |= sanitize_output_pins(config->relays, NODE_RELAY_MAX, used, config->reset_gpio);
    changed |= sanitize_output_pins(config->mosfets, NODE_MOSFET_MAX, used, config->reset_gpio);

    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        node_universal_pin_config_t *pin = &config->universal_io[i];
        if (!pin->enabled) {
            continue;
        }
        if (pin->role != NODE_PIN_UNIVERSAL_INPUT && pin->role != NODE_PIN_UNIVERSAL_OUTPUT) {
            pin->enabled = false;
            pin->role = NODE_PIN_DISABLED;
            pin->gpio = -1;
            changed = true;
            continue;
        }
        if (pin->gpio == config->reset_gpio || !gpio_mark_used(used, pin->gpio)) {
            pin->enabled = false;
            pin->role = NODE_PIN_DISABLED;
            pin->gpio = -1;
            changed = true;
        }
    }

    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        node_led_strip_config_t *pin = &config->led_strips[i];
        if (!pin->enabled) {
            continue;
        }
        if (pin->gpio == config->reset_gpio || !gpio_mark_used(used, pin->gpio)) {
            pin->enabled = false;
            pin->gpio = -1;
            changed = true;
        }
        if (pin->pixel_count == 0) {
            pin->pixel_count = 30;
            changed = true;
        }
    }

    return changed;
}
