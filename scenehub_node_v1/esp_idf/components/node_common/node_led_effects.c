#include "node_led_effects.h"

#include <stddef.h>
#include <string.h>

#define EFFECT_DEFAULTS(duration_ms_, step_ms_, repeat_count_, size_, intensity_, density_, fade_, palette_mode_, red_, green_, blue_, white_, red2_, green2_, blue2_, white2_, bg_red_, bg_green_, bg_blue_, bg_white_) \
    {                                                                                                                                                                                          \
        .duration_ms = (duration_ms_),                                                                                                                                                         \
        .step_ms = (step_ms_),                                                                                                                                                                 \
        .repeat_count = (repeat_count_),                                                                                                                                                       \
        .size = (size_),                                                                                                                                                                       \
        .intensity = (intensity_),                                                                                                                                                             \
        .density = (density_),                                                                                                                                                                 \
        .fade = (fade_),                                                                                                                                                                       \
        .palette_mode = (palette_mode_),                                                                                                                                                       \
        .red = (red_),                                                                                                                                                                         \
        .green = (green_),                                                                                                                                                                     \
        .blue = (blue_),                                                                                                                                                                       \
        .white = (white_),                                                                                                                                                                     \
        .red2 = (red2_),                                                                                                                                                                       \
        .green2 = (green2_),                                                                                                                                                                   \
        .blue2 = (blue2_),                                                                                                                                                                     \
        .white2 = (white2_),                                                                                                                                                                   \
        .bg_red = (bg_red_),                                                                                                                                                                   \
        .bg_green = (bg_green_),                                                                                                                                                               \
        .bg_blue = (bg_blue_),                                                                                                                                                                 \
        .bg_white = (bg_white_),                                                                                                                                                               \
    }

static const char *s_led_effect_groups[] = {
    "Palette",
    "Flash",
    "Motion",
    "Chase",
    "Fade",
};

