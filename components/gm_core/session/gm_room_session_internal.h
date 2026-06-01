#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gm_room_session.h"
#include "room_scenario.h"

#define GM_ROOM_SESSION_EVENT_QUEUE_LEN 32
#define GM_ROOM_SESSION_RUNTIME_QUEUE_LEN 32
#define GM_ROOM_SESSION_EVENT_QUEUE_WAIT_TICKS pdMS_TO_TICKS(5)
#define GM_ROOM_SESSION_RUNTIME_QUEUE_WAIT_TICKS pdMS_TO_TICKS(5)
#define GM_ROOM_SESSION_RUNTIME_QUEUE_CRITICAL_WAIT_TICKS pdMS_TO_TICKS(50)
#define GM_ROOM_SESSION_EVENT_TASK_STACK 12288
#define GM_ROOM_SESSION_RUNTIME_TASK_STACK 16384

typedef enum {
    GM_ROOM_RUNTIME_CAUSE_WAKE = 0,
    GM_ROOM_RUNTIME_CAUSE_EVENT,
} gm_room_runtime_cause_kind_t;

typedef struct {
    gm_room_runtime_cause_kind_t kind;
    scenehub_event_t event;
} gm_room_runtime_cause_t;

extern gm_room_session_t g_gm_room_sessions[GM_SESSION_MAX_ROOMS];
extern QueueHandle_t s_event_queue;
extern QueueHandle_t s_runtime_queue;

gm_room_session_t *alloc_session_locked(const char *room_id);
gm_room_session_t *find_session_mutable_locked(const char *room_id);
esp_err_t gm_room_session_sessions_lock(void);
void gm_room_session_sessions_unlock(void);
void gm_room_session_mark_session_changed_locked(gm_room_session_t *session);
void gm_room_session_defer_cancel_request_locked(const char *request_id);
bool gm_room_session_wait_event_matches_message(const gm_room_scenario_wait_event_match_t *expected,
                                                const scenehub_event_t *message);
void gm_room_session_scenario_clear_wait_locked(gm_room_session_t *session);
void scenario_clear_branch_runtimes_locked(gm_room_session_t *session);
void scenario_clear_running_snapshot_locked(gm_room_session_t *session);
void scenario_clear_flags_locked(gm_room_session_t *session);
esp_err_t scenario_init_branch_runtimes_locked(
    gm_room_session_t *session,
    const gm_room_session_prepared_scenario_t *prepared_scenario);
void gm_room_session_store_prepared_scenario_locked(
    gm_room_session_t *session,
    const gm_room_session_prepared_scenario_t *prepared_scenario);
const gm_room_session_prepared_scenario_t *gm_room_session_get_prepared_scenario_locked(
    const gm_room_session_t *session);
void gm_room_session_clear_prepared_scenario_locked(gm_room_session_t *session);
esp_err_t finish_game_without_audio_locked(gm_room_session_t *session, uint64_t now_ms);
esp_err_t scenario_set_flag_locked(gm_room_session_t *session,
                                   const char *name,
                                   bool value);
bool scenario_wait_flags_met_locked(const gm_room_session_t *session);
bool scenario_apply_wait_timeout_locked(gm_room_session_t *session,
                                        const room_scenario_t *scenario,
                                        uint32_t now_ms);
void scenario_set_wait_skip_from_step_locked(gm_room_session_t *session,
                                             const room_scenario_step_t *step);
bool scenario_time_reached(uint32_t now_ms, uint32_t target_ms);
void scenario_set_error_locked(gm_room_session_t *session, const char *message);
const char *scenario_validation_error_message(const room_scenario_validation_report_t *report);
uint64_t gm_room_session_now_ms(void);
uint32_t gm_room_session_scenario_now_ms(void);

void gm_room_session_event_task(void *ctx);
void gm_room_session_set_async_workers_enabled_for_test(bool enabled);
void gm_room_session_record_event_drop(bool critical, bool runtime_queue);
esp_err_t gm_room_session_runtime_post_event_with_wait(const scenehub_event_t *message,
                                                       TickType_t wait_ticks);
esp_err_t gm_room_session_runtime_post_event(const scenehub_event_t *message);
void gm_room_session_runtime_wake(void);
void gm_room_session_runtime_task(void *ctx);
