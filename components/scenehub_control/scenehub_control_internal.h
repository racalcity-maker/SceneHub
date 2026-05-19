#pragma once

#include "scenehub_control.h"
#include "scenehub_state.h"

void scenehub_control_copy(char *dst, size_t dst_size, const char *src);
bool scenehub_control_persistence_enabled(void);
esp_err_t scenehub_control_prepare_result(const char *room_id,
                                          const char *action_id,
                                          scenehub_control_result_t *out_result);
void scenehub_control_set_result(scenehub_control_result_t *result,
                                 scenehub_control_status_t status,
                                 esp_err_t err,
                                 bool state_changed,
                                 const char *error_code,
                                 const char *message);
void scenehub_control_finish_success_with_invalidation(scenehub_control_result_t *result,
                                                       scenehub_state_slice_t slice,
                                                       const char *target_id,
                                                       const char *reason);
void scenehub_control_finish_success_no_state_change(scenehub_control_result_t *result);
void scenehub_control_fill_common_error(scenehub_control_result_t *result, esp_err_t err);
void scenehub_control_log_timer(const char *source,
                                const char *room_id,
                                const char *title,
                                const char *details);
void scenehub_control_log_device_action(const char *source,
                                        const char *device_id,
                                        bool warning,
                                        const char *command_id);
esp_err_t scenehub_control_finalize_api_result_with_invalidation(scenehub_control_result_t *result,
                                                                 esp_err_t err,
                                                                 scenehub_state_slice_t slice,
                                                                 const char *target_id,
                                                                 const char *reason);
esp_err_t scenehub_control_finalize_no_state_change_result(scenehub_control_result_t *result,
                                                           esp_err_t err);
