#include "hardware_io.h"

#include "hardware_io_internal.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#ifndef CONFIG_SCENEHUB_RELAY1_GPIO
#define CONFIG_SCENEHUB_RELAY1_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_RELAY2_GPIO
#define CONFIG_SCENEHUB_RELAY2_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_RELAY3_GPIO
#define CONFIG_SCENEHUB_RELAY3_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_RELAY4_GPIO
#define CONFIG_SCENEHUB_RELAY4_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_RELAY_ACTIVE_LOW
#define CONFIG_SCENEHUB_RELAY_ACTIVE_LOW 1
#endif

typedef struct {
    int gpio;
    bool active_low;
    bool enabled;
    bool on;
    esp_timer_handle_t pulse_timer;
} hardware_io_relay_t;

static hardware_io_relay_t s_relays[HARDWARE_IO_RELAY_CHANNEL_COUNT] = {
    {.gpio = CONFIG_SCENEHUB_RELAY1_GPIO, .active_low = CONFIG_SCENEHUB_RELAY_ACTIVE_LOW},
    {.gpio = CONFIG_SCENEHUB_RELAY2_GPIO, .active_low = CONFIG_SCENEHUB_RELAY_ACTIVE_LOW},
    {.gpio = CONFIG_SCENEHUB_RELAY3_GPIO, .active_low = CONFIG_SCENEHUB_RELAY_ACTIVE_LOW},
    {.gpio = CONFIG_SCENEHUB_RELAY4_GPIO, .active_low = CONFIG_SCENEHUB_RELAY_ACTIVE_LOW},
};

static bool hardware_io_relay_channel_valid(uint8_t channel)
{
    return channel >= 1 && channel <= HARDWARE_IO_RELAY_CHANNEL_COUNT;
}

static esp_err_t hardware_io_relay_write_locked(uint8_t channel, bool on)
{
    hardware_io_relay_t *relay = NULL;
    if (!hardware_io_relay_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    relay = &s_relays[channel - 1];
    if (!relay->enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    int level = relay->active_low ? (on ? 0 : 1) : (on ? 1 : 0);
    esp_err_t err = gpio_set_level((gpio_num_t)relay->gpio, level);
    if (err != ESP_OK) {
        return err;
    }
    relay->on = on;
    return ESP_OK;
}

static void hardware_io_relay_stop_pulse_locked(hardware_io_relay_t *relay)
{
    if (relay && relay->pulse_timer) {
        (void)esp_timer_stop(relay->pulse_timer);
    }
}

static void hardware_io_relay_pulse_timer(void *arg)
{
    uint32_t channel = (uint32_t)(uintptr_t)arg;
    if (hardware_io_lock() != ESP_OK) {
        return;
    }
    (void)hardware_io_relay_write_locked((uint8_t)channel, false);
    hardware_io_unlock();
}

esp_err_t hardware_io_relay_init_locked(void)
{
    esp_err_t err = ESP_OK;
    for (uint8_t i = 0; i < HARDWARE_IO_RELAY_CHANNEL_COUNT; ++i) {
        hardware_io_relay_t *relay = &s_relays[i];
        if (relay->gpio < 0) {
            relay->enabled = false;
            relay->on = false;
            continue;
        }
        if (!GPIO_IS_VALID_OUTPUT_GPIO(relay->gpio)) {
            return ESP_ERR_INVALID_ARG;
        }
        int safe_level = relay->active_low ? 1 : 0;
        err = gpio_set_level((gpio_num_t)relay->gpio, safe_level);
        if (err != ESP_OK) {
            return err;
        }
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << relay->gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&cfg);
        if (err != ESP_OK) {
            return err;
        }
        err = gpio_set_level((gpio_num_t)relay->gpio, safe_level);
        if (err != ESP_OK) {
            return err;
        }
        relay->enabled = true;
        relay->on = false;
        esp_timer_create_args_t timer_args = {
            .callback = hardware_io_relay_pulse_timer,
            .arg = (void *)(uintptr_t)(i + 1),
            .name = "relay_pulse",
        };
        err = esp_timer_create(&timer_args, &relay->pulse_timer);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t hardware_io_relay_set(uint8_t channel, bool on)
{
    esp_err_t err = ESP_OK;
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (hardware_io_relay_channel_valid(channel)) {
        hardware_io_relay_stop_pulse_locked(&s_relays[channel - 1]);
    }
    err = hardware_io_relay_write_locked(channel, on);
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_relay_toggle(uint8_t channel)
{
    esp_err_t err = ESP_OK;
    bool next = false;
    if (!hardware_io_relay_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    hardware_io_relay_stop_pulse_locked(&s_relays[channel - 1]);
    next = !s_relays[channel - 1].on;
    err = hardware_io_relay_write_locked(channel, next);
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_relay_pulse(uint8_t channel, uint32_t duration_ms)
{
    hardware_io_relay_t *relay = NULL;
    esp_err_t err = ESP_OK;
    if (!hardware_io_relay_channel_valid(channel) || duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    relay = &s_relays[channel - 1];
    if (!relay->enabled || !relay->pulse_timer) {
        hardware_io_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    hardware_io_relay_stop_pulse_locked(relay);
    err = hardware_io_relay_write_locked(channel, true);
    if (err == ESP_OK) {
        err = esp_timer_start_once(relay->pulse_timer, (uint64_t)duration_ms * 1000ULL);
        if (err != ESP_OK) {
            (void)hardware_io_relay_write_locked(channel, false);
        }
    }
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_relay_get(uint8_t channel, bool *out_on)
{
    if (!hardware_io_relay_channel_valid(channel) || !out_on) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_relays[channel - 1].enabled) {
        hardware_io_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    *out_on = s_relays[channel - 1].on;
    hardware_io_unlock();
    return ESP_OK;
}

esp_err_t hardware_io_relay_safe_off_all_locked(void)
{
    esp_err_t first_err = ESP_OK;
    for (uint8_t i = 0; i < HARDWARE_IO_RELAY_CHANNEL_COUNT; ++i) {
        hardware_io_relay_t *relay = &s_relays[i];
        if (!relay->enabled) {
            continue;
        }
        hardware_io_relay_stop_pulse_locked(relay);
        esp_err_t err = hardware_io_relay_write_locked((uint8_t)(i + 1), false);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}

esp_err_t hardware_io_relay_get_status(hardware_io_relay_status_t *out,
                                       size_t max_count,
                                       size_t *out_count)
{
    size_t count = HARDWARE_IO_RELAY_CHANNEL_COUNT;
    if (!out || max_count < count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < count; ++i) {
        const hardware_io_relay_t *relay = &s_relays[i];
        out[i] = (hardware_io_relay_status_t) {
            .channel = (uint8_t)(i + 1),
            .gpio = relay->gpio,
            .enabled = relay->enabled,
            .active_low = relay->active_low,
            .on = relay->on,
        };
    }
    hardware_io_unlock();
    if (out_count) {
        *out_count = count;
    }
    return ESP_OK;
}
