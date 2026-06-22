#include "node_hardware_io_internal.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "node_board.h"

#define NODE_HW_MOSFET_MAX_VALUE 255
#define NODE_HW_MOSFET_PWM_FREQ_HZ 1000
#define NODE_HW_MOSFET_FADE_TICK_US 20000ULL
#define NODE_HW_MOSFET_EFFECT_TICK_US 20000ULL
#define NODE_HW_MOSFET_BROKEN_ON_MIN_MS 30U
#define NODE_HW_MOSFET_BROKEN_ON_MAX_MS 180U
#define NODE_HW_MOSFET_BROKEN_OFF_MIN_MS 25U
#define NODE_HW_MOSFET_BROKEN_OFF_MAX_MS 420U

typedef enum {
    NODE_HW_MOSFET_EFFECT_NONE = 0,
    NODE_HW_MOSFET_EFFECT_BLINK,
    NODE_HW_MOSFET_EFFECT_BREATHE,
    NODE_HW_MOSFET_EFFECT_BROKEN_FLUORESCENT,
} node_hw_mosfet_effect_mode_t;

typedef struct {
    esp_timer_handle_t timer;
    bool active;
    node_hw_mosfet_effect_mode_t mode;
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
} node_hw_mosfet_effect_t;

typedef struct {
    bool configured;
    int gpio;
    bool active_low;
    ledc_channel_t ledc_channel;
    uint8_t value;
    uint8_t pulse_restore_value;
    uint8_t fade_from;
    uint8_t fade_target;
    uint32_t fade_duration_ms;
    uint64_t fade_started_ms;
    esp_timer_handle_t pulse_timer;
    esp_timer_handle_t fade_timer;
    node_hw_mosfet_effect_t effect;
} node_hw_mosfet_t;

static node_hw_mosfet_t s_mosfets[NODE_MOSFET_MAX] = {
    {.ledc_channel = LEDC_CHANNEL_0},
    {.ledc_channel = LEDC_CHANNEL_1},
    {.ledc_channel = LEDC_CHANNEL_2},
    {.ledc_channel = LEDC_CHANNEL_3},
};

static StaticSemaphore_t s_mosfet_mutex_storage;
static SemaphoreHandle_t s_mosfet_mutex;

static bool node_hw_mosfet_channel_valid(uint8_t channel)
{
    return channel >= 1 && channel <= NODE_MOSFET_MAX;
}

static uint32_t node_hw_mosfet_random_range(uint32_t min_ms, uint32_t max_ms)
{
    uint32_t span = 0;

    if (max_ms <= min_ms) {
        return min_ms;
    }
    span = max_ms - min_ms + 1U;
    return min_ms + (esp_random() % span);
}

static uint64_t node_hw_mosfet_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static bool node_hw_mosfet_lock(TickType_t timeout_ticks)
{
    return s_mosfet_mutex && xSemaphoreTake(s_mosfet_mutex, timeout_ticks) == pdTRUE;
}

static void node_hw_mosfet_unlock(void)
{
    if (s_mosfet_mutex) {
        xSemaphoreGive(s_mosfet_mutex);
    }
}

static esp_err_t node_hw_mosfet_write_locked(uint8_t channel, uint8_t value)
{
    if (!node_hw_mosfet_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }

    node_hw_mosfet_t *mosfet = &s_mosfets[channel - 1];
    if (!mosfet->configured) {
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t duty = mosfet->active_low ? (NODE_HW_MOSFET_MAX_VALUE - value) : value;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, mosfet->ledc_channel, duty);
    if (err != ESP_OK) {
        return err;
    }
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, mosfet->ledc_channel);
    if (err != ESP_OK) {
        return err;
    }

    mosfet->value = value;
    g_node_hw.mosfets[channel - 1].state_on = value != 0;
    return ESP_OK;
}

static void node_hw_mosfet_effect_clear_locked(node_hw_mosfet_effect_t *effect)
{
    if (!effect) {
        return;
    }
    if (effect->timer) {
        (void)esp_timer_stop(effect->timer);
    }
    effect->active = false;
    effect->mode = NODE_HW_MOSFET_EFFECT_NONE;
    effect->remaining = 0;
    effect->on_phase = false;
    effect->hold_phase = false;
}

static uint32_t node_hw_mosfet_next_broken_delay_ms(bool on_phase)
{
    return on_phase
               ? node_hw_mosfet_random_range(NODE_HW_MOSFET_BROKEN_ON_MIN_MS,
                                             NODE_HW_MOSFET_BROKEN_ON_MAX_MS)
               : node_hw_mosfet_random_range(NODE_HW_MOSFET_BROKEN_OFF_MIN_MS,
                                             NODE_HW_MOSFET_BROKEN_OFF_MAX_MS);
}

