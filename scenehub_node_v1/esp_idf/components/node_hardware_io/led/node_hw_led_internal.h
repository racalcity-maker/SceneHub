#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "node_hardware_io_internal.h"

typedef struct {
    bool configured;
    uint8_t channel;
    int gpio;
    uint16_t pixel_count;
    node_led_color_order_t color_order;
    bool rgbw;
    led_strip_handle_t handle;
    SemaphoreHandle_t mutex;
    TaskHandle_t effect_task;
    volatile uint32_t effect_seq;
    volatile bool effect_active;
    node_hw_led_effect_t active_effect;
    node_hw_led_effect_config_t active_config;
    node_hw_led_effect_t worker_effect;
    node_hw_led_effect_config_t worker_config;
    uint32_t worker_effect_seq;
} node_hw_led_strip_t;

const char *effect_name(node_hw_led_effect_t effect);
const char *effect_source_name(const node_hw_led_effect_config_t *config);
uint8_t scale_component(uint8_t value, uint8_t brightness);
led_model_t led_model_from_config(node_led_chipset_t chipset);
bool take_strip_lock(node_hw_led_strip_t *strip);
void give_strip_lock(node_hw_led_strip_t *strip);
bool effect_cancelled(node_hw_led_strip_t *strip, uint32_t effect_seq);
esp_err_t activate_effect(node_hw_led_strip_t *strip,
                          node_hw_led_effect_t effect,
                          const node_hw_led_effect_config_t *config);
void clear_effect_active_if_current(node_hw_led_strip_t *strip, uint32_t effect_seq);
StackType_t *allocate_effect_task_stack(size_t idx);
bool delay_effect_ms(node_hw_led_strip_t *strip, uint32_t duration_ms, uint32_t effect_seq);
void stop_effect(node_hw_led_strip_t *strip);
void led_effect_task(void *arg);
void map_rgb_to_driver(node_led_color_order_t color_order,
                       uint8_t red,
                       uint8_t green,
                       uint8_t blue,
                       uint8_t *out_red,
                       uint8_t *out_green,
                       uint8_t *out_blue);
esp_err_t set_pixel_scaled(node_hw_led_strip_t *strip,
                           uint32_t index,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue,
                           uint8_t white,
                           uint8_t brightness);
esp_err_t refresh_strip(node_hw_led_strip_t *strip);
esp_err_t clear_strip(node_hw_led_strip_t *strip);
esp_err_t clear_strip_locked(node_hw_led_strip_t *strip);
esp_err_t fill_strip(node_hw_led_strip_t *strip,
                     uint8_t red,
                     uint8_t green,
                     uint8_t blue,
                     uint8_t white,
                     uint8_t brightness);
esp_err_t fill_strip_locked(node_hw_led_strip_t *strip,
                            uint8_t red,
                            uint8_t green,
                            uint8_t blue,
                            uint8_t white,
                            uint8_t brightness);
void hsv_to_rgb(uint16_t hue,
                uint8_t saturation,
                uint8_t value,
                uint8_t *out_red,
                uint8_t *out_green,
                uint8_t *out_blue);
uint8_t scale_u8(uint8_t value, uint16_t scale);
uint8_t triangle_wave_u8(uint32_t position, uint32_t period);
void palette_color(node_led_palette_mode_t palette,
                   uint32_t seed,
                   uint8_t *out_red,
                   uint8_t *out_green,
                   uint8_t *out_blue);
uint32_t hash_u32(uint32_t value);
uint8_t blend_u8(uint8_t background, uint8_t foreground, uint8_t alpha);
void mix_rgbw(uint8_t bg_red,
              uint8_t bg_green,
              uint8_t bg_blue,
              uint8_t bg_white,
              uint8_t fg_red,
              uint8_t fg_green,
              uint8_t fg_blue,
              uint8_t fg_white,
              uint8_t alpha,
              uint8_t *out_red,
              uint8_t *out_green,
              uint8_t *out_blue,
              uint8_t *out_white);
esp_err_t fill_background(node_hw_led_strip_t *strip, const node_hw_led_effect_config_t *config);
uint32_t effect_size_or_default(const node_hw_led_effect_config_t *config, uint32_t fallback);
uint32_t effect_density_or_default(const node_hw_led_effect_config_t *config, uint32_t fallback);
uint32_t effect_intensity_or_default(const node_hw_led_effect_config_t *config, uint32_t fallback);
uint32_t effect_fade_or_default(const node_hw_led_effect_config_t *config, uint32_t fallback);
esp_err_t run_blink(node_hw_led_strip_t *strip,
                    const node_hw_led_effect_config_t *config,
                    uint32_t effect_seq);
esp_err_t run_breathe(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq);
esp_err_t run_rainbow(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq);
esp_err_t run_rainbow_cycle(node_hw_led_strip_t *strip,
                            const node_hw_led_effect_config_t *config,
                            uint32_t effect_seq);
esp_err_t run_color_wipe(node_hw_led_strip_t *strip,
                         const node_hw_led_effect_config_t *config,
                         uint32_t effect_seq);
esp_err_t run_scanner(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq);
esp_err_t run_theater(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq);
esp_err_t run_strobe(node_hw_led_strip_t *strip,
                     const node_hw_led_effect_config_t *config,
                     uint32_t effect_seq);
esp_err_t run_pulse(node_hw_led_strip_t *strip,
                    const node_hw_led_effect_config_t *config,
                    uint32_t effect_seq);
esp_err_t run_fade_in_out(node_hw_led_strip_t *strip,
                          const node_hw_led_effect_config_t *config,
                          uint32_t effect_seq);
esp_err_t run_twinkle(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq);
esp_err_t run_twinkle_random(node_hw_led_strip_t *strip,
                             const node_hw_led_effect_config_t *config,
                             uint32_t effect_seq);
esp_err_t run_sparkle(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq);
esp_err_t run_glitter(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq);
esp_err_t run_comet(node_hw_led_strip_t *strip,
                    const node_hw_led_effect_config_t *config,
                    uint32_t effect_seq,
                    bool bounce);
esp_err_t run_running_lights(node_hw_led_strip_t *strip,
                             const node_hw_led_effect_config_t *config,
                             uint32_t effect_seq);
esp_err_t run_fire_flicker(node_hw_led_strip_t *strip,
                           const node_hw_led_effect_config_t *config,
                           uint32_t effect_seq);
esp_err_t run_chase_common(node_hw_led_strip_t *strip,
                           const node_hw_led_effect_config_t *config,
                           uint32_t effect_seq,
                           bool dual);
esp_err_t run_breath_wave(node_hw_led_strip_t *strip,
                          const node_hw_led_effect_config_t *config,
                          uint32_t effect_seq);
