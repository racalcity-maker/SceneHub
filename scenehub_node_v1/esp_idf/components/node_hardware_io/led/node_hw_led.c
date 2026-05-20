#include "node_hardware_io_internal.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "node_board.h"

static const char *TAG = "node_hw_led";

typedef struct {
    bool configured;
    uint8_t channel;
    int gpio;
    uint16_t pixel_count;
    node_led_color_order_t color_order;
    bool rgbw;
    led_strip_handle_t handle;
} node_hw_led_strip_t;

static node_hw_led_strip_t s_led_strips[NODE_LED_STRIP_MAX];

static uint8_t scale_component(uint8_t value, uint8_t brightness)
{
    return (uint8_t)(((uint16_t)value * (uint16_t)brightness + 127U) / 255U);
}

static void delay_ms(uint32_t duration_ms)
{
    if (duration_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
    }
}

static led_model_t led_model_from_config(node_led_chipset_t chipset)
{
    return chipset == NODE_LED_CHIPSET_SK6812 ? LED_MODEL_SK6812 : LED_MODEL_WS2812;
}

static node_hw_led_strip_t *find_led_strip(uint8_t strip)
{
    if (strip == 0 || strip > NODE_LED_STRIP_MAX) {
        return NULL;
    }
    return &s_led_strips[strip - 1];
}