static void node_hw_mosfet_effect_cancel_locked(uint8_t channel)
{
    if (!node_hw_mosfet_channel_valid(channel)) {
        return;
    }
    node_hw_mosfet_effect_clear_locked(&s_mosfets[channel - 1].effect);
}

static esp_err_t node_hw_mosfet_stop_base_timers_locked(uint8_t channel)
{
    if (!node_hw_mosfet_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }

    node_hw_mosfet_t *mosfet = &s_mosfets[channel - 1];
    if (!mosfet->configured) {
        return ESP_ERR_NOT_FOUND;
    }
    if (mosfet->pulse_timer) {
        (void)esp_timer_stop(mosfet->pulse_timer);
    }
    if (mosfet->fade_timer) {
        (void)esp_timer_stop(mosfet->fade_timer);
    }
    return ESP_OK;
}

static void node_hw_mosfet_pulse_timer(void *arg)
{
    uint8_t channel = (uint8_t)(uintptr_t)arg;
    if (!node_hw_mosfet_channel_valid(channel) ||
        !node_hw_mosfet_lock(pdMS_TO_TICKS(20))) {
        return;
    }

    node_hw_mosfet_t *mosfet = &s_mosfets[channel - 1];
    (void)node_hw_mosfet_write_locked(channel, mosfet->pulse_restore_value);
    node_hw_mosfet_unlock();
}

static void node_hw_mosfet_fade_timer(void *arg)
{
    uint8_t channel = (uint8_t)(uintptr_t)arg;
    if (!node_hw_mosfet_channel_valid(channel) ||
        !node_hw_mosfet_lock(pdMS_TO_TICKS(20))) {
        return;
    }

    node_hw_mosfet_t *mosfet = &s_mosfets[channel - 1];
    uint64_t elapsed_ms = node_hw_mosfet_now_ms() - mosfet->fade_started_ms;
    if (elapsed_ms >= mosfet->fade_duration_ms || mosfet->fade_duration_ms == 0) {
        (void)node_hw_mosfet_write_locked(channel, mosfet->fade_target);
        if (mosfet->fade_timer) {
            (void)esp_timer_stop(mosfet->fade_timer);
        }
        node_hw_mosfet_unlock();
        return;
    }

    int32_t delta = (int32_t)mosfet->fade_target - (int32_t)mosfet->fade_from;
    uint8_t value = (uint8_t)((int32_t)mosfet->fade_from +
                              (delta * (int32_t)elapsed_ms) / (int32_t)mosfet->fade_duration_ms);
    (void)node_hw_mosfet_write_locked(channel, value);
    node_hw_mosfet_unlock();
}

static void node_hw_mosfet_effect_timer(void *arg)
{
    uint8_t channel = (uint8_t)(uintptr_t)arg;
    if (!node_hw_mosfet_channel_valid(channel) ||
        !node_hw_mosfet_lock(pdMS_TO_TICKS(20))) {
        return;
    }

    node_hw_mosfet_effect_t *effect = &s_mosfets[channel - 1].effect;
    if (!effect->active) {
        node_hw_mosfet_unlock();
        return;
    }

    if (effect->mode == NODE_HW_MOSFET_EFFECT_BLINK) {
        if (effect->on_phase) {
            (void)node_hw_mosfet_write_locked(channel, 0);
            effect->on_phase = false;
            (void)esp_timer_start_once(effect->timer, (uint64_t)effect->off_ms * 1000ULL);
            node_hw_mosfet_unlock();
            return;
        }
        if (effect->remaining > 0 && effect->remaining != UINT32_MAX) {
            effect->remaining--;
        }
        if (effect->remaining == 0) {
            effect->active = false;
            effect->mode = NODE_HW_MOSFET_EFFECT_NONE;
            (void)node_hw_mosfet_write_locked(channel, effect->final_value);
            node_hw_mosfet_unlock();
            return;
        }
        (void)node_hw_mosfet_write_locked(channel, effect->value);
        effect->on_phase = true;
        (void)esp_timer_start_once(effect->timer, (uint64_t)effect->on_ms * 1000ULL);
        node_hw_mosfet_unlock();
        return;
    }

    if (effect->mode == NODE_HW_MOSFET_EFFECT_BROKEN_FLUORESCENT) {
        uint32_t delay_ms = 0;

        effect->on_phase = !effect->on_phase;
        (void)node_hw_mosfet_write_locked(channel, effect->on_phase ? effect->value : 0);
        delay_ms = node_hw_mosfet_next_broken_delay_ms(effect->on_phase);
        (void)esp_timer_start_once(effect->timer, (uint64_t)delay_ms * 1000ULL);
        node_hw_mosfet_unlock();
        return;
    }

    uint64_t elapsed_ms = node_hw_mosfet_now_ms() - effect->phase_started_ms;
    if (!effect->hold_phase && elapsed_ms < effect->fade_ms) {
        int32_t from = effect->on_phase ? effect->min_value : effect->max_value;
        int32_t to = effect->on_phase ? effect->max_value : effect->min_value;
        int32_t value = from + ((to - from) * (int32_t)elapsed_ms) / (int32_t)effect->fade_ms;
        (void)node_hw_mosfet_write_locked(channel, (uint8_t)value);
        node_hw_mosfet_unlock();
        return;
    }
    if (!effect->hold_phase) {
        (void)node_hw_mosfet_write_locked(channel,
                                          effect->on_phase ? effect->max_value : effect->min_value);
        effect->hold_phase = true;
        effect->phase_started_ms = node_hw_mosfet_now_ms();
        node_hw_mosfet_unlock();
        return;
    }
    if (elapsed_ms < effect->hold_ms) {
        node_hw_mosfet_unlock();
        return;
    }
    if (!effect->on_phase) {
        if (effect->remaining > 0 && effect->remaining != UINT32_MAX) {
            effect->remaining--;
        }
        if (effect->remaining == 0) {
            effect->active = false;
            effect->mode = NODE_HW_MOSFET_EFFECT_NONE;
            (void)node_hw_mosfet_write_locked(channel, effect->final_value);
            node_hw_mosfet_unlock();
            return;
        }
    }
    effect->on_phase = !effect->on_phase;
    effect->hold_phase = false;
    effect->phase_started_ms = node_hw_mosfet_now_ms();
    node_hw_mosfet_unlock();
}

