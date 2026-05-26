#include "node_hw_led_internal.h"

#include "esp_check.h"

static const char *TAG = "node_hw_led_motion";

esp_err_t run_rainbow(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 50U;
    uint32_t count = config->count;
    uint32_t frame_step = 8U;
    esp_err_t err = ESP_OK;

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t base_hue = 0; base_hue < 360U; base_hue += frame_step) {
            if (effect_cancelled(strip, effect_seq) || !take_strip_lock(strip)) {
                return ESP_OK;
            }
            for (uint32_t pixel = 0; pixel < strip->pixel_count; ++pixel) {
                uint16_t hue = (uint16_t)((base_hue + ((pixel * 360U) / (strip->pixel_count ? strip->pixel_count : 1U))) % 360U);
                uint8_t red = 0;
                uint8_t green = 0;
                uint8_t blue = 0;
                hsv_to_rgb(hue, 255, 255, &red, &green, &blue);
                err = set_pixel_scaled(strip, pixel, red, green, blue, 0, config->brightness);
                if (err != ESP_OK) {
                    give_strip_lock(strip);
                    return err;
                }
            }
            err = refresh_strip(strip);
            give_strip_lock(strip);
            if (err != ESP_OK) {
                return err;
            }
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
    }
    return ESP_OK;
}

esp_err_t run_color_wipe(node_hw_led_strip_t *strip,
                         const node_hw_led_effect_config_t *config,
                         uint32_t effect_seq)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 30U;
    uint32_t count = config->count;
    esp_err_t err = ESP_OK;

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        if (effect_cancelled(strip, effect_seq)) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(clear_strip_locked(strip), TAG, "wipe clear failed");
        for (uint32_t pixel = 0; pixel < strip->pixel_count; ++pixel) {
            if (effect_cancelled(strip, effect_seq) || !take_strip_lock(strip)) {
                return ESP_OK;
            }
            err = set_pixel_scaled(strip,
                                   pixel,
                                   config->red,
                                   config->green,
                                   config->blue,
                                   config->white,
                                   config->brightness);
            if (err == ESP_OK) {
                err = refresh_strip(strip);
            }
            give_strip_lock(strip);
            if (err != ESP_OK) {
                return err;
            }
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
    }
    return ESP_OK;
}

esp_err_t run_scanner(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 40U;
    uint32_t count = config->count;
    esp_err_t err = ESP_OK;

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t pixel = 0; pixel < strip->pixel_count; ++pixel) {
            if (effect_cancelled(strip, effect_seq) || !take_strip_lock(strip)) {
                return ESP_OK;
            }
            err = fill_strip(strip,
                             config->bg_red,
                             config->bg_green,
                             config->bg_blue,
                             config->bg_white,
                             config->brightness);
            if (err == ESP_OK) {
                err = set_pixel_scaled(strip,
                                       pixel,
                                       config->red,
                                       config->green,
                                       config->blue,
                                       config->white,
                                       config->brightness);
            }
            if (err == ESP_OK) {
                err = refresh_strip(strip);
            }
            give_strip_lock(strip);
            if (err != ESP_OK) {
                return err;
            }
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
        if (strip->pixel_count > 1U) {
            for (uint32_t pixel = strip->pixel_count - 2U;; --pixel) {
                if (effect_cancelled(strip, effect_seq) || !take_strip_lock(strip)) {
                    return ESP_OK;
                }
                err = fill_strip(strip,
                                 config->bg_red,
                                 config->bg_green,
                                 config->bg_blue,
                                 config->bg_white,
                                 config->brightness);
                if (err == ESP_OK) {
                    err = set_pixel_scaled(strip,
                                           pixel,
                                           config->red,
                                           config->green,
                                           config->blue,
                                           config->white,
                                           config->brightness);
                }
                if (err == ESP_OK) {
                    err = refresh_strip(strip);
                }
                give_strip_lock(strip);
                if (err != ESP_OK) {
                    return err;
                }
                if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                    return ESP_OK;
                }
                if (pixel == 0U) {
                    break;
                }
            }
        }
    }

    if (!effect_cancelled(strip, effect_seq)) {
        ESP_RETURN_ON_ERROR(fill_strip_locked(strip,
                                              config->bg_red,
                                              config->bg_green,
                                              config->bg_blue,
                                              config->bg_white,
                                              config->brightness),
                            TAG,
                            "scanner final background failed");
    }
    return ESP_OK;
}