static void map_rgb_to_driver(node_led_color_order_t color_order,
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

    /* The driver is configured for GRB/GRBW. These remaps preserve the
     * user-visible RGB semantics for other strip color orders. */
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

static esp_err_t set_pixel_scaled(node_hw_led_strip_t *strip,
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

static esp_err_t refresh_strip(node_hw_led_strip_t *strip)
{
    if (!strip || !strip->configured || !strip->handle) {
        return ESP_ERR_NOT_FOUND;
    }
    return led_strip_refresh(strip->handle);
}

static esp_err_t clear_strip(node_hw_led_strip_t *strip)
{
    if (!strip || !strip->configured || !strip->handle) {
        return ESP_ERR_NOT_FOUND;
    }
    return led_strip_clear(strip->handle);
}

static esp_err_t fill_strip(node_hw_led_strip_t *strip,
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

static void hsv_to_rgb(uint16_t hue,
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

static esp_err_t run_blink(node_hw_led_strip_t *strip,
                           const node_hw_led_effect_config_t *config)
{
    uint32_t on_ms = config->duration_ms;
    uint32_t off_ms = config->step_ms ? config->step_ms : config->duration_ms;
    uint32_t count = config->count ? config->count : 1;
    for (uint32_t i = 0; i < count; ++i) {
        ESP_RETURN_ON_ERROR(fill_strip(strip,
                                       config->red,
                                       config->green,
                                       config->blue,
                                       config->white,
                                       config->brightness),
                            TAG,
                            "blink fill failed");
        delay_ms(on_ms);
        ESP_RETURN_ON_ERROR(clear_strip(strip), TAG, "blink clear failed");
        if (i + 1U < count) {
            delay_ms(off_ms);
        }
    }
    return ESP_OK;
}

static esp_err_t run_breathe(node_hw_led_strip_t *strip,
                             const node_hw_led_effect_config_t *config)
{
    uint32_t cycle_ms = config->duration_ms ? config->duration_ms : 1000U;
    uint32_t step_ms = config->step_ms ? config->step_ms : 40U;
    uint32_t count = config->count ? config->count : 1U;
    uint32_t half_steps = cycle_ms / (step_ms * 2U);

    if (half_steps == 0) {
        half_steps = 1;
    }

    for (uint32_t cycle = 0; cycle < count; ++cycle) {
        for (uint32_t step = 0; step <= half_steps; ++step) {
            uint8_t brightness = (uint8_t)((255U * step) / half_steps);
            ESP_RETURN_ON_ERROR(fill_strip(strip,
                                           config->red,
                                           config->green,
                                           config->blue,
                                           config->white,
                                           brightness),
                                TAG,
                                "breathe up failed");
            delay_ms(step_ms);
        }
        for (uint32_t step = half_steps; step > 0; --step) {
            uint8_t brightness = (uint8_t)((255U * (step - 1U)) / half_steps);
            ESP_RETURN_ON_ERROR(fill_strip(strip,
                                           config->red,
                                           config->green,
                                           config->blue,
                                           config->white,
                                           brightness),
                                TAG,
                                "breathe down failed");
            delay_ms(step_ms);
        }
    }
    return ESP_OK;
}

static esp_err_t run_rainbow(node_hw_led_strip_t *strip,
                             const node_hw_led_effect_config_t *config)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 50U;
    uint32_t count = config->count ? config->count : 1U;
    uint32_t frame_step = 8U;

    for (uint32_t cycle = 0; cycle < count; ++cycle) {
        for (uint32_t base_hue = 0; base_hue < 360U; base_hue += frame_step) {
            for (uint32_t pixel = 0; pixel < strip->pixel_count; ++pixel) {
                uint16_t hue = (uint16_t)((base_hue + ((pixel * 360U) / (strip->pixel_count ? strip->pixel_count : 1U))) % 360U);
                uint8_t red = 0;
                uint8_t green = 0;
                uint8_t blue = 0;
                hsv_to_rgb(hue, 255, 255, &red, &green, &blue);
                ESP_RETURN_ON_ERROR(set_pixel_scaled(strip,
                                                     pixel,
                                                     red,
                                                     green,
                                                     blue,
                                                     0,
                                                     config->brightness),
                                    TAG,
                                    "rainbow pixel failed");
            }
            ESP_RETURN_ON_ERROR(refresh_strip(strip), TAG, "rainbow refresh failed");
            delay_ms(step_ms);
        }
    }
    return ESP_OK;
}

static esp_err_t run_color_wipe(node_hw_led_strip_t *strip,
                                const node_hw_led_effect_config_t *config)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 30U;
    uint32_t count = config->count ? config->count : 1U;

    for (uint32_t cycle = 0; cycle < count; ++cycle) {
        ESP_RETURN_ON_ERROR(clear_strip(strip), TAG, "wipe clear failed");
        for (uint32_t pixel = 0; pixel < strip->pixel_count; ++pixel) {
            ESP_RETURN_ON_ERROR(set_pixel_scaled(strip,
                                                 pixel,
                                                 config->red,
                                                 config->green,
                                                 config->blue,
                                                 config->white,
                                                 config->brightness),
                                TAG,
                                "wipe pixel failed");
            ESP_RETURN_ON_ERROR(refresh_strip(strip), TAG, "wipe refresh failed");
            delay_ms(step_ms);
        }
    }
    return ESP_OK;
}

static esp_err_t run_scanner(node_hw_led_strip_t *strip,
                             const node_hw_led_effect_config_t *config)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 40U;
    uint32_t count = config->count ? config->count : 1U;

    for (uint32_t cycle = 0; cycle < count; ++cycle) {
        for (uint32_t pixel = 0; pixel < strip->pixel_count; ++pixel) {
            ESP_RETURN_ON_ERROR(clear_strip(strip), TAG, "scanner clear failed");
            ESP_RETURN_ON_ERROR(set_pixel_scaled(strip,
                                                 pixel,
                                                 config->red,
                                                 config->green,
                                                 config->blue,
                                                 config->white,
                                                 config->brightness),
                                TAG,
                                "scanner forward pixel failed");
            ESP_RETURN_ON_ERROR(refresh_strip(strip), TAG, "scanner forward refresh failed");
            delay_ms(step_ms);
        }
        if (strip->pixel_count > 1U) {
            for (uint32_t pixel = strip->pixel_count - 2U; pixel > 0; --pixel) {
                ESP_RETURN_ON_ERROR(clear_strip(strip), TAG, "scanner clear failed");
                ESP_RETURN_ON_ERROR(set_pixel_scaled(strip,
                                                     pixel,
                                                     config->red,
                                                     config->green,
                                                     config->blue,
                                                     config->white,
                                                     config->brightness),
                                    TAG,
                                    "scanner reverse pixel failed");
                ESP_RETURN_ON_ERROR(refresh_strip(strip), TAG, "scanner reverse refresh failed");
                delay_ms(step_ms);
            }
        }
    }
    return ESP_OK;
}

static esp_err_t run_theater(node_hw_led_strip_t *strip,
                             const node_hw_led_effect_config_t *config)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 80U;
    uint32_t count = config->count ? config->count : 1U;

    for (uint32_t cycle = 0; cycle < count; ++cycle) {
        for (uint32_t phase = 0; phase < 3U; ++phase) {
            ESP_RETURN_ON_ERROR(clear_strip(strip), TAG, "theater clear failed");
            for (uint32_t pixel = phase; pixel < strip->pixel_count; pixel += 3U) {
                ESP_RETURN_ON_ERROR(set_pixel_scaled(strip,
                                                     pixel,
                                                     config->red,
                                                     config->green,
                                                     config->blue,
                                                     config->white,
                                                     config->brightness),
                                    TAG,
                                    "theater pixel failed");
            }
            ESP_RETURN_ON_ERROR(refresh_strip(strip), TAG, "theater refresh failed");
            delay_ms(step_ms);
        }
    }
    return ESP_OK;
}

