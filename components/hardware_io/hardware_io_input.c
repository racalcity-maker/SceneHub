#include "hardware_io.h"

#include "hardware_io_internal.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "event_bus.h"
#include "sdkconfig.h"

#ifndef CONFIG_SCENEHUB_INPUT1_GPIO
#define CONFIG_SCENEHUB_INPUT1_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_INPUT2_GPIO
#define CONFIG_SCENEHUB_INPUT2_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_INPUT3_GPIO
#define CONFIG_SCENEHUB_INPUT3_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_INPUT4_GPIO
#define CONFIG_SCENEHUB_INPUT4_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_INPUT_ACTIVE_LOW
#define CONFIG_SCENEHUB_INPUT_ACTIVE_LOW 1
#endif
#ifndef CONFIG_SCENEHUB_INPUT_PULLUP
#define CONFIG_SCENEHUB_INPUT_PULLUP 1
#endif
#ifndef CONFIG_SCENEHUB_INPUT_PULLDOWN
#define CONFIG_SCENEHUB_INPUT_PULLDOWN 0
#endif
#ifndef CONFIG_SCENEHUB_INPUT_DEBOUNCE_MS
#define CONFIG_SCENEHUB_INPUT_DEBOUNCE_MS 30
#endif

#define INPUT_POLL_INTERVAL_US 5000ULL

typedef struct {
    int gpio;
    bool active_low;
    bool enabled;
    bool physical_high;
    bool active;
    bool candidate_physical_high;
    uint64_t candidate_since_ms;
    uint64_t last_change_ms;
} hardware_io_input_t;

typedef struct {
    uint8_t channel;
    bool old_active;
    bool physical_high;
    bool active;
} input_change_t;

static hardware_io_input_t s_inputs[HARDWARE_IO_INPUT_CHANNEL_COUNT] = {
    {.gpio = CONFIG_SCENEHUB_INPUT1_GPIO, .active_low = CONFIG_SCENEHUB_INPUT_ACTIVE_LOW},
    {.gpio = CONFIG_SCENEHUB_INPUT2_GPIO, .active_low = CONFIG_SCENEHUB_INPUT_ACTIVE_LOW},
    {.gpio = CONFIG_SCENEHUB_INPUT3_GPIO, .active_low = CONFIG_SCENEHUB_INPUT_ACTIVE_LOW},
    {.gpio = CONFIG_SCENEHUB_INPUT4_GPIO, .active_low = CONFIG_SCENEHUB_INPUT_ACTIVE_LOW},
};
static esp_timer_handle_t s_input_poll_timer = NULL;

static bool hardware_io_input_channel_valid(uint8_t channel)
{
    return channel >= 1 && channel <= HARDWARE_IO_INPUT_CHANNEL_COUNT;
}

static bool input_active_from_level(const hardware_io_input_t *input, bool physical_high)
{
    return input && input->active_low ? !physical_high : physical_high;
}

static void input_post_event(uint8_t channel, const char *event_id)
{
    event_bus_message_t message = {
        .type = EVENT_DEVICE_CONTROL,
        .payload_type = EVENT_BUS_PAYLOAD_DEVICE_CONTROL,
    };
    snprintf(message.topic, sizeof(message.topic), "system_input/%u", (unsigned)channel);
    snprintf(message.payload, sizeof(message.payload), "input.%s", event_id);
    snprintf(message.data.device_control.device_id,
             sizeof(message.data.device_control.device_id),
             "internal");
    snprintf(message.data.device_control.action_id,
             sizeof(message.data.device_control.action_id),
             "input.%s",
             event_id);
    snprintf(message.data.device_control.source,
             sizeof(message.data.device_control.source),
             "event");
    (void)event_bus_post_priority(&message, EVENT_BUS_PRIORITY_HIGH, 0);
}

static void input_publish_change(const input_change_t *change)
{
    char channel_event[24] = {0};
    if (!change) {
        return;
    }
    snprintf(channel_event, sizeof(channel_event), "ch%u_changed", (unsigned)change->channel);
    input_post_event(change->channel, channel_event);

    snprintf(channel_event,
             sizeof(channel_event),
             "ch%u_%s",
             (unsigned)change->channel,
             change->physical_high ? "high" : "low");
    input_post_event(change->channel, channel_event);

    if (change->old_active != change->active) {
        snprintf(channel_event,
                 sizeof(channel_event),
                 "ch%u_%s",
                 (unsigned)change->channel,
                 change->active ? "pressed" : "released");
        input_post_event(change->channel, channel_event);
    }
}

