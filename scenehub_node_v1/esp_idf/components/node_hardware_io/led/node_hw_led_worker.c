#include "node_hw_led_internal.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "node_hw_led_work";
#define NODE_HW_LED_EFFECT_DELAY_SLICE_MS 20U
#define NODE_HW_LED_EFFECT_TASK_STACK_WORDS 4096U

extern StackType_t *s_led_task_stack_ext[NODE_LED_STRIP_MAX];
extern StackType_t s_led_task_stack_fallback[NODE_LED_STRIP_MAX][NODE_HW_LED_EFFECT_TASK_STACK_WORDS];

const char *effect_name(node_hw_led_effect_t effect)
{
    switch (effect) {
    case NODE_HW_LED_EFFECT_BLINK:
        return "blink";
    case NODE_HW_LED_EFFECT_BREATHE:
        return "breathe";
    case NODE_HW_LED_EFFECT_RAINBOW:
        return "rainbow";
    case NODE_LED_EFFECT_RAINBOW_CYCLE:
        return "rainbow_cycle";
    case NODE_HW_LED_EFFECT_COLOR_WIPE:
        return "color_wipe";
    case NODE_HW_LED_EFFECT_SCANNER:
        return "scanner";
    case NODE_HW_LED_EFFECT_THEATER:
        return "theater_chase";
    case NODE_HW_LED_EFFECT_STROBE:
        return "strobe";
    case NODE_LED_EFFECT_PULSE:
        return "pulse";
    case NODE_LED_EFFECT_FADE_IN_OUT:
        return "fade_in_out";
    case NODE_LED_EFFECT_TWINKLE:
        return "twinkle";
    case NODE_LED_EFFECT_TWINKLE_RANDOM:
        return "twinkle_random";
    case NODE_LED_EFFECT_SPARKLE:
        return "sparkle";
    case NODE_LED_EFFECT_GLITTER:
        return "glitter";
    case NODE_LED_EFFECT_COMET:
        return "comet";
    case NODE_LED_EFFECT_LARSON:
        return "larson";
    case NODE_LED_EFFECT_RUNNING_LIGHTS:
        return "running_lights";
    case NODE_LED_EFFECT_FIRE_FLICKER:
        return "fire_flicker";
    case NODE_LED_EFFECT_CHASE_DUAL:
        return "chase_dual";
    case NODE_LED_EFFECT_CHASE_SINGLE:
        return "chase_single";
    case NODE_LED_EFFECT_BOUNCE:
        return "bounce";
    case NODE_LED_EFFECT_BREATH_WAVE:
        return "breath_wave";
    default:
        return "unknown";
    }
}

const char *effect_source_name(const node_hw_led_effect_config_t *config)
{
    return (config && config->source[0]) ? config->source : "unknown";
}

bool take_strip_lock(node_hw_led_strip_t *strip)
{
    return strip && strip->mutex && xSemaphoreTake(strip->mutex, portMAX_DELAY) == pdTRUE;
}

void give_strip_lock(node_hw_led_strip_t *strip)
{
    if (strip && strip->mutex) {
        xSemaphoreGive(strip->mutex);
    }
}

bool effect_cancelled(node_hw_led_strip_t *strip, uint32_t effect_seq)
{
    bool cancelled = true;

    if (!strip || !take_strip_lock(strip)) {
        return true;
    }
    cancelled = strip->effect_seq != effect_seq || !strip->effect_active;
    give_strip_lock(strip);
    return cancelled;
}

esp_err_t activate_effect(node_hw_led_strip_t *strip,
                          node_hw_led_effect_t effect,
                          const node_hw_led_effect_config_t *config)
{
    if (!strip || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!take_strip_lock(strip)) {
        return ESP_ERR_INVALID_STATE;
    }
    strip->active_effect = effect;
    strip->active_config = *config;
    strip->effect_active = true;
    ++strip->effect_seq;
    give_strip_lock(strip);
    return ESP_OK;
}

void clear_effect_active_if_current(node_hw_led_strip_t *strip, uint32_t effect_seq)
{
    if (!strip || !take_strip_lock(strip)) {
        return;
    }
    if (strip->effect_seq == effect_seq) {
        strip->effect_active = false;
    }
    give_strip_lock(strip);
}

StackType_t *allocate_effect_task_stack(size_t idx)
{
#if CONFIG_SPIRAM && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    if (idx < NODE_LED_STRIP_MAX && !s_led_task_stack_ext[idx]) {
        size_t stack_bytes = NODE_HW_LED_EFFECT_TASK_STACK_WORDS * sizeof(StackType_t);
        s_led_task_stack_ext[idx] = (StackType_t *)heap_caps_malloc(stack_bytes,
                                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_led_task_stack_ext[idx]) {
            memset(s_led_task_stack_ext[idx], 0, stack_bytes);
            ESP_LOGI(TAG,
                     "led fx stack strip=%u source=psram bytes=%u",
                     (unsigned)(idx + 1U),
                     (unsigned)stack_bytes);
        } else {
            ESP_LOGW(TAG,
                     "led fx stack strip=%u psram alloc failed; using internal fallback",
                     (unsigned)(idx + 1U));
        }
    }
    if (idx < NODE_LED_STRIP_MAX && s_led_task_stack_ext[idx]) {
        return s_led_task_stack_ext[idx];
    }
