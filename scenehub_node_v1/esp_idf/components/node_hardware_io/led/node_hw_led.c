#include "node_hw_led_internal.h"

#include "esp_check.h"
#include "esp_log.h"
#include "node_board.h"

static const char *TAG = "node_hw_led";
#define NODE_HW_LED_EFFECT_TASK_STACK_WORDS 4096U

static node_hw_led_strip_t s_led_strips[NODE_LED_STRIP_MAX];
static StaticSemaphore_t s_led_mutex_storage[NODE_LED_STRIP_MAX];
static StaticTask_t s_led_task_storage[NODE_LED_STRIP_MAX];
StackType_t s_led_task_stack_fallback[NODE_LED_STRIP_MAX][NODE_HW_LED_EFFECT_TASK_STACK_WORDS];
StackType_t *s_led_task_stack_ext[NODE_LED_STRIP_MAX];
static bool s_led_initialized;

static node_hw_led_strip_t *find_led_strip(uint8_t strip)
{
    if (strip == 0 || strip > NODE_LED_STRIP_MAX) {
        return NULL;
    }
    return &s_led_strips[strip - 1];
}


static esp_err_t configure_led_strip(size_t idx, const node_led_strip_config_t *pin)
{
    node_hw_led_strip_t *strip = &s_led_strips[idx];
    led_strip_config_t strip_config = {
        .strip_gpio_num = pin->gpio,
        .max_leds = pin->pixel_count,
        .led_model = led_model_from_config(pin->chipset),
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    esp_err_t err = ESP_OK;

#ifdef LED_STRIP_COLOR_COMPONENT_FMT_GRBW
    strip_config.color_component_format = pin->rgbw ? LED_STRIP_COLOR_COMPONENT_FMT_GRBW
                                                    : LED_STRIP_COLOR_COMPONENT_FMT_GRB;
#else
    strip_config.led_pixel_format = pin->rgbw ? LED_PIXEL_FORMAT_GRBW : LED_PIXEL_FORMAT_GRB;
#endif

    err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip->handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to init strip channel=%u gpio=%d err=%s",
                 (unsigned)pin->channel,
                 pin->gpio,
                 esp_err_to_name(err));
        return err;
    }

    strip->channel = pin->channel;
    strip->gpio = pin->gpio;
    strip->pixel_count = pin->pixel_count;
    strip->color_order = pin->color_order;
    strip->rgbw = pin->rgbw;
    if (!strip->mutex) {
        strip->mutex = xSemaphoreCreateMutexStatic(&s_led_mutex_storage[idx]);
        if (!strip->mutex) {
            (void)led_strip_del(strip->handle);
            strip->handle = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    if (!strip->effect_task) {
        StackType_t *task_stack = allocate_effect_task_stack(idx);
        if (!task_stack) {
            (void)led_strip_del(strip->handle);
            strip->handle = NULL;
            strip->mutex = NULL;
            return ESP_ERR_NO_MEM;
        }
        strip->effect_task = xTaskCreateStatic(led_effect_task,
                                               "node_led_fx",
                                               NODE_HW_LED_EFFECT_TASK_STACK_WORDS,
                                               strip,
                                               tskIDLE_PRIORITY + 1,
                                               task_stack,
                                               &s_led_task_storage[idx]);
        if (!strip->effect_task) {
            (void)led_strip_del(strip->handle);
            strip->handle = NULL;
            strip->mutex = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    strip->configured = true;
    g_node_hw.status.configured_led_strips++;

    return led_strip_clear(strip->handle);
}

esp_err_t node_hw_led_init(const node_config_t *config)
{
    /* Boot-only lifecycle: LED strip hardware runtime is initialized once during
     * startup. Live wiring reconfigure is not supported on this path because
     * effect tasks, strip handles and static task storage require an explicit
     * stop/deinit sequence before re-init. */
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_led_initialized) {
        ESP_LOGE(TAG, "node_hw_led_init is boot-only; repeated init rejected");
        return ESP_ERR_INVALID_STATE;
    }

    g_node_hw.status.configured_led_strips = 0;

    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *pin = &config->led_strips[i];
        if (!pin->enabled || pin->gpio < 0 || !node_board_gpio_is_allowed(pin->gpio)) {
            continue;
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(configure_led_strip(i, pin));
    }
    s_led_initialized = true;
    return ESP_OK;
}

esp_err_t node_hw_led_all_off(void)
{
    esp_err_t first_err = ESP_OK;

    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        if (!s_led_strips[i].configured) {
            continue;
        }
        stop_effect(&s_led_strips[i]);
        esp_err_t err = clear_strip_locked(&s_led_strips[i]);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
    }

    return first_err;
}

esp_err_t node_hw_led_off(uint8_t strip)
{
    node_hw_led_strip_t *runtime = find_led_strip(strip);
    if (!runtime || !runtime->configured) {
        return ESP_ERR_NOT_FOUND;
    }
    stop_effect(runtime);
    ESP_LOGI(TAG, "off strip=%u", (unsigned)strip);
    return clear_strip_locked(runtime);
}

esp_err_t node_hw_led_solid(uint8_t strip,
                            uint8_t red,
                            uint8_t green,
                            uint8_t blue,
                            uint8_t white,
                            uint8_t brightness)
{
    node_hw_led_strip_t *runtime = find_led_strip(strip);
    if (!runtime || !runtime->configured) {
        return ESP_ERR_NOT_FOUND;
    }
    stop_effect(runtime);
    ESP_LOGI(TAG,
             "solid strip=%u color=%02x%02x%02x%02x brightness=%u",
             (unsigned)strip,
             (unsigned)red,
             (unsigned)green,
             (unsigned)blue,
             (unsigned)white,
             (unsigned)brightness);
    return fill_strip_locked(runtime, red, green, blue, white, brightness);
}

esp_err_t node_hw_led_run_effect(uint8_t strip,
                                 node_hw_led_effect_t effect,
                                 const node_hw_led_effect_config_t *config)
{
    node_hw_led_strip_t *runtime = find_led_strip(strip);

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime || !runtime->configured) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!runtime->effect_task) {
        ESP_LOGW(TAG, "run_effect strip=%u rejected: effect task missing", (unsigned)strip);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = activate_effect(runtime, effect, config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "run_effect strip=%u rejected: state update failed err=%s",
                 (unsigned)strip,
                 esp_err_to_name(err));
        return err;
    }
    xTaskNotifyGive(runtime->effect_task);
    return ESP_OK;
}
