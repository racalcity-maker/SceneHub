#include "node_config_migrations.h"

#include <string.h>

static void copy_led_effect_preset_v7(node_led_effect_preset_t *dst, const node_led_effect_preset_v7_t *src)
{
    if (!dst || !src) {
        return;
    }
    dst->duration_ms = src->duration_ms;
    dst->step_ms = src->step_ms;
    dst->repeat_count = src->repeat_count;
    dst->size = 1;
    dst->intensity = 255;
    dst->density = 0;
    dst->fade = 0;
    dst->palette_mode = NODE_LED_PALETTE_NONE;
    dst->red = src->red;
    dst->green = src->green;
    dst->blue = src->blue;
    dst->white = src->white;
    dst->red2 = src->red2;
    dst->green2 = src->green2;
    dst->blue2 = src->blue2;
    dst->white2 = src->white2;
    dst->bg_red = 0;
    dst->bg_green = 0;
    dst->bg_blue = 0;
    dst->bg_white = 0;
}

void node_config_migrate_v1(const node_config_v1_t *legacy, node_config_t *config)
{
    if (!legacy || !config) {
        return;
    }

    node_config_set_factory_defaults(config);
    config->version = g_node_config_version;
    memcpy(config->node_id, legacy->node_id, sizeof(config->node_id));
    memcpy(config->node_name, legacy->node_name, sizeof(config->node_name));
    memcpy(config->wifi_ssid, legacy->wifi_ssid, sizeof(config->wifi_ssid));
    memcpy(config->wifi_password, legacy->wifi_password, sizeof(config->wifi_password));
    memcpy(config->controller_host, legacy->controller_host, sizeof(config->controller_host));
    config->mqtt_port = legacy->mqtt_port;
    memcpy(config->mqtt_client_id, legacy->mqtt_client_id, sizeof(config->mqtt_client_id));
    config->reset_gpio = legacy->reset_gpio;
    config->pin_config_locked = legacy->pin_config_locked;
    for (uint8_t i = 0; i < NODE_RELAY_MAX; ++i) {
        config->relays[i].enabled = legacy->relays[i].enabled;
        config->relays[i].channel = legacy->relays[i].channel;
        config->relays[i].gpio = legacy->relays[i].gpio;
        config->relays[i].active_low = legacy->relays[i].active_low;
        memcpy(config->relays[i].label, legacy->relays[i].label, sizeof(config->relays[i].label));
    }
    for (uint8_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        config->mosfets[i].enabled = legacy->mosfets[i].enabled;
        config->mosfets[i].channel = legacy->mosfets[i].channel;
        config->mosfets[i].gpio = legacy->mosfets[i].gpio;
        config->mosfets[i].active_low = legacy->mosfets[i].active_low;
        memcpy(config->mosfets[i].label, legacy->mosfets[i].label, sizeof(config->mosfets[i].label));
    }
    memcpy(config->universal_io, legacy->universal_io, sizeof(config->universal_io));
    for (uint8_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        config->led_strips[i].enabled = legacy->led_strips[i].enabled;
        config->led_strips[i].channel = legacy->led_strips[i].channel;
        config->led_strips[i].gpio = legacy->led_strips[i].gpio;
        config->led_strips[i].pixel_count = legacy->led_strips[i].pixel_count;
        config->led_strips[i].chipset = NODE_LED_CHIPSET_WS2812;
        config->led_strips[i].color_order = NODE_LED_COLOR_ORDER_GRB;
        config->led_strips[i].rgbw = false;
        set_led_strip_runtime_defaults(&config->led_strips[i]);
        memcpy(config->led_strips[i].label, legacy->led_strips[i].label, sizeof(config->led_strips[i].label));
    }
}

