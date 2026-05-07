#include "hardware_io.h"

#include "hardware_io_internal.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "event_bus.h"
#include "sdkconfig.h"

#ifndef CONFIG_SCENEHUB_GPIO1_GPIO
#define CONFIG_SCENEHUB_GPIO1_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_GPIO2_GPIO
#define CONFIG_SCENEHUB_GPIO2_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_GPIO3_GPIO
#define CONFIG_SCENEHUB_GPIO3_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_GPIO4_GPIO
#define CONFIG_SCENEHUB_GPIO4_GPIO -1
#endif
#ifndef CONFIG_SCENEHUB_GPIO1_MODE
#define CONFIG_SCENEHUB_GPIO1_MODE 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO2_MODE
#define CONFIG_SCENEHUB_GPIO2_MODE 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO3_MODE
#define CONFIG_SCENEHUB_GPIO3_MODE 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO4_MODE
#define CONFIG_SCENEHUB_GPIO4_MODE 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO_ACTIVE_LOW
#define CONFIG_SCENEHUB_GPIO_ACTIVE_LOW 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO_PULLUP
#define CONFIG_SCENEHUB_GPIO_PULLUP 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO_PULLDOWN
#define CONFIG_SCENEHUB_GPIO_PULLDOWN 0
#endif
#ifndef CONFIG_SCENEHUB_GPIO_DEBOUNCE_MS
#define CONFIG_SCENEHUB_GPIO_DEBOUNCE_MS 30
#endif

#define GPIO_POLL_INTERVAL_US 5000ULL

typedef struct {
    int gpio;
    hardware_io_gpio_mode_t mode;
    bool active_low;
    bool enabled;
    bool physical_high;
    bool active;
    bool candidate_physical_high;
    uint64_t candidate_since_ms;
    uint64_t last_change_ms;
    esp_timer_handle_t pulse_timer;
    bool pulse_active;
    bool pulse_restore_active;
} hardware_io_gpio_t;

typedef struct {
    uint8_t channel;
    bool old_active;
    bool physical_high;
    bool active;
} gpio_change_t;

static hardware_io_gpio_t s_gpios[HARDWARE_IO_GPIO_CHANNEL_COUNT] = {
    {.gpio = CONFIG_SCENEHUB_GPIO1_GPIO, .mode = CONFIG_SCENEHUB_GPIO1_MODE, .active_low = CONFIG_SCENEHUB_GPIO_ACTIVE_LOW},
    {.gpio = CONFIG_SCENEHUB_GPIO2_GPIO, .mode = CONFIG_SCENEHUB_GPIO2_MODE, .active_low = CONFIG_SCENEHUB_GPIO_ACTIVE_LOW},
    {.gpio = CONFIG_SCENEHUB_GPIO3_GPIO, .mode = CONFIG_SCENEHUB_GPIO3_MODE, .active_low = CONFIG_SCENEHUB_GPIO_ACTIVE_LOW},
    {.gpio = CONFIG_SCENEHUB_GPIO4_GPIO, .mode = CONFIG_SCENEHUB_GPIO4_MODE, .active_low = CONFIG_SCENEHUB_GPIO_ACTIVE_LOW},
};
static esp_timer_handle_t s_gpio_poll_timer = NULL;

static bool hardware_io_gpio_channel_valid(uint8_t channel)
{
    return channel >= 1 && channel <= HARDWARE_IO_GPIO_CHANNEL_COUNT;
}

static bool gpio_mode_valid(hardware_io_gpio_mode_t mode)
{
    return mode == HARDWARE_IO_GPIO_MODE_DISABLED ||
           mode == HARDWARE_IO_GPIO_MODE_INPUT ||
           mode == HARDWARE_IO_GPIO_MODE_OUTPUT;
}

static bool gpio_active_from_level(const hardware_io_gpio_t *gpio, bool physical_high)
{
    return gpio && gpio->active_low ? !physical_high : physical_high;
}

static int gpio_level_from_active(const hardware_io_gpio_t *gpio, bool active)
{
    return gpio && gpio->active_low ? (active ? 0 : 1) : (active ? 1 : 0);
}

