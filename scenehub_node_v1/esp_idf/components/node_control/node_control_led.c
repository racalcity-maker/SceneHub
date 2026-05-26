#include "node_control_internal.h"

#include <string.h>

#include "esp_log.h"
#include "node_hardware_io.h"

static const char *TAG = "node_control";

static const node_led_strip_config_t *find_led_strip_config(uint8_t strip)
{
    if (strip == 0 || strip > NODE_LED_STRIP_MAX) {
        return NULL;
    }
    const node_led_strip_config_t *cfg = &g_node_control_config.led_strips[strip - 1];
    return cfg->enabled ? cfg : NULL;
}

static const node_led_effect_preset_t *find_led_effect_preset(const node_led_strip_config_t *strip_cfg,
                                                              const char *effect)
{
    node_led_effect_id_t id = NODE_LED_EFFECT_INVALID;
    if (!strip_cfg || !effect) {
        return NULL;
    }
    id = node_led_effect_id_from_name(effect);
    if (id == NODE_LED_EFFECT_INVALID || id >= NODE_LED_EFFECT_COUNT) {
        return NULL;
    }
    return &strip_cfg->effects.items[id];
}

static esp_err_t reject_led_error(node_control_result_t *result, esp_err_t err, const char *fallback)
{
    if (err == ESP_ERR_NOT_FOUND) {
        return result_rejected(result, "not_configured");
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return result_rejected(result, "invalid_args");
    }
    return result_rejected(result, fallback ? fallback : "internal_error");
}

esp_err_t execute_led_off(const node_control_command_t *command, node_control_result_t *result)
{
    int strip = 0;
    if (!read_int_arg(command->args_json, "channel", &strip)) {
        return result_rejected(result, "missing_channel");
    }
    ESP_LOGI(TAG,
             "led.off strip=%d source=%s request_id=%s",
             strip,
             command_source_safe(command),
             command_request_id_text(command));
    esp_err_t err = node_hardware_io_led_off((uint8_t)strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "led.off strip=%d source=%s request_id=%s failed err=%s",
                 strip,
                 command_source_safe(command),
                 command_request_id_text(command),
                 esp_err_to_name(err));
        return reject_led_error(result, err, "led_off_failed");
    }
    result_done(result);
    return ESP_OK;
}

esp_err_t execute_led_solid(const node_control_command_t *command, node_control_result_t *result)
{
    int strip = 0;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t white = 0;
    uint8_t brightness = 255;

    if (!read_int_arg(command->args_json, "channel", &strip)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_color_arg(command->args_json, "color", &red, &green, &blue, &white)) {
        return result_rejected(result, "missing_color");
    }
    if (!read_optional_pwm_arg(command->args_json, "brightness", 255, &brightness)) {
        return result_rejected(result, "invalid_args");
    }
    ESP_LOGI(TAG,
             "led.solid strip=%d source=%s request_id=%s color=%02x%02x%02x%02x brightness=%u",
             strip,
             command_source_safe(command),
             command_request_id_text(command),
             (unsigned)red,
             (unsigned)green,
             (unsigned)blue,
             (unsigned)white,
             (unsigned)brightness);
    esp_err_t err = node_hardware_io_led_solid((uint8_t)strip,
                                               red,
                                               green,
                                               blue,
                                               white,
                                               brightness);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "led.solid strip=%d source=%s request_id=%s failed err=%s",
                 strip,
                 command_source_safe(command),
                 command_request_id_text(command),
                 esp_err_to_name(err));
        return reject_led_error(result, err, "led_solid_failed");
    }
    result_done(result);
    return ESP_OK;
}

