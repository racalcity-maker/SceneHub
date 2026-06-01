#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "gm_game_profile.h"
#include "gm_room_session.h"
#include "hardware_io.h"
#include "quest_common_limits.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCENEHUB_CONTROL_ERROR_CODE_MAX_LEN 32
#define SCENEHUB_CONTROL_MESSAGE_MAX_LEN    96
#define SCENEHUB_CONTROL_ACTION_ID_MAX_LEN  32
#define SCENEHUB_CONTROL_REQUEST_ID_MAX_LEN 48
#define SCENEHUB_CONTROL_REMOTE_STATUS_MAX_LEN 16

typedef enum {
    SCENEHUB_CONTROL_STATUS_DONE = 0,
    SCENEHUB_CONTROL_STATUS_ACCEPTED,
    SCENEHUB_CONTROL_STATUS_REJECTED,
    SCENEHUB_CONTROL_STATUS_FAILED,
    SCENEHUB_CONTROL_STATUS_TIMEOUT,
} scenehub_control_status_t;

typedef struct {
    scenehub_control_status_t status;
    esp_err_t err;
    bool state_changed;
    bool has_request_id;
    bool has_remote_status;
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char action_id[SCENEHUB_CONTROL_ACTION_ID_MAX_LEN];
    char error_code[SCENEHUB_CONTROL_ERROR_CODE_MAX_LEN];
    char message[SCENEHUB_CONTROL_MESSAGE_MAX_LEN];
    char request_id[SCENEHUB_CONTROL_REQUEST_ID_MAX_LEN];
    char remote_status[SCENEHUB_CONTROL_REMOTE_STATUS_MAX_LEN];
} scenehub_control_result_t;

/*
 * Write-side control envelope. HTTP handlers may serialize this result, but
 * domain modules should not depend on Web UI response formats.
 */

typedef struct {
    char device_name[QUEST_DEVICE_NAME_MAX_LEN];
    char command_label[QUEST_DEVICE_NAME_MAX_LEN];
} scenehub_control_device_command_info_t;

typedef struct {
    char request_id[SCENEHUB_CONTROL_REQUEST_ID_MAX_LEN];
    cJSON *device_description;
} scenehub_control_device_interface_info_t;

const char *scenehub_control_status_str(scenehub_control_status_t status);
esp_err_t scenehub_control_init(void);
void scenehub_control_set_persistence_enabled_for_test(bool enabled);
esp_err_t scenehub_control_prepare_session_scenario(
    const room_scenario_t *scenario,
    gm_room_session_prepared_scenario_t *out);

esp_err_t scenehub_control_execute_room_action(const char *source,
                                               const char *room_id,
                                               const char *action_id,
                                               scenehub_control_result_t *out_result);
esp_err_t scenehub_control_timer_start(const char *source,
                                       const char *room_id,
                                       uint32_t duration_ms,
                                       scenehub_control_result_t *out_result);
esp_err_t scenehub_control_timer_pause(const char *source,
                                       const char *room_id,
                                       scenehub_control_result_t *out_result);
esp_err_t scenehub_control_timer_resume(const char *source,
                                        const char *room_id,
                                        scenehub_control_result_t *out_result);
esp_err_t scenehub_control_timer_reset(const char *source,
                                       const char *room_id,
                                       bool has_duration,
                                       uint32_t duration_ms,
                                       scenehub_control_result_t *out_result);
esp_err_t scenehub_control_timer_add(const char *source,
                                     const char *room_id,
                                     int32_t delta_ms,
                                     scenehub_control_result_t *out_result);
esp_err_t scenehub_control_session_finish(const char *source,
                                          const char *room_id,
                                          scenehub_control_result_t *out_result);
esp_err_t scenehub_control_hint_send(const char *source,
                                     const char *room_id,
                                     const char *message,
                                     scenehub_control_result_t *out_result);
esp_err_t scenehub_control_hint_clear(const char *source,
                                      const char *room_id,
                                      scenehub_control_result_t *out_result);
esp_err_t scenehub_control_select_profile(const char *source,
                                          const char *room_id,
                                          const char *profile_id,
                                          scenehub_control_result_t *out_result);
esp_err_t scenehub_control_save_profile(const char *source,
                                        const gm_game_profile_t *profile,
                                        scenehub_control_result_t *out_result);
esp_err_t scenehub_control_save_profile_payload(const char *source,
                                                const cJSON *payload,
                                                cJSON **out_profile_json,
                                                scenehub_control_result_t *out_result);
esp_err_t scenehub_control_delete_profile(const char *source,
                                          const char *profile_id,
                                          scenehub_control_result_t *out_result);
esp_err_t scenehub_control_import_profiles(const char *source,
                                           cJSON *root,
                                           scenehub_control_result_t *out_result);
esp_err_t scenehub_control_load_profiles(const char *source,
                                         scenehub_control_result_t *out_result);
esp_err_t scenehub_control_save_profiles_store(const char *source,
                                               scenehub_control_result_t *out_result);
