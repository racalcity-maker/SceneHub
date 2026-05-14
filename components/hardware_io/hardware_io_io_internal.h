#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_timer.h"

#include "hardware_io.h"
#include "hardware_io_internal.h"

typedef struct {
    int gpio;
    hardware_io_io_mode_t mode;
    bool active_low;
    bool enabled;
    bool physical_high;
    bool active;
    bool candidate_physical_high;
    uint64_t candidate_since_ms;
    uint64_t last_change_ms;
    esp_timer_handle_t pulse_timer;
    esp_timer_handle_t effect_timer;
    bool pulse_active;
    bool pulse_restore_active;
    bool effect_active;
    bool effect_on_phase;
    uint32_t effect_on_ms;
    uint32_t effect_off_ms;
    uint32_t effect_remaining;
    bool effect_final_active;
} hardware_io_io_t;

typedef struct {
    uint8_t channel;
    bool old_active;
    bool physical_high;
    bool active;
} io_change_t;

extern hardware_io_io_t s_ios[HARDWARE_IO_IO_CHANNEL_COUNT];
extern esp_timer_handle_t s_io_poll_timer;

bool hardware_io_io_channel_valid(uint8_t channel);
bool hardware_io_io_mode_valid(hardware_io_io_mode_t mode);
bool hardware_io_io_active_from_level(const hardware_io_io_t *gpio, bool physical_high);
int hardware_io_io_level_from_active(const hardware_io_io_t *gpio, bool active);
esp_err_t hardware_io_io_write_locked(uint8_t channel, bool active);
void hardware_io_io_stop_pulse_locked(hardware_io_io_t *line);
void hardware_io_io_stop_effect_locked(hardware_io_io_t *line);
esp_err_t hardware_io_io_ensure_poll_timer_locked(void);
void hardware_io_io_stop_poll_if_unused_locked(void);
esp_err_t hardware_io_io_ensure_output_timers_locked(uint8_t channel, hardware_io_io_t *line);
