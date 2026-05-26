#include "node_hw_led_internal.h"

uint8_t scale_component(uint8_t value, uint8_t brightness)
{
    return (uint8_t)(((uint16_t)value * (uint16_t)brightness + 127U) / 255U);
}

led_model_t led_model_from_config(node_led_chipset_t chipset)
{
    return chipset == NODE_LED_CHIPSET_SK6812 ? LED_MODEL_SK6812 : LED_MODEL_WS2812;
}

void map_rgb_to_driver(node_led_color_order_t color_order,
                       uint8_t red,
                       uint8_t green,
                       uint8_t blue,
                       uint8_t *out_red,
                       uint8_t *out_green,
                       uint8_t *out_blue)
{
    uint8_t mapped_red = red;
    uint8_t mapped_green = green;
    uint8_t mapped_blue = blue;

    switch (color_order) {
    case NODE_LED_COLOR_ORDER_RGB:
        mapped_red = green;
        mapped_green = red;
        mapped_blue = blue;
        break;
    case NODE_LED_COLOR_ORDER_RBG:
        mapped_red = blue;
        mapped_green = red;
        mapped_blue = green;
        break;
    case NODE_LED_COLOR_ORDER_GRB:
        mapped_red = red;
        mapped_green = green;
        mapped_blue = blue;
        break;
    case NODE_LED_COLOR_ORDER_GBR:
        mapped_red = blue;
        mapped_green = green;
        mapped_blue = red;
        break;
    case NODE_LED_COLOR_ORDER_BRG:
        mapped_red = red;
        mapped_green = blue;
        mapped_blue = green;
        break;
    case NODE_LED_COLOR_ORDER_BGR:
        mapped_red = green;
        mapped_green = blue;
        mapped_blue = red;
        break;
    default:
        break;
    }

    if (out_red) {
        *out_red = mapped_red;
    }
    if (out_green) {
        *out_green = mapped_green;
    }
    if (out_blue) {
        *out_blue = mapped_blue;
    }
}

esp_err_t set_pixel_scaled(node_hw_led_strip_t *strip,
                           uint32_t index,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue,
                           uint8_t white,
                           uint8_t brightness)
{
    uint8_t scaled_red = scale_component(red, brightness);
    uint8_t scaled_green = scale_component(green, brightness);
    uint8_t scaled_blue = scale_component(blue, brightness);
    uint8_t scaled_white = scale_component(white, brightness);
    uint8_t driver_red = 0;
    uint8_t driver_green = 0;
    uint8_t driver_blue = 0;

    if (!strip || !strip->configured || !strip->handle) {
        return ESP_ERR_NOT_FOUND;
    }

    map_rgb_to_driver(strip->color_order,
                      scaled_red,
                      scaled_green,
                      scaled_blue,
                      &driver_red,
                      &driver_green,
                      &driver_blue);

    if (strip->rgbw) {
        return led_strip_set_pixel_rgbw(strip->handle,
                                        index,
                                        driver_red,
                                        driver_green,
                                        driver_blue,
                                        scaled_white);
    }
    return led_strip_set_pixel(strip->handle, index, driver_red, driver_green, driver_blue);
}

esp_err_t refresh_strip(node_hw_led_strip_t *strip)
{
    if (!strip || !strip->configured || !strip->handle) {
        return ESP_ERR_NOT_FOUND;
    }
    return led_strip_refresh(strip->handle);
}

esp_err_t clear_strip(node_hw_led_strip_t *strip)
{
    if (!strip || !strip->configured || !strip->handle) {
        return ESP_ERR_NOT_FOUND;
    }
    return led_strip_clear(strip->handle);
}