esp_err_t execute_led_blink(const node_control_command_t *command,
                            node_control_result_t *result,
                            bool allow_timing_overrides)
{
    int strip = 0;
    int times = 0;
    bool has_times = false;
    uint32_t override_on_ms = 0;
    uint32_t override_off_ms = 0;
    const node_led_strip_config_t *strip_cfg = NULL;
    node_hw_led_effect_config_t config = {
        .red = 255,
        .green = 255,
        .blue = 255,
        .white = 0,
        .brightness = 255,
        .duration_ms = 500,
        .step_ms = 500,
        .count = 0,
    };

    if (!read_int_arg(command->args_json, "channel", &strip)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_color_arg(command->args_json, "color", &config.red, &config.green, &config.blue, &config.white)) {
        return result_rejected(result, "missing_color");
    }
    copy_text(config.source, sizeof(config.source), command_source_safe(command));
    strip_cfg = find_led_strip_config((uint8_t)strip);
    if (strip_cfg) {
        config.duration_ms = strip_cfg->blink.on_ms ? strip_cfg->blink.on_ms : config.duration_ms;
        config.step_ms = strip_cfg->blink.off_ms ? strip_cfg->blink.off_ms : config.step_ms;
        config.count = strip_cfg->blink.repeat_count;
    }
    if (allow_timing_overrides) {
        (void)read_u32_arg(command->args_json, "on_ms", &override_on_ms);
        (void)read_u32_arg(command->args_json, "off_ms", &override_off_ms);
        if (override_on_ms > 0) {
            config.duration_ms = override_on_ms;
        }
        if (override_off_ms > 0) {
            config.step_ms = override_off_ms;
        }
    } else {
        static const char *const forbidden_keys[] = {"on_ms", "off_ms"};
        if (json_has_any_key(command->args_json, forbidden_keys, sizeof(forbidden_keys) / sizeof(forbidden_keys[0]))) {
            return result_rejected(result, "advanced_overrides_forbidden");
        }
    }
    has_times = read_int_arg(command->args_json, "times", &times);
    if (has_times && times < 0) {
        return result_rejected(result, "invalid_args");
    }
    if (has_times) {
        config.count = (uint32_t)times;
    }
    if (config.duration_ms == 0 || config.step_ms == 0 ||
        (config.count > 0 &&
         ((uint64_t)(config.duration_ms + config.step_ms) * (uint64_t)config.count) > NODE_CONTROL_MAX_LED_EFFECT_MS)) {
        return result_rejected(result, "invalid_args");
    }

    ESP_LOGI(TAG,
             "led.blink strip=%d source=%s request_id=%s color=%02x%02x%02x%02x on_ms=%lu off_ms=%lu times=%lu",
             strip,
             command_source_safe(command),
             command_request_id_text(command),
             (unsigned)config.red,
             (unsigned)config.green,
             (unsigned)config.blue,
             (unsigned)config.white,
             (unsigned long)config.duration_ms,
             (unsigned long)config.step_ms,
             (unsigned long)config.count);
    esp_err_t err = node_hardware_io_led_run_effect((uint8_t)strip, NODE_HW_LED_EFFECT_BLINK, &config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "led.blink strip=%d source=%s request_id=%s failed err=%s",
                 strip,
                 command_source_safe(command),
                 command_request_id_text(command),
                 esp_err_to_name(err));
        return reject_led_error(result, err, "led_blink_failed");
    }
    result_started(result);
    return ESP_OK;
}