#endif
    return idx < NODE_LED_STRIP_MAX ? s_led_task_stack_fallback[idx] : NULL;
}

bool delay_effect_ms(node_hw_led_strip_t *strip, uint32_t duration_ms, uint32_t effect_seq)
{
    uint32_t remaining = duration_ms;

    while (remaining > 0) {
        uint32_t slice_ms = remaining > NODE_HW_LED_EFFECT_DELAY_SLICE_MS
                                ? NODE_HW_LED_EFFECT_DELAY_SLICE_MS
                                : remaining;
        if (effect_cancelled(strip, effect_seq)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(slice_ms));
        remaining -= slice_ms;
    }

    return !effect_cancelled(strip, effect_seq);
}

void stop_effect(node_hw_led_strip_t *strip)
{
    if (!strip) {
        return;
    }
    if (!take_strip_lock(strip)) {
        return;
    }
    strip->effect_active = false;
    ++strip->effect_seq;
    give_strip_lock(strip);
}

void led_effect_task(void *arg)
{
    node_hw_led_strip_t *strip = (node_hw_led_strip_t *)arg;

    while (true) {
        esp_err_t err = ESP_OK;

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!strip) {
            continue;
        }

        if (!take_strip_lock(strip)) {
            ESP_LOGW(TAG, "effect snapshot failed strip=%u", (unsigned)strip->channel);
            continue;
        }
        strip->worker_effect = strip->active_effect;
        strip->worker_config = strip->active_config;
        strip->worker_effect_seq = strip->effect_seq;
        give_strip_lock(strip);

        ESP_LOGI(TAG,
                 "effect start strip=%u source=%s effect=%s duration_ms=%lu step_ms=%lu count=%lu brightness=%u",
                 (unsigned)strip->channel,
                 effect_source_name(&strip->worker_config),
                 effect_name(strip->worker_effect),
                 (unsigned long)strip->worker_config.duration_ms,
                 (unsigned long)strip->worker_config.step_ms,
                 (unsigned long)strip->worker_config.count,
                 (unsigned)strip->worker_config.brightness);

        switch (strip->worker_effect) {
        case NODE_HW_LED_EFFECT_BLINK:
            err = run_blink(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_HW_LED_EFFECT_BREATHE:
            err = run_breathe(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_HW_LED_EFFECT_RAINBOW:
            err = run_rainbow(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_LED_EFFECT_RAINBOW_CYCLE:
            err = run_rainbow_cycle(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_HW_LED_EFFECT_COLOR_WIPE:
            err = run_color_wipe(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_HW_LED_EFFECT_SCANNER:
            err = run_scanner(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_HW_LED_EFFECT_THEATER:
            err = run_theater(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_HW_LED_EFFECT_STROBE:
            err = run_strobe(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_LED_EFFECT_PULSE:
            err = run_pulse(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_LED_EFFECT_FADE_IN_OUT:
            err = run_fade_in_out(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_LED_EFFECT_TWINKLE:
            err = run_twinkle(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_LED_EFFECT_TWINKLE_RANDOM:
            err = run_twinkle_random(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_LED_EFFECT_SPARKLE:
            err = run_sparkle(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_LED_EFFECT_GLITTER:
            err = run_glitter(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_LED_EFFECT_COMET:
            err = run_comet(strip, &strip->worker_config, strip->worker_effect_seq, false);
            break;
        case NODE_LED_EFFECT_LARSON:
            err = run_comet(strip, &strip->worker_config, strip->worker_effect_seq, true);
            break;
        case NODE_LED_EFFECT_RUNNING_LIGHTS:
            err = run_running_lights(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_LED_EFFECT_FIRE_FLICKER:
            err = run_fire_flicker(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        case NODE_LED_EFFECT_CHASE_DUAL:
            err = run_chase_common(strip, &strip->worker_config, strip->worker_effect_seq, true);
            break;
        case NODE_LED_EFFECT_CHASE_SINGLE:
            err = run_chase_common(strip, &strip->worker_config, strip->worker_effect_seq, false);
            break;
        case NODE_LED_EFFECT_BOUNCE:
            err = run_comet(strip, &strip->worker_config, strip->worker_effect_seq, true);
            break;
        case NODE_LED_EFFECT_BREATH_WAVE:
            err = run_breath_wave(strip, &strip->worker_config, strip->worker_effect_seq);
            break;
        default:
            err = ESP_ERR_INVALID_ARG;
            break;
        }

        if (err != ESP_OK) {
            clear_effect_active_if_current(strip, strip->worker_effect_seq);
            ESP_LOGW(TAG,
                     "effect failed strip=%u source=%s effect=%s err=%s",
                     (unsigned)strip->channel,
                     effect_source_name(&strip->worker_config),
                     effect_name(strip->worker_effect),
                     esp_err_to_name(err));
        } else if (effect_cancelled(strip, strip->worker_effect_seq)) {
            ESP_LOGI(TAG,
                     "effect cancelled strip=%u source=%s effect=%s",
                     (unsigned)strip->channel,
                     effect_source_name(&strip->worker_config),
                     effect_name(strip->worker_effect));
        } else {
            clear_effect_active_if_current(strip, strip->worker_effect_seq);
            ESP_LOGI(TAG,
                     "effect done strip=%u source=%s effect=%s",
                     (unsigned)strip->channel,
                     effect_source_name(&strip->worker_config),
                     effect_name(strip->worker_effect));
        }
    }
}
