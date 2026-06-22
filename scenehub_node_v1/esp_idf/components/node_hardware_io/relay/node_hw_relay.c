#include "node_hardware_io_internal.h"

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define NODE_HW_RELAY_BROKEN_ON_MIN_MS 30U
#define NODE_HW_RELAY_BROKEN_ON_MAX_MS 180U
#define NODE_HW_RELAY_BROKEN_OFF_MIN_MS 25U
#define NODE_HW_RELAY_BROKEN_OFF_MAX_MS 420U

typedef struct {
    esp_timer_handle_t timer;
    bool active;
    bool on_phase;
} node_hw_relay_effect_t;

static node_hw_relay_effect_t s_relay_effects[NODE_RELAY_MAX];
static StaticSemaphore_t s_relay_mutex_storage;
static SemaphoreHandle_t s_relay_mutex;

static bool node_hw_relay_channel_valid(uint8_t channel)
{
    return channel >= 1 && channel <= NODE_RELAY_MAX;
}

static bool node_hw_relay_lock(TickType_t timeout_ticks)
{
    return s_relay_mutex && xSemaphoreTake(s_relay_mutex, timeout_ticks) == pdTRUE;
}

static void node_hw_relay_unlock(void)
{
    if (s_relay_mutex) {
        xSemaphoreGive(s_relay_mutex);
    }
}

static uint32_t node_hw_relay_random_range(uint32_t min_ms, uint32_t max_ms)
{
    uint32_t span = 0;

    if (max_ms <= min_ms) {
        return min_ms;
    }
    span = max_ms - min_ms + 1U;
    return min_ms + (esp_random() % span);
}

static void node_hw_relay_effect_clear_locked(uint8_t channel)
{
    node_hw_relay_effect_t *effect = NULL;

    if (!node_hw_relay_channel_valid(channel)) {
        return;
    }
    effect = &s_relay_effects[channel - 1];
    if (effect->timer) {
        (void)esp_timer_stop(effect->timer);
    }
    effect->active = false;
    effect->on_phase = false;
}

static uint32_t node_hw_relay_next_effect_delay_ms(bool on_phase)
{
    return on_phase
               ? node_hw_relay_random_range(NODE_HW_RELAY_BROKEN_ON_MIN_MS,
                                            NODE_HW_RELAY_BROKEN_ON_MAX_MS)
               : node_hw_relay_random_range(NODE_HW_RELAY_BROKEN_OFF_MIN_MS,
                                            NODE_HW_RELAY_BROKEN_OFF_MAX_MS);
}

static void node_hw_relay_effect_timer(void *arg)
{
    uint8_t channel = (uint8_t)(uintptr_t)arg;
    node_hw_relay_effect_t *effect = NULL;
    node_hw_output_slot_t *slot = NULL;
    uint32_t delay_ms = 0;

    if (!node_hw_relay_channel_valid(channel) ||
        !node_hw_relay_lock(pdMS_TO_TICKS(20))) {
        return;
    }

    effect = &s_relay_effects[channel - 1];
    slot = &g_node_hw.relays[channel - 1];
    if (!effect->active || !slot->configured) {
        node_hw_relay_unlock();
        return;
    }

    effect->on_phase = !effect->on_phase;
    (void)node_hw_output_slot_set(slot, effect->on_phase);
    delay_ms = node_hw_relay_next_effect_delay_ms(effect->on_phase);
    (void)esp_timer_start_once(effect->timer, (uint64_t)delay_ms * 1000ULL);
    node_hw_relay_unlock();
}

static esp_err_t configure_relay_slot(size_t idx, const node_output_pin_config_t *pin)
{
    if (!pin->enabled || pin->gpio < 0) {
        return ESP_OK;
    }
    esp_err_t err = node_hw_configure_output_gpio(pin->gpio, pin->active_low);
    if (err != ESP_OK) {
        return err;
    }
    node_hw_assign_output_slot(&g_node_hw.relays[idx], pin->gpio, pin->active_low);
    g_node_hw.status.configured_relays++;
    return ESP_OK;
}

esp_err_t node_hw_relay_init(const node_config_t *config)
{
    esp_err_t err = ESP_OK;

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_relay_mutex) {
        s_relay_mutex = xSemaphoreCreateMutexStatic(&s_relay_mutex_storage);
    }
    if (!s_relay_mutex) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < NODE_RELAY_MAX; ++i) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(configure_relay_slot(i, &config->relays[i]));
        if (!g_node_hw.relays[i].configured || s_relay_effects[i].timer) {
            continue;
        }
        esp_timer_create_args_t effect_args = {
            .callback = node_hw_relay_effect_timer,
            .arg = (void *)(uintptr_t)(i + 1U),
            .name = "node_rl_fx",
        };
        err = esp_timer_create(&effect_args, &s_relay_effects[i].timer);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t node_hw_relay_set(uint8_t channel, bool on)
{
    node_hw_output_slot_t *slot = NULL;
    esp_err_t err = ESP_OK;

    if (!node_hw_relay_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_hw_relay_lock(pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }
    slot = &g_node_hw.relays[channel - 1];
    if (!slot->configured) {
        node_hw_relay_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    node_hw_relay_effect_clear_locked(channel);
    err = node_hw_output_slot_set(slot, on);
    node_hw_relay_unlock();
    return err;
}

esp_err_t node_hw_relay_broken_fluorescent(uint8_t channel)
{
    node_hw_output_slot_t *slot = NULL;
    node_hw_relay_effect_t *effect = NULL;
    esp_err_t err = ESP_OK;
    uint32_t delay_ms = 0;

    if (!node_hw_relay_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_hw_relay_lock(pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }
    slot = &g_node_hw.relays[channel - 1];
    effect = &s_relay_effects[channel - 1];
    if (!slot->configured || !effect->timer) {
        node_hw_relay_unlock();
        return slot->configured ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }

    node_hw_relay_effect_clear_locked(channel);
    effect->active = true;
    effect->on_phase = true;
    err = node_hw_output_slot_set(slot, true);
    if (err == ESP_OK) {
        delay_ms = node_hw_relay_next_effect_delay_ms(true);
        err = esp_timer_start_once(effect->timer, (uint64_t)delay_ms * 1000ULL);
    }
    if (err != ESP_OK) {
        node_hw_relay_effect_clear_locked(channel);
        (void)node_hw_output_slot_set(slot, false);
    }
    node_hw_relay_unlock();
    return err;
}

esp_err_t node_hw_relay_all_off(void)
{
    esp_err_t first_err = ESP_OK;

    for (size_t i = 0; i < NODE_RELAY_MAX; ++i) {
        if (!g_node_hw.relays[i].configured) {
            continue;
        }
        esp_err_t err = node_hw_relay_set((uint8_t)(i + 1U), false);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
    }

    return first_err;
}
