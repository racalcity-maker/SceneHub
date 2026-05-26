#include "node_config_internal.h"

#include <stdio.h>
#include <string.h>

void node_config_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    snprintf(dst, dst_size, "%s", src);
}

void migrate_effect_aux_colors(node_led_effect_id_t id, node_led_effect_preset_t *preset)
{
    const node_led_effect_descriptor_t *desc = NULL;

    if (!preset) {
        return;
    }

    desc = node_led_effect_descriptor(id);
    if (!desc) {
        return;
    }

    if ((desc->controls & NODE_LED_CTRL_BACKGROUND_COLOR) != 0 &&
        (desc->controls & NODE_LED_CTRL_SECONDARY_COLOR) == 0) {
        preset->bg_red = preset->red2;
        preset->bg_green = preset->green2;
        preset->bg_blue = preset->blue2;
        preset->bg_white = preset->white2;
        preset->red2 = 0;
        preset->green2 = 0;
        preset->blue2 = 0;
        preset->white2 = 0;
        return;
    }

    if ((desc->controls & NODE_LED_CTRL_SECONDARY_COLOR) == 0) {
        preset->red2 = 0;
        preset->green2 = 0;
        preset->blue2 = 0;
        preset->white2 = 0;
    }
}

void migrate_strip_effect_aux_colors(node_led_strip_config_t *strip)
{
    if (!strip) {
        return;
    }
    for (size_t i = 0; i < NODE_LED_EFFECT_COUNT; ++i) {
        migrate_effect_aux_colors((node_led_effect_id_t)i, &strip->effects.items[i]);
    }
}

static void set_led_effect_preset_defaults(node_led_effect_preset_t *preset,
                                           uint32_t duration_ms,
                                           uint32_t step_ms,
                                           uint16_t repeat_count,
                                           uint16_t size,
                                           uint16_t intensity,
                                           uint16_t density,
                                           uint16_t fade,
                                           node_led_palette_mode_t palette_mode,
                                           uint8_t red,
                                           uint8_t green,
                                           uint8_t blue,
                                           uint8_t white,
                                           uint8_t red2,
                                           uint8_t green2,
                                           uint8_t blue2,
                                           uint8_t white2,
                                           uint8_t bg_red,
                                           uint8_t bg_green,
                                           uint8_t bg_blue,
                                           uint8_t bg_white)
{
    if (!preset) {
        return;
    }
    preset->duration_ms = duration_ms;
    preset->step_ms = step_ms;
    preset->repeat_count = repeat_count;
    preset->size = size;
    preset->intensity = intensity;
    preset->density = density;
    preset->fade = fade;
    preset->palette_mode = palette_mode;
    preset->red = red;
    preset->green = green;
    preset->blue = blue;
    preset->white = white;
    preset->red2 = red2;
    preset->green2 = green2;
    preset->blue2 = blue2;
    preset->white2 = white2;
    preset->bg_red = bg_red;
    preset->bg_green = bg_green;
    preset->bg_blue = bg_blue;
    preset->bg_white = bg_white;
}

void set_led_strip_runtime_defaults(node_led_strip_config_t *strip)
{
    if (!strip) {
        return;
    }

    strip->blink.on_ms = 500;
    strip->blink.off_ms = 500;
    strip->blink.repeat_count = 0;
    strip->blink.red = 255;
    strip->blink.green = 255;
    strip->blink.blue = 255;
    strip->blink.white = 0;

    strip->breathe.cycle_ms = 2000;
    strip->breathe.step_ms = 40;
    strip->breathe.repeat_count = 0;
    strip->breathe.red = 255;
    strip->breathe.green = 255;
    strip->breathe.blue = 255;
    strip->breathe.white = 0;

    set_led_effects_defaults(&strip->effects);
}

static void copy_led_effect_preset(node_led_effect_preset_t *dst, const node_led_effect_preset_t *src)
{
    if (!dst || !src) {
        return;
    }
    *dst = *src;
}

static void copy_led_effect_preset_to_all(node_led_effect_presets_t *effects, const node_led_effect_preset_t *preset)
{
    if (!effects || !preset) {
        return;
    }
    for (size_t i = 0; i < NODE_LED_EFFECT_COUNT; ++i) {
        copy_led_effect_preset(&effects->items[i], preset);
    }
}

