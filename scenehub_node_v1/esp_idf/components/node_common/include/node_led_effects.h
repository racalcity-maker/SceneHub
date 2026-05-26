#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    NODE_LED_PALETTE_NONE = 0,
    NODE_LED_PALETTE_RAINBOW,
    NODE_LED_PALETTE_WARM,
    NODE_LED_PALETTE_COOL,
    NODE_LED_PALETTE_FIRE,
    NODE_LED_PALETTE_RANDOM,
} node_led_palette_mode_t;

typedef enum {
    NODE_LED_EFFECT_BLINK = 0,
    NODE_LED_EFFECT_BREATHE,
    NODE_LED_EFFECT_RAINBOW,
    NODE_LED_EFFECT_RAINBOW_CYCLE,
    NODE_LED_EFFECT_COLOR_WIPE,
    NODE_LED_EFFECT_SCANNER,
    NODE_LED_EFFECT_THEATER_CHASE,
    NODE_LED_EFFECT_STROBE,
    NODE_LED_EFFECT_PULSE,
    NODE_LED_EFFECT_FADE_IN_OUT,
    NODE_LED_EFFECT_TWINKLE,
    NODE_LED_EFFECT_TWINKLE_RANDOM,
    NODE_LED_EFFECT_SPARKLE,
    NODE_LED_EFFECT_GLITTER,
    NODE_LED_EFFECT_COMET,
    NODE_LED_EFFECT_LARSON,
    NODE_LED_EFFECT_RUNNING_LIGHTS,
    NODE_LED_EFFECT_FIRE_FLICKER,
    NODE_LED_EFFECT_CHASE_DUAL,
    NODE_LED_EFFECT_CHASE_SINGLE,
    NODE_LED_EFFECT_BOUNCE,
    NODE_LED_EFFECT_BREATH_WAVE,
    NODE_LED_EFFECT_COUNT,
    NODE_LED_EFFECT_INVALID = 255,
} node_led_effect_id_t;

enum {
    NODE_LED_CTRL_SPEED = 1u << 0,
    NODE_LED_CTRL_INTENSITY = 1u << 1,
    NODE_LED_CTRL_SIZE = 1u << 2,
    NODE_LED_CTRL_DENSITY = 1u << 3,
    NODE_LED_CTRL_FADE = 1u << 4,
    NODE_LED_CTRL_PRIMARY_COLOR = 1u << 5,
    NODE_LED_CTRL_SECONDARY_COLOR = 1u << 6,
    NODE_LED_CTRL_BACKGROUND_COLOR = 1u << 7,
    NODE_LED_CTRL_FLASH_ON = 1u << 8,
    NODE_LED_CTRL_FLASH_OFF = 1u << 9,
    NODE_LED_CTRL_REPEAT_COUNT = 1u << 10,
    NODE_LED_CTRL_PALETTE_MODE = 1u << 11,
};

typedef struct {
    uint32_t duration_ms;
    uint32_t step_ms;
    uint16_t repeat_count;
    uint16_t size;
    uint16_t intensity;
    uint16_t density;
    uint16_t fade;
    node_led_palette_mode_t palette_mode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t white;
    uint8_t red2;
    uint8_t green2;
    uint8_t blue2;
    uint8_t white2;
    uint8_t bg_red;
    uint8_t bg_green;
    uint8_t bg_blue;
    uint8_t bg_white;
} node_led_effect_defaults_t;

typedef struct {
    node_led_effect_id_t id;
    const char *name;
    const char *label;
    const char *group;
    const char *note;
    uint32_t controls;
    node_led_effect_defaults_t defaults;
} node_led_effect_descriptor_t;

const node_led_effect_descriptor_t *node_led_effect_descriptors(void);
size_t node_led_effect_descriptor_count(void);
size_t node_led_effect_group_count(void);
const node_led_effect_descriptor_t *node_led_effect_descriptor(node_led_effect_id_t id);
const node_led_effect_defaults_t *node_led_effect_defaults(node_led_effect_id_t id);
const char *node_led_effect_name(node_led_effect_id_t id);
const char *node_led_effect_label(node_led_effect_id_t id);
const char *node_led_effect_group_name(size_t index);
node_led_effect_id_t node_led_effect_id_from_name(const char *name);
const char *node_led_palette_mode_name(node_led_palette_mode_t mode);
node_led_palette_mode_t node_led_palette_mode_from_name(const char *name);
bool node_led_effect_is_advanced(node_led_effect_id_t id);
