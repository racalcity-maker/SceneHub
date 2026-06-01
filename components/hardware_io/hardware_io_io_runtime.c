#include "hardware_io_io_internal.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "event_bus.h"
#include "quest_device.h"
#include "scenehub_config.h"

#define IO_POLL_INTERVAL_US 5000ULL

void hardware_io_io_stop_pulse_locked(hardware_io_io_t *line)
{
    if (line && line->pulse_timer) {
        (void)esp_timer_stop(line->pulse_timer);
    }
    if (line) {
        line->pulse_active = false;
    }
}

static void io_post_event(uint8_t channel, const char *event_id)
{
    scenehub_event_t message = {0};
    if (scenehub_event_make_device_control_event(&message,
                                                 QUEST_DEVICE_SYSTEM_IO_ID,
                                                 event_id,
                                                 NULL,
                                                 0) != ESP_OK) {
        return;
    }
    snprintf(message.topic, sizeof(message.topic), "system_io/%u", (unsigned)channel);
    snprintf(message.payload, sizeof(message.payload), "io.%s", event_id);
    (void)event_bus_post_priority(&message, EVENT_BUS_PRIORITY_HIGH, 0);
}

static void io_publish_change(const io_change_t *change)
{
    char channel_event[24] = {0};
    if (!change) {
        return;
    }
    snprintf(channel_event, sizeof(channel_event), "ch%u_changed", (unsigned)change->channel);
    io_post_event(change->channel, channel_event);

    snprintf(channel_event,
             sizeof(channel_event),
             "ch%u_%s",
             (unsigned)change->channel,
             change->physical_high ? "high" : "low");
    io_post_event(change->channel, channel_event);

    if (change->old_active != change->active) {
        snprintf(channel_event,
                 sizeof(channel_event),
                 "ch%u_%s",
                 (unsigned)change->channel,
                 change->active ? "active" : "inactive");
        io_post_event(change->channel, channel_event);
    }
}

