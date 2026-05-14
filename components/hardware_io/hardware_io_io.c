#include "hardware_io_io_internal.h"

#include <string.h>

#include "driver/gpio.h"
#include "sdkconfig.h"

#ifndef CONFIG_SCENEHUB_GPIO1_GPIO
#define CONFIG_SCENEHUB_GPIO1_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_GPIO2_GPIO
#define CONFIG_SCENEHUB_GPIO2_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_GPIO3_GPIO
#define CONFIG_SCENEHUB_GPIO3_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_GPIO4_GPIO
#define CONFIG_SCENEHUB_GPIO4_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_GPIO1_MODE
#define CONFIG_SCENEHUB_GPIO1_MODE 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO2_MODE
#define CONFIG_SCENEHUB_GPIO2_MODE 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO3_MODE
#define CONFIG_SCENEHUB_GPIO3_MODE 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO4_MODE
#define CONFIG_SCENEHUB_GPIO4_MODE 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO_ACTIVE_LOW
#define CONFIG_SCENEHUB_GPIO_ACTIVE_LOW 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO_PULLUP
#define CONFIG_SCENEHUB_GPIO_PULLUP 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO_PULLDOWN
#define CONFIG_SCENEHUB_GPIO_PULLDOWN 0
#endif

hardware_io_io_t s_ios[HARDWARE_IO_IO_CHANNEL_COUNT] = {
    {.gpio = CONFIG_SCENEHUB_GPIO1_GPIO, .mode = CONFIG_SCENEHUB_GPIO1_MODE, .active_low = CONFIG_SCENEHUB_GPIO_ACTIVE_LOW},
    {.gpio = CONFIG_SCENEHUB_GPIO2_GPIO, .mode = CONFIG_SCENEHUB_GPIO2_MODE, .active_low = CONFIG_SCENEHUB_GPIO_ACTIVE_LOW},
    {.gpio = CONFIG_SCENEHUB_GPIO3_GPIO, .mode = CONFIG_SCENEHUB_GPIO3_MODE, .active_low = CONFIG_SCENEHUB_GPIO_ACTIVE_LOW},
    {.gpio = CONFIG_SCENEHUB_GPIO4_GPIO, .mode = CONFIG_SCENEHUB_GPIO4_MODE, .active_low = CONFIG_SCENEHUB_GPIO_ACTIVE_LOW},
};
esp_timer_handle_t s_io_poll_timer = NULL;

bool hardware_io_io_channel_valid(uint8_t channel)
{
    return channel >= 1 && channel <= HARDWARE_IO_IO_CHANNEL_COUNT;
}

bool hardware_io_io_mode_valid(hardware_io_io_mode_t mode)
{
    return mode == HARDWARE_IO_IO_MODE_DISABLED ||
           mode == HARDWARE_IO_IO_MODE_INPUT ||
           mode == HARDWARE_IO_IO_MODE_OUTPUT;
}

bool hardware_io_io_active_from_level(const hardware_io_io_t *gpio, bool physical_high)
{
    return gpio && gpio->active_low ? !physical_high : physical_high;
}

int hardware_io_io_level_from_active(const hardware_io_io_t *gpio, bool active)
{
    return gpio && gpio->active_low ? (active ? 0 : 1) : (active ? 1 : 0);
}

