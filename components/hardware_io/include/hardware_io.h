#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HARDWARE_IO_RELAY_CHANNEL_COUNT 4
#define HARDWARE_IO_MOSFET_CHANNEL_COUNT 4
#define HARDWARE_IO_INPUT_CHANNEL_COUNT 4
#define HARDWARE_IO_GPIO_CHANNEL_COUNT 4
#define HARDWARE_IO_MOSFET_MAX_VALUE 255

typedef struct {
    uint8_t channel;
    int gpio;
    bool enabled;
    bool active_low;
    bool on;
} hardware_io_relay_status_t;

typedef struct {
    uint8_t channel;
    int gpio;
    bool enabled;
    uint8_t value;
    uint32_t pwm_freq_hz;
    bool pulse_active;
    bool fade_active;
} hardware_io_mosfet_status_t;

typedef struct {
    uint8_t channel;
    int gpio;
    bool enabled;
    bool active_low;
    bool physical_high;
    bool active;
    uint32_t debounce_ms;
    uint64_t last_change_ms;
} hardware_io_input_status_t;

typedef enum {
    HARDWARE_IO_GPIO_MODE_DISABLED = 0,
    HARDWARE_IO_GPIO_MODE_INPUT = 1,
    HARDWARE_IO_GPIO_MODE_OUTPUT = 2,
} hardware_io_gpio_mode_t;

typedef struct {
    uint8_t channel;
    int gpio;
    bool enabled;
    hardware_io_gpio_mode_t mode;
    bool active_low;
    bool physical_high;
    bool active;
    bool pulse_active;
    uint64_t last_change_ms;
} hardware_io_gpio_status_t;

esp_err_t hardware_io_init(void);
bool hardware_io_is_available(void);
esp_err_t hardware_io_safe_off_all(void);

esp_err_t hardware_io_relay_set(uint8_t channel, bool on);
esp_err_t hardware_io_relay_toggle(uint8_t channel);
esp_err_t hardware_io_relay_pulse(uint8_t channel, uint32_t duration_ms);
esp_err_t hardware_io_relay_get(uint8_t channel, bool *out_on);
esp_err_t hardware_io_relay_get_status(hardware_io_relay_status_t *out,
                                       size_t max_count,
                                       size_t *out_count);

esp_err_t hardware_io_mosfet_set(uint8_t channel, uint8_t value);
esp_err_t hardware_io_mosfet_fade(uint8_t channel, uint8_t target, uint32_t duration_ms);
esp_err_t hardware_io_mosfet_pulse(uint8_t channel, uint8_t value, uint32_t duration_ms);
esp_err_t hardware_io_mosfet_all_off(void);
esp_err_t hardware_io_mosfet_get(uint8_t channel, uint8_t *out_value);
esp_err_t hardware_io_mosfet_get_status(hardware_io_mosfet_status_t *out,
                                        size_t max_count,
                                        size_t *out_count);

esp_err_t hardware_io_input_get(uint8_t channel, bool *out_active, bool *out_physical_high);
esp_err_t hardware_io_input_get_status(hardware_io_input_status_t *out,
                                       size_t max_count,
                                       size_t *out_count);

esp_err_t hardware_io_gpio_set(uint8_t channel, bool active);
esp_err_t hardware_io_gpio_toggle(uint8_t channel);
esp_err_t hardware_io_gpio_pulse(uint8_t channel, bool active, uint32_t duration_ms);
esp_err_t hardware_io_gpio_get(uint8_t channel, bool *out_active, bool *out_physical_high);
esp_err_t hardware_io_gpio_get_status(hardware_io_gpio_status_t *out,
                                      size_t max_count,
                                      size_t *out_count);

#ifdef __cplusplus
}
#endif