esp_err_t clear_strip_locked(node_hw_led_strip_t *strip)
{
    esp_err_t err = ESP_ERR_INVALID_STATE;

    if (!strip || !strip->mutex || xSemaphoreTake(strip->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    err = clear_strip(strip);
    xSemaphoreGive(strip->mutex);
    return err;
}

esp_err_t fill_strip(node_hw_led_strip_t *strip,
                     uint8_t red,
                     uint8_t green,
                     uint8_t blue,
                     uint8_t white,
                     uint8_t brightness)
{
    if (!strip || !strip->configured || !strip->handle) {
        return ESP_ERR_NOT_FOUND;
    }
    for (uint32_t i = 0; i < strip->pixel_count; ++i) {
        esp_err_t err = set_pixel_scaled(strip, i, red, green, blue, white, brightness);
        if (err != ESP_OK) {
            return err;
        }
    }
    return refresh_strip(strip);
}

esp_err_t fill_strip_locked(node_hw_led_strip_t *strip,
                            uint8_t red,
                            uint8_t green,
                            uint8_t blue,
                            uint8_t white,
                            uint8_t brightness)
{
    esp_err_t err = ESP_ERR_INVALID_STATE;

    if (!strip || !strip->mutex || xSemaphoreTake(strip->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    err = fill_strip(strip, red, green, blue, white, brightness);
    xSemaphoreGive(strip->mutex);
    return err;
}

void hsv_to_rgb(uint16_t hue,
                uint8_t saturation,
                uint8_t value,
                uint8_t *out_red,
                uint8_t *out_green,
                uint8_t *out_blue)
{
    uint8_t region = (uint8_t)((hue % 360U) / 60U);
    uint16_t remainder = (uint16_t)(((hue % 360U) - (region * 60U)) * 255U / 60U);
    uint8_t p = (uint8_t)((value * (255U - saturation)) / 255U);
    uint8_t q = (uint8_t)((value * (255U - ((saturation * remainder) / 255U))) / 255U);
    uint8_t t = (uint8_t)((value * (255U - ((saturation * (255U - remainder)) / 255U))) / 255U);
    uint8_t red = value;
    uint8_t green = value;
    uint8_t blue = value;

    switch (region) {
    case 0:
        red = value;
        green = t;
        blue = p;
        break;
    case 1:
        red = q;
        green = value;
        blue = p;
        break;
    case 2:
        red = p;
        green = value;
        blue = t;
        break;
    case 3:
        red = p;
        green = q;
        blue = value;
        break;
    case 4:
        red = t;
        green = p;
        blue = value;
        break;
    case 5:
    default:
        red = value;
        green = p;
        blue = q;
        break;
    }

    if (out_red) {
        *out_red = red;
    }
    if (out_green) {
        *out_green = green;
    }
    if (out_blue) {
        *out_blue = blue;
    }
}

uint8_t scale_u8(uint8_t value, uint16_t scale)
{
    if (scale >= 255U) {
        return value;
    }
    return (uint8_t)(((uint16_t)value * scale) / 255U);
}

uint8_t triangle_wave_u8(uint32_t position, uint32_t period)
{
    uint32_t phase = 0;
    if (period < 2U) {
        return 255U;
    }
    phase = position % period;
    if (phase < (period / 2U)) {
        return (uint8_t)((phase * 510U) / period);
    }
    return (uint8_t)(255U - (((phase - (period / 2U)) * 510U) / period));
}

void palette_color(node_led_palette_mode_t palette,
                   uint32_t seed,
                   uint8_t *out_red,
                   uint8_t *out_green,
                   uint8_t *out_blue)
{
    uint16_t hue = 0;
    switch (palette) {
    case NODE_LED_PALETTE_WARM:
        hue = (uint16_t)(18U + (seed % 24U));
        hsv_to_rgb(hue, 230, 255, out_red, out_green, out_blue);
        break;
    case NODE_LED_PALETTE_COOL:
        hue = (uint16_t)(170U + (seed % 70U));
        hsv_to_rgb(hue, 220, 255, out_red, out_green, out_blue);
        break;
    case NODE_LED_PALETTE_FIRE:
        hue = (uint16_t)(10U + (seed % 35U));
        hsv_to_rgb(hue, 255, (uint8_t)(180U + (seed % 76U)), out_red, out_green, out_blue);
        break;
    case NODE_LED_PALETTE_RANDOM:
    case NODE_LED_PALETTE_RAINBOW:
        hue = (uint16_t)(seed % 360U);
        hsv_to_rgb(hue, 255, 255, out_red, out_green, out_blue);
        break;
    case NODE_LED_PALETTE_NONE:
    default:
        *out_red = 255;
        *out_green = 255;
        *out_blue = 255;
        break;
    }
}

uint32_t hash_u32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

uint8_t blend_u8(uint8_t background, uint8_t foreground, uint8_t alpha)
{
    uint16_t bg = (uint16_t)background * (uint16_t)(255U - alpha);
    uint16_t fg = (uint16_t)foreground * (uint16_t)alpha;
    return (uint8_t)((bg + fg + 127U) / 255U);
}

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
              uint8_t *out_white)
{
    if (out_red) {
        *out_red = blend_u8(bg_red, fg_red, alpha);
    }
    if (out_green) {
        *out_green = blend_u8(bg_green, fg_green, alpha);
    }
    if (out_blue) {
        *out_blue = blend_u8(bg_blue, fg_blue, alpha);
    }
    if (out_white) {
        *out_white = blend_u8(bg_white, fg_white, alpha);
    }
}

esp_err_t fill_background(node_hw_led_strip_t *strip, const node_hw_led_effect_config_t *config)
{
    return fill_strip(strip,
                      config->bg_red,
                      config->bg_green,
                      config->bg_blue,
                      config->bg_white,
                      config->brightness);
}

uint32_t effect_size_or_default(const node_hw_led_effect_config_t *config, uint32_t fallback)
{
    if (!config) {
        return fallback;
    }
    return config->size > 0 ? config->size : fallback;
}

uint32_t effect_density_or_default(const node_hw_led_effect_config_t *config, uint32_t fallback)
{
    if (!config) {
        return fallback;
    }
    return config->density > 0 ? config->density : fallback;
}

uint32_t effect_intensity_or_default(const node_hw_led_effect_config_t *config, uint32_t fallback)
{
    if (!config) {
        return fallback;
    }
    return config->intensity > 0 ? config->intensity : fallback;
}

uint32_t effect_fade_or_default(const node_hw_led_effect_config_t *config, uint32_t fallback)
{
    if (!config) {
        return fallback;
    }
    return config->fade > 0 ? config->fade : fallback;
}
