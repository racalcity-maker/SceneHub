#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "quest_common_limits.h"
#include "scenehub_events.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMMAND_EXECUTOR_SOURCE_MAX_LEN 16
#define COMMAND_EXECUTOR_REQUEST_ID_MAX_LEN 48
#define COMMAND_EXECUTOR_COMMAND_MAX_LEN 48

typedef struct {
    char source[COMMAND_EXECUTOR_SOURCE_MAX_LEN];
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char scenario_id[QUEST_SCENARIO_ID_MAX_LEN];
    char branch_id[QUEST_BRANCH_ID_MAX_LEN];
    char step_id[QUEST_STEP_ID_MAX_LEN];
    char device_id[QUEST_ID_MAX_LEN];
    char command_id[QUEST_ID_MAX_LEN];
    char params_json[QUEST_PAYLOAD_MAX_LEN];
    bool require_manual_allowed;
    bool require_scenario_allowed;
} command_executor_request_t;

typedef struct {
    bool result_required;
    uint32_t timeout_ms;
    char request_id[COMMAND_EXECUTOR_REQUEST_ID_MAX_LEN];
    char source_id[QUEST_EVENT_SOURCE_ID_MAX_LEN];
    char command[COMMAND_EXECUTOR_COMMAND_MAX_LEN];
    char error_code[COMMAND_EXECUTOR_COMMAND_MAX_LEN];
} command_executor_dispatch_t;

esp_err_t command_executor_execute(const command_executor_request_t *request,
                                   command_executor_dispatch_t *out_dispatch,
                                   char *error,
                                   size_t error_size);

esp_err_t command_executor_execute_device_command(const char *device_id,
                                                  const char *command_id,
                                                  const char *params_json);

void command_executor_on_event(const scenehub_event_t *message);
size_t command_executor_poll_timeouts(scenehub_event_t *out_events, size_t max_events);
uint64_t command_executor_next_timeout_deadline_ms(void);
void command_executor_cancel_request(const char *request_id);
void command_executor_reset_pending(void);

#ifdef __cplusplus
}
#endif