esp_err_t hardware_io_io_write_locked(uint8_t channel, bool active)
{
    hardware_io_io_t *line = NULL;
    if (!hardware_io_io_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    line = &s_ios[channel - 1];
    if (!line->enabled || line->mode != HARDWARE_IO_IO_MODE_OUTPUT) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = gpio_set_level((gpio_num_t)line->gpio, hardware_io_io_level_from_active(line, active));
    if (err != ESP_OK) {
        return err;
    }
    line->active = active;
    line->physical_high = hardware_io_io_level_from_active(line, active) != 0;
    line->last_change_ms = hardware_io_now_ms();
    return ESP_OK;
}

static esp_err_t io_configure_channel_locked(uint8_t channel, hardware_io_io_mode_t mode)
{
    hardware_io_io_t *line = NULL;
    esp_err_t err = ESP_OK;
    uint64_t now_ms = hardware_io_now_ms();
    if (!hardware_io_io_channel_valid(channel) || !hardware_io_io_mode_valid(mode)) {
        return ESP_ERR_INVALID_ARG;
    }
    line = &s_ios[channel - 1];
    hardware_io_io_stop_pulse_locked(line);
    hardware_io_io_stop_effect_locked(line);
    if (line->enabled && line->mode == HARDWARE_IO_IO_MODE_OUTPUT) {
        (void)hardware_io_io_write_locked(channel, false);
    }
    line->mode = mode;
    line->last_change_ms = now_ms;
    if (mode == HARDWARE_IO_IO_MODE_DISABLED) {
        if (line->gpio >= 0 && GPIO_IS_VALID_GPIO(line->gpio)) {
            (void)gpio_reset_pin((gpio_num_t)line->gpio);
        }
        line->enabled = false;
        line->physical_high = false;
        line->active = false;
        line->candidate_physical_high = false;
        line->candidate_since_ms = now_ms;
        hardware_io_io_stop_poll_if_unused_locked();
        return ESP_OK;
    }
    if (line->gpio < 0 || !GPIO_IS_VALID_GPIO(line->gpio)) {
        line->enabled = false;
        return ESP_ERR_INVALID_ARG;
    }
    if (mode == HARDWARE_IO_IO_MODE_OUTPUT) {
        int safe_level = 0;
        if (!GPIO_IS_VALID_OUTPUT_GPIO(line->gpio)) {
            line->enabled = false;
            return ESP_ERR_INVALID_ARG;
        }
        safe_level = hardware_io_io_level_from_active(line, false);
        err = gpio_set_level((gpio_num_t)line->gpio, safe_level);
        if (err != ESP_OK) {
            line->enabled = false;
            return err;
        }
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << line->gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&cfg);
        if (err != ESP_OK) {
            line->enabled = false;
            return err;
        }
        err = gpio_set_level((gpio_num_t)line->gpio, safe_level);
        if (err != ESP_OK) {
            line->enabled = false;
            return err;
        }
        err = hardware_io_io_ensure_output_timers_locked(channel, line);
        if (err != ESP_OK) {
            line->enabled = false;
            return err;
        }
        line->enabled = true;
        line->physical_high = safe_level != 0;
        line->active = false;
        line->candidate_physical_high = line->physical_high;
        line->candidate_since_ms = now_ms;
        hardware_io_io_stop_poll_if_unused_locked();
        return ESP_OK;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << line->gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = CONFIG_SCENEHUB_GPIO_PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = CONFIG_SCENEHUB_GPIO_PULLDOWN ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&cfg);
    if (err != ESP_OK) {
        line->enabled = false;
        return err;
    }
    line->physical_high = gpio_get_level((gpio_num_t)line->gpio) != 0;
    line->active = hardware_io_io_active_from_level(line, line->physical_high);
    line->candidate_physical_high = line->physical_high;
    line->candidate_since_ms = now_ms;
    line->enabled = true;
    return hardware_io_io_ensure_poll_timer_locked();
}

