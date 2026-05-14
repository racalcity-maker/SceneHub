#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "quest_common_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORCH_TIMELINE_CAPACITY 128
#define ORCH_TIMELINE_SOURCE_MAX_LEN 16
#define ORCH_TIMELINE_TEXT_MAX_LEN 24
#define ORCH_TIMELINE_TITLE_MAX_LEN 64
#define ORCH_TIMELINE_DETAILS_MAX_LEN 128

typedef enum {
    ORCH_TIMELINE_TYPE_EVENT = 0,
    ORCH_TIMELINE_TYPE_DEVICE_STATUS,
    ORCH_TIMELINE_TYPE_RUNTIME_CHANGED,
    ORCH_TIMELINE_TYPE_SCENARIO_TRIGGERED,
    ORCH_TIMELINE_TYPE_TIMER_CHANGED,
    ORCH_TIMELINE_TYPE_DEVICE_ACTION,
    ORCH_TIMELINE_TYPE_ACTION_FAILED,
    ORCH_TIMELINE_TYPE_CONFIG_CHANGED,
} orchestrator_timeline_type_t;

typedef enum {
    ORCH_TIMELINE_SEVERITY_INFO = 0,
    ORCH_TIMELINE_SEVERITY_WARNING,
    ORCH_TIMELINE_SEVERITY_ERROR,
} orchestrator_timeline_severity_t;

typedef struct {
    uint64_t timestamp_ms;
    orchestrator_timeline_type_t type;
    char type_text[ORCH_TIMELINE_TEXT_MAX_LEN];
    orchestrator_timeline_severity_t severity;
    char severity_text[ORCH_TIMELINE_TEXT_MAX_LEN];
    char source[ORCH_TIMELINE_SOURCE_MAX_LEN];
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char device_id[QUEST_ID_MAX_LEN];
    char title[ORCH_TIMELINE_TITLE_MAX_LEN];
    char details[ORCH_TIMELINE_DETAILS_MAX_LEN];
} orchestrator_timeline_entry_t;

esp_err_t orchestrator_timeline_init(void);
esp_err_t orchestrator_timeline_log(orchestrator_timeline_type_t type,
                                    orchestrator_timeline_severity_t severity,
                                    const char *source,
                                    const char *room_id,
                                    const char *device_id,
                                    const char *title,
                                    const char *details);
esp_err_t orchestrator_timeline_list_recent(size_t max_items,
                                            orchestrator_timeline_entry_t *out_items,
                                            size_t *out_count);
void orchestrator_timeline_reset(void);

#ifdef __cplusplus
}
#endif
