#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "quest_common_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORCH_AUDIT_SOURCE_MAX_LEN 16
#define ORCH_AUDIT_ERROR_MAX_LEN  32
#define ORCH_AUDIT_ACTION_ID_MAX_LEN 96
#define ORCH_AUDIT_CAPACITY       64

typedef struct {
    uint64_t timestamp_ms;
    char source[ORCH_AUDIT_SOURCE_MAX_LEN];
    char device_id[QUEST_ID_MAX_LEN];
    char action_id[ORCH_AUDIT_ACTION_ID_MAX_LEN];
    bool success;
    char error_code[ORCH_AUDIT_ERROR_MAX_LEN];
} orchestrator_audit_entry_t;

esp_err_t orchestrator_audit_init(void);
esp_err_t orchestrator_audit_log_device_action(const char *source,
                                               const char *device_id,
                                               const char *action_id,
                                               bool success,
                                               const char *error_code);
esp_err_t orchestrator_audit_list_recent(size_t max_items,
                                         orchestrator_audit_entry_t *out_items,
                                         size_t *out_count);
void orchestrator_audit_reset(void);

#ifdef __cplusplus
}
#endif