esp_err_t hardware_io_io_init_locked(void)
{
    esp_err_t err = ESP_OK;
    bool any_input = false;
    for (uint8_t i = 0; i < HARDWARE_IO_IO_CHANNEL_COUNT; ++i) {
        hardware_io_io_t *line = &s_ios[i];
        if (!hardware_io_io_mode_valid(line->mode)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (line->mode == HARDWARE_IO_IO_MODE_DISABLED) {
            line->enabled = false;
            line->active = false;
            line->physical_high = false;
            continue;
        }
        if (line->gpio < 0 || !GPIO_IS_VALID_GPIO(line->gpio)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (line->mode == HARDWARE_IO_IO_MODE_OUTPUT) {
            int safe_level = 0;
            if (!GPIO_IS_VALID_OUTPUT_GPIO(line->gpio)) {
                return ESP_ERR_INVALID_ARG;
            }
            safe_level = hardware_io_io_level_from_active(line, false);
            err = gpio_set_level((gpio_num_t)line->gpio, safe_level);
            if (err != ESP_OK) {
                return err;
            }
            gpio_config_t cfg = {
                .pin_bit_mask = 1ULL << line->gpio,
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            err = gpio_config(&cfg);
            if (err != ESP_OK) {
                return err;
            }
            err = gpio_set_level((gpio_num_t)line->gpio, safe_level);
            if (err != ESP_OK) {
                return err;
            }
            err = hardware_io_io_ensure_output_timers_locked((uint8_t)(i + 1), line);
            if (err != ESP_OK) {
                return err;
            }
            line->physical_high = safe_level != 0;
            line->active = false;
        } else {
            gpio_config_t cfg = {
                .pin_bit_mask = 1ULL << line->gpio,
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = CONFIG_SCENEHUB_GPIO_PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
                .pull_down_en = CONFIG_SCENEHUB_GPIO_PULLDOWN ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            err = gpio_config(&cfg);
            if (err != ESP_OK) {
                return err;
            }
            line->physical_high = gpio_get_level((gpio_num_t)line->gpio) != 0;
            line->active = hardware_io_io_active_from_level(line, line->physical_high);
            line->candidate_physical_high = line->physical_high;
            line->candidate_since_ms = hardware_io_now_ms();
            any_input = true;
        }
        line->last_change_ms = hardware_io_now_ms();
        line->enabled = true;
    }
    return any_input ? hardware_io_io_ensure_poll_timer_locked() : ESP_OK;
}

esp_err_t hardware_io_io_set_mode(uint8_t channel, hardware_io_io_mode_t mode)
{
    esp_err_t err = ESP_OK;
    if (!hardware_io_io_channel_valid(channel) || !hardware_io_io_mode_valid(mode)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    err = io_configure_channel_locked(channel, mode);
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_io_set(uint8_t channel, bool active)
{
    esp_err_t err = ESP_OK;
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (hardware_io_io_channel_valid(channel)) {
        hardware_io_io_stop_pulse_locked(&s_ios[channel - 1]);
        hardware_io_io_stop_effect_locked(&s_ios[channel - 1]);
    }
    err = hardware_io_io_write_locked(channel, active);
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_io_toggle(uint8_t channel)
{
    esp_err_t err = ESP_OK;
    bool next = false;
    if (!hardware_io_io_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    hardware_io_io_stop_pulse_locked(&s_ios[channel - 1]);
    hardware_io_io_stop_effect_locked(&s_ios[channel - 1]);
    next = !s_ios[channel - 1].active;
    err = hardware_io_io_write_locked(channel, next);
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_io_get(uint8_t channel, bool *out_active, bool *out_physical_high)
{
    if (!hardware_io_io_channel_valid(channel) || (!out_active && !out_physical_high)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    const hardware_io_io_t *line = &s_ios[channel - 1];
    if (!line->enabled) {
        hardware_io_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (out_active) {
        *out_active = line->active;
    }
    if (out_physical_high) {
        *out_physical_high = line->physical_high;
    }
    hardware_io_unlock();
    return ESP_OK;
}

esp_err_t hardware_io_io_get_status(hardware_io_io_status_t *out,
                                    size_t max_count,
                                    size_t *out_count)
{
    size_t count = HARDWARE_IO_IO_CHANNEL_COUNT;
    if (!out || max_count < count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < count; ++i) {
        const hardware_io_io_t *line = &s_ios[i];
        out[i] = (hardware_io_io_status_t) {
            .channel = (uint8_t)(i + 1),
            .gpio = line->gpio,
            .enabled = line->enabled,
            .mode = line->mode,
            .active_low = line->active_low,
            .physical_high = line->physical_high,
            .active = line->active,
            .pulse_active = line->pulse_active,
            .effect_active = line->effect_active,
            .last_change_ms = line->last_change_ms,
        };
    }
    hardware_io_unlock();
    if (out_count) {
        *out_count = count;
    }
    return ESP_OK;
}
