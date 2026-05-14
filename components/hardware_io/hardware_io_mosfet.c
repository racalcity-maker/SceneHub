#include "hardware_io.h"

#include "hardware_io_internal.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#ifndef CONFIG_SCENEHUB_MOSFET1_GPIO
#define CONFIG_SCENEHUB_MOSFET1_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_MOSFET2_GPIO
#define CONFIG_SCENEHUB_MOSFET2_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_MOSFET3_GPIO
#define CONFIG_SCENEHUB_MOSFET3_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_MOSFET4_GPIO
#define CONFIG_SCENEHUB_MOSFET4_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_MOSFET_PWM_FREQ_HZ
#define CONFIG_SCENEHUB_MOSFET_PWM_FREQ_HZ 1000
#endif

#define HARDWARE_IO_MOSFET_FADE_TICK_US 20000ULL

typedef struct {
    int gpio;
    ledc_channel_t ledc_channel;
    bool enabled;
    uint8_t value;
    uint8_t pulse_restore_value;
    uint8_t fade_from;
    uint8_t fade_target;
    uint32_t fade_duration_ms;
    uint64_t fade_started_ms;
    esp_timer_handle_t pulse_timer;
    esp_timer_handle_t fade_timer;
} hardware_io_mosfet_t;

static hardware_io_mosfet_t s_mosfets[HARDWARE_IO_MOSFET_CHANNEL_COUNT] = {
    {.gpio = CONFIG_SCENEHUB_MOSFET1_GPIO, .ledc_channel = LEDC_CHANNEL_0},
    {.gpio = CONFIG_SCENEHUB_MOSFET2_GPIO, .ledc_channel = LEDC_CHANNEL_1},
    {.gpio = CONFIG_SCENEHUB_MOSFET3_GPIO, .ledc_channel = LEDC_CHANNEL_2},
    {.gpio = CONFIG_SCENEHUB_MOSFET4_GPIO, .ledc_channel = LEDC_CHANNEL_3},
};

static bool hardware_io_mosfet_channel_valid(uint8_t channel)
{
    return channel >= 1 && channel <= HARDWARE_IO_MOSFET_CHANNEL_COUNT;
}

