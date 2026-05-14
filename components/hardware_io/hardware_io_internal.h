#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

esp_err_t hardware_io_lock(void);
void hardware_io_unlock(void);
uint64_t hardware_io_now_ms(void);

esp_err_t hardware_io_relay_init_locked(void);
esp_err_t hardware_io_mosfet_init_locked(void);
esp_err_t hardware_io_mosfet_effects_init_locked(void);
esp_err_t hardware_io_io_init_locked(void);
esp_err_t hardware_io_relay_safe_off_all_locked(void);
esp_err_t hardware_io_mosfet_safe_off_all_locked(void);
esp_err_t hardware_io_io_safe_off_all_locked(void);
esp_err_t hardware_io_mosfet_effect_write_locked(uint8_t channel, uint8_t value);
esp_err_t hardware_io_mosfet_stop_base_timers_locked(uint8_t channel);
void hardware_io_mosfet_effect_cancel_locked(uint8_t channel);
bool hardware_io_mosfet_effect_active_locked(uint8_t channel);