esp_err_t execute_led_breathe(const node_control_command_t *command,
                              node_control_result_t *result,
                              bool allow_motion_overrides)
{
    int strip = 0;
    int count = 0;
    bool has_count = false;
    const node_led_strip_config_t *strip_cfg = NULL;
    node_hw_led_effect_config_t config = {
        .red = 255,
        .green = 255,
        .blue = 255,
        .white = 0,
        .brightness = 255,
        .duration_ms = 250,
        .step_ms = 50,
        .count = 0,
    };

    if (!read_int_arg(command->args_json, "channel", &strip)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_color_arg(command->args_json, "color", &config.red, &config.green, &config.blue, &config.white)) {
        return result_rejected(result, "missing_color");
    }
    copy_text(config.source, sizeof(config.source), command_source_safe(command));
    strip_cfg = find_led_strip_config((uint8_t)strip);
    if (strip_cfg) {
        config.duration_ms = strip_cfg->breathe.cycle_ms ? strip_cfg->breathe.cycle_ms : config.duration_ms;
        config.step_ms = strip_cfg->breathe.step_ms ? strip_cfg->breathe.step_ms : config.step_ms;
        config.count = strip_cfg->breathe.repeat_count;
    }
    if (allow_motion_overrides) {
        (void)read_u32_arg(command->args_json, "duration_ms", &config.duration_ms);
        (void)read_u32_arg(command->args_json, "step_ms", &config.step_ms);
        has_count = read_int_arg(command->args_json, "count", &count);
    } else {
        static const char *const forbidden_keys[] = {"duration_ms", "step_ms", "count"};
        if (json_has_any_key(command->args_json, forbidden_keys, sizeof(forbidden_keys) / sizeof(forbidden_keys[0]))) {
            return result_rejected(result, "advanced_overrides_forbidden");
        }
    }
    if (has_count && count < 0) {
        return result_rejected(result, "invalid_args");
    }
    if (has_count) {
        config.count = (uint32_t)count;
    }
    if (config.duration_ms > NODE_CONTROL_MAX_LED_DURATION_MS ||
        config.step_ms > NODE_CONTROL_MAX_LED_STEP_MS ||
        (config.count > 0 &&
         ((uint64_t)config.duration_ms * (uint64_t)config.count) > NODE_CONTROL_MAX_LED_EFFECT_MS)) {
        return result_rejected(result, "invalid_args");
    }

    ESP_LOGI(TAG,
             "led.breathe strip=%d source=%s request_id=%s color=%02x%02x%02x%02x duration_ms=%lu step_ms=%lu count=%lu",
             strip,
             command_source_safe(command),
             command_request_id_text(command),
             (unsigned)config.red,
             (unsigned)config.green,
             (unsigned)config.blue,
             (unsigned)config.white,
             (unsigned long)config.duration_ms,
             (unsigned long)config.step_ms,
             (unsigned long)config.count);
    esp_err_t err = node_hardware_io_led_run_effect((uint8_t)strip, NODE_HW_LED_EFFECT_BREATHE, &config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "led.breathe strip=%d source=%s request_id=%s failed err=%s",
                 strip,
                 command_source_safe(command),
                 command_request_id_text(command),
                 esp_err_to_name(err));
        return reject_led_error(result, err, "led_breathe_failed");
    }
    result_started(result);
    return ESP_OK;
}