static const node_led_effect_descriptor_t s_led_effects[] = {
    {NODE_LED_EFFECT_BLINK, "blink", "Blink", "Flash", "Hub sends color and optional times. The node keeps timing defaults here.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_FLASH_ON | NODE_LED_CTRL_FLASH_OFF | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(500, 500, 0, 1, 255, 0, 0, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_BREATHE, "breathe", "Breathe", "Fade", "Hub sends color. The node keeps the motion defaults here.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(2000, 40, 0, 8, 180, 0, 160, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_RAINBOW, "rainbow", "Rainbow", "Palette", "Palette motion. The strip colors come from the selected palette.", NODE_LED_CTRL_SPEED | NODE_LED_CTRL_INTENSITY | NODE_LED_CTRL_REPEAT_COUNT | NODE_LED_CTRL_PALETTE_MODE, EFFECT_DEFAULTS(0, 50, 0, 0, 128, 0, 0, NODE_LED_PALETTE_RAINBOW, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_RAINBOW_CYCLE, "rainbow_cycle", "Rainbow Cycle", "Palette", "Full-strip hue cycle with stronger color travel.", NODE_LED_CTRL_SPEED | NODE_LED_CTRL_INTENSITY | NODE_LED_CTRL_REPEAT_COUNT | NODE_LED_CTRL_PALETTE_MODE, EFFECT_DEFAULTS(0, 40, 0, 0, 160, 0, 0, NODE_LED_PALETTE_RAINBOW, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_COLOR_WIPE, "color_wipe", "Color Wipe", "Chase", "Single color fills the strip segment by segment.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_SIZE | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 30, 0, 1, 255, 0, 0, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_SCANNER, "scanner", "Scanner", "Motion", "Moving beam with configurable background.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_SIZE | NODE_LED_CTRL_FADE | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 40, 0, 3, 255, 0, 96, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_THEATER_CHASE, "theater_chase", "Theater Chase", "Chase", "Marquee-style chase with lit and dim phases.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_SIZE | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 80, 0, 3, 255, 0, 0, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_STROBE, "strobe", "Strobe", "Flash", "Flash pattern with explicit on and off timing.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_FLASH_ON | NODE_LED_CTRL_FLASH_OFF | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(60, 60, 0, 1, 255, 0, 0, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_PULSE, "pulse", "Pulse", "Fade", "Single color pulse with adjustable strength.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_INTENSITY | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 45, 0, 8, 180, 0, 0, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_FADE_IN_OUT, "fade_in_out", "Fade In Out", "Fade", "Single color fade with stronger tail control.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_FADE | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 40, 0, 8, 255, 0, 160, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_TWINKLE, "twinkle", "Twinkle", "Flash", "Random lit pixels over a background color.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_DENSITY | NODE_LED_CTRL_FADE | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 50, 0, 1, 255, 40, 120, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_TWINKLE_RANDOM, "twinkle_random", "Twinkle Random", "Flash", "Random palette twinkles over a background color.", NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_DENSITY | NODE_LED_CTRL_FADE | NODE_LED_CTRL_REPEAT_COUNT | NODE_LED_CTRL_PALETTE_MODE, EFFECT_DEFAULTS(0, 45, 0, 1, 255, 45, 120, NODE_LED_PALETTE_RANDOM, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_SPARKLE, "sparkle", "Sparkle", "Flash", "Short bright hits over a background color.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_DENSITY | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 35, 0, 1, 255, 20, 0, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_GLITTER, "glitter", "Glitter", "Flash", "Dense sparkle layer with a soft background.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_DENSITY | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 30, 0, 1, 255, 18, 0, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 16, 16, 16, 0)},
    {NODE_LED_EFFECT_COMET, "comet", "Comet", "Motion", "Moving head with a fading tail.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_SIZE | NODE_LED_CTRL_FADE | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 35, 0, 6, 255, 0, 180, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_LARSON, "larson", "Larson", "Motion", "Scanner-style sweep with a stronger tail.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_SIZE | NODE_LED_CTRL_FADE | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 35, 0, 4, 255, 0, 180, NODE_LED_PALETTE_NONE, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_RUNNING_LIGHTS, "running_lights", "Running Lights", "Motion", "Wave of light running across the strip.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_SIZE | NODE_LED_CTRL_INTENSITY | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 35, 0, 5, 160, 0, 0, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_FIRE_FLICKER, "fire_flicker", "Fire Flicker", "Palette", "Warm palette flicker with density and strength control.", NODE_LED_CTRL_SPEED | NODE_LED_CTRL_INTENSITY | NODE_LED_CTRL_DENSITY | NODE_LED_CTRL_REPEAT_COUNT | NODE_LED_CTRL_PALETTE_MODE, EFFECT_DEFAULTS(0, 45, 0, 1, 180, 70, 0, NODE_LED_PALETTE_FIRE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_CHASE_DUAL, "chase_dual", "Chase Dual", "Chase", "Alternating dual-color chase with explicit background.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_SECONDARY_COLOR | NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_SIZE | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 45, 0, 2, 255, 0, 0, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 255, 128, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_CHASE_SINGLE, "chase_single", "Chase Single", "Chase", "Single-color chase over a background.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_SIZE | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 40, 0, 2, 255, 0, 0, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_BOUNCE, "bounce", "Bounce", "Motion", "Back-and-forth motion with fade control.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_BACKGROUND_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_SIZE | NODE_LED_CTRL_FADE | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 35, 0, 4, 255, 0, 150, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0)},
    {NODE_LED_EFFECT_BREATH_WAVE, "breath_wave", "Breath Wave", "Fade", "Dual-color breathing wave along the strip.", NODE_LED_CTRL_PRIMARY_COLOR | NODE_LED_CTRL_SECONDARY_COLOR | NODE_LED_CTRL_SPEED | NODE_LED_CTRL_SIZE | NODE_LED_CTRL_FADE | NODE_LED_CTRL_REPEAT_COUNT, EFFECT_DEFAULTS(0, 50, 0, 8, 255, 0, 150, NODE_LED_PALETTE_NONE, 255, 255, 255, 0, 32, 64, 96, 0, 0, 0, 0, 0)},
};

const node_led_effect_descriptor_t *node_led_effect_descriptors(void)
{
    return s_led_effects;
}

size_t node_led_effect_descriptor_count(void)
{
    return sizeof(s_led_effects) / sizeof(s_led_effects[0]);
}

size_t node_led_effect_group_count(void)
{
    return sizeof(s_led_effect_groups) / sizeof(s_led_effect_groups[0]);
}

const node_led_effect_descriptor_t *node_led_effect_descriptor(node_led_effect_id_t id)
{
    size_t count = node_led_effect_descriptor_count();
    for (size_t i = 0; i < count; ++i) {
        if (s_led_effects[i].id == id) {
            return &s_led_effects[i];
        }
    }
    return NULL;
}

const node_led_effect_defaults_t *node_led_effect_defaults(node_led_effect_id_t id)
{
    const node_led_effect_descriptor_t *desc = node_led_effect_descriptor(id);
    return desc ? &desc->defaults : NULL;
}

const char *node_led_effect_name(node_led_effect_id_t id)
{
    const node_led_effect_descriptor_t *desc = node_led_effect_descriptor(id);
    return desc ? desc->name : "";
}

const char *node_led_effect_label(node_led_effect_id_t id)
{
    const node_led_effect_descriptor_t *desc = node_led_effect_descriptor(id);
    return desc ? desc->label : "";
}

const char *node_led_effect_group_name(size_t index)
{
    return index < node_led_effect_group_count() ? s_led_effect_groups[index] : "";
}

node_led_effect_id_t node_led_effect_id_from_name(const char *name)
{
    size_t count = node_led_effect_descriptor_count();
    if (!name || !*name) {
        return NODE_LED_EFFECT_INVALID;
    }
    if (strcmp(name, "theater") == 0) {
        return NODE_LED_EFFECT_THEATER_CHASE;
    }
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(s_led_effects[i].name, name) == 0) {
            return s_led_effects[i].id;
        }
    }
    return NODE_LED_EFFECT_INVALID;
}

const char *node_led_palette_mode_name(node_led_palette_mode_t mode)
{
    switch (mode) {
    case NODE_LED_PALETTE_NONE:
        return "none";
    case NODE_LED_PALETTE_RAINBOW:
        return "rainbow";
    case NODE_LED_PALETTE_WARM:
        return "warm";
    case NODE_LED_PALETTE_COOL:
        return "cool";
    case NODE_LED_PALETTE_FIRE:
        return "fire";
    case NODE_LED_PALETTE_RANDOM:
        return "random";
    default:
        return "none";
    }
}

node_led_palette_mode_t node_led_palette_mode_from_name(const char *name)
{
    if (!name || !*name || strcmp(name, "none") == 0) {
        return NODE_LED_PALETTE_NONE;
    }
    if (strcmp(name, "rainbow") == 0) {
        return NODE_LED_PALETTE_RAINBOW;
    }
    if (strcmp(name, "warm") == 0) {
        return NODE_LED_PALETTE_WARM;
    }
    if (strcmp(name, "cool") == 0) {
        return NODE_LED_PALETTE_COOL;
    }
    if (strcmp(name, "fire") == 0) {
        return NODE_LED_PALETTE_FIRE;
    }
    if (strcmp(name, "random") == 0) {
        return NODE_LED_PALETTE_RANDOM;
    }
    return NODE_LED_PALETTE_NONE;
}

bool node_led_effect_is_advanced(node_led_effect_id_t id)
{
    return id != NODE_LED_EFFECT_BLINK && id != NODE_LED_EFFECT_BREATHE && id < NODE_LED_EFFECT_COUNT;
}
