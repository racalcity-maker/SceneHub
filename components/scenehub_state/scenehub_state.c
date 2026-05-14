#include "scenehub_state.h"

#include <stdbool.h>
#include <string.h>

#include "device_control_ingest.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gm_game_profile.h"
#include "gm_room_session.h"
#include "orchestrator_registry.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "ws_runtime.h"

static TaskHandle_t s_state_watcher_task = NULL;
static uint32_t s_last_combined_generation = 0;
static uint32_t s_last_ingest_generation = 0;
static uint32_t s_last_session_generation = 0;
static uint32_t s_ws_versions_generation = 0;
static uint32_t s_last_ws_broadcast_at_ms = 0;
static uint32_t s_pending_invalidation_mask = 0;
static bool s_ws_broadcast_pending = false;
static scenehub_state_versions_t s_last_versions = {0};
static size_t s_pending_explicit_count = 0;

typedef struct {
    scenehub_state_slice_t slice;
    char target_id[SCENEHUB_STATE_TARGET_ID_MAX_LEN];
    char reason[SCENEHUB_STATE_REASON_MAX_LEN];
} scenehub_state_pending_invalidation_t;

static scenehub_state_pending_invalidation_t s_pending_explicit[8];

#define SCENEHUB_STATE_WS_MIN_INTERVAL_MS 250U

enum {
    SCENEHUB_INVALIDATE_ROOM_CATALOG = 1u << 0,
    SCENEHUB_INVALIDATE_DEVICES_CATALOG = 1u << 1,
    SCENEHUB_INVALIDATE_ROOM_SCENARIOS = 1u << 2,
    SCENEHUB_INVALIDATE_ROOM_PROFILES = 1u << 3,
    SCENEHUB_INVALIDATE_DEVICES_RUNTIME = 1u << 4,
    SCENEHUB_INVALIDATE_ROOM_RUNTIME = 1u << 5,
    SCENEHUB_INVALIDATE_SYSTEM_SUMMARY = 1u << 6,
};

static void scenehub_state_copy_text(char *dst, size_t dst_size, const char *src)
{
    size_t len = 0;

    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }

    len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static uint32_t scenehub_state_combine_versions(const scenehub_state_versions_t *versions)
{
    if (!versions) {
        return 0;
    }
    return versions->static_generation ^ (versions->runtime_generation << 1);
}

static uint32_t scenehub_state_slice_mask(scenehub_state_slice_t slice)
{
    switch (slice) {
    case SCENEHUB_STATE_SLICE_ROOM_CATALOG:
        return SCENEHUB_INVALIDATE_ROOM_CATALOG;
    case SCENEHUB_STATE_SLICE_DEVICES_CATALOG:
        return SCENEHUB_INVALIDATE_DEVICES_CATALOG;
    case SCENEHUB_STATE_SLICE_ROOM_SCENARIOS:
        return SCENEHUB_INVALIDATE_ROOM_SCENARIOS;
    case SCENEHUB_STATE_SLICE_ROOM_PROFILES:
        return SCENEHUB_INVALIDATE_ROOM_PROFILES;
    case SCENEHUB_STATE_SLICE_DEVICES_RUNTIME:
        return SCENEHUB_INVALIDATE_DEVICES_RUNTIME;
    case SCENEHUB_STATE_SLICE_ROOM_RUNTIME:
        return SCENEHUB_INVALIDATE_ROOM_RUNTIME;
    case SCENEHUB_STATE_SLICE_SYSTEM_SUMMARY:
        return SCENEHUB_INVALIDATE_SYSTEM_SUMMARY;
    case SCENEHUB_STATE_SLICE_FULL_SNAPSHOT:
        return SCENEHUB_INVALIDATE_ROOM_CATALOG |
               SCENEHUB_INVALIDATE_DEVICES_CATALOG |
               SCENEHUB_INVALIDATE_ROOM_SCENARIOS |
               SCENEHUB_INVALIDATE_ROOM_PROFILES |
               SCENEHUB_INVALIDATE_DEVICES_RUNTIME |
               SCENEHUB_INVALIDATE_ROOM_RUNTIME |
               SCENEHUB_INVALIDATE_SYSTEM_SUMMARY;
    case SCENEHUB_STATE_SLICE_NONE:
    default:
        return 0;
    }
}