esp_err_t execute_led_effect(const node_control_command_t *command,
                             node_control_result_t *result,
                             bool allow_advanced_overrides)
{
    int strip = 0;
    char effect[24] = {0};
    char palette_mode[16] = {0};
    uint8_t brightness = 255;
    uint32_t value32 = 0;
    const node_led_strip_config_t *strip_cfg = NULL;
    const node_led_effect_preset_t *preset = NULL;
    const node_led_effect_descriptor_t *effect_desc = NULL;
    node_led_effect_id_t effect_id = NODE_LED_EFFECT_INVALID;
    node_hw_led_effect_t led_effect = NODE_LED_EFFECT_BLINK;
    node_hw_led_effect_config_t config = {
        .red = 255,
        .green = 255,
        .blue = 255,
        .white = 0,
        .red2 = 0,
        .green2 = 0,
        .blue2 = 0,
        .white2 = 0,
        .bg_red = 0,
        .bg_green = 0,
        .bg_blue = 0,
        .bg_white = 0,
        .brightness = 255,
        .duration_ms = 250,
        .step_ms = 50,
        .count = 0,
        .size = 1,
        .intensity = 255,
        .density = 0,
        .fade = 0,
        .palette_mode = NODE_LED_PALETTE_NONE,
    };

    if (!read_int_arg(command->args_json, "channel", &strip)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_string_arg(command->args_json, "effect", effect, sizeof(effect))) {
        return result_rejected(result, "missing_effect");
    }
    copy_text(config.source, sizeof(config.source), command_source_safe(command));
    strip_cfg = find_led_strip_config((uint8_t)strip);
    effect_id = node_led_effect_id_from_name(effect);
    if (effect_id == NODE_LED_EFFECT_INVALID || !node_led_effect_is_advanced(effect_id)) {
        return result_rejected(result, "invalid_args");
    }
    effect_desc = node_led_effect_descriptor(effect_id);
    preset = find_led_effect_preset(strip_cfg, effect);
    if (preset) {
        if (preset->duration_ms > 0) {
            config.duration_ms = preset->duration_ms;
        }
        if (preset->step_ms > 0) {
            config.step_ms = preset->step_ms;
        }
        config.count = preset->repeat_count;
        config.size = preset->size;
        config.intensity = preset->intensity;
        config.density = preset->density;
        config.fade = preset->fade;
        config.palette_mode = preset->palette_mode;
        config.red = preset->red;
        config.green = preset->green;
        config.blue = preset->blue;
        config.white = preset->white;
        config.red2 = preset->red2;
        config.green2 = preset->green2;
        config.blue2 = preset->blue2;
        config.white2 = preset->white2;
        config.bg_red = preset->bg_red;
        config.bg_green = preset->bg_green;
        config.bg_blue = preset->bg_blue;
        config.bg_white = preset->bg_white;
    }
    if (allow_advanced_overrides) {
        if (!read_optional_color_arg(command->args_json,
                                     "color",
                                     config.red,
                                     config.green,
                                     config.blue,
                                     config.white,
                                     &config.red,
                                     &config.green,
                                     &config.blue,
                                     &config.white)) {
            return result_rejected(result, "invalid_args");
        }
        if (effect_desc && (effect_desc->controls & NODE_LED_CTRL_SECONDARY_COLOR)) {
            if (!read_optional_color_arg(command->args_json,
                                         "secondary_color",
                                         config.red2,
                                         config.green2,
                                         config.blue2,
                                         config.white2,
                                         &config.red2,
                                         &config.green2,
                                         &config.blue2,
                                         &config.white2)) {
                return result_rejected(result, "invalid_args");
            }
        }
        if (effect_desc && (effect_desc->controls & NODE_LED_CTRL_BACKGROUND_COLOR)) {
            if (!read_optional_color_arg(command->args_json,
                                         "background_color",
                                         config.bg_red,
                                         config.bg_green,
                                         config.bg_blue,
                                         config.bg_white,
                                         &config.bg_red,
                                         &config.bg_green,
                                         &config.bg_blue,
                                         &config.bg_white)) {
                return result_rejected(result, "invalid_args");
            }
        }
        if (effect_desc) {
            uint8_t legacy_red = 0;
            uint8_t legacy_green = 0;
            uint8_t legacy_blue = 0;
            uint8_t legacy_white = 0;
            bool has_secondary = read_color_arg(command->args_json,
                                                "secondary_color",
                                                &legacy_red,
                                                &legacy_green,
                                                &legacy_blue,
                                                &legacy_white);
            bool has_background = read_color_arg(command->args_json,
                                                 "background_color",
                                                 &legacy_red,
                                                 &legacy_green,
                                                 &legacy_blue,
                                                 &legacy_white);
            if (!has_secondary &&
                !has_background &&
                read_color_arg(command->args_json, "color2", &legacy_red, &legacy_green, &legacy_blue, &legacy_white)) {
                if (effect_desc->controls & NODE_LED_CTRL_SECONDARY_COLOR) {
                    config.red2 = legacy_red;
                    config.green2 = legacy_green;
                    config.blue2 = legacy_blue;
                    config.white2 = legacy_white;
                }
                if ((effect_desc->controls & NODE_LED_CTRL_BACKGROUND_COLOR) &&
                    !(effect_desc->controls & NODE_LED_CTRL_SECONDARY_COLOR)) {
                    config.bg_red = legacy_red;
                    config.bg_green = legacy_green;
                    config.bg_blue = legacy_blue;
                    config.bg_white = legacy_white;
                }
            }
        }
        if (!read_optional_pwm_arg(command->args_json, "brightness", 255, &brightness)) {
            return result_rejected(result, "invalid_args");
        }
        config.brightness = brightness;
        (void)read_u32_arg(command->args_json, "duration_ms", &config.duration_ms);
        (void)read_u32_arg(command->args_json, "step_ms", &config.step_ms);
        (void)read_u32_arg(command->args_json, "count", &config.count);
        if (read_u32_arg(command->args_json, "size", &value32) && value32 <= UINT16_MAX) {
            config.size = (uint16_t)value32;
        }
        if (read_u32_arg(command->args_json, "intensity", &value32) && value32 <= UINT16_MAX) {
            config.intensity = (uint16_t)value32;
        }
        if (read_u32_arg(command->args_json, "density", &value32) && value32 <= UINT16_MAX) {
            config.density = (uint16_t)value32;
        }
        if (read_u32_arg(command->args_json, "fade", &value32) && value32 <= UINT16_MAX) {
            config.fade = (uint16_t)value32;
        }
        if (read_string_arg(command->args_json, "palette_mode", palette_mode, sizeof(palette_mode))) {
            config.palette_mode = node_led_palette_mode_from_name(palette_mode);
        }
    } else {
        static const char *const forbidden_keys[] = {
            "duration_ms", "step_ms", "count", "size", "intensity", "density", "fade",
            "palette_mode", "color", "secondary_color", "background_color", "color2", "brightness"
        };
        if (json_has_any_key(command->args_json, forbidden_keys, sizeof(forbidden_keys) / sizeof(forbidden_keys[0]))) {
            return result_rejected(result, "advanced_overrides_forbidden");
        }
    }

    if (config.duration_ms > NODE_CONTROL_MAX_LED_DURATION_MS ||
        config.step_ms > NODE_CONTROL_MAX_LED_STEP_MS) {
        return result_rejected(result, "invalid_args");
    }

    led_effect = effect_id;

    if (effect_id == NODE_LED_EFFECT_BREATHE &&
        config.count > 0 &&
        (uint64_t)config.duration_ms * (uint64_t)config.count > NODE_CONTROL_MAX_LED_EFFECT_MS) {
        return result_rejected(result, "invalid_args");
    }

    if ((effect_id == NODE_LED_EFFECT_BLINK || effect_id == NODE_LED_EFFECT_STROBE) &&
        config.count > 0 &&
        ((uint64_t)(config.duration_ms + config.step_ms) * (uint64_t)config.count) > NODE_CONTROL_MAX_LED_EFFECT_MS) {
        return result_rejected(result, "invalid_args");
    }

    ESP_LOGI(TAG,
             "led.effect strip=%d source=%s request_id=%s effect=%s color=%02x%02x%02x%02x secondary=%02x%02x%02x%02x background=%02x%02x%02x%02x brightness=%u duration_ms=%lu step_ms=%lu count=%lu size=%u intensity=%u density=%u fade=%u palette=%s",
             strip,
             command_source_safe(command),
             command_request_id_text(command),
             effect,
             (unsigned)config.red,
             (unsigned)config.green,
             (unsigned)config.blue,
             (unsigned)config.white,
             (unsigned)config.red2,
             (unsigned)config.green2,
             (unsigned)config.blue2,
             (unsigned)config.white2,
             (unsigned)config.bg_red,
             (unsigned)config.bg_green,
             (unsigned)config.bg_blue,
             (unsigned)config.bg_white,
             (unsigned)config.brightness,
             (unsigned long)config.duration_ms,
             (unsigned long)config.step_ms,
             (unsigned long)config.count,
             (unsigned)config.size,
             (unsigned)config.intensity,
             (unsigned)config.density,
             (unsigned)config.fade,
             node_led_palette_mode_name(config.palette_mode));
    esp_err_t err = node_hardware_io_led_run_effect((uint8_t)strip, led_effect, &config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "led.effect strip=%d source=%s request_id=%s effect=%s failed err=%s",
                 strip,
                 command_source_safe(command),
                 command_request_id_text(command),
                 effect,
                 esp_err_to_name(err));
        return reject_led_error(result, err, "led_effect_failed");
    }
    result_started(result);
    return ESP_OK;
}
