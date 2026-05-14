#include "hardware_io.h"

#include "hardware_io_internal.h"

#include "esp_timer.h"

#define MOSFET_EFFECT_TICK_US 20000ULL

typedef enum {
    MOSFET_EFFECT_NONE = 0,
    MOSFET_EFFECT_BLINK,
    MOSFET_EFFECT_BREATHE,
} mosfet_effect_mode_t;

typedef struct {
    esp_timer_handle_t timer;
    bool active;
    mosfet_effect_mode_t mode;
    uint8_t value;
    uint8_t min_value;
    uint8_t max_value;
    uint8_t final_value;
    bool on_phase;
    bool hold_phase;
    uint32_t on_ms;
    uint32_t off_ms;
    uint32_t fade_ms;
    uint32_t hold_ms;
    uint32_t remaining;
    uint64_t phase_started_ms;
} mosfet_effect_t;

static mosfet_effect_t s_effects[HARDWARE_IO_MOSFET_CHANNEL_COUNT];

static bool mosfet_effect_channel_valid(uint8_t channel)
{
    return channel >= 1 && channel <= HARDWARE_IO_MOSFET_CHANNEL_COUNT;
}

static void mosfet_effect_clear_locked(mosfet_effect_t *effect)
{
    if (!effect) {
        return;
    }
    if (effect->timer) {
        (void)esp_timer_stop(effect->timer);
    }
    effect->active = false;
    effect->mode = MOSFET_EFFECT_NONE;
    effect->remaining = 0;
    effect->on_phase = false;
    effect->hold_phase = false;
}

void hardware_io_mosfet_effect_cancel_locked(uint8_t channel)
{
    if (!mosfet_effect_channel_valid(channel)) {
        return;
    }
    mosfet_effect_clear_locked(&s_effects[channel - 1]);
}

bool hardware_io_mosfet_effect_active_locked(uint8_t channel)
{
    if (!mosfet_effect_channel_valid(channel)) {
        return false;
    }
    return s_effects[channel - 1].active;
}

static void mosfet_effect_timer(void *arg)
{
    uint8_t channel = (uint8_t)(uintptr_t)arg;
    if (!mosfet_effect_channel_valid(channel) || hardware_io_lock() != ESP_OK) {
        return;
    }
    mosfet_effect_t *effect = &s_effects[channel - 1];
    if (!effect->active) {
        hardware_io_unlock();
        return;
    }

    if (effect->mode == MOSFET_EFFECT_BLINK) {
        if (effect->on_phase) {
            (void)hardware_io_mosfet_effect_write_locked(channel, 0);
            effect->on_phase = false;
            (void)esp_timer_start_once(effect->timer, (uint64_t)effect->off_ms * 1000ULL);
            hardware_io_unlock();
            return;
        }
        if (effect->remaining > 0 && effect->remaining != UINT32_MAX) {
            effect->remaining--;
        }
        if (effect->remaining == 0) {
            effect->active = false;
            effect->mode = MOSFET_EFFECT_NONE;
            (void)hardware_io_mosfet_effect_write_locked(channel, effect->final_value);
            hardware_io_unlock();
            return;
        }
        (void)hardware_io_mosfet_effect_write_locked(channel, effect->value);
        effect->on_phase = true;
        (void)esp_timer_start_once(effect->timer, (uint64_t)effect->on_ms * 1000ULL);
        hardware_io_unlock();
        return;
    }

    uint64_t elapsed_ms = hardware_io_now_ms() - effect->phase_started_ms;
    if (!effect->hold_phase && elapsed_ms < effect->fade_ms) {
        int32_t from = effect->on_phase ? effect->min_value : effect->max_value;
        int32_t to = effect->on_phase ? effect->max_value : effect->min_value;
        int32_t value = from + ((to - from) * (int32_t)elapsed_ms) / (int32_t)effect->fade_ms;
        (void)hardware_io_mosfet_effect_write_locked(channel, (uint8_t)value);
        hardware_io_unlock();
        return;
    }
    if (!effect->hold_phase) {
        (void)hardware_io_mosfet_effect_write_locked(channel,
                                                     effect->on_phase ? effect->max_value : effect->min_value);
        effect->hold_phase = true;
        effect->phase_started_ms = hardware_io_now_ms();
        hardware_io_unlock();
        return;
    }
    if (elapsed_ms < effect->hold_ms) {
        hardware_io_unlock();
        return;
    }
    if (!effect->on_phase) {
        if (effect->remaining > 0 && effect->remaining != UINT32_MAX) {
            effect->remaining--;
        }
        if (effect->remaining == 0) {
            effect->active = false;
            effect->mode = MOSFET_EFFECT_NONE;
            (void)hardware_io_mosfet_effect_write_locked(channel, effect->final_value);
            hardware_io_unlock();
            return;
        }
    }
    effect->on_phase = !effect->on_phase;
    effect->hold_phase = false;
    effect->phase_started_ms = hardware_io_now_ms();
    hardware_io_unlock();
}