static const char *scenehub_state_slice_name(scenehub_state_slice_t slice)
{
    switch (slice) {
    case SCENEHUB_STATE_SLICE_ROOM_CATALOG:
        return "room.catalog";
    case SCENEHUB_STATE_SLICE_DEVICES_CATALOG:
        return "devices.catalog";
    case SCENEHUB_STATE_SLICE_ROOM_SCENARIOS:
        return "room.scenarios";
    case SCENEHUB_STATE_SLICE_ROOM_PROFILES:
        return "room.profiles";
    case SCENEHUB_STATE_SLICE_DEVICES_RUNTIME:
        return "devices.runtime";
    case SCENEHUB_STATE_SLICE_ROOM_RUNTIME:
        return "room.runtime";
    case SCENEHUB_STATE_SLICE_SYSTEM_SUMMARY:
        return "system.summary";
    case SCENEHUB_STATE_SLICE_FULL_SNAPSHOT:
        return "full.snapshot";
    case SCENEHUB_STATE_SLICE_NONE:
    default:
        return "";
    }
}

static const char *scenehub_state_slice_scope(scenehub_state_slice_t slice)
{
    switch (slice) {
    case SCENEHUB_STATE_SLICE_ROOM_SCENARIOS:
    case SCENEHUB_STATE_SLICE_ROOM_PROFILES:
    case SCENEHUB_STATE_SLICE_ROOM_RUNTIME:
        return "room";
    case SCENEHUB_STATE_SLICE_DEVICES_RUNTIME:
        return "device";
    case SCENEHUB_STATE_SLICE_FULL_SNAPSHOT:
        return "recovery";
    case SCENEHUB_STATE_SLICE_ROOM_CATALOG:
    case SCENEHUB_STATE_SLICE_DEVICES_CATALOG:
    case SCENEHUB_STATE_SLICE_SYSTEM_SUMMARY:
    case SCENEHUB_STATE_SLICE_NONE:
    default:
        return "global";
    }
}

static uint32_t scenehub_state_slice_generation(scenehub_state_slice_t slice,
                                                const scenehub_state_versions_t *versions)
{
    if (!versions) {
        return 0;
    }

    switch (slice) {
    case SCENEHUB_STATE_SLICE_ROOM_CATALOG:
        return versions->rooms;
    case SCENEHUB_STATE_SLICE_DEVICES_CATALOG:
        return versions->devices;
    case SCENEHUB_STATE_SLICE_ROOM_SCENARIOS:
        return versions->scenarios;
    case SCENEHUB_STATE_SLICE_ROOM_PROFILES:
        return versions->profiles;
    case SCENEHUB_STATE_SLICE_DEVICES_RUNTIME:
        return versions->ingest;
    case SCENEHUB_STATE_SLICE_ROOM_RUNTIME:
        return versions->session;
    case SCENEHUB_STATE_SLICE_SYSTEM_SUMMARY:
    case SCENEHUB_STATE_SLICE_FULL_SNAPSHOT:
        return versions->combined_generation;
    case SCENEHUB_STATE_SLICE_NONE:
    default:
        return 0;
    }
}

static void scenehub_state_queue_explicit_invalidation(scenehub_state_slice_t slice,
                                                       const char *target_id,
                                                       const char *reason)
{
    size_t i = 0;

    if (slice == SCENEHUB_STATE_SLICE_NONE) {
        return;
    }

    for (i = 0; i < s_pending_explicit_count; ++i) {
        if (s_pending_explicit[i].slice == slice &&
            strcmp(s_pending_explicit[i].target_id, target_id ? target_id : "") == 0) {
            scenehub_state_copy_text(s_pending_explicit[i].reason,
                                     sizeof(s_pending_explicit[i].reason),
                                     reason);
            return;
        }
    }

    if (s_pending_explicit_count >= (sizeof(s_pending_explicit) / sizeof(s_pending_explicit[0]))) {
        return;
    }

    s_pending_explicit[s_pending_explicit_count].slice = slice;
    scenehub_state_copy_text(s_pending_explicit[s_pending_explicit_count].target_id,
                             sizeof(s_pending_explicit[s_pending_explicit_count].target_id),
                             target_id);
    scenehub_state_copy_text(s_pending_explicit[s_pending_explicit_count].reason,
                             sizeof(s_pending_explicit[s_pending_explicit_count].reason),
                             reason);
    ++s_pending_explicit_count;
}

static uint32_t scenehub_state_pending_explicit_mask(void)
{
    uint32_t mask = 0;
    size_t i = 0;

    for (i = 0; i < s_pending_explicit_count; ++i) {
        mask |= scenehub_state_slice_mask(s_pending_explicit[i].slice);
    }
    return mask;
}

