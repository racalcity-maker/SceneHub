#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GM_TIMER_IDLE = 0,
    GM_TIMER_RUNNING,
    GM_TIMER_PAUSED,
    GM_TIMER_FINISHED,
} gm_timer_state_t;

typedef struct {
    gm_timer_state_t state;
    uint32_t duration_ms;
    uint32_t remaining_ms;
    uint64_t started_at_ms;
    uint64_t last_updated_ms;
    uint64_t paused_at_ms;
} gm_timer_t;

void gm_timer_reset(gm_timer_t *timer, uint32_t duration_ms, uint64_t now_ms);
esp_err_t gm_timer_start(gm_timer_t *timer, uint32_t duration_ms, uint64_t now_ms);
esp_err_t gm_timer_pause(gm_timer_t *timer, uint64_t now_ms);
esp_err_t gm_timer_resume(gm_timer_t *timer, uint64_t now_ms);
esp_err_t gm_timer_finish(gm_timer_t *timer, uint64_t now_ms);
esp_err_t gm_timer_set_remaining(gm_timer_t *timer, uint32_t remaining_ms, uint64_t now_ms);
esp_err_t gm_timer_add_time(gm_timer_t *timer, int32_t delta_ms, uint64_t now_ms);
uint32_t gm_timer_get_remaining(const gm_timer_t *timer, uint64_t now_ms);
bool gm_timer_is_active(const gm_timer_t *timer, uint64_t now_ms);

#ifdef __cplusplus
}
#endif