static esp_err_t configure_mosfet_slot(size_t idx, const node_output_pin_config_t *pin)
{
    if (!pin->enabled || pin->gpio < 0) {
        s_mosfets[idx].configured = false;
        s_mosfets[idx].gpio = -1;
        s_mosfets[idx].value = 0;
        return ESP_OK;
    }
    if (!node_board_gpio_is_allowed(pin->gpio) || !GPIO_IS_VALID_OUTPUT_GPIO(pin->gpio)) {
        return ESP_ERR_INVALID_ARG;
    }

    node_hw_assign_output_slot(&g_node_hw.mosfets[idx], pin->gpio, pin->active_low);
    s_mosfets[idx].configured = true;
    s_mosfets[idx].gpio = pin->gpio;
    s_mosfets[idx].active_low = pin->active_low;
    s_mosfets[idx].value = 0;
    g_node_hw.status.configured_mosfets++;
    return ESP_OK;
}

esp_err_t node_hw_mosfet_init(const node_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mosfet_mutex) {
        s_mosfet_mutex = xSemaphoreCreateMutexStatic(&s_mosfet_mutex_storage);
    }
    if (!s_mosfet_mutex) {
        return ESP_ERR_NO_MEM;
    }

    bool any_configured = false;
    for (size_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(configure_mosfet_slot(i, &config->mosfets[i]));
        any_configured = any_configured || s_mosfets[i].configured;
    }
    if (!any_configured) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = NODE_HW_MOSFET_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        return err;
    }

    for (uint8_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        node_hw_mosfet_t *mosfet = &s_mosfets[i];
        if (!mosfet->configured) {
            continue;
        }

        ledc_channel_config_t channel_cfg = {
            .gpio_num = mosfet->gpio,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = mosfet->ledc_channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = mosfet->active_low ? NODE_HW_MOSFET_MAX_VALUE : 0,
            .hpoint = 0,
        };
        err = ledc_channel_config(&channel_cfg);
        if (err != ESP_OK) {
            return err;
        }

        if (!mosfet->pulse_timer) {
            esp_timer_create_args_t pulse_args = {
                .callback = node_hw_mosfet_pulse_timer,
                .arg = (void *)(uintptr_t)(i + 1),
                .name = "node_mf_pulse",
            };
            err = esp_timer_create(&pulse_args, &mosfet->pulse_timer);
            if (err != ESP_OK) {
                return err;
            }
        }
        if (!mosfet->fade_timer) {
            esp_timer_create_args_t fade_args = {
                .callback = node_hw_mosfet_fade_timer,
                .arg = (void *)(uintptr_t)(i + 1),
                .name = "node_mf_fade",
            };
            err = esp_timer_create(&fade_args, &mosfet->fade_timer);
            if (err != ESP_OK) {
                return err;
            }
        }
        if (!mosfet->effect.timer) {
            esp_timer_create_args_t effect_args = {
                .callback = node_hw_mosfet_effect_timer,
                .arg = (void *)(uintptr_t)(i + 1),
                .name = "node_mf_fx",
            };
            err = esp_timer_create(&effect_args, &mosfet->effect.timer);
            if (err != ESP_OK) {
                return err;
            }
        }
    }
    return ESP_OK;
}