void node_config_migrate_v2(const node_config_v2_t *legacy, node_config_t *config)
{
    if (!legacy || !config) {
        return;
    }

    node_config_set_factory_defaults(config);
    config->version = g_node_config_version;
    memcpy(config->node_id, legacy->node_id, sizeof(config->node_id));
    memcpy(config->node_name, legacy->node_name, sizeof(config->node_name));
    memcpy(config->wifi_ssid, legacy->wifi_ssid, sizeof(config->wifi_ssid));
    memcpy(config->wifi_password, legacy->wifi_password, sizeof(config->wifi_password));
    memcpy(config->controller_host, legacy->controller_host, sizeof(config->controller_host));
    config->mqtt_port = legacy->mqtt_port;
    memcpy(config->mqtt_client_id, legacy->mqtt_client_id, sizeof(config->mqtt_client_id));
    config->reset_gpio = legacy->reset_gpio;
    config->pin_config_locked = legacy->pin_config_locked;
    for (uint8_t i = 0; i < NODE_RELAY_MAX; ++i) {
        config->relays[i].enabled = legacy->relays[i].enabled;
        config->relays[i].channel = legacy->relays[i].channel;
        config->relays[i].gpio = legacy->relays[i].gpio;
        config->relays[i].active_low = legacy->relays[i].active_low;
        memcpy(config->relays[i].label, legacy->relays[i].label, sizeof(config->relays[i].label));
    }
    for (uint8_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        config->mosfets[i].enabled = legacy->mosfets[i].enabled;
        config->mosfets[i].channel = legacy->mosfets[i].channel;
        config->mosfets[i].gpio = legacy->mosfets[i].gpio;
        config->mosfets[i].active_low = legacy->mosfets[i].active_low;
        memcpy(config->mosfets[i].label, legacy->mosfets[i].label, sizeof(config->mosfets[i].label));
    }
    memcpy(config->universal_io, legacy->universal_io, sizeof(config->universal_io));
    for (uint8_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        config->led_strips[i].enabled = legacy->led_strips[i].enabled;
        config->led_strips[i].channel = legacy->led_strips[i].channel;
        config->led_strips[i].gpio = legacy->led_strips[i].gpio;
        config->led_strips[i].pixel_count = legacy->led_strips[i].pixel_count;
        config->led_strips[i].chipset = legacy->led_strips[i].chipset;
        config->led_strips[i].color_order = legacy->led_strips[i].color_order;
        config->led_strips[i].rgbw = legacy->led_strips[i].rgbw;
        set_led_strip_runtime_defaults(&config->led_strips[i]);
        memcpy(config->led_strips[i].label, legacy->led_strips[i].label, sizeof(config->led_strips[i].label));
    }
}