static void input_poll_timer(void *arg)
{
    static input_change_t changes[HARDWARE_IO_INPUT_CHANNEL_COUNT];
    size_t change_count = 0;
    uint64_t now_ms = hardware_io_now_ms();
    (void)arg;
    memset(changes, 0, sizeof(changes));

    if (hardware_io_lock() != ESP_OK) {
        return;
    }
    for (uint8_t i = 0; i < HARDWARE_IO_INPUT_CHANNEL_COUNT; ++i) {
        hardware_io_input_t *input = &s_inputs[i];
        if (!input->enabled) {
            continue;
        }
        bool physical_high = gpio_get_level((gpio_num_t)input->gpio) != 0;
        if (physical_high != input->candidate_physical_high) {
            input->candidate_physical_high = physical_high;
            input->candidate_since_ms = now_ms;
            continue;
        }
        if (physical_high == input->physical_high) {
            continue;
        }
        if (now_ms - input->candidate_since_ms < CONFIG_SCENEHUB_INPUT_DEBOUNCE_MS) {
            continue;
        }
        bool old_active = input->active;
        bool active = input_active_from_level(input, physical_high);
        input->physical_high = physical_high;
        input->active = active;
        input->last_change_ms = now_ms;
        changes[change_count++] = (input_change_t) {
            .channel = (uint8_t)(i + 1),
            .old_active = old_active,
            .physical_high = physical_high,
            .active = active,
        };
    }
    hardware_io_unlock();

    for (size_t i = 0; i < change_count; ++i) {
        input_publish_change(&changes[i]);
    }
}

esp_err_t hardware_io_input_init_locked(void)
{
    esp_err_t err = ESP_OK;
    bool any_enabled = false;
    for (uint8_t i = 0; i < HARDWARE_IO_INPUT_CHANNEL_COUNT; ++i) {
        hardware_io_input_t *input = &s_inputs[i];
        if (input->gpio < 0) {
            input->enabled = false;
            input->physical_high = false;
            input->active = false;
            input->candidate_physical_high = false;
            continue;
        }
        if (!GPIO_IS_VALID_GPIO(input->gpio)) {
            return ESP_ERR_INVALID_ARG;
        }
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << input->gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = CONFIG_SCENEHUB_INPUT_PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = CONFIG_SCENEHUB_INPUT_PULLDOWN ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&cfg);
        if (err != ESP_OK) {
            return err;
        }
        input->physical_high = gpio_get_level((gpio_num_t)input->gpio) != 0;
        input->active = input_active_from_level(input, input->physical_high);
        input->candidate_physical_high = input->physical_high;
        input->candidate_since_ms = hardware_io_now_ms();
        input->last_change_ms = input->candidate_since_ms;
        input->enabled = true;
        any_enabled = true;
    }
    if (!any_enabled) {
        return ESP_OK;
    }
    if (!s_input_poll_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = input_poll_timer,
            .name = "input_poll",
        };
        err = esp_timer_create(&timer_args, &s_input_poll_timer);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = esp_timer_start_periodic(s_input_poll_timer, INPUT_POLL_INTERVAL_US);
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

esp_err_t hardware_io_input_get(uint8_t channel, bool *out_active, bool *out_physical_high)
{
    if (!hardware_io_input_channel_valid(channel) || (!out_active && !out_physical_high)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    const hardware_io_input_t *input = &s_inputs[channel - 1];
    if (!input->enabled) {
        hardware_io_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (out_active) {
        *out_active = input->active;
    }
    if (out_physical_high) {
        *out_physical_high = input->physical_high;
    }
    hardware_io_unlock();
    return ESP_OK;
}

esp_err_t hardware_io_input_get_status(hardware_io_input_status_t *out,
                                       size_t max_count,
                                       size_t *out_count)
{
    size_t count = HARDWARE_IO_INPUT_CHANNEL_COUNT;
    if (!out || max_count < count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < count; ++i) {
        const hardware_io_input_t *input = &s_inputs[i];
        out[i] = (hardware_io_input_status_t) {
            .channel = (uint8_t)(i + 1),
            .gpio = input->gpio,
            .enabled = input->enabled,
            .active_low = input->active_low,
            .physical_high = input->physical_high,
            .active = input->active,
            .debounce_ms = CONFIG_SCENEHUB_INPUT_DEBOUNCE_MS,
            .last_change_ms = input->last_change_ms,
        };
    }
    hardware_io_unlock();
    if (out_count) {
        *out_count = count;
    }
    return ESP_OK;
}
