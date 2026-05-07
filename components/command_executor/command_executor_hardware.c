#include "command_executor_internal.h"

#include <string.h>

#include "hardware_io.h"
#include "quest_device.h"
#include "sdkconfig.h"

#ifndef CONFIG_SCENEHUB_RELAY_MAX_PULSE_MS
#define CONFIG_SCENEHUB_RELAY_MAX_PULSE_MS 60000
#endif
#ifndef CONFIG_SCENEHUB_MOSFET_MAX_PULSE_MS
#define CONFIG_SCENEHUB_MOSFET_MAX_PULSE_MS 60000
#endif
#ifndef CONFIG_SCENEHUB_MOSFET_MAX_FADE_MS
#define CONFIG_SCENEHUB_MOSFET_MAX_FADE_MS 60000
#endif
#ifndef CONFIG_SCENEHUB_GPIO_MAX_PULSE_MS
#define CONFIG_SCENEHUB_GPIO_MAX_PULSE_MS 60000
#endif

static esp_err_t hardware_get_channel(const command_executor_request_t *request,
                                      char *error,
                                      size_t error_size,
                                      uint8_t max_channel,
                                      uint8_t *out_channel)
{
    int channel = 0;
    esp_err_t err = command_executor_params_get_int(request ? request->params_json : NULL,
                                                    "channel",
                                                    &channel,
                                                    true);
    if (err != ESP_OK) {
        return command_executor_fail(error, error_size, err, "hardware_channel_param_missing");
    }
    if (channel < 1 || channel > max_channel) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "hardware_channel_invalid");
    }
    *out_channel = (uint8_t)channel;
    return ESP_OK;
}

static esp_err_t hardware_get_pwm_value(const command_executor_request_t *request,
                                        const char *key,
                                        char *error,
                                        size_t error_size,
                                        uint8_t *out_value)
{
    int value = 0;
    esp_err_t err = command_executor_params_get_int(request ? request->params_json : NULL,
                                                    key,
                                                    &value,
                                                    true);
    if (err != ESP_OK) {
        return command_executor_fail(error, error_size, err, "mosfet_value_param_missing");
    }
    if (value < 0 || value > HARDWARE_IO_MOSFET_MAX_VALUE) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "mosfet_value_invalid");
    }
    *out_value = (uint8_t)value;
    return ESP_OK;
}

static esp_err_t hardware_execute_relay(const command_executor_request_t *request,
                                        const quest_device_command_t *command,
                                        char *error,
                                        size_t error_size)
{
    uint8_t channel = 0;
    esp_err_t err = hardware_get_channel(request,
                                         error,
                                         error_size,
                                         HARDWARE_IO_RELAY_CHANNEL_COUNT,
                                         &channel);
    if (err != ESP_OK) {
        return err;
    }
    if (strcmp(command->id, "set") == 0 || command_executor_command_name_is(command, "relay.set")) {
        bool on = false;
        err = command_executor_params_get_bool(request->params_json, "on", &on, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "relay_on_param_missing");
        }
        err = hardware_io_relay_set(channel, on);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "relay_set_failed");
    }
    if (strcmp(command->id, "pulse") == 0 || command_executor_command_name_is(command, "relay.pulse")) {
        int duration_ms = 0;
        err = command_executor_params_get_int(request->params_json,
                                              "duration_ms",
                                              &duration_ms,
                                              true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "relay_duration_param_missing");
        }
        if (duration_ms <= 0) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "relay_duration_invalid");
        }
        if (duration_ms > CONFIG_SCENEHUB_RELAY_MAX_PULSE_MS) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "relay_duration_invalid");
        }
        err = hardware_io_relay_pulse(channel, (uint32_t)duration_ms);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "relay_pulse_failed");
    }
    if (strcmp(command->id, "toggle") == 0 || command_executor_command_name_is(command, "relay.toggle")) {
        err = hardware_io_relay_toggle(channel);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "relay_toggle_failed");
    }
    return command_executor_fail(error, error_size, ESP_ERR_NOT_SUPPORTED, "relay_command_unsupported");
}

static esp_err_t hardware_execute_mosfet(const command_executor_request_t *request,
                                         const quest_device_command_t *command,
                                         char *error,
                                         size_t error_size)
{
    uint8_t channel = 0;
    uint8_t value = 0;
    if (strcmp(command->id, "all_off") == 0 || command_executor_command_name_is(command, "mosfet.all_off")) {
        esp_err_t err = hardware_io_mosfet_all_off();
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "mosfet_all_off_failed");
    }
    esp_err_t err = hardware_get_channel(request,
                                         error,
                                         error_size,
                                         HARDWARE_IO_MOSFET_CHANNEL_COUNT,
                                         &channel);
    if (err != ESP_OK) {
        return err;
    }
    if (strcmp(command->id, "set") == 0 || command_executor_command_name_is(command, "mosfet.set")) {
        err = hardware_get_pwm_value(request, "value", error, error_size, &value);
        if (err != ESP_OK) {
            return err;
        }
        err = hardware_io_mosfet_set(channel, value);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "mosfet_set_failed");
    }
    if (strcmp(command->id, "fade") == 0 || command_executor_command_name_is(command, "mosfet.fade")) {
        int duration_ms = 0;
        err = hardware_get_pwm_value(request, "target", error, error_size, &value);
        if (err != ESP_OK) {
            return err;
        }
        err = command_executor_params_get_int(request->params_json,
                                              "duration_ms",
                                              &duration_ms,
                                              true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "mosfet_duration_param_missing");
        }
        if (duration_ms < 0) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "mosfet_duration_invalid");
        }
        if (duration_ms > CONFIG_SCENEHUB_MOSFET_MAX_FADE_MS) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "mosfet_fade_duration_invalid");
        }
        err = hardware_io_mosfet_fade(channel, value, (uint32_t)duration_ms);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "mosfet_fade_failed");
    }
    if (strcmp(command->id, "pulse") == 0 || command_executor_command_name_is(command, "mosfet.pulse")) {
        int duration_ms = 0;
        err = hardware_get_pwm_value(request, "value", error, error_size, &value);
        if (err != ESP_OK) {
            return err;
        }
        err = command_executor_params_get_int(request->params_json,
                                              "duration_ms",
                                              &duration_ms,
                                              true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "mosfet_duration_param_missing");
        }
        if (duration_ms <= 0) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "mosfet_duration_invalid");
        }
        if (duration_ms > CONFIG_SCENEHUB_MOSFET_MAX_PULSE_MS) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "mosfet_duration_invalid");
        }
        err = hardware_io_mosfet_pulse(channel, value, (uint32_t)duration_ms);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "mosfet_pulse_failed");
    }
    return command_executor_fail(error, error_size, ESP_ERR_NOT_SUPPORTED, "mosfet_command_unsupported");
}