void node_config_migrate_v3(const node_config_v3_t *legacy, node_config_t *config)
{
    if (!legacy || !config) {
        return;
    }

    node_config_set_factory_defaults(config);
    config->version = g_node_config_version;
    memcpy(config->node_id, legacy->node_id, sizeof(config->node_id));
    memcpy(config->node_name, legacy->node_name, sizeof(config->node_name));
    memcpy(config->wifi_ssid, legacy->wifi_ssid, sizeof(config->wifi_ssid));
    memcpy(config->wifi_password, legacy->wifi_password, sizeof(config->wifi_password));
    memcpy(config->controller_host, legacy->controller_host, sizeof(config->controller_host));
    config->mqtt_port = legacy->mqtt_port;
    memcpy(config->mqtt_client_id, legacy->mqtt_client_id, sizeof(config->mqtt_client_id));
    config->reset_gpio = legacy->reset_gpio;
    config->pin_config_locked = legacy->pin_config_locked;
    for (uint8_t i = 0; i < NODE_RELAY_MAX; ++i) {
        config->relays[i].enabled = legacy->relays[i].enabled;
        config->relays[i].channel = legacy->relays[i].channel;
        config->relays[i].gpio = legacy->relays[i].gpio;
        config->relays[i].active_low = legacy->relays[i].active_low;
        memcpy(config->relays[i].label, legacy->relays[i].label, sizeof(config->relays[i].label));
    }
    for (uint8_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        config->mosfets[i].enabled = legacy->mosfets[i].enabled;
        config->mosfets[i].channel = legacy->mosfets[i].channel;
        config->mosfets[i].gpio = legacy->mosfets[i].gpio;
        config->mosfets[i].active_low = legacy->mosfets[i].active_low;
        memcpy(config->mosfets[i].label, legacy->mosfets[i].label, sizeof(config->mosfets[i].label));
    }
    memcpy(config->universal_io, legacy->universal_io, sizeof(config->universal_io));
    for (uint8_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        config->led_strips[i].enabled = legacy->led_strips[i].enabled;
        config->led_strips[i].channel = legacy->led_strips[i].channel;
        config->led_strips[i].gpio = legacy->led_strips[i].gpio;
        config->led_strips[i].pixel_count = legacy->led_strips[i].pixel_count;
        config->led_strips[i].chipset = legacy->led_strips[i].chipset;
        config->led_strips[i].color_order = legacy->led_strips[i].color_order;
        config->led_strips[i].rgbw = legacy->led_strips[i].rgbw;
        config->led_strips[i].blink.on_ms = 500;
        config->led_strips[i].blink.off_ms = 500;
        config->led_strips[i].blink.repeat_count = 0;
        config->led_strips[i].breathe.cycle_ms = legacy->led_strips[i].effect_duration_ms ? legacy->led_strips[i].effect_duration_ms : 2000U;
        config->led_strips[i].breathe.step_ms = legacy->led_strips[i].effect_step_ms ? legacy->led_strips[i].effect_step_ms : 40U;
        config->led_strips[i].breathe.repeat_count = legacy->led_strips[i].effect_repeat_count ? legacy->led_strips[i].effect_repeat_count : 0U;
        set_led_effect_presets_from_values(&config->led_strips[i].effects,
                                           legacy->led_strips[i].effect_duration_ms ? legacy->led_strips[i].effect_duration_ms : 250U,
                                           legacy->led_strips[i].effect_step_ms ? legacy->led_strips[i].effect_step_ms : 50U,
                                           legacy->led_strips[i].effect_repeat_count ? legacy->led_strips[i].effect_repeat_count : 0U,
                                           255, 255, 255, 0, 0, 0, 0, 0);
        migrate_strip_effect_aux_colors(&config->led_strips[i]);
        memcpy(config->led_strips[i].label, legacy->led_strips[i].label, sizeof(config->led_strips[i].label));
    }
}

