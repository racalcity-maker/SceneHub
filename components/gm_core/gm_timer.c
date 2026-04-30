#include "gm_timer.h"

#include <limits.h>
#include <string.h>

static uint32_t gm_timer_clamp_remaining(int64_t value_ms)
{
    if (value_ms <= 0) {
        return 0;
    }
    if (value_ms > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)value_ms;
}

uint32_t gm_timer_get_remaining(const gm_timer_t *timer, uint64_t now_ms)
{
    if (!timer) {
        return 0;
    }
    if (timer->state == GM_TIMER_IDLE || timer->state == GM_TIMER_FINISHED) {
        return timer->remaining_ms;
    }
    if (timer->state == GM_TIMER_PAUSED) {
        return timer->remaining_ms;
    }
    if (timer->state != GM_TIMER_RUNNING) {
        return timer->remaining_ms;
    }
    if (now_ms <= timer->last_updated_ms) {
        return timer->remaining_ms;
    }
    return gm_timer_clamp_remaining((int64_t)timer->remaining_ms - (int64_t)(now_ms - timer->last_updated_ms));
}

bool gm_timer_is_active(const gm_timer_t *timer, uint64_t now_ms)
{
    if (!timer) {
        return false;
    }
    return timer->state == GM_TIMER_RUNNING && gm_timer_get_remaining(timer, now_ms) > 0;
}

void gm_timer_reset(gm_timer_t *timer, uint32_t duration_ms, uint64_t now_ms)
{
    if (!timer) {
        return;
    }
    memset(timer, 0, sizeof(*timer));
    timer->state = GM_TIMER_IDLE;
    timer->duration_ms = duration_ms;
    timer->remaining_ms = duration_ms;
    timer->last_updated_ms = now_ms;
}

esp_err_t gm_timer_start(gm_timer_t *timer, uint32_t duration_ms, uint64_t now_ms)
{
    if (!timer) {
        return ESP_ERR_INVALID_ARG;
    }
    gm_timer_reset(timer, duration_ms, now_ms);
    timer->state = GM_TIMER_RUNNING;
    timer->started_at_ms = now_ms;
    timer->last_updated_ms = now_ms;
    return ESP_OK;
}

esp_err_t gm_timer_pause(gm_timer_t *timer, uint64_t now_ms)
{
    if (!timer) {
        return ESP_ERR_INVALID_ARG;
    }
    if (timer->state != GM_TIMER_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    timer->remaining_ms = gm_timer_get_remaining(timer, now_ms);
    timer->state = GM_TIMER_PAUSED;
    timer->paused_at_ms = now_ms;
    timer->last_updated_ms = now_ms;
    return ESP_OK;
}

esp_err_t gm_timer_resume(gm_timer_t *timer, uint64_t now_ms)
{
    if (!timer) {
        return ESP_ERR_INVALID_ARG;
    }
    if (timer->state != GM_TIMER_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    timer->state = GM_TIMER_RUNNING;
    timer->last_updated_ms = now_ms;
    timer->paused_at_ms = 0;
    return ESP_OK;
}

esp_err_t gm_timer_finish(gm_timer_t *timer, uint64_t now_ms)
{
    if (!timer) {
        return ESP_ERR_INVALID_ARG;
    }
    timer->remaining_ms = gm_timer_get_remaining(timer, now_ms);
    timer->state = GM_TIMER_FINISHED;
    timer->last_updated_ms = now_ms;
    return ESP_OK;
}

esp_err_t gm_timer_set_remaining(gm_timer_t *timer, uint32_t remaining_ms, uint64_t now_ms)
{
    if (!timer) {
        return ESP_ERR_INVALID_ARG;
    }
    timer->remaining_ms = remaining_ms;
    if (remaining_ms == 0) {
        timer->state = GM_TIMER_FINISHED;
    }
    timer->last_updated_ms = now_ms;
    return ESP_OK;
}

esp_err_t gm_timer_add_time(gm_timer_t *timer, int32_t delta_ms, uint64_t now_ms)
{
    uint32_t base_ms;
    if (!timer) {
        return ESP_ERR_INVALID_ARG;
    }
    base_ms = gm_timer_get_remaining(timer, now_ms);
    timer->remaining_ms = gm_timer_clamp_remaining((int64_t)base_ms + delta_ms);
    if (timer->remaining_ms == 0 && timer->state != GM_TIMER_IDLE) {
        timer->state = GM_TIMER_FINISHED;
    } else if (timer->state == GM_TIMER_FINISHED && timer->remaining_ms > 0) {
        timer->state = GM_TIMER_PAUSED;
    }
    timer->last_updated_ms = now_ms;
    return ESP_OK;
}