static void set_led_effect_defaults(node_led_effect_id_t id, node_led_effect_preset_t *preset)
{
    const node_led_effect_defaults_t *defaults = node_led_effect_defaults(id);

    if (!preset) {
        return;
    }

    if (defaults) {
        set_led_effect_preset_defaults(preset,
                                       defaults->duration_ms,
                                       defaults->step_ms,
                                       defaults->repeat_count,
                                       defaults->size,
                                       defaults->intensity,
                                       defaults->density,
                                       defaults->fade,
                                       defaults->palette_mode,
                                       defaults->red,
                                       defaults->green,
                                       defaults->blue,
                                       defaults->white,
                                       defaults->red2,
                                       defaults->green2,
                                       defaults->blue2,
                                       defaults->white2,
                                       defaults->bg_red,
                                       defaults->bg_green,
                                       defaults->bg_blue,
                                       defaults->bg_white);
        return;
    }

    set_led_effect_preset_defaults(preset,
                                   0,
                                   50,
                                   0,
                                   1,
                                   255,
                                   0,
                                   0,
                                   NODE_LED_PALETTE_NONE,
                                   255,
                                   255,
                                   255,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0);
}

void set_led_effects_defaults(node_led_effect_presets_t *effects)
{
    if (!effects) {
        return;
    }
    for (size_t i = 0; i < NODE_LED_EFFECT_COUNT; ++i) {
        set_led_effect_defaults((node_led_effect_id_t)i, &effects->items[i]);
    }
}

void set_led_effect_presets_from_values(node_led_effect_presets_t *effects,
                                        uint32_t duration_ms,
                                        uint32_t step_ms,
                                        uint16_t repeat_count,
                                        uint8_t red,
                                        uint8_t green,
                                        uint8_t blue,
                                        uint8_t white,
                                        uint8_t red2,
                                        uint8_t green2,
                                        uint8_t blue2,
                                        uint8_t white2)
{
    node_led_effect_preset_t preset = {0};

    if (!effects) {
        return;
    }

    set_led_effect_preset_defaults(&preset,
                                   duration_ms,
                                   step_ms,
                                   repeat_count,
                                   1,
                                   255,
                                   0,
                                   0,
                                   NODE_LED_PALETTE_NONE,
                                   red,
                                   green,
                                   blue,
                                   white,
                                   red2,
                                   green2,
                                   blue2,
                                   white2,
                                   0,
                                   0,
                                   0,
                                   0);
    copy_led_effect_preset_to_all(effects, &preset);
}

void node_config_set_factory_defaults(node_config_t *config)
{
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->version = g_node_config_version;
    node_config_copy_text(config->node_id, sizeof(config->node_id), "scenehub_node_s3");
    node_config_copy_text(config->node_name, sizeof(config->node_name), "SceneHub Node S3");
    node_config_copy_text(config->mqtt_client_id, sizeof(config->mqtt_client_id), "dcc-scenehub-node-s3");
    config->mqtt_port = 1883;
    config->reset_gpio = 0;

    for (uint8_t i = 0; i < NODE_RELAY_MAX; ++i) {
        config->relays[i].channel = i + 1;
        config->relays[i].gpio = -1;
        config->relays[i].pulse_duration_ms = 300;
        config->relays[i].fade_duration_ms = 500;
        config->relays[i].blink_on_ms = 250;
        config->relays[i].blink_off_ms = 250;
        config->relays[i].blink_repeat_count = 3;
        config->relays[i].breathe_fade_ms = 1000;
        config->relays[i].breathe_hold_ms = 0;
        config->relays[i].breathe_repeat_count = 1;
        config->relays[i].default_value = 255;
        config->relays[i].default_target = 255;
        config->relays[i].default_min = 0;
        config->relays[i].default_max = 255;
        config->relays[i].default_final_value = 0;
        snprintf(config->relays[i].label, sizeof(config->relays[i].label), "Relay %u", (unsigned)(i + 1));
    }
    for (uint8_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        config->mosfets[i].channel = i + 1;
        config->mosfets[i].gpio = -1;
        config->mosfets[i].pulse_duration_ms = 300;
        config->mosfets[i].fade_duration_ms = 500;
        config->mosfets[i].blink_on_ms = 250;
        config->mosfets[i].blink_off_ms = 250;
        config->mosfets[i].blink_repeat_count = 3;
        config->mosfets[i].breathe_fade_ms = 1000;
        config->mosfets[i].breathe_hold_ms = 0;
        config->mosfets[i].breathe_repeat_count = 1;
        config->mosfets[i].default_value = 255;
        config->mosfets[i].default_target = 255;
        config->mosfets[i].default_min = 0;
        config->mosfets[i].default_max = 255;
        config->mosfets[i].default_final_value = 0;
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
        set_led_strip_runtime_defaults(&config->led_strips[i]);
        snprintf(config->led_strips[i].label, sizeof(config->led_strips[i].label), "LED Strip %u", (unsigned)(i + 1));
    }
}