void node_config_migrate_v4(const node_config_v4_t *legacy, node_config_t *config)
{
    if (!legacy || !config) {
        return;
    }

    node_config_set_factory_defaults(config);
    config->version = g_node_config_version;
    memcpy(config->node_id, legacy->node_id, sizeof(config->node_id));
    memcpy(config->node_name, legacy->node_name, sizeof(config->node_name));
    memcpy(config->wifi_ssid, legacy->wifi_ssid, sizeof(config->wifi_ssid));
    memcpy(config->wifi_password, legacy->wifi_password, sizeof(config->wifi_password));
    memcpy(config->controller_host, legacy->controller_host, sizeof(config->controller_host));
    config->mqtt_port = legacy->mqtt_port;
    memcpy(config->mqtt_client_id, legacy->mqtt_client_id, sizeof(config->mqtt_client_id));
    config->reset_gpio = legacy->reset_gpio;
    config->pin_config_locked = legacy->pin_config_locked;
    for (uint8_t i = 0; i < NODE_RELAY_MAX; ++i) {
        config->relays[i].enabled = legacy->relays[i].enabled;
        config->relays[i].channel = legacy->relays[i].channel;
        config->relays[i].gpio = legacy->relays[i].gpio;
        config->relays[i].active_low = legacy->relays[i].active_low;
        memcpy(config->relays[i].label, legacy->relays[i].label, sizeof(config->relays[i].label));
    }
    for (uint8_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        config->mosfets[i].enabled = legacy->mosfets[i].enabled;
        config->mosfets[i].channel = legacy->mosfets[i].channel;
        config->mosfets[i].gpio = legacy->mosfets[i].gpio;
        config->mosfets[i].active_low = legacy->mosfets[i].active_low;
        memcpy(config->mosfets[i].label, legacy->mosfets[i].label, sizeof(config->mosfets[i].label));
    }
    memcpy(config->universal_io, legacy->universal_io, sizeof(config->universal_io));
    for (uint8_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        config->led_strips[i].enabled = legacy->led_strips[i].enabled;
        config->led_strips[i].channel = legacy->led_strips[i].channel;
        config->led_strips[i].gpio = legacy->led_strips[i].gpio;
        config->led_strips[i].pixel_count = legacy->led_strips[i].pixel_count;
        config->led_strips[i].chipset = legacy->led_strips[i].chipset;
        config->led_strips[i].color_order = legacy->led_strips[i].color_order;
        config->led_strips[i].rgbw = legacy->led_strips[i].rgbw;
        config->led_strips[i].blink.on_ms = 500;
        config->led_strips[i].blink.off_ms = 500;
        config->led_strips[i].blink.repeat_count = 0;
        config->led_strips[i].breathe.cycle_ms = legacy->led_strips[i].effect_duration_ms ? legacy->led_strips[i].effect_duration_ms : 2000U;
        config->led_strips[i].breathe.step_ms = legacy->led_strips[i].effect_step_ms ? legacy->led_strips[i].effect_step_ms : 40U;
        config->led_strips[i].breathe.repeat_count = legacy->led_strips[i].effect_repeat_count ? legacy->led_strips[i].effect_repeat_count : 0U;
        set_led_effect_presets_from_values(&config->led_strips[i].effects,
                                           legacy->led_strips[i].effect_duration_ms ? legacy->led_strips[i].effect_duration_ms : 250U,
                                           legacy->led_strips[i].effect_step_ms ? legacy->led_strips[i].effect_step_ms : 50U,
                                           legacy->led_strips[i].effect_repeat_count ? legacy->led_strips[i].effect_repeat_count : 0U,
                                           255, 255, 255, 0, 0, 0, 0, 0);
        migrate_strip_effect_aux_colors(&config->led_strips[i]);
        memcpy(config->led_strips[i].label, legacy->led_strips[i].label, sizeof(config->led_strips[i].label));
    }
}

