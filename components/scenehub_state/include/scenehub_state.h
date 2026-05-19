#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t rooms;
    uint32_t devices;
    uint32_t scenarios;
    uint32_t profiles;
    uint32_t ingest;
    uint32_t session;
    uint32_t static_generation;
    uint32_t runtime_generation;
    uint32_t combined_generation;
} scenehub_state_versions_t;

#define SCENEHUB_STATE_TARGET_ID_MAX_LEN 32
#define SCENEHUB_STATE_REASON_MAX_LEN 48

typedef enum {
    SCENEHUB_STATE_SLICE_NONE = 0,
    SCENEHUB_STATE_SLICE_ROOM_CATALOG,
    SCENEHUB_STATE_SLICE_DEVICES_CATALOG,
    SCENEHUB_STATE_SLICE_ROOM_SCENARIOS,
    SCENEHUB_STATE_SLICE_ROOM_PROFILES,
    SCENEHUB_STATE_SLICE_GM_SIDEBAR_PRESETS,
    SCENEHUB_STATE_SLICE_DEVICES_RUNTIME,
    SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
    SCENEHUB_STATE_SLICE_SYSTEM_SUMMARY,
    SCENEHUB_STATE_SLICE_FULL_SNAPSHOT,
} scenehub_state_slice_t;

esp_err_t scenehub_state_init(void);
esp_err_t scenehub_state_get_versions(scenehub_state_versions_t *out_versions);
void scenehub_state_notify_changed(void);
void scenehub_state_notify_invalidation(scenehub_state_slice_t slice,
                                        const char *target_id,
                                        const char *reason);

#ifdef __cplusplus
}
#endif