esp_err_t run_theater(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 80U;
    uint32_t count = config->count;
    esp_err_t err = ESP_OK;

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t phase = 0; phase < 3U; ++phase) {
            if (effect_cancelled(strip, effect_seq) || !take_strip_lock(strip)) {
                return ESP_OK;
            }
            err = fill_strip(strip,
                             config->bg_red,
                             config->bg_green,
                             config->bg_blue,
                             config->bg_white,
                             config->brightness);
            for (uint32_t pixel = phase; err == ESP_OK && pixel < strip->pixel_count; pixel += 3U) {
                err = set_pixel_scaled(strip,
                                       pixel,
                                       config->red,
                                       config->green,
                                       config->blue,
                                       config->white,
                                       config->brightness);
            }
            if (err == ESP_OK) {
                err = refresh_strip(strip);
            }
            give_strip_lock(strip);
            if (err != ESP_OK) {
                return err;
            }
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
    }
    return ESP_OK;
}

esp_err_t run_rainbow_cycle(node_hw_led_strip_t *strip,
                            const node_hw_led_effect_config_t *config,
                            uint32_t effect_seq)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 40U;
    uint32_t count = config->count;
    uint32_t frame_step = effect_intensity_or_default(config, 160U) / 24U;
    esp_err_t err = ESP_OK;

    if (frame_step == 0) {
        frame_step = 1;
    }

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t base_hue = 0; base_hue < 360U; base_hue += frame_step) {
            if (effect_cancelled(strip, effect_seq) || !take_strip_lock(strip)) {
                return ESP_OK;
            }
            for (uint32_t pixel = 0; pixel < strip->pixel_count; ++pixel) {
                uint16_t hue = (uint16_t)((base_hue + ((pixel * 720U) / (strip->pixel_count ? strip->pixel_count : 1U))) % 360U);
                uint8_t red = 0;
                uint8_t green = 0;
                uint8_t blue = 0;
                hsv_to_rgb(hue, 255, 255, &red, &green, &blue);
                err = set_pixel_scaled(strip, pixel, red, green, blue, 0, config->brightness);
                if (err != ESP_OK) {
                    give_strip_lock(strip);
                    return err;
                }
            }
            err = refresh_strip(strip);
            give_strip_lock(strip);
            if (err != ESP_OK) {
                return err;
            }
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
    }
    return ESP_OK;
}

esp_err_t run_comet(node_hw_led_strip_t *strip,
                    const node_hw_led_effect_config_t *config,
                    uint32_t effect_seq,
                    bool bounce)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 35U;
    uint32_t count = config->count;
    uint32_t tail = effect_size_or_default(config, 6U);
    uint32_t fade = effect_fade_or_default(config, 180U);
    esp_err_t err = ESP_OK;
    uint32_t path = strip->pixel_count + tail;

    if (tail > strip->pixel_count && strip->pixel_count > 0) {
        tail = strip->pixel_count;
    }
    if (tail == 0U) {
        tail = 1U;
    }
    if (fade > 255U) {
        fade = 255U;
    }

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t direction = 0; direction < (bounce ? 2U : 1U); ++direction) {
            for (uint32_t head = 0; head < path; ++head) {
                if (effect_cancelled(strip, effect_seq) || !take_strip_lock(strip)) {
                    return ESP_OK;
                }
                err = fill_background(strip, config);
                for (uint32_t tail_index = 0; err == ESP_OK && tail_index < tail; ++tail_index) {
                    int32_t position = direction == 0
                                           ? (int32_t)head - (int32_t)tail_index
                                           : (int32_t)(path - 1U - head) + (int32_t)tail_index - (int32_t)(tail - 1U);
                    uint8_t alpha = 0;
                    uint8_t out_red = 0;
                    uint8_t out_green = 0;
                    uint8_t out_blue = 0;
                    uint8_t out_white = 0;

                    if (position < 0 || (uint32_t)position >= strip->pixel_count) {
                        continue;
                    }

                    alpha = (uint8_t)((255U * (tail - tail_index)) / tail);
                    alpha = scale_u8(alpha, (uint16_t)fade);
                    mix_rgbw(config->bg_red,
                             config->bg_green,
                             config->bg_blue,
                             config->bg_white,
                             config->red,
                             config->green,
                             config->blue,
                             config->white,
                             alpha,
                             &out_red,
                             &out_green,
                             &out_blue,
                             &out_white);
                    err = set_pixel_scaled(strip,
                                           (uint32_t)position,
                                           out_red,
                                           out_green,
                                           out_blue,
                                           out_white,
                                           config->brightness);
                }
                if (err == ESP_OK) {
                    err = refresh_strip(strip);
                }
                give_strip_lock(strip);
                if (err != ESP_OK) {
                    return err;
                }
                if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                    return ESP_OK;
                }
            }
        }
    }

    return ESP_OK;
}