void node_config_migrate_v5(const node_config_v5_t *legacy, node_config_t *config)
{
    if (!legacy || !config) {
        return;
    }

    node_config_set_factory_defaults(config);
    config->version = g_node_config_version;
    memcpy(config->node_id, legacy->node_id, sizeof(config->node_id));
    memcpy(config->node_name, legacy->node_name, sizeof(config->node_name));
    memcpy(config->wifi_ssid, legacy->wifi_ssid, sizeof(config->wifi_ssid));
    memcpy(config->wifi_password, legacy->wifi_password, sizeof(config->wifi_password));
    memcpy(config->controller_host, legacy->controller_host, sizeof(config->controller_host));
    config->mqtt_port = legacy->mqtt_port;
    memcpy(config->mqtt_client_id, legacy->mqtt_client_id, sizeof(config->mqtt_client_id));
    config->reset_gpio = legacy->reset_gpio;
    config->pin_config_locked = legacy->pin_config_locked;
    for (uint8_t i = 0; i < NODE_RELAY_MAX; ++i) {
        config->relays[i].enabled = legacy->relays[i].enabled;
        config->relays[i].channel = legacy->relays[i].channel;
        config->relays[i].gpio = legacy->relays[i].gpio;
        config->relays[i].active_low = legacy->relays[i].active_low;
        memcpy(config->relays[i].label, legacy->relays[i].label, sizeof(config->relays[i].label));
    }
    for (uint8_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        config->mosfets[i].enabled = legacy->mosfets[i].enabled;
        config->mosfets[i].channel = legacy->mosfets[i].channel;
        config->mosfets[i].gpio = legacy->mosfets[i].gpio;
        config->mosfets[i].active_low = legacy->mosfets[i].active_low;
        memcpy(config->mosfets[i].label, legacy->mosfets[i].label, sizeof(config->mosfets[i].label));
    }
    memcpy(config->universal_io, legacy->universal_io, sizeof(config->universal_io));
    for (uint8_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        config->led_strips[i].enabled = legacy->led_strips[i].enabled;
        config->led_strips[i].channel = legacy->led_strips[i].channel;
        config->led_strips[i].gpio = legacy->led_strips[i].gpio;
        config->led_strips[i].pixel_count = legacy->led_strips[i].pixel_count;
        config->led_strips[i].chipset = legacy->led_strips[i].chipset;
        config->led_strips[i].color_order = legacy->led_strips[i].color_order;
        config->led_strips[i].rgbw = legacy->led_strips[i].rgbw;
        config->led_strips[i].blink.on_ms = legacy->led_strips[i].blink_on_ms ? legacy->led_strips[i].blink_on_ms : 500U;
        config->led_strips[i].blink.off_ms = legacy->led_strips[i].blink_off_ms ? legacy->led_strips[i].blink_off_ms : 500U;
        config->led_strips[i].blink.repeat_count = legacy->led_strips[i].blink_repeat_count ? legacy->led_strips[i].blink_repeat_count : 0U;
        config->led_strips[i].breathe.cycle_ms = legacy->led_strips[i].breathe_cycle_ms ? legacy->led_strips[i].breathe_cycle_ms : 2000U;
        config->led_strips[i].breathe.step_ms = legacy->led_strips[i].breathe_step_ms ? legacy->led_strips[i].breathe_step_ms : 40U;
        config->led_strips[i].breathe.repeat_count = legacy->led_strips[i].breathe_repeat_count ? legacy->led_strips[i].breathe_repeat_count : 0U;
        set_led_effect_presets_from_values(&config->led_strips[i].effects,
                                           legacy->led_strips[i].effect_duration_ms ? legacy->led_strips[i].effect_duration_ms : 250U,
                                           legacy->led_strips[i].effect_step_ms ? legacy->led_strips[i].effect_step_ms : 50U,
                                           legacy->led_strips[i].effect_repeat_count ? legacy->led_strips[i].effect_repeat_count : 0U,
                                           legacy->led_strips[i].effect_red,
                                           legacy->led_strips[i].effect_green,
                                           legacy->led_strips[i].effect_blue,
                                           legacy->led_strips[i].effect_white,
                                           legacy->led_strips[i].effect_red2,
                                           legacy->led_strips[i].effect_green2,
                                           legacy->led_strips[i].effect_blue2,
                                           legacy->led_strips[i].effect_white2);
        migrate_strip_effect_aux_colors(&config->led_strips[i]);
        memcpy(config->led_strips[i].label, legacy->led_strips[i].label, sizeof(config->led_strips[i].label));
    }
}