static esp_err_t hardware_io_gpio_write_locked(uint8_t channel, bool active)
{
    hardware_io_gpio_t *line = NULL;
    if (!hardware_io_gpio_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    line = &s_gpios[channel - 1];
    if (!line->enabled || line->mode != HARDWARE_IO_GPIO_MODE_OUTPUT) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = gpio_set_level((gpio_num_t)line->gpio, gpio_level_from_active(line, active));
    if (err != ESP_OK) {
        return err;
    }
    line->active = active;
    line->physical_high = gpio_level_from_active(line, active) != 0;
    line->last_change_ms = hardware_io_now_ms();
    return ESP_OK;
}

static void gpio_stop_pulse_locked(hardware_io_gpio_t *line)
{
    if (line && line->pulse_timer) {
        (void)esp_timer_stop(line->pulse_timer);
    }
    if (line) {
        line->pulse_active = false;
    }
}

static void gpio_post_event(uint8_t channel, const char *event_id)
{
    event_bus_message_t message = {
        .type = EVENT_DEVICE_CONTROL,
        .payload_type = EVENT_BUS_PAYLOAD_DEVICE_CONTROL,
    };
    snprintf(message.topic, sizeof(message.topic), "system_gpio/%u", (unsigned)channel);
    snprintf(message.payload, sizeof(message.payload), "gpio.%s", event_id);
    snprintf(message.data.device_control.device_id,
             sizeof(message.data.device_control.device_id),
             "internal");
    snprintf(message.data.device_control.action_id,
             sizeof(message.data.device_control.action_id),
             "gpio.%s",
             event_id);
    snprintf(message.data.device_control.source,
             sizeof(message.data.device_control.source),
             "event");
    (void)event_bus_post_priority(&message, EVENT_BUS_PRIORITY_HIGH, 0);
}

static void gpio_publish_change(const gpio_change_t *change)
{
    char channel_event[24] = {0};
    if (!change) {
        return;
    }
    snprintf(channel_event, sizeof(channel_event), "ch%u_changed", (unsigned)change->channel);
    gpio_post_event(change->channel, channel_event);

    snprintf(channel_event,
             sizeof(channel_event),
             "ch%u_%s",
             (unsigned)change->channel,
             change->physical_high ? "high" : "low");
    gpio_post_event(change->channel, channel_event);

    if (change->old_active != change->active) {
        snprintf(channel_event,
                 sizeof(channel_event),
                 "ch%u_%s",
                 (unsigned)change->channel,
                 change->active ? "active" : "inactive");
        gpio_post_event(change->channel, channel_event);
    }
}

static void gpio_poll_timer(void *arg)
{
    static gpio_change_t changes[HARDWARE_IO_GPIO_CHANNEL_COUNT];
    size_t change_count = 0;
    uint64_t now_ms = hardware_io_now_ms();
    (void)arg;
    memset(changes, 0, sizeof(changes));

    if (hardware_io_lock() != ESP_OK) {
        return;
    }
    for (uint8_t i = 0; i < HARDWARE_IO_GPIO_CHANNEL_COUNT; ++i) {
        hardware_io_gpio_t *line = &s_gpios[i];
        if (!line->enabled || line->mode != HARDWARE_IO_GPIO_MODE_INPUT) {
            continue;
        }
        bool physical_high = gpio_get_level((gpio_num_t)line->gpio) != 0;
        if (physical_high != line->candidate_physical_high) {
            line->candidate_physical_high = physical_high;
            line->candidate_since_ms = now_ms;
            continue;
        }
        if (physical_high == line->physical_high) {
            continue;
        }
        if (now_ms - line->candidate_since_ms < CONFIG_SCENEHUB_GPIO_DEBOUNCE_MS) {
            continue;
        }
        bool old_active = line->active;
        bool active = gpio_active_from_level(line, physical_high);
        line->physical_high = physical_high;
        line->active = active;
        line->last_change_ms = now_ms;
        changes[change_count++] = (gpio_change_t) {
            .channel = (uint8_t)(i + 1),
            .old_active = old_active,
            .physical_high = physical_high,
            .active = active,
        };
    }
    hardware_io_unlock();

    for (size_t i = 0; i < change_count; ++i) {
        gpio_publish_change(&changes[i]);
    }
}

static void gpio_pulse_timer(void *arg)
{
    uint8_t channel = (uint8_t)(uintptr_t)arg;
    if (!hardware_io_gpio_channel_valid(channel) || hardware_io_lock() != ESP_OK) {
        return;
    }
    hardware_io_gpio_t *line = &s_gpios[channel - 1];
    bool restore_active = line->pulse_restore_active;
    line->pulse_active = false;
    (void)hardware_io_gpio_write_locked(channel, restore_active);
    hardware_io_unlock();
}

esp_err_t hardware_io_gpio_init_locked(void)
{
    esp_err_t err = ESP_OK;
    bool any_input = false;
    for (uint8_t i = 0; i < HARDWARE_IO_GPIO_CHANNEL_COUNT; ++i) {
        hardware_io_gpio_t *line = &s_gpios[i];
        if (!gpio_mode_valid(line->mode)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (line->mode == HARDWARE_IO_GPIO_MODE_DISABLED) {
            line->enabled = false;
            line->active = false;
            line->physical_high = false;
            continue;
        }
        if (line->gpio < 0) {
            return ESP_ERR_INVALID_ARG;
        }
        if (!GPIO_IS_VALID_GPIO(line->gpio)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (line->mode == HARDWARE_IO_GPIO_MODE_OUTPUT && !GPIO_IS_VALID_OUTPUT_GPIO(line->gpio)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (line->mode == HARDWARE_IO_GPIO_MODE_OUTPUT) {
            int safe_level = gpio_level_from_active(line, false);
            err = gpio_set_level((gpio_num_t)line->gpio, safe_level);
            if (err != ESP_OK) {
                return err;
            }
            gpio_config_t cfg = {
                .pin_bit_mask = 1ULL << line->gpio,
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            err = gpio_config(&cfg);
            if (err != ESP_OK) {
                return err;
            }
            err = gpio_set_level((gpio_num_t)line->gpio, safe_level);
            if (err != ESP_OK) {
                return err;
            }
            esp_timer_create_args_t timer_args = {
                .callback = gpio_pulse_timer,
                .arg = (void *)(uintptr_t)(i + 1),
                .name = "gpio_pulse",
            };
            err = esp_timer_create(&timer_args, &line->pulse_timer);
            if (err != ESP_OK) {
                return err;
            }
            line->physical_high = safe_level != 0;
            line->active = false;
        } else {
            gpio_config_t cfg = {
                .pin_bit_mask = 1ULL << line->gpio,
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = CONFIG_SCENEHUB_GPIO_PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
                .pull_down_en = CONFIG_SCENEHUB_GPIO_PULLDOWN ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            err = gpio_config(&cfg);
            if (err != ESP_OK) {
                return err;
            }
            line->physical_high = gpio_get_level((gpio_num_t)line->gpio) != 0;
            line->active = gpio_active_from_level(line, line->physical_high);
            line->candidate_physical_high = line->physical_high;
            line->candidate_since_ms = hardware_io_now_ms();
            any_input = true;
        }
        line->last_change_ms = hardware_io_now_ms();
        line->enabled = true;
    }
    if (!any_input) {
        return ESP_OK;
    }
    if (!s_gpio_poll_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = gpio_poll_timer,
            .name = "gpio_poll",
        };
        err = esp_timer_create(&timer_args, &s_gpio_poll_timer);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = esp_timer_start_periodic(s_gpio_poll_timer, GPIO_POLL_INTERVAL_US);
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

esp_err_t hardware_io_gpio_set(uint8_t channel, bool active)
{
    esp_err_t err = ESP_OK;
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (hardware_io_gpio_channel_valid(channel)) {
        gpio_stop_pulse_locked(&s_gpios[channel - 1]);
    }
    err = hardware_io_gpio_write_locked(channel, active);
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_gpio_toggle(uint8_t channel)
{
    esp_err_t err = ESP_OK;
    bool next = false;
    if (!hardware_io_gpio_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    gpio_stop_pulse_locked(&s_gpios[channel - 1]);
    next = !s_gpios[channel - 1].active;
    err = hardware_io_gpio_write_locked(channel, next);
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_gpio_pulse(uint8_t channel, bool active, uint32_t duration_ms)
{
    hardware_io_gpio_t *line = NULL;
    esp_err_t err = ESP_OK;
    if (!hardware_io_gpio_channel_valid(channel) || duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    line = &s_gpios[channel - 1];
    if (!line->enabled || line->mode != HARDWARE_IO_GPIO_MODE_OUTPUT || !line->pulse_timer) {
        hardware_io_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    gpio_stop_pulse_locked(line);
    line->pulse_restore_active = line->active;
    err = hardware_io_gpio_write_locked(channel, active);
    if (err == ESP_OK) {
        line->pulse_active = true;
        err = esp_timer_start_once(line->pulse_timer, (uint64_t)duration_ms * 1000ULL);
        if (err != ESP_OK) {
            line->pulse_active = false;
            (void)hardware_io_gpio_write_locked(channel, line->pulse_restore_active);
        }
    }
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_gpio_get(uint8_t channel, bool *out_active, bool *out_physical_high)
{
    if (!hardware_io_gpio_channel_valid(channel) || (!out_active && !out_physical_high)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    const hardware_io_gpio_t *line = &s_gpios[channel - 1];
    if (!line->enabled) {
        hardware_io_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (out_active) {
        *out_active = line->active;
    }
    if (out_physical_high) {
        *out_physical_high = line->physical_high;
    }
    hardware_io_unlock();
    return ESP_OK;
}

esp_err_t hardware_io_gpio_safe_off_all_locked(void)
{
    esp_err_t first_err = ESP_OK;
    for (uint8_t i = 0; i < HARDWARE_IO_GPIO_CHANNEL_COUNT; ++i) {
        hardware_io_gpio_t *line = &s_gpios[i];
        if (!line->enabled || line->mode != HARDWARE_IO_GPIO_MODE_OUTPUT) {
            continue;
        }
        gpio_stop_pulse_locked(line);
        esp_err_t err = hardware_io_gpio_write_locked((uint8_t)(i + 1), false);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}

esp_err_t hardware_io_gpio_get_status(hardware_io_gpio_status_t *out,
                                      size_t max_count,
                                      size_t *out_count)
{
    size_t count = HARDWARE_IO_GPIO_CHANNEL_COUNT;
    if (!out || max_count < count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < count; ++i) {
        const hardware_io_gpio_t *line = &s_gpios[i];
        out[i] = (hardware_io_gpio_status_t) {
            .channel = (uint8_t)(i + 1),
            .gpio = line->gpio,
            .enabled = line->enabled,
            .mode = line->mode,
            .active_low = line->active_low,
            .physical_high = line->physical_high,
            .active = line->active,
            .pulse_active = line->pulse_active,
            .last_change_ms = line->last_change_ms,
        };
    }
    hardware_io_unlock();
    if (out_count) {
        *out_count = count;
    }
    return ESP_OK;
}