static esp_err_t run_strobe(node_hw_led_strip_t *strip,
                            const node_hw_led_effect_config_t *config)
{
    uint32_t on_ms = config->duration_ms ? config->duration_ms : 60U;
    uint32_t off_ms = config->step_ms ? config->step_ms : 60U;
    uint32_t count = config->count ? config->count : 3U;

    for (uint32_t cycle = 0; cycle < count; ++cycle) {
        ESP_RETURN_ON_ERROR(fill_strip(strip,
                                       config->red,
                                       config->green,
                                       config->blue,
                                       config->white,
                                       config->brightness),
                            TAG,
                            "strobe fill failed");
        delay_ms(on_ms);
        ESP_RETURN_ON_ERROR(clear_strip(strip), TAG, "strobe clear failed");
        if (cycle + 1U < count) {
            delay_ms(off_ms);
        }
    }
    return ESP_OK;
}

static esp_err_t configure_led_strip(size_t idx, const node_led_strip_config_t *pin)
{
    node_hw_led_strip_t *strip = &s_led_strips[idx];
    led_strip_config_t strip_config = {
        .strip_gpio_num = pin->gpio,
        .max_leds = pin->pixel_count,
        .led_model = led_model_from_config(pin->chipset),
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    esp_err_t err = ESP_OK;

#ifdef LED_STRIP_COLOR_COMPONENT_FMT_GRBW
    strip_config.color_component_format = pin->rgbw ? LED_STRIP_COLOR_COMPONENT_FMT_GRBW
                                                    : LED_STRIP_COLOR_COMPONENT_FMT_GRB;
#else
    strip_config.led_pixel_format = pin->rgbw ? LED_PIXEL_FORMAT_GRBW : LED_PIXEL_FORMAT_GRB;
#endif

    err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip->handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to init strip channel=%u gpio=%d err=%s",
                 (unsigned)pin->channel,
                 pin->gpio,
                 esp_err_to_name(err));
        return err;
    }

    strip->configured = true;
    strip->channel = pin->channel;
    strip->gpio = pin->gpio;
    strip->pixel_count = pin->pixel_count;
    strip->color_order = pin->color_order;
    strip->rgbw = pin->rgbw;
    g_node_hw.status.configured_led_strips++;

    return led_strip_clear(strip->handle);
}

esp_err_t node_hw_led_init(const node_config_t *config)
{
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        if (s_led_strips[i].handle) {
            (void)led_strip_del(s_led_strips[i].handle);
            s_led_strips[i].handle = NULL;
        }
    }
    memset(s_led_strips, 0, sizeof(s_led_strips));

    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *pin = &config->led_strips[i];
        if (!pin->enabled || pin->gpio < 0 || !node_board_gpio_is_allowed(pin->gpio)) {
            continue;
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(configure_led_strip(i, pin));
    }
    return ESP_OK;
}

esp_err_t node_hw_led_all_off(void)
{
    esp_err_t first_err = ESP_OK;

    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        if (!s_led_strips[i].configured) {
            continue;
        }
        esp_err_t err = clear_strip(&s_led_strips[i]);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
    }

    return first_err;
}

esp_err_t node_hw_led_off(uint8_t strip)
{
    node_hw_led_strip_t *runtime = find_led_strip(strip);
    return clear_strip(runtime);
}

esp_err_t node_hw_led_solid(uint8_t strip,
                            uint8_t red,
                            uint8_t green,
                            uint8_t blue,
                            uint8_t white,
                            uint8_t brightness)
{
    node_hw_led_strip_t *runtime = find_led_strip(strip);
    return fill_strip(runtime, red, green, blue, white, brightness);
}

esp_err_t node_hw_led_run_effect(uint8_t strip,
                                 node_hw_led_effect_t effect,
                                 const node_hw_led_effect_config_t *config)
{
    node_hw_led_strip_t *runtime = find_led_strip(strip);

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime || !runtime->configured) {
        return ESP_ERR_NOT_FOUND;
    }

    switch (effect) {
    case NODE_HW_LED_EFFECT_BLINK:
        return run_blink(runtime, config);
    case NODE_HW_LED_EFFECT_BREATHE:
        return run_breathe(runtime, config);
    case NODE_HW_LED_EFFECT_RAINBOW:
        return run_rainbow(runtime, config);
    case NODE_HW_LED_EFFECT_COLOR_WIPE:
        return run_color_wipe(runtime, config);
    case NODE_HW_LED_EFFECT_SCANNER:
        return run_scanner(runtime, config);
    case NODE_HW_LED_EFFECT_THEATER:
        return run_theater(runtime, config);
    case NODE_HW_LED_EFFECT_STROBE:
        return run_strobe(runtime, config);
    default:
        return ESP_ERR_INVALID_ARG;
    }
}