void node_config_migrate_v6(const node_config_v6_t *legacy, node_config_t *config)
{
    if (!legacy || !config) {
        return;
    }

    node_config_set_factory_defaults(config);
    config->version = g_node_config_version;
    memcpy(config->node_id, legacy->node_id, sizeof(config->node_id));
    memcpy(config->node_name, legacy->node_name, sizeof(config->node_name));
    memcpy(config->wifi_ssid, legacy->wifi_ssid, sizeof(config->wifi_ssid));
    memcpy(config->wifi_password, legacy->wifi_password, sizeof(config->wifi_password));
    memcpy(config->controller_host, legacy->controller_host, sizeof(config->controller_host));
    config->mqtt_port = legacy->mqtt_port;
    memcpy(config->mqtt_client_id, legacy->mqtt_client_id, sizeof(config->mqtt_client_id));
    config->reset_gpio = legacy->reset_gpio;
    config->pin_config_locked = legacy->pin_config_locked;
    for (uint8_t i = 0; i < NODE_RELAY_MAX; ++i) {
        config->relays[i].enabled = legacy->relays[i].enabled;
        config->relays[i].channel = legacy->relays[i].channel;
        config->relays[i].gpio = legacy->relays[i].gpio;
        config->relays[i].active_low = legacy->relays[i].active_low;
        memcpy(config->relays[i].label, legacy->relays[i].label, sizeof(config->relays[i].label));
    }
    for (uint8_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        config->mosfets[i].enabled = legacy->mosfets[i].enabled;
        config->mosfets[i].channel = legacy->mosfets[i].channel;
        config->mosfets[i].gpio = legacy->mosfets[i].gpio;
        config->mosfets[i].active_low = legacy->mosfets[i].active_low;
        memcpy(config->mosfets[i].label, legacy->mosfets[i].label, sizeof(config->mosfets[i].label));
    }
    memcpy(config->universal_io, legacy->universal_io, sizeof(config->universal_io));
    for (uint8_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        config->led_strips[i].enabled = legacy->led_strips[i].enabled;
        config->led_strips[i].channel = legacy->led_strips[i].channel;
        config->led_strips[i].gpio = legacy->led_strips[i].gpio;
        config->led_strips[i].pixel_count = legacy->led_strips[i].pixel_count;
        config->led_strips[i].chipset = legacy->led_strips[i].chipset;
        config->led_strips[i].color_order = legacy->led_strips[i].color_order;
        config->led_strips[i].rgbw = legacy->led_strips[i].rgbw;
        config->led_strips[i].blink.on_ms = legacy->led_strips[i].blink.on_ms ? legacy->led_strips[i].blink.on_ms : 500U;
        config->led_strips[i].blink.off_ms = legacy->led_strips[i].blink.off_ms ? legacy->led_strips[i].blink.off_ms : 500U;
        config->led_strips[i].blink.repeat_count = legacy->led_strips[i].blink.repeat_count;
        config->led_strips[i].blink.red = 255;
        config->led_strips[i].blink.green = 255;
        config->led_strips[i].blink.blue = 255;
        config->led_strips[i].blink.white = 0;
        config->led_strips[i].breathe.cycle_ms = legacy->led_strips[i].breathe.cycle_ms ? legacy->led_strips[i].breathe.cycle_ms : 2000U;
        config->led_strips[i].breathe.step_ms = legacy->led_strips[i].breathe.step_ms ? legacy->led_strips[i].breathe.step_ms : 40U;
        config->led_strips[i].breathe.repeat_count = legacy->led_strips[i].breathe.repeat_count;
        config->led_strips[i].breathe.red = 255;
        config->led_strips[i].breathe.green = 255;
        config->led_strips[i].breathe.blue = 255;
        config->led_strips[i].breathe.white = 0;
        set_led_effects_defaults(&config->led_strips[i].effects);
        copy_led_effect_preset_v7(&config->led_strips[i].effects.items[NODE_LED_EFFECT_RAINBOW], &legacy->led_strips[i].effects.rainbow);
        copy_led_effect_preset_v7(&config->led_strips[i].effects.items[NODE_LED_EFFECT_COLOR_WIPE], &legacy->led_strips[i].effects.color_wipe);
        copy_led_effect_preset_v7(&config->led_strips[i].effects.items[NODE_LED_EFFECT_SCANNER], &legacy->led_strips[i].effects.scanner);
        copy_led_effect_preset_v7(&config->led_strips[i].effects.items[NODE_LED_EFFECT_THEATER_CHASE], &legacy->led_strips[i].effects.theater);
        copy_led_effect_preset_v7(&config->led_strips[i].effects.items[NODE_LED_EFFECT_STROBE], &legacy->led_strips[i].effects.strobe);
        migrate_strip_effect_aux_colors(&config->led_strips[i]);
        memcpy(config->led_strips[i].label, legacy->led_strips[i].label, sizeof(config->led_strips[i].label));
    }
}