esp_err_t hardware_io_mosfet_effects_init_locked(void)
{
    esp_err_t err = ESP_OK;
    for (uint8_t i = 0; i < HARDWARE_IO_MOSFET_CHANNEL_COUNT; ++i) {
        if (s_effects[i].timer) {
            continue;
        }
        esp_timer_create_args_t args = {
            .callback = mosfet_effect_timer,
            .arg = (void *)(uintptr_t)(i + 1),
            .name = "mosfet_fx",
        };
        err = esp_timer_create(&args, &s_effects[i].timer);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t hardware_io_mosfet_effect_blink(uint8_t channel,
                                          uint8_t value,
                                          uint32_t on_ms,
                                          uint32_t off_ms,
                                          uint32_t count,
                                          uint8_t final_value)
{
    if (!mosfet_effect_channel_valid(channel) || on_ms == 0 || off_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    mosfet_effect_t *effect = &s_effects[channel - 1];
    esp_err_t err = hardware_io_mosfet_stop_base_timers_locked(channel);
    if (err == ESP_OK && !effect->timer) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) {
        hardware_io_unlock();
        return err;
    }
    mosfet_effect_clear_locked(effect);
    effect->active = true;
    effect->mode = MOSFET_EFFECT_BLINK;
    effect->value = value;
    effect->final_value = final_value;
    effect->on_phase = true;
    effect->on_ms = on_ms;
    effect->off_ms = off_ms;
    effect->remaining = count == 0 ? UINT32_MAX : count;
    err = hardware_io_mosfet_effect_write_locked(channel, value);
    if (err == ESP_OK) {
        err = esp_timer_start_once(effect->timer, (uint64_t)on_ms * 1000ULL);
    }
    if (err != ESP_OK) {
        mosfet_effect_clear_locked(effect);
        (void)hardware_io_mosfet_effect_write_locked(channel, final_value);
    }
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_mosfet_effect_breathe(uint8_t channel,
                                            uint8_t min_value,
                                            uint8_t max_value,
                                            uint32_t fade_ms,
                                            uint32_t hold_ms,
                                            uint32_t count,
                                            uint8_t final_value)
{
    if (!mosfet_effect_channel_valid(channel) || fade_ms == 0 || min_value > max_value) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    mosfet_effect_t *effect = &s_effects[channel - 1];
    esp_err_t err = hardware_io_mosfet_stop_base_timers_locked(channel);
    if (err == ESP_OK && !effect->timer) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) {
        hardware_io_unlock();
        return err;
    }
    mosfet_effect_clear_locked(effect);
    effect->active = true;
    effect->mode = MOSFET_EFFECT_BREATHE;
    effect->min_value = min_value;
    effect->max_value = max_value;
    effect->final_value = final_value;
    effect->fade_ms = fade_ms;
    effect->hold_ms = hold_ms;
    effect->remaining = count == 0 ? UINT32_MAX : count;
    effect->on_phase = true;
    effect->hold_phase = false;
    effect->phase_started_ms = hardware_io_now_ms();
    err = hardware_io_mosfet_effect_write_locked(channel, min_value);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(effect->timer, MOSFET_EFFECT_TICK_US);
    }
    if (err != ESP_OK) {
        mosfet_effect_clear_locked(effect);
        (void)hardware_io_mosfet_effect_write_locked(channel, final_value);
    }
    hardware_io_unlock();
    return err;
}