static void scenehub_state_broadcast_explicit_invalidations(const scenehub_state_versions_t *versions)
{
    size_t i = 0;

    for (i = 0; i < s_pending_explicit_count; ++i) {
        if (s_pending_explicit[i].slice == SCENEHUB_STATE_SLICE_FULL_SNAPSHOT) {
            ws_runtime_resync_required_t resync = {
                .reason = s_pending_explicit[i].reason,
                .target_id = s_pending_explicit[i].target_id,
                .generation = scenehub_state_slice_generation(s_pending_explicit[i].slice, versions),
            };
            (void)ws_runtime_broadcast_resync_required(&resync);
            continue;
        }

        ws_runtime_invalidation_t event = {
            .slice = scenehub_state_slice_name(s_pending_explicit[i].slice),
            .target_id = s_pending_explicit[i].target_id,
            .scope = scenehub_state_slice_scope(s_pending_explicit[i].slice),
            .reason = s_pending_explicit[i].reason,
            .generation = scenehub_state_slice_generation(s_pending_explicit[i].slice, versions),
        };
        (void)ws_runtime_broadcast_invalidation(&event);
    }

    s_pending_explicit_count = 0;
}

static uint32_t scenehub_state_collect_change_mask(const scenehub_state_versions_t *prev,
                                                   const scenehub_state_versions_t *current)
{
    uint32_t mask = 0;

    if (!prev || !current) {
        return 0;
    }
    if (prev->rooms != current->rooms) {
        mask |= SCENEHUB_INVALIDATE_ROOM_CATALOG;
    }
    if (prev->devices != current->devices) {
        mask |= SCENEHUB_INVALIDATE_DEVICES_CATALOG | SCENEHUB_INVALIDATE_SYSTEM_SUMMARY;
    }
    if (prev->scenarios != current->scenarios) {
        mask |= SCENEHUB_INVALIDATE_ROOM_SCENARIOS;
    }
    if (prev->profiles != current->profiles) {
        mask |= SCENEHUB_INVALIDATE_ROOM_PROFILES;
    }
    if (prev->ingest != current->ingest) {
        mask |= SCENEHUB_INVALIDATE_DEVICES_RUNTIME | SCENEHUB_INVALIDATE_SYSTEM_SUMMARY;
    }
    if (prev->session != current->session) {
        mask |= SCENEHUB_INVALIDATE_ROOM_RUNTIME | SCENEHUB_INVALIDATE_SYSTEM_SUMMARY;
    }

    return mask;
}

static void scenehub_state_broadcast_invalidation(uint32_t mask,
                                                  const scenehub_state_versions_t *versions)
{
    ws_runtime_invalidation_t event = {0};

    if (!mask || !versions) {
        return;
    }

    event.scope = "global";
    event.target_id = "";

    if (mask & SCENEHUB_INVALIDATE_ROOM_CATALOG) {
        event.slice = "room.catalog";
        event.reason = "room_catalog_changed";
        event.generation = versions->rooms;
        (void)ws_runtime_broadcast_invalidation(&event);
    }
    if (mask & SCENEHUB_INVALIDATE_DEVICES_CATALOG) {
        event.slice = "devices.catalog";
        event.reason = "device_catalog_changed";
        event.generation = versions->devices;
        (void)ws_runtime_broadcast_invalidation(&event);
    }
    if (mask & SCENEHUB_INVALIDATE_ROOM_SCENARIOS) {
        event.slice = "room.scenarios";
        event.reason = "scenario_catalog_changed";
        event.generation = versions->scenarios;
        (void)ws_runtime_broadcast_invalidation(&event);
    }
    if (mask & SCENEHUB_INVALIDATE_ROOM_PROFILES) {
        event.slice = "room.profiles";
        event.reason = "profile_catalog_changed";
        event.generation = versions->profiles;
        (void)ws_runtime_broadcast_invalidation(&event);
    }
    if (mask & SCENEHUB_INVALIDATE_DEVICES_RUNTIME) {
        event.slice = "devices.runtime";
        event.reason = "device_runtime_changed";
        event.generation = versions->ingest;
        (void)ws_runtime_broadcast_invalidation(&event);
    }
    if (mask & SCENEHUB_INVALIDATE_ROOM_RUNTIME) {
        event.slice = "room.runtime";
        event.reason = "room_runtime_changed";
        event.generation = versions->session;
        (void)ws_runtime_broadcast_invalidation(&event);
    }
    if (mask & SCENEHUB_INVALIDATE_SYSTEM_SUMMARY) {
        event.slice = "system.summary";
        event.reason = "system_summary_changed";
        event.generation = versions->combined_generation;
        (void)ws_runtime_broadcast_invalidation(&event);
    }
}

esp_err_t scenehub_state_get_versions(scenehub_state_versions_t *out_versions)
{
    if (!out_versions) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_versions, 0, sizeof(*out_versions));
    out_versions->rooms = room_catalog_generation();
    out_versions->devices = quest_device_generation();
    out_versions->scenarios = room_scenario_generation();
    out_versions->profiles = gm_game_profile_generation();
    out_versions->ingest = device_control_ingest_generation();
    out_versions->session = gm_room_session_generation();
    out_versions->static_generation = out_versions->devices ^
                                      (out_versions->rooms << 1) ^
                                      (out_versions->scenarios << 2) ^
                                      (out_versions->profiles << 3);
    out_versions->runtime_generation = out_versions->ingest ^
                                       (out_versions->session << 1);
    out_versions->combined_generation = scenehub_state_combine_versions(out_versions);
    return ESP_OK;
}

