#pragma once

#include "node_config.h"

extern const uint32_t g_node_config_version;
extern const uint32_t g_node_led_editor_version;

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
} node_led_effect_preset_v8_t;

typedef struct {
    node_led_effect_preset_v8_t items[NODE_LED_EFFECT_COUNT];
} node_led_effect_presets_v8_t;

typedef struct {
    node_led_blink_preset_t blink;
    node_led_breathe_preset_t breathe;
    node_led_effect_presets_v8_t effects;
} node_led_editor_strip_v1_t;

typedef struct {
    uint32_t version;
    node_led_editor_strip_v1_t led_strips[NODE_LED_STRIP_MAX];
} node_led_editor_config_v1_t;

typedef struct {
    node_led_blink_preset_t blink;
    node_led_breathe_preset_t breathe;
    node_led_effect_presets_t effects;
} node_led_editor_strip_v2_t;

typedef struct {
    uint32_t version;
    node_led_editor_strip_v2_t led_strips[NODE_LED_STRIP_MAX];
} node_led_editor_config_v2_t;

void node_config_copy_text(char *dst, size_t dst_size, const char *src);
void set_led_effects_defaults(node_led_effect_presets_t *effects);
void set_led_strip_runtime_defaults(node_led_strip_config_t *strip);
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
                                        uint8_t white2);
void migrate_effect_aux_colors(node_led_effect_id_t id, node_led_effect_preset_t *preset);
void migrate_strip_effect_aux_colors(node_led_strip_config_t *strip);
void node_config_overlay_led_editor(node_config_t *config);
