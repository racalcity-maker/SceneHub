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
#define GM_ROOM_SESSION_EVENT_TASK_STACK 12288

extern gm_room_session_t g_gm_room_sessions[GM_SESSION_MAX_ROOMS];
extern QueueHandle_t s_event_queue;

void *gm_room_session_heap_alloc(size_t size);
esp_err_t gm_room_session_execute_quest_device_command_internal(
    const room_scenario_device_command_t *step_command,
    char *error,
    size_t error_size);

gm_room_session_t *alloc_session_locked(const char *room_id);
gm_room_session_t *find_session_mutable_locked(const char *room_id);
esp_err_t gm_room_session_sessions_lock(void);
void gm_room_session_sessions_unlock(void);
void gm_room_session_mark_session_changed_locked(gm_room_session_t *session);
void gm_room_session_scenario_clear_wait_locked(gm_room_session_t *session);
void scenario_clear_branch_runtimes_locked(gm_room_session_t *session);
void scenario_clear_running_snapshot_locked(gm_room_session_t *session);
void scenario_clear_flags_locked(gm_room_session_t *session);
uint16_t scenario_branch_end_index(const gm_room_scenario_branch_runtime_t *branch,
                                   const room_scenario_t *scenario);
esp_err_t scenario_init_branch_runtimes_locked(gm_room_session_t *session);
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
void gm_room_session_scenario_branch_load_into_session(
    gm_room_session_t *session,
    const gm_room_scenario_branch_runtime_t *branch);
void gm_room_session_scenario_branch_save_from_session(
    gm_room_scenario_branch_runtime_t *branch,
    const gm_room_session_t *session);
void gm_room_session_scenario_update_summary_from_branches_locked(gm_room_session_t *session);
uint64_t gm_room_session_now_ms(void);
uint32_t gm_room_session_scenario_now_ms(void);
esp_err_t gm_room_session_execute_branch_locked(gm_room_session_t *session,
                                                gm_room_scenario_branch_runtime_t *branch,
                                                uint32_t now_ms,
                                                uint8_t budget);

void gm_room_session_event_handler(const event_bus_message_t *message);
void gm_room_session_event_task(void *ctx);