void scenehub_state_notify_changed(void)
{
    scenehub_state_notify_invalidation(SCENEHUB_STATE_SLICE_NONE, NULL, NULL);
}

void scenehub_state_notify_invalidation(scenehub_state_slice_t slice,
                                        const char *target_id,
                                        const char *reason)
{
    scenehub_state_versions_t versions = {0};
    uint32_t change_mask = 0;
    uint32_t explicit_mask = 0;
    uint32_t coarse_mask = 0;
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (scenehub_state_get_versions(&versions) != ESP_OK) {
        return;
    }

    orchestrator_registry_invalidate();

    change_mask = scenehub_state_collect_change_mask(&s_last_versions, &versions);
    scenehub_state_queue_explicit_invalidation(slice, target_id, reason);

    s_last_ingest_generation = versions.ingest;
    s_last_session_generation = versions.session;
    if (change_mask == 0 &&
        !s_ws_broadcast_pending &&
        s_pending_invalidation_mask == 0 &&
        s_pending_explicit_count == 0) {
        return;
    }

    s_last_versions = versions;
    s_last_combined_generation = versions.combined_generation;
    s_pending_invalidation_mask |= change_mask;

    if (s_last_ws_broadcast_at_ms != 0 &&
        (now_ms - s_last_ws_broadcast_at_ms) < SCENEHUB_STATE_WS_MIN_INTERVAL_MS) {
        s_ws_broadcast_pending = true;
        return;
    }

    ++s_ws_versions_generation;
    s_last_ws_broadcast_at_ms = now_ms;
    s_ws_broadcast_pending = false;

    ws_runtime_versions_changed_t payload = {
        .generation = s_ws_versions_generation,
        .rooms = versions.rooms,
        .devices = versions.devices,
        .scenarios = versions.scenarios,
        .profiles = versions.profiles,
        .ingest = versions.ingest,
        .session = versions.session,
        .static_generation = versions.static_generation,
        .runtime_generation = versions.runtime_generation,
    };
    (void)ws_runtime_broadcast_versions_changed(&payload);
    explicit_mask = scenehub_state_pending_explicit_mask();
    scenehub_state_broadcast_explicit_invalidations(&versions);
    coarse_mask = s_pending_invalidation_mask & ~explicit_mask;
    scenehub_state_broadcast_invalidation(coarse_mask, &versions);
    s_pending_invalidation_mask = 0;
}

static void scenehub_state_watcher_task(void *ctx)
{
    (void)ctx;

    for (;;) {
        const uint32_t ingest_generation = device_control_ingest_generation();
        const uint32_t session_generation = gm_room_session_generation();
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        char changed_device_id[QUEST_ID_MAX_LEN] = {0};
        char changed_room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
        const bool ingest_changed = ingest_generation != s_last_ingest_generation;
        const bool session_changed = session_generation != s_last_session_generation;

        if (ingest_changed) {
            if (device_control_ingest_get_last_changed_device_id(changed_device_id,
                                                                 sizeof(changed_device_id)) != ESP_OK) {
                changed_device_id[0] = '\0';
            }
            scenehub_state_notify_invalidation(SCENEHUB_STATE_SLICE_DEVICES_RUNTIME,
                                               changed_device_id,
                                               "watcher_ingest");
        }
        if (session_changed) {
            if (gm_room_session_get_last_changed_room_id(changed_room_id,
                                                         sizeof(changed_room_id)) != ESP_OK) {
                changed_room_id[0] = '\0';
            }
            scenehub_state_notify_invalidation(SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                               changed_room_id,
                                               "watcher_session");
        } else if (!ingest_changed &&
                   s_ws_broadcast_pending &&
                   (s_last_ws_broadcast_at_ms == 0 ||
                    (now_ms - s_last_ws_broadcast_at_ms) >= SCENEHUB_STATE_WS_MIN_INTERVAL_MS)) {
            scenehub_state_notify_changed();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t scenehub_state_init(void)
{
    scenehub_state_versions_t versions = {0};
    esp_err_t err = scenehub_state_get_versions(&versions);
    if (err != ESP_OK) {
        return err;
    }

    s_last_combined_generation = versions.combined_generation;
    s_last_ingest_generation = versions.ingest;
    s_last_session_generation = versions.session;
    s_last_versions = versions;

    if (s_state_watcher_task) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreate(scenehub_state_watcher_task,
                                "scenehub_state",
                                4096,
                                NULL,
                                5,
                                &s_state_watcher_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
