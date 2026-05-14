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
#ifndef CONFIG_SCENEHUB_HARDWARE_MAX_EFFECT_MS
#define CONFIG_SCENEHUB_HARDWARE_MAX_EFFECT_MS 600000
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

static esp_err_t hardware_get_optional_pwm_value(const command_executor_request_t *request,
                                                 const char *key,
                                                 uint8_t fallback,
                                                 uint8_t *out_value)
{
    int value = fallback;
    esp_err_t err = command_executor_params_get_int(request ? request->params_json : NULL,
                                                    key,
                                                    &value,
                                                    false);
    if (err != ESP_OK) {
        return err;
    }
    if (value < 0 || value > HARDWARE_IO_MOSFET_MAX_VALUE) {
        return ESP_ERR_INVALID_ARG;
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
    if (strcmp(command->id, "blink") == 0 || command_executor_command_name_is(command, "relay.blink")) {
        int on_ms = 0;
        int off_ms = 0;
        int count = 0;
        bool final_on = false;
        err = command_executor_params_get_int(request->params_json, "on_ms", &on_ms, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "relay_on_ms_param_missing");
        }
        err = command_executor_params_get_int(request->params_json, "off_ms", &off_ms, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "relay_off_ms_param_missing");
        }
        err = command_executor_params_get_int(request->params_json, "count", &count, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "relay_count_param_missing");
        }
        (void)command_executor_params_get_bool(request->params_json, "final_on", &final_on, false);
        if (on_ms <= 0 || off_ms <= 0 || count < 0 ||
            (count > 0 && ((uint64_t)(on_ms + off_ms) * (uint64_t)count) > CONFIG_SCENEHUB_HARDWARE_MAX_EFFECT_MS)) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "relay_blink_invalid");
        }
        err = hardware_io_relay_blink(channel, (uint32_t)on_ms, (uint32_t)off_ms, (uint32_t)count, final_on);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "relay_blink_failed");
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
    if (strcmp(command->id, "blink") == 0 || command_executor_command_name_is(command, "mosfet.blink")) {
        int on_ms = 0;
        int off_ms = 0;
        int count = 0;
        uint8_t final_value = 0;
        err = hardware_get_pwm_value(request, "value", error, error_size, &value);
        if (err != ESP_OK) {
            return err;
        }
        err = hardware_get_optional_pwm_value(request, "final_value", 0, &final_value);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "mosfet_final_value_invalid");
        }
        err = command_executor_params_get_int(request->params_json, "on_ms", &on_ms, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "mosfet_on_ms_param_missing");
        }
        err = command_executor_params_get_int(request->params_json, "off_ms", &off_ms, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "mosfet_off_ms_param_missing");
        }
        err = command_executor_params_get_int(request->params_json, "count", &count, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "mosfet_count_param_missing");
        }
        if (on_ms <= 0 || off_ms <= 0 || count < 0 ||
            (count > 0 && ((uint64_t)(on_ms + off_ms) * (uint64_t)count) > CONFIG_SCENEHUB_HARDWARE_MAX_EFFECT_MS)) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "mosfet_blink_invalid");
        }
        err = hardware_io_mosfet_effect_blink(channel,
                                              value,
                                              (uint32_t)on_ms,
                                              (uint32_t)off_ms,
                                              (uint32_t)count,
                                              final_value);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "mosfet_blink_failed");
    }
    if (strcmp(command->id, "breathe") == 0 || command_executor_command_name_is(command, "mosfet.breathe")) {
        int fade_ms = 0;
        int hold_ms = 0;
        int count = 0;
        uint8_t min_value = 0;
        uint8_t max_value = 0;
        uint8_t final_value = 0;
        err = hardware_get_pwm_value(request, "min", error, error_size, &min_value);
        if (err != ESP_OK) {
            return err;
        }
        err = hardware_get_pwm_value(request, "max", error, error_size, &max_value);
        if (err != ESP_OK) {
            return err;
        }
        err = hardware_get_optional_pwm_value(request, "final_value", 0, &final_value);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "mosfet_final_value_invalid");
        }
        err = command_executor_params_get_int(request->params_json, "fade_ms", &fade_ms, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "mosfet_fade_ms_param_missing");
        }
        err = command_executor_params_get_int(request->params_json, "hold_ms", &hold_ms, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "mosfet_hold_ms_param_missing");
        }
        err = command_executor_params_get_int(request->params_json, "count", &count, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "mosfet_count_param_missing");
        }
        if (fade_ms <= 0 || hold_ms < 0 || count < 0 || min_value > max_value ||
            (count > 0 && ((uint64_t)(fade_ms * 2 + hold_ms * 2) * (uint64_t)count) > CONFIG_SCENEHUB_HARDWARE_MAX_EFFECT_MS)) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "mosfet_breathe_invalid");
        }
        err = hardware_io_mosfet_effect_breathe(channel,
                                                min_value,
                                                max_value,
                                                (uint32_t)fade_ms,
                                                (uint32_t)hold_ms,
                                                (uint32_t)count,
                                                final_value);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "mosfet_breathe_failed");
    }
    return command_executor_fail(error, error_size, ESP_ERR_NOT_SUPPORTED, "mosfet_command_unsupported");
}