esp_err_t node_hw_mosfet_set(uint8_t channel, bool on)
{
    return node_hw_mosfet_set_value(channel, on ? NODE_HW_MOSFET_MAX_VALUE : 0);
}

esp_err_t node_hw_mosfet_set_value(uint8_t channel, uint8_t value)
{
    if (!node_hw_mosfet_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_hw_mosfet_lock(pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }

    node_hw_mosfet_t *mosfet = &s_mosfets[channel - 1];
    if (!mosfet->configured) {
        node_hw_mosfet_unlock();
        return ESP_ERR_NOT_FOUND;
    }

    (void)node_hw_mosfet_stop_base_timers_locked(channel);
    node_hw_mosfet_effect_cancel_locked(channel);
    esp_err_t err = node_hw_mosfet_write_locked(channel, value);
    node_hw_mosfet_unlock();
    return err;
}

esp_err_t node_hw_mosfet_fade(uint8_t channel, uint8_t target, uint32_t duration_ms)
{
    if (!node_hw_mosfet_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (duration_ms == 0) {
        return node_hw_mosfet_set_value(channel, target);
    }
    if (!node_hw_mosfet_lock(pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }

    node_hw_mosfet_t *mosfet = &s_mosfets[channel - 1];
    if (!mosfet->configured || !mosfet->fade_timer) {
        node_hw_mosfet_unlock();
        return mosfet->configured ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }

    if (mosfet->pulse_timer) {
        (void)esp_timer_stop(mosfet->pulse_timer);
    }
    node_hw_mosfet_effect_cancel_locked(channel);
    (void)esp_timer_stop(mosfet->fade_timer);
    mosfet->fade_from = mosfet->value;
    mosfet->fade_target = target;
    mosfet->fade_duration_ms = duration_ms;
    mosfet->fade_started_ms = node_hw_mosfet_now_ms();
    esp_err_t err = esp_timer_start_periodic(mosfet->fade_timer, NODE_HW_MOSFET_FADE_TICK_US);
    node_hw_mosfet_unlock();
    return err;
}

esp_err_t node_hw_mosfet_pulse(uint8_t channel, uint8_t value, uint32_t duration_ms)
{
    if (!node_hw_mosfet_channel_valid(channel) || duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_hw_mosfet_lock(pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }

    node_hw_mosfet_t *mosfet = &s_mosfets[channel - 1];
    if (!mosfet->configured || !mosfet->pulse_timer) {
        node_hw_mosfet_unlock();
        return mosfet->configured ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }

    if (mosfet->fade_timer) {
        (void)esp_timer_stop(mosfet->fade_timer);
    }
    node_hw_mosfet_effect_cancel_locked(channel);
    (void)esp_timer_stop(mosfet->pulse_timer);
    mosfet->pulse_restore_value = mosfet->value;
    esp_err_t err = node_hw_mosfet_write_locked(channel, value);
    if (err == ESP_OK) {
        err = esp_timer_start_once(mosfet->pulse_timer, (uint64_t)duration_ms * 1000ULL);
        if (err != ESP_OK) {
            (void)node_hw_mosfet_write_locked(channel, mosfet->pulse_restore_value);
        }
    }
    node_hw_mosfet_unlock();
    return err;
}

esp_err_t node_hw_mosfet_blink(uint8_t channel,
                               uint8_t value,
                               uint32_t on_ms,
                               uint32_t off_ms,
                               uint32_t count,
                               uint8_t final_value)
{
    if (!node_hw_mosfet_channel_valid(channel) || on_ms == 0 || off_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_hw_mosfet_lock(pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }

    node_hw_mosfet_t *mosfet = &s_mosfets[channel - 1];
    node_hw_mosfet_effect_t *effect = &mosfet->effect;
    if (!mosfet->configured || !effect->timer) {
        node_hw_mosfet_unlock();
        return mosfet->configured ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = node_hw_mosfet_stop_base_timers_locked(channel);
    if (err != ESP_OK) {
        node_hw_mosfet_unlock();
        return err;
    }
    node_hw_mosfet_effect_clear_locked(effect);
    effect->active = true;
    effect->mode = NODE_HW_MOSFET_EFFECT_BLINK;
    effect->value = value;
    effect->final_value = final_value;
    effect->on_phase = true;
    effect->on_ms = on_ms;
    effect->off_ms = off_ms;
    effect->remaining = count == 0 ? UINT32_MAX : count;
    err = node_hw_mosfet_write_locked(channel, value);
    if (err == ESP_OK) {
        err = esp_timer_start_once(effect->timer, (uint64_t)on_ms * 1000ULL);
    }
    if (err != ESP_OK) {
        node_hw_mosfet_effect_clear_locked(effect);
        (void)node_hw_mosfet_write_locked(channel, final_value);
    }
    node_hw_mosfet_unlock();
    return err;
}

esp_err_t node_hw_mosfet_breathe(uint8_t channel,
                                 uint8_t min_value,
                                 uint8_t max_value,
                                 uint32_t fade_ms,
                                 uint32_t hold_ms,
                                 uint32_t count,
                                 uint8_t final_value)
{
    if (!node_hw_mosfet_channel_valid(channel) || fade_ms == 0 || min_value > max_value) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_hw_mosfet_lock(pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }

    node_hw_mosfet_t *mosfet = &s_mosfets[channel - 1];
    node_hw_mosfet_effect_t *effect = &mosfet->effect;
    if (!mosfet->configured || !effect->timer) {
        node_hw_mosfet_unlock();
        return mosfet->configured ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = node_hw_mosfet_stop_base_timers_locked(channel);
    if (err != ESP_OK) {
        node_hw_mosfet_unlock();
        return err;
    }
    node_hw_mosfet_effect_clear_locked(effect);
    effect->active = true;
    effect->mode = NODE_HW_MOSFET_EFFECT_BREATHE;
    effect->min_value = min_value;
    effect->max_value = max_value;
    effect->final_value = final_value;
    effect->fade_ms = fade_ms;
    effect->hold_ms = hold_ms;
    effect->remaining = count == 0 ? UINT32_MAX : count;
    effect->on_phase = true;
    effect->hold_phase = false;
    effect->phase_started_ms = node_hw_mosfet_now_ms();
    err = node_hw_mosfet_write_locked(channel, min_value);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(effect->timer, NODE_HW_MOSFET_EFFECT_TICK_US);
    }
    if (err != ESP_OK) {
        node_hw_mosfet_effect_clear_locked(effect);
        (void)node_hw_mosfet_write_locked(channel, final_value);
    }
    node_hw_mosfet_unlock();
    return err;
}

esp_err_t node_hw_mosfet_all_off(void)
{
    if (!node_hw_mosfet_lock(pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t first_err = ESP_OK;
    for (uint8_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        node_hw_mosfet_t *mosfet = &s_mosfets[i];
        if (!mosfet->configured) {
            continue;
        }
        (void)node_hw_mosfet_stop_base_timers_locked((uint8_t)(i + 1));
        node_hw_mosfet_effect_cancel_locked((uint8_t)(i + 1));
        esp_err_t err = node_hw_mosfet_write_locked((uint8_t)(i + 1), 0);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    node_hw_mosfet_unlock();
    return first_err;
}

esp_err_t node_hw_mosfet_broken_fluorescent(uint8_t channel, uint8_t value)
{
    node_hw_mosfet_t *mosfet = NULL;
    node_hw_mosfet_effect_t *effect = NULL;
    esp_err_t err = ESP_OK;
    uint32_t delay_ms = 0;

    if (!node_hw_mosfet_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_hw_mosfet_lock(pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }

    mosfet = &s_mosfets[channel - 1];
    effect = &mosfet->effect;
    if (!mosfet->configured || !effect->timer) {
        node_hw_mosfet_unlock();
        return mosfet->configured ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }

    err = node_hw_mosfet_stop_base_timers_locked(channel);
    if (err != ESP_OK) {
        node_hw_mosfet_unlock();
        return err;
    }
    node_hw_mosfet_effect_clear_locked(effect);
    effect->active = true;
    effect->mode = NODE_HW_MOSFET_EFFECT_BROKEN_FLUORESCENT;
    effect->value = value;
    effect->on_phase = true;
    err = node_hw_mosfet_write_locked(channel, value);
    if (err == ESP_OK) {
        delay_ms = node_hw_mosfet_next_broken_delay_ms(true);
        err = esp_timer_start_once(effect->timer, (uint64_t)delay_ms * 1000ULL);
    }
    if (err != ESP_OK) {
        node_hw_mosfet_effect_clear_locked(effect);
        (void)node_hw_mosfet_write_locked(channel, 0);
    }
    node_hw_mosfet_unlock();
    return err;
}