static esp_err_t hardware_execute_input(const command_executor_request_t *request,
                                        const quest_device_command_t *command,
                                        char *error,
                                        size_t error_size)
{
    uint8_t channel = 0;
    bool active = false;
    bool physical_high = false;
    esp_err_t err = hardware_get_channel(request,
                                         error,
                                         error_size,
                                         HARDWARE_IO_INPUT_CHANNEL_COUNT,
                                         &channel);
    if (err != ESP_OK) {
        return err;
    }
    if (strcmp(command->id, "get_state") == 0 || command_executor_command_name_is(command, "input.get_state")) {
        err = hardware_io_input_get(channel, &active, &physical_high);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "input_get_state_failed");
    }
    return command_executor_fail(error, error_size, ESP_ERR_NOT_SUPPORTED, "input_command_unsupported");
}

static esp_err_t hardware_execute_gpio(const command_executor_request_t *request,
                                       const quest_device_command_t *command,
                                       char *error,
                                       size_t error_size)
{
    uint8_t channel = 0;
    esp_err_t err = hardware_get_channel(request,
                                         error,
                                         error_size,
                                         HARDWARE_IO_GPIO_CHANNEL_COUNT,
                                         &channel);
    if (err != ESP_OK) {
        return err;
    }
    if (strcmp(command->id, "set") == 0 || command_executor_command_name_is(command, "gpio.set")) {
        bool active = false;
        err = command_executor_params_get_bool(request->params_json, "active", &active, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "gpio_active_param_missing");
        }
        err = hardware_io_gpio_set(channel, active);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "gpio_set_failed");
    }
    if (strcmp(command->id, "pulse") == 0 || command_executor_command_name_is(command, "gpio.pulse")) {
        bool active = false;
        int duration_ms = 0;
        err = command_executor_params_get_bool(request->params_json, "active", &active, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "gpio_active_param_missing");
        }
        err = command_executor_params_get_int(request->params_json,
                                              "duration_ms",
                                              &duration_ms,
                                              true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "gpio_duration_param_missing");
        }
        if (duration_ms <= 0 || duration_ms > CONFIG_SCENEHUB_GPIO_MAX_PULSE_MS) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "gpio_duration_invalid");
        }
        err = hardware_io_gpio_pulse(channel, active, (uint32_t)duration_ms);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "gpio_pulse_failed");
    }
    if (strcmp(command->id, "toggle") == 0 || command_executor_command_name_is(command, "gpio.toggle")) {
        err = hardware_io_gpio_toggle(channel);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "gpio_toggle_failed");
    }
    if (strcmp(command->id, "get_state") == 0 || command_executor_command_name_is(command, "gpio.get_state")) {
        bool active = false;
        bool physical_high = false;
        err = hardware_io_gpio_get(channel, &active, &physical_high);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "gpio_get_state_failed");
    }
    return command_executor_fail(error, error_size, ESP_ERR_NOT_SUPPORTED, "gpio_command_unsupported");
}

esp_err_t command_executor_execute_hardware(const command_executor_request_t *request,
                                            const quest_device_command_t *command,
                                            char *error,
                                            size_t error_size)
{
    if (!request || !command) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "hardware_command_invalid");
    }
    if (!hardware_io_is_available()) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_STATE, "hardware_io_unavailable");
    }
    if (strcmp(request->device_id, QUEST_DEVICE_SYSTEM_RELAY_ID) == 0) {
        return hardware_execute_relay(request, command, error, error_size);
    }
    if (strcmp(request->device_id, QUEST_DEVICE_SYSTEM_MOSFET_ID) == 0) {
        return hardware_execute_mosfet(request, command, error, error_size);
    }
    if (strcmp(request->device_id, QUEST_DEVICE_SYSTEM_INPUT_ID) == 0) {
        return hardware_execute_input(request, command, error, error_size);
    }
    if (strcmp(request->device_id, QUEST_DEVICE_SYSTEM_GPIO_ID) == 0) {
        return hardware_execute_gpio(request, command, error, error_size);
    }
    return command_executor_fail(error, error_size, ESP_ERR_NOT_SUPPORTED, "hardware_command_unsupported");
}