esp_err_t scenehub_control_select_scenario(const char *source,
                                           const char *room_id,
                                           const char *scenario_id,
                                           scenehub_control_result_t *out_result);
esp_err_t scenehub_control_scenario_start(const char *source,
                                          const char *room_id,
                                          scenehub_control_result_t *out_result);
esp_err_t scenehub_control_scenario_stop(const char *source,
                                         const char *room_id,
                                         scenehub_control_result_t *out_result);
esp_err_t scenehub_control_scenario_next(const char *source,
                                         const char *room_id,
                                         const char *branch_id,
                                         scenehub_control_result_t *out_result);
esp_err_t scenehub_control_scenario_approve(const char *source,
                                            const char *room_id,
                                            scenehub_control_result_t *out_result);
esp_err_t scenehub_control_scenario_reset(const char *source,
                                          const char *room_id,
                                          scenehub_control_result_t *out_result);
esp_err_t scenehub_control_validate_scenario(const char *source,
                                             const room_scenario_t *scenario,
                                             room_scenario_validation_report_t *out_report,
                                             scenehub_control_result_t *out_result);
esp_err_t scenehub_control_validate_scenario_payload(const char *source,
                                                     const cJSON *payload,
                                                     char *out_scenario_id,
                                                     size_t out_scenario_id_size,
                                                     room_scenario_validation_report_t *out_report,
                                                     scenehub_control_result_t *out_result);
esp_err_t scenehub_control_validate_scenario_payload_into(
    const char *source,
    const cJSON *payload,
    room_scenario_t *scratch_scenario,
    char *out_scenario_id,
    size_t out_scenario_id_size,
    room_scenario_validation_report_t *out_report,
    scenehub_control_result_t *out_result);
esp_err_t scenehub_control_save_scenario(const char *source,
                                         const room_scenario_t *scenario,
                                         scenehub_control_result_t *out_result);
esp_err_t scenehub_control_save_scenario_payload(const char *source,
                                                 const cJSON *payload,
                                                 cJSON **out_scenario_json,
                                                 scenehub_control_result_t *out_result);
esp_err_t scenehub_control_delete_scenario(const char *source,
                                           const char *scenario_id,
                                           scenehub_control_result_t *out_result);
esp_err_t scenehub_control_import_scenarios(const char *source,
                                            cJSON *root,
                                            scenehub_control_result_t *out_result);
esp_err_t scenehub_control_load_scenarios(const char *source,
                                          scenehub_control_result_t *out_result);
esp_err_t scenehub_control_save_scenarios_store(const char *source,
                                                scenehub_control_result_t *out_result);
esp_err_t scenehub_control_save_device(const char *source,
                                       const quest_device_t *device,
                                       scenehub_control_result_t *out_result);
esp_err_t scenehub_control_save_device_payload(const char *source,
                                               const cJSON *payload,
                                               cJSON **out_device_json,
                                               scenehub_control_result_t *out_result);
esp_err_t scenehub_control_delete_device(const char *source,
                                         const char *device_id,
                                         scenehub_control_result_t *out_result);
esp_err_t scenehub_control_device_command_run(const char *source,
                                              const char *device_id,
                                              const char *command_id,
                                              const char *params_json,
                                              bool confirmed,
                                              scenehub_control_device_command_info_t *out_info,
                                              scenehub_control_result_t *out_result);
esp_err_t scenehub_control_device_describe_interface(
    const char *source,
    const char *client_id,
    scenehub_control_device_interface_info_t *out_info,
    scenehub_control_result_t *out_result);
esp_err_t scenehub_control_import_devices(const char *source,
                                          cJSON *root,
                                          scenehub_control_result_t *out_result);
esp_err_t scenehub_control_load_devices(const char *source,
                                        scenehub_control_result_t *out_result);
esp_err_t scenehub_control_save_devices_store(const char *source,
                                              scenehub_control_result_t *out_result);
esp_err_t scenehub_control_save_sidebar_presets_payload(const char *source,
                                                        const cJSON *payload,
                                                        scenehub_control_result_t *out_result);
esp_err_t scenehub_control_import_sidebar_presets(const char *source,
                                                  cJSON *root,
                                                  scenehub_control_result_t *out_result);
esp_err_t scenehub_control_load_sidebar_presets(const char *source,
                                                scenehub_control_result_t *out_result);
esp_err_t scenehub_control_save_room(const char *source,
                                     const char *room_id,
                                     const char *name,
                                     scenehub_control_result_t *out_result);
esp_err_t scenehub_control_delete_room(const char *source,
                                       const char *room_id,
                                       bool delete_content,
                                       bool *out_existed,
                                       size_t *out_removed_profiles,
                                       size_t *out_removed_scenarios,
                                       scenehub_control_result_t *out_result);
esp_err_t scenehub_control_hardware_io_set_mode(const char *source,
                                                uint8_t channel,
                                                hardware_io_io_mode_t mode,
                                                scenehub_control_result_t *out_result);

#ifdef __cplusplus
}
#endif