static void io_poll_timer(void *arg)
{
    static io_change_t changes[HARDWARE_IO_IO_CHANNEL_COUNT];
    size_t change_count = 0;
    uint64_t now_ms = hardware_io_now_ms();
    (void)arg;
    memset(changes, 0, sizeof(changes));

    if (hardware_io_lock() != ESP_OK) {
        return;
    }
    for (uint8_t i = 0; i < HARDWARE_IO_IO_CHANNEL_COUNT; ++i) {
        hardware_io_io_t *line = &s_ios[i];
        if (!line->enabled || line->mode != HARDWARE_IO_IO_MODE_INPUT) {
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
        bool active = hardware_io_io_active_from_level(line, physical_high);
        line->physical_high = physical_high;
        line->active = active;
        line->last_change_ms = now_ms;
        changes[change_count++] = (io_change_t) {
            .channel = (uint8_t)(i + 1),
            .old_active = old_active,
            .physical_high = physical_high,
            .active = active,
        };
    }
    hardware_io_unlock();

    for (size_t i = 0; i < change_count; ++i) {
        io_publish_change(&changes[i]);
    }
}

static void io_pulse_timer(void *arg)
{
    uint8_t channel = (uint8_t)(uintptr_t)arg;
    if (!hardware_io_io_channel_valid(channel) || hardware_io_lock() != ESP_OK) {
        return;
    }
    hardware_io_io_t *line = &s_ios[channel - 1];
    bool restore_active = line->pulse_restore_active;
    line->pulse_active = false;
    (void)hardware_io_io_write_locked(channel, restore_active);
    hardware_io_unlock();
}

void hardware_io_io_stop_effect_locked(hardware_io_io_t *line)
{
    if (line && line->effect_timer) {
        (void)esp_timer_stop(line->effect_timer);
    }
    if (line) {
        line->effect_active = false;
        line->effect_on_phase = false;
        line->effect_remaining = 0;
    }
}

static void io_effect_timer(void *arg)
{
    uint8_t channel = (uint8_t)(uintptr_t)arg;
    if (!hardware_io_io_channel_valid(channel) || hardware_io_lock() != ESP_OK) {
        return;
    }
    hardware_io_io_t *line = &s_ios[channel - 1];
    if (!line->effect_active) {
        hardware_io_unlock();
        return;
    }
    if (line->effect_on_phase) {
        (void)hardware_io_io_write_locked(channel, false);
        line->effect_on_phase = false;
        uint32_t delay_ms = line->effect_off_ms ? line->effect_off_ms : 1;
        (void)esp_timer_start_once(line->effect_timer, (uint64_t)delay_ms * 1000ULL);
        hardware_io_unlock();
        return;
    }
    if (line->effect_remaining > 0 && line->effect_remaining != UINT32_MAX) {
        line->effect_remaining--;
    }
    if (line->effect_remaining == 0) {
        line->effect_active = false;
        (void)hardware_io_io_write_locked(channel, line->effect_final_active);
        hardware_io_unlock();
        return;
    }
    (void)hardware_io_io_write_locked(channel, true);
    line->effect_on_phase = true;
    uint32_t delay_ms = line->effect_on_ms ? line->effect_on_ms : 1;
    (void)esp_timer_start_once(line->effect_timer, (uint64_t)delay_ms * 1000ULL);
    hardware_io_unlock();
}

static bool io_any_input_locked(void)
{
    for (uint8_t i = 0; i < HARDWARE_IO_IO_CHANNEL_COUNT; ++i) {
        if (s_ios[i].enabled && s_ios[i].mode == HARDWARE_IO_IO_MODE_INPUT) {
            return true;
        }
    }
    return false;
}

esp_err_t hardware_io_io_ensure_poll_timer_locked(void)
{
    esp_err_t err = ESP_OK;
    if (!s_io_poll_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = io_poll_timer,
            .name = "io_poll",
        };
        err = esp_timer_create(&timer_args, &s_io_poll_timer);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = esp_timer_start_periodic(s_io_poll_timer, IO_POLL_INTERVAL_US);
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

void hardware_io_io_stop_poll_if_unused_locked(void)
{
    if (!io_any_input_locked() && s_io_poll_timer) {
        (void)esp_timer_stop(s_io_poll_timer);
    }
}

esp_err_t hardware_io_io_ensure_output_timers_locked(uint8_t channel, hardware_io_io_t *line)
{
    esp_err_t err = ESP_OK;
    if (!line->pulse_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = io_pulse_timer,
            .arg = (void *)(uintptr_t)channel,
            .name = "io_pulse",
        };
        err = esp_timer_create(&timer_args, &line->pulse_timer);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (!line->effect_timer) {
        esp_timer_create_args_t effect_args = {
            .callback = io_effect_timer,
            .arg = (void *)(uintptr_t)channel,
            .name = "io_blink",
        };
        err = esp_timer_create(&effect_args, &line->effect_timer);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t hardware_io_io_pulse(uint8_t channel, bool active, uint32_t duration_ms)
{
    hardware_io_io_t *line = NULL;
    esp_err_t err = ESP_OK;
    if (!hardware_io_io_channel_valid(channel) || duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    line = &s_ios[channel - 1];
    if (!line->enabled || line->mode != HARDWARE_IO_IO_MODE_OUTPUT || !line->pulse_timer) {
        hardware_io_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    hardware_io_io_stop_pulse_locked(line);
    hardware_io_io_stop_effect_locked(line);
    line->pulse_restore_active = line->active;
    err = hardware_io_io_write_locked(channel, active);
    if (err == ESP_OK) {
        line->pulse_active = true;
        err = esp_timer_start_once(line->pulse_timer, (uint64_t)duration_ms * 1000ULL);
        if (err != ESP_OK) {
            line->pulse_active = false;
            (void)hardware_io_io_write_locked(channel, line->pulse_restore_active);
        }
    }
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_io_blink(uint8_t channel,
                               uint32_t on_ms,
                               uint32_t off_ms,
                               uint32_t count,
                               bool final_active)
{
    hardware_io_io_t *line = NULL;
    esp_err_t err = ESP_OK;
    if (!hardware_io_io_channel_valid(channel) || on_ms == 0 || off_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hardware_io_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    line = &s_ios[channel - 1];
    if (!line->enabled || line->mode != HARDWARE_IO_IO_MODE_OUTPUT || !line->effect_timer) {
        hardware_io_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    hardware_io_io_stop_pulse_locked(line);
    hardware_io_io_stop_effect_locked(line);
    line->effect_active = true;
    line->effect_on_phase = true;
    line->effect_on_ms = on_ms;
    line->effect_off_ms = off_ms;
    line->effect_remaining = count == 0 ? UINT32_MAX : count;
    line->effect_final_active = final_active;
    err = hardware_io_io_write_locked(channel, true);
    if (err == ESP_OK) {
        err = esp_timer_start_once(line->effect_timer, (uint64_t)on_ms * 1000ULL);
    }
    if (err != ESP_OK) {
        line->effect_active = false;
        (void)hardware_io_io_write_locked(channel, final_active);
    }
    hardware_io_unlock();
    return err;
}

esp_err_t hardware_io_io_safe_off_all_locked(void)
{
    esp_err_t first_err = ESP_OK;
    for (uint8_t i = 0; i < HARDWARE_IO_IO_CHANNEL_COUNT; ++i) {
        hardware_io_io_t *line = &s_ios[i];
        if (!line->enabled || line->mode != HARDWARE_IO_IO_MODE_OUTPUT) {
            continue;
        }
        hardware_io_io_stop_pulse_locked(line);
        hardware_io_io_stop_effect_locked(line);
        esp_err_t err = hardware_io_io_write_locked((uint8_t)(i + 1), false);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}
