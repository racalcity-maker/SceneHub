#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t hardware_io_lock(void);
void hardware_io_unlock(void);
uint64_t hardware_io_now_ms(void);

esp_err_t hardware_io_relay_init_locked(void);
esp_err_t hardware_io_mosfet_init_locked(void);
esp_err_t hardware_io_input_init_locked(void);
esp_err_t hardware_io_gpio_init_locked(void);
esp_err_t hardware_io_relay_safe_off_all_locked(void);
esp_err_t hardware_io_mosfet_safe_off_all_locked(void);
esp_err_t hardware_io_gpio_safe_off_all_locked(void);