void node_config_migrate_v7(const node_config_v7_t *legacy, node_config_t *config)
{
    if (!legacy || !config) {
        return;
    }

    node_config_set_factory_defaults(config);
    config->version = g_node_config_version;
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
        config->led_strips[i].chipset = legacy->led_strips[i].chipset;
        config->led_strips[i].color_order = legacy->led_strips[i].color_order;
        config->led_strips[i].rgbw = legacy->led_strips[i].rgbw;
        config->led_strips[i].blink = legacy->led_strips[i].blink;
        config->led_strips[i].breathe = legacy->led_strips[i].breathe;
        set_led_effects_defaults(&config->led_strips[i].effects);
        copy_led_effect_preset_v7(&config->led_strips[i].effects.items[NODE_LED_EFFECT_RAINBOW], &legacy->led_strips[i].effects.rainbow);
        copy_led_effect_preset_v7(&config->led_strips[i].effects.items[NODE_LED_EFFECT_COLOR_WIPE], &legacy->led_strips[i].effects.color_wipe);
        copy_led_effect_preset_v7(&config->led_strips[i].effects.items[NODE_LED_EFFECT_SCANNER], &legacy->led_strips[i].effects.scanner);
        copy_led_effect_preset_v7(&config->led_strips[i].effects.items[NODE_LED_EFFECT_THEATER_CHASE], &legacy->led_strips[i].effects.theater);
        copy_led_effect_preset_v7(&config->led_strips[i].effects.items[NODE_LED_EFFECT_STROBE], &legacy->led_strips[i].effects.strobe);
        migrate_strip_effect_aux_colors(&config->led_strips[i]);
        memcpy(config->led_strips[i].label, legacy->led_strips[i].label, sizeof(config->led_strips[i].label));
    }
}

void node_config_migrate_v8(const node_config_v8_t *legacy, node_config_t *config)
{
    if (!legacy || !config) {
        return;
    }

    node_config_set_factory_defaults(config);
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
        config->led_strips[i].chipset = legacy->led_strips[i].chipset;
        config->led_strips[i].color_order = legacy->led_strips[i].color_order;
        config->led_strips[i].rgbw = legacy->led_strips[i].rgbw;
        config->led_strips[i].blink = legacy->led_strips[i].blink;
        config->led_strips[i].breathe = legacy->led_strips[i].breathe;
        memcpy(config->led_strips[i].label, legacy->led_strips[i].label, sizeof(config->led_strips[i].label));
        for (size_t effect_index = 0; effect_index < NODE_LED_EFFECT_COUNT; ++effect_index) {
            const node_led_effect_preset_v8_t *src = &legacy->led_strips[i].effects.items[effect_index];
            node_led_effect_preset_t *dst = &config->led_strips[i].effects.items[effect_index];
            dst->duration_ms = src->duration_ms;
            dst->step_ms = src->step_ms;
            dst->repeat_count = src->repeat_count;
            dst->size = src->size;
            dst->intensity = src->intensity;
            dst->density = src->density;
            dst->fade = src->fade;
            dst->palette_mode = src->palette_mode;
            dst->red = src->red;
            dst->green = src->green;
            dst->blue = src->blue;
            dst->white = src->white;
            dst->red2 = src->red2;
            dst->green2 = src->green2;
            dst->blue2 = src->blue2;
            dst->white2 = src->white2;
            migrate_effect_aux_colors((node_led_effect_id_t)effect_index, dst);
        }
    }
    config->version = g_node_config_version;
}