static esp_err_t hardware_io_mosfet_write_locked(uint8_t channel, uint8_t value)
{
    hardware_io_mosfet_t *mosfet = NULL;
    esp_err_t err = ESP_OK;
    if (!hardware_io_mosfet_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    mosfet = &s_mosfets[channel - 1];
    if (!mosfet->enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    err = ledc_set_duty(LEDC_LOW_SPEED_MODE, mosfet->ledc_channel, value);
    if (err != ESP_OK) {
        return err;
    }
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, mosfet->ledc_channel);
    if (err != ESP_OK) {
        return err;
    }
    mosfet->value = value;
    return ESP_OK;
}

static void hardware_io_mosfet_pulse_timer(void *arg)
{
    uint32_t channel = (uint32_t)(uintptr_t)arg;
    hardware_io_mosfet_t *mosfet = NULL;
    if (!hardware_io_mosfet_channel_valid((uint8_t)channel) || hardware_io_lock() != ESP_OK) {
        return;
    }
    mosfet = &s_mosfets[channel - 1];
    (void)hardware_io_mosfet_write_locked((uint8_t)channel, mosfet->pulse_restore_value);
    hardware_io_unlock();
}

static void hardware_io_mosfet_fade_timer(void *arg)
{
    uint32_t channel = (uint32_t)(uintptr_t)arg;
    hardware_io_mosfet_t *mosfet = NULL;
    uint64_t elapsed_ms = 0;
    uint8_t value = 0;
    if (!hardware_io_mosfet_channel_valid((uint8_t)channel) || hardware_io_lock() != ESP_OK) {
        return;
    }
    mosfet = &s_mosfets[channel - 1];
    elapsed_ms = hardware_io_now_ms() - mosfet->fade_started_ms;
    if (elapsed_ms >= mosfet->fade_duration_ms || mosfet->fade_duration_ms == 0) {
        (void)hardware_io_mosfet_write_locked((uint8_t)channel, mosfet->fade_target);
        (void)esp_timer_stop(mosfet->fade_timer);
        hardware_io_unlock();
        return;
    }
    int32_t delta = (int32_t)mosfet->fade_target - (int32_t)mosfet->fade_from;
    value = (uint8_t)((int32_t)mosfet->fade_from +
                      (delta * (int32_t)elapsed_ms) / (int32_t)mosfet->fade_duration_ms);
    (void)hardware_io_mosfet_write_locked((uint8_t)channel, value);
    hardware_io_unlock();
}

static bool hardware_io_has_mosfet_channels(void)
{
    for (uint8_t i = 0; i < HARDWARE_IO_MOSFET_CHANNEL_COUNT; ++i) {
        if (s_mosfets[i].gpio >= 0) {
            return true;
        }
    }
    return false;
}

esp_err_t hardware_io_mosfet_init_locked(void)
{
    esp_err_t err = ESP_OK;
    if (!hardware_io_has_mosfet_channels()) {
        return ESP_OK;
    }
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = CONFIG_SCENEHUB_MOSFET_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < HARDWARE_IO_MOSFET_CHANNEL_COUNT; ++i) {
        hardware_io_mosfet_t *mosfet = &s_mosfets[i];
        if (mosfet->gpio < 0) {
            mosfet->enabled = false;
            mosfet->value = 0;
            continue;
        }
        if (!GPIO_IS_VALID_OUTPUT_GPIO(mosfet->gpio)) {
            return ESP_ERR_INVALID_ARG;
        }
        ledc_channel_config_t channel_cfg = {
            .gpio_num = mosfet->gpio,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = mosfet->ledc_channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        };
        err = ledc_channel_config(&channel_cfg);
        if (err != ESP_OK) {
            return err;
        }
        mosfet->enabled = true;
        mosfet->value = 0;
        esp_timer_create_args_t pulse_args = {
            .callback = hardware_io_mosfet_pulse_timer,
            .arg = (void *)(uintptr_t)(i + 1),
            .name = "mosfet_pulse",
        };
        err = esp_timer_create(&pulse_args, &mosfet->pulse_timer);
        if (err != ESP_OK) {
            return err;
        }
        esp_timer_create_args_t fade_args = {
            .callback = hardware_io_mosfet_fade_timer,
            .arg = (void *)(uintptr_t)(i + 1),
            .name = "mosfet_fade",
        };
        err = esp_timer_create(&fade_args, &mosfet->fade_timer);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t hardware_io_mosfet_set(uint8_t channel, uint8_t value)
{
    hardware_io_mosfet_t *mosfet = NULL;
    esp_err_t err = ESP_OK;
    if (!hardware_io_mosfet_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    mosfet = &s_mosfets[channel - 1];
    if (mosfet->pulse_timer) {
        (void)esp_timer_stop(mosfet->pulse_timer);
    }
    if (mosfet->fade_timer) {
        (void)esp_timer_stop(mosfet->fade_timer);
    }
    hardware_io_mosfet_effect_cancel_locked(channel);
    err = hardware_io_mosfet_write_locked(channel, value);
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_mosfet_fade(uint8_t channel, uint8_t target, uint32_t duration_ms)
{
    hardware_io_mosfet_t *mosfet = NULL;
    esp_err_t err = ESP_OK;
    if (!hardware_io_mosfet_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (duration_ms == 0) {
        return hardware_io_mosfet_set(channel, target);
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    mosfet = &s_mosfets[channel - 1];
    if (!mosfet->enabled || !mosfet->fade_timer) {
        hardware_io_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (mosfet->pulse_timer) {
        (void)esp_timer_stop(mosfet->pulse_timer);
    }
    hardware_io_mosfet_effect_cancel_locked(channel);
    (void)esp_timer_stop(mosfet->fade_timer);
    mosfet->fade_from = mosfet->value;
    mosfet->fade_target = target;
    mosfet->fade_duration_ms = duration_ms;
    mosfet->fade_started_ms = hardware_io_now_ms();
    err = esp_timer_start_periodic(mosfet->fade_timer, HARDWARE_IO_MOSFET_FADE_TICK_US);
    if (err != ESP_OK) {
        hardware_io_unlock();
        return err;
    }
    hardware_io_unlock();
    return ESP_OK;
}

esp_err_t hardware_io_mosfet_pulse(uint8_t channel, uint8_t value, uint32_t duration_ms)
{
    hardware_io_mosfet_t *mosfet = NULL;
    esp_err_t err = ESP_OK;
    if (!hardware_io_mosfet_channel_valid(channel) || duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    mosfet = &s_mosfets[channel - 1];
    if (!mosfet->enabled || !mosfet->pulse_timer) {
        hardware_io_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (mosfet->fade_timer) {
        (void)esp_timer_stop(mosfet->fade_timer);
    }
    hardware_io_mosfet_effect_cancel_locked(channel);
    (void)esp_timer_stop(mosfet->pulse_timer);
    mosfet->pulse_restore_value = mosfet->value;
    err = hardware_io_mosfet_write_locked(channel, value);
    if (err == ESP_OK) {
        err = esp_timer_start_once(mosfet->pulse_timer, (uint64_t)duration_ms * 1000ULL);
        if (err != ESP_OK) {
            (void)hardware_io_mosfet_write_locked(channel, mosfet->pulse_restore_value);
        }
    }
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_mosfet_effect_write_locked(uint8_t channel, uint8_t value)
{
    if (!hardware_io_mosfet_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    return hardware_io_mosfet_write_locked(channel, value);
}

esp_err_t hardware_io_mosfet_stop_base_timers_locked(uint8_t channel)
{
    if (!hardware_io_mosfet_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    hardware_io_mosfet_t *mosfet = &s_mosfets[channel - 1];
    if (!mosfet->enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mosfet->pulse_timer) {
        (void)esp_timer_stop(mosfet->pulse_timer);
    }
    if (mosfet->fade_timer) {
        (void)esp_timer_stop(mosfet->fade_timer);
    }
    return ESP_OK;
}

esp_err_t hardware_io_mosfet_get(uint8_t channel, uint8_t *out_value)
{
    if (!hardware_io_mosfet_channel_valid(channel) || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_mosfets[channel - 1].enabled) {
        hardware_io_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    *out_value = s_mosfets[channel - 1].value;
    hardware_io_unlock();
    return ESP_OK;
}

esp_err_t hardware_io_mosfet_safe_off_all_locked(void)
{
    esp_err_t first_err = ESP_OK;
    for (uint8_t i = 0; i < HARDWARE_IO_MOSFET_CHANNEL_COUNT; ++i) {
        hardware_io_mosfet_t *mosfet = &s_mosfets[i];
        if (!mosfet->enabled) {
            continue;
        }
        if (mosfet->pulse_timer) {
            (void)esp_timer_stop(mosfet->pulse_timer);
        }
        if (mosfet->fade_timer) {
            (void)esp_timer_stop(mosfet->fade_timer);
        }
        hardware_io_mosfet_effect_cancel_locked((uint8_t)(i + 1));
        esp_err_t err = hardware_io_mosfet_write_locked((uint8_t)(i + 1), 0);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}

esp_err_t hardware_io_mosfet_all_off(void)
{
    esp_err_t err = ESP_OK;
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    err = hardware_io_mosfet_safe_off_all_locked();
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_mosfet_get_status(hardware_io_mosfet_status_t *out,
                                        size_t max_count,
                                        size_t *out_count)
{
    size_t count = HARDWARE_IO_MOSFET_CHANNEL_COUNT;
    if (!out || max_count < count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < count; ++i) {
        const hardware_io_mosfet_t *mosfet = &s_mosfets[i];
        out[i] = (hardware_io_mosfet_status_t) {
            .channel = (uint8_t)(i + 1),
            .gpio = mosfet->gpio,
            .enabled = mosfet->enabled,
            .value = mosfet->value,
            .pwm_freq_hz = CONFIG_SCENEHUB_MOSFET_PWM_FREQ_HZ,
            .pulse_active = mosfet->pulse_timer && esp_timer_is_active(mosfet->pulse_timer),
            .fade_active = mosfet->fade_timer && esp_timer_is_active(mosfet->fade_timer),
            .effect_active = hardware_io_mosfet_effect_active_locked((uint8_t)(i + 1)),
        };
    }
    hardware_io_unlock();
    if (out_count) {
        *out_count = count;
    }
    return ESP_OK;
}