esp_err_t run_running_lights(node_hw_led_strip_t *strip,
                             const node_hw_led_effect_config_t *config,
                             uint32_t effect_seq)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 35U;
    uint32_t count = config->count;
    uint32_t span = effect_size_or_default(config, 5U) * 24U;
    uint32_t intensity = effect_intensity_or_default(config, 160U);
    esp_err_t err = ESP_OK;

    if (span < 24U) {
        span = 24U;
    }
    if (intensity > 255U) {
        intensity = 255U;
    }

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t frame = 0; frame < 128U; ++frame) {
            if (effect_cancelled(strip, effect_seq) || !take_strip_lock(strip)) {
                return ESP_OK;
            }
            err = fill_background(strip, config);
            for (uint32_t pixel = 0; err == ESP_OK && pixel < strip->pixel_count; ++pixel) {
                uint8_t wave = triangle_wave_u8(frame * 11U + pixel * span, 512U);
                uint8_t alpha = scale_u8(wave, (uint16_t)intensity);
                uint8_t out_red = 0;
                uint8_t out_green = 0;
                uint8_t out_blue = 0;
                uint8_t out_white = 0;
                mix_rgbw(config->bg_red,
                         config->bg_green,
                         config->bg_blue,
                         config->bg_white,
                         config->red,
                         config->green,
                         config->blue,
                         config->white,
                         alpha,
                         &out_red,
                         &out_green,
                         &out_blue,
                         &out_white);
                err = set_pixel_scaled(strip,
                                       pixel,
                                       out_red,
                                       out_green,
                                       out_blue,
                                       out_white,
                                       config->brightness);
            }
            if (err == ESP_OK) {
                err = refresh_strip(strip);
            }
            give_strip_lock(strip);
            if (err != ESP_OK) {
                return err;
            }
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
    }

    return ESP_OK;
}

esp_err_t run_chase_common(node_hw_led_strip_t *strip,
                           const node_hw_led_effect_config_t *config,
                           uint32_t effect_seq,
                           bool dual)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 45U;
    uint32_t count = config->count;
    uint32_t size = effect_size_or_default(config, 2U);
    uint32_t spacing = dual ? size * 3U : size * 2U;
    esp_err_t err = ESP_OK;

    if (size == 0U) {
        size = 1U;
    }
    if (spacing == 0U) {
        spacing = 2U;
    }

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t offset = 0; offset < spacing; ++offset) {
            if (effect_cancelled(strip, effect_seq) || !take_strip_lock(strip)) {
                return ESP_OK;
            }
            err = fill_background(strip, config);
            for (uint32_t pixel = 0; err == ESP_OK && pixel < strip->pixel_count; ++pixel) {
                uint32_t band = ((pixel + offset) / size) % (dual ? 3U : 2U);
                if (band == 0U) {
                    err = set_pixel_scaled(strip, pixel, config->red, config->green, config->blue, config->white, config->brightness);
                } else if (dual && band == 1U) {
                    err = set_pixel_scaled(strip, pixel, config->red2, config->green2, config->blue2, config->white2, config->brightness);
                }
            }
            if (err == ESP_OK) {
                err = refresh_strip(strip);
            }
            give_strip_lock(strip);
            if (err != ESP_OK) {
                return err;
            }
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
    }

    return ESP_OK;
}

esp_err_t run_breath_wave(node_hw_led_strip_t *strip,
                          const node_hw_led_effect_config_t *config,
                          uint32_t effect_seq)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 50U;
    uint32_t count = config->count;
    uint32_t span = effect_size_or_default(config, 8U) * 12U;
    uint32_t fade = effect_fade_or_default(config, 150U);
    esp_err_t err = ESP_OK;

    if (span < 24U) {
        span = 24U;
    }
    if (fade > 255U) {
        fade = 255U;
    }

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t frame = 0; frame < 128U; ++frame) {
            if (effect_cancelled(strip, effect_seq) || !take_strip_lock(strip)) {
                return ESP_OK;
            }
            for (uint32_t pixel = 0; err == ESP_OK && pixel < strip->pixel_count; ++pixel) {
                uint8_t wave = triangle_wave_u8(frame * 9U + pixel * span, 512U);
                uint8_t alpha = scale_u8(wave, (uint16_t)fade);
                uint8_t out_red = 0;
                uint8_t out_green = 0;
                uint8_t out_blue = 0;
                uint8_t out_white = 0;
                mix_rgbw(config->red2,
                         config->green2,
                         config->blue2,
                         config->white2,
                         config->red,
                         config->green,
                         config->blue,
                         config->white,
                         alpha,
                         &out_red,
                         &out_green,
                         &out_blue,
                         &out_white);
                err = set_pixel_scaled(strip,
                                       pixel,
                                       out_red,
                                       out_green,
                                       out_blue,
                                       out_white,
                                       config->brightness);
            }
            if (err == ESP_OK) {
                err = refresh_strip(strip);
            }
            give_strip_lock(strip);
            if (err != ESP_OK) {
                return err;
            }
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
    }

    return ESP_OK;
}