static esp_err_t hardware_execute_io(const command_executor_request_t *request,
                                     const quest_device_command_t *command,
                                     char *error,
                                     size_t error_size)
{
    uint8_t channel = 0;
    esp_err_t err = hardware_get_channel(request,
                                         error,
                                         error_size,
                                         HARDWARE_IO_IO_CHANNEL_COUNT,
                                         &channel);
    if (err != ESP_OK) {
        return err;
    }
    if (strcmp(command->id, "set") == 0 || command_executor_command_name_is(command, "io.set")) {
        bool active = false;
        err = command_executor_params_get_bool(request->params_json, "active", &active, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "io_active_param_missing");
        }
        err = hardware_io_io_set(channel, active);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "io_set_failed");
    }
    if (strcmp(command->id, "pulse") == 0 || command_executor_command_name_is(command, "io.pulse")) {
        bool active = false;
        int duration_ms = 0;
        err = command_executor_params_get_bool(request->params_json, "active", &active, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "io_active_param_missing");
        }
        err = command_executor_params_get_int(request->params_json,
                                              "duration_ms",
                                              &duration_ms,
                                              true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "io_duration_param_missing");
        }
        if (duration_ms <= 0 || duration_ms > CONFIG_SCENEHUB_GPIO_MAX_PULSE_MS) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "io_duration_invalid");
        }
        err = hardware_io_io_pulse(channel, active, (uint32_t)duration_ms);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "io_pulse_failed");
    }
    if (strcmp(command->id, "blink") == 0 || command_executor_command_name_is(command, "io.blink")) {
        int on_ms = 0;
        int off_ms = 0;
        int count = 0;
        bool final_active = false;
        err = command_executor_params_get_int(request->params_json, "on_ms", &on_ms, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "io_on_ms_param_missing");
        }
        err = command_executor_params_get_int(request->params_json, "off_ms", &off_ms, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "io_off_ms_param_missing");
        }
        err = command_executor_params_get_int(request->params_json, "count", &count, true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "io_count_param_missing");
        }
        (void)command_executor_params_get_bool(request->params_json, "final_active", &final_active, false);
        if (on_ms <= 0 || off_ms <= 0 || count < 0 ||
            (count > 0 && ((uint64_t)(on_ms + off_ms) * (uint64_t)count) > CONFIG_SCENEHUB_HARDWARE_MAX_EFFECT_MS)) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "io_blink_invalid");
        }
        err = hardware_io_io_blink(channel, (uint32_t)on_ms, (uint32_t)off_ms, (uint32_t)count, final_active);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "io_blink_failed");
    }
    if (strcmp(command->id, "toggle") == 0 || command_executor_command_name_is(command, "io.toggle")) {
        err = hardware_io_io_toggle(channel);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "io_toggle_failed");
    }
    if (strcmp(command->id, "get_state") == 0 || command_executor_command_name_is(command, "io.get_state")) {
        bool active = false;
        bool physical_high = false;
        err = hardware_io_io_get(channel, &active, &physical_high);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "io_get_state_failed");
    }
    return command_executor_fail(error, error_size, ESP_ERR_NOT_SUPPORTED, "io_command_unsupported");
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
    if (strcmp(request->device_id, QUEST_DEVICE_SYSTEM_IO_ID) == 0) {
        return hardware_execute_io(request, command, error, error_size);
    }
    return command_executor_fail(error, error_size, ESP_ERR_NOT_SUPPORTED, "hardware_command_unsupported");
}
