#include "orchestrator_registry_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "event_bus.h"
#include "scenehub_command_result.h"

EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_cached_snapshot;
static SemaphoreHandle_t s_cache_mutex = NULL;
static StaticSemaphore_t s_cache_mutex_storage;
static portMUX_TYPE s_cache_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_cache_invalidate_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_cache_source_generation = 0;
static uint32_t s_cache_ingest_generation = 0;
static uint32_t s_cache_gm_generation = 0;
static uint32_t s_cache_version = 0;
static uint64_t s_cache_built_at_ms = 0;
static bool s_cache_valid = false;
static bool s_cache_invalidate_pending = false;
static bool s_event_handler_registered = false;

static void orch_registry_event_handler(const scenehub_event_t *message);
static esp_err_t orch_cache_ensure_mutex(void);

static esp_err_t orch_cache_ensure_mutex(void)
{
    if (s_cache_mutex) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_cache_mutex_init_lock);
    if (!s_cache_mutex) {
        s_cache_mutex = xSemaphoreCreateMutexStatic(&s_cache_mutex_storage);
    }
    portEXIT_CRITICAL(&s_cache_mutex_init_lock);
    return s_cache_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t orch_cache_lock(void)
{
    esp_err_t err = orch_cache_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }
    return (xSemaphoreTake(s_cache_mutex, portMAX_DELAY) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void orch_cache_unlock(void)
{
    if (s_cache_mutex) {
        xSemaphoreGive(s_cache_mutex);
    }
}

static void orch_cache_mark_invalidate_pending(void)
{
    portENTER_CRITICAL(&s_cache_invalidate_lock);
    s_cache_invalidate_pending = true;
    portEXIT_CRITICAL(&s_cache_invalidate_lock);
}

static bool orch_cache_take_invalidate_pending(void)
{
    bool pending = false;
    portENTER_CRITICAL(&s_cache_invalidate_lock);
    pending = s_cache_invalidate_pending;
    s_cache_invalidate_pending = false;
    portEXIT_CRITICAL(&s_cache_invalidate_lock);
    return pending;
}

static esp_err_t orch_current_config_generation(uint32_t *out_generation)
{
    if (!out_generation) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_generation = quest_device_generation() ^
                      (room_catalog_generation() << 1) ^
                      (room_scenario_generation() << 2) ^
                      (gm_game_profile_generation() << 3);
    return ESP_OK;
}

esp_err_t orchestrator_registry_init(void)
{
    esp_err_t err = ESP_OK;
    (void)gm_room_session_init();
    (void)room_scenario_init();
    err = orch_cache_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }
    if (!s_event_handler_registered) {
        err = event_bus_register_handler(orch_registry_event_handler);
        if (err != ESP_OK) {
            return err;
        }
        s_event_handler_registered = true;
    }
    return ESP_OK;
}

static void orch_registry_event_handler(const scenehub_event_t *message)
{
    if (!message) {
        return;
    }
    if (message->type == SCENEHUB_EVENT_DEVICE_CONFIG_CHANGED ||
        message->type == SCENEHUB_EVENT_RUNTIME_CONTROL ||
        scenehub_event_is_device_status(message) ||
        scenehub_event_is_device_runtime(message) ||
        scenehub_event_is_device_control(message)) {
        orch_cache_mark_invalidate_pending();
    }
}

static esp_err_t orch_cache_ensure_snapshot_locked(void)
{
    uint32_t source_generation = 0;
    uint32_t ingest_generation = device_control_ingest_generation();
    uint32_t gm_generation = gm_room_session_generation();
    uint64_t now_ms = orch_now_ms();
    bool expired = false;
    esp_err_t err = orch_current_config_generation(&source_generation);
    if (err != ESP_OK) {
        return err;
    }
    if (orch_cache_take_invalidate_pending()) {
        s_cache_valid = false;
    }

    if (s_cache_valid) {
        uint32_t ttl_ms = ORCH_REGISTRY_CACHE_TTL_MS;
        if (device_control_ingest_count() > 0 && ttl_ms < 1000) {
            ttl_ms = 1000;
        }
        expired = now_ms < s_cache_built_at_ms ||
                  (now_ms - s_cache_built_at_ms) >= ttl_ms;
        if (s_cache_source_generation == source_generation &&
            s_cache_ingest_generation == ingest_generation &&
            s_cache_gm_generation == gm_generation &&
            !expired) {
            return ESP_OK;
        }
    }

    err = orch_snapshot_builder_build_uncached(&s_cached_snapshot);
    if (err != ESP_OK) {
        s_cache_valid = false;
        return err;
    }
    s_cache_source_generation = s_cached_snapshot.generation;
    s_cache_ingest_generation = ingest_generation;
    s_cache_gm_generation = gm_generation;
    s_cache_built_at_ms = orch_now_ms();
    s_cache_version++;
    s_cached_snapshot.cache_version = s_cache_version;
    s_cached_snapshot.snapshot_built_at_ms = s_cache_built_at_ms;
    s_cache_valid = true;
    return ESP_OK;
}

esp_err_t orchestrator_registry_build_snapshot(orch_registry_snapshot_t *out)
{
    esp_err_t err;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    err = orch_cache_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = orch_cache_ensure_snapshot_locked();
    if (err == ESP_OK) {
        *out = s_cached_snapshot;
    }
    orch_cache_unlock();
    return err;
}

esp_err_t orchestrator_registry_get_system_summary(orch_gm_system_summary_t *out)
{
    quest_device_t *devices = NULL;
    size_t device_capacity = 0;
    size_t device_count = 0;
    uint64_t now_ms = orch_now_ms();
    bool services_degraded = false;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (orch_current_config_generation(&out->generation) != ESP_OK) {
        return ESP_FAIL;
    }
    out->generation ^= (device_control_ingest_generation() << 4);
    out->generation ^= (gm_room_session_generation() << 5);
    if (room_catalog_init() == ESP_OK && room_catalog_refresh() == ESP_OK) {
        size_t rooms = room_catalog_count();
        out->room_count = (uint8_t)(rooms > UINT8_MAX ? UINT8_MAX : rooms);
    }
    for (int id = 0; id < SERVICE_STATUS_COUNT; ++id) {
        service_status_entry_t entry = {0};
        if (!service_status_get((service_status_id_t)id, &entry)) {
            continue;
        }
        if (entry.fault) {
            out->fault_count++;
            out->issue_count++;
            out->has_fault = true;
        } else if ((entry.init_attempted && !entry.init_ok) ||
                   (entry.start_attempted && !entry.start_ok)) {
            out->degraded_count++;
            out->issue_count++;
            out->has_degraded = true;
            services_degraded = true;
        }
    }

    if (orch_scratch_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    devices = orch_scratch_devices(&device_capacity);
    if (quest_device_list(NULL, 0, &device_count, false) != ESP_OK && device_count == 0) {
        orch_scratch_unlock();
        return ESP_FAIL;
    }
    if (device_count > 0 && (!devices || device_capacity == 0)) {
        orch_scratch_unlock();
        return ESP_ERR_NO_MEM;
    }
    if (device_capacity > 0 && device_count > 0) {
        size_t listed = device_count;
        if (listed > device_capacity) {
            listed = device_capacity;
        }
        if (quest_device_list(devices, listed, &device_count, false) != ESP_OK &&
            device_count == 0) {
            orch_scratch_unlock();
            return ESP_FAIL;
        }
        if (device_count > listed) {
            device_count = listed;
        }
        out->device_count = (uint8_t)(device_count > UINT8_MAX ? UINT8_MAX : device_count);
        for (size_t i = 0; i < device_count; ++i) {
            orch_device_entry_t device = {0};
            device_control_ingest_device_t ingest = {0};
            orch_device_view_fill_device(&devices[i], services_degraded, &device);
            if (device.connectivity == ORCH_CONNECTIVITY_ONLINE) {
                out->online_device_count++;
            }
            if (device.health == ORCH_HEALTH_FAULT) {
                out->fault_count++;
                out->has_fault = true;
            } else if (device.health == ORCH_HEALTH_DEGRADED) {
                out->degraded_count++;
                out->has_degraded = true;
            }
            if (device.connectivity == ORCH_CONNECTIVITY_OFFLINE) {
                out->issue_count++;
            }
            if (device_control_ingest_get_device(device.client_id[0] ? device.client_id : device.device_id,
                                                 &ingest) == ESP_OK) {
                orch_health_t diag_health = orch_health_from_diag_level(ingest.diag_level);
                if (ingest.has_diag && diag_health != ORCH_HEALTH_OK) {
                    out->issue_count++;
                }
                if (ingest.has_result &&
                    scenehub_command_result_is_failure(ingest.result_status)) {
                    out->issue_count++;
                }
            }
        }
    }
    if (out->room_count > 0) {
        size_t room_count = room_catalog_count();
        for (size_t i = 0; i < room_count; ++i) {
            room_catalog_entry_t room = {0};
            gm_room_session_timer_view_t timer_view = {0};
            if (room_catalog_get(i, &room) != ESP_OK) {
                continue;
            }
            if (gm_room_session_get_timer_view(room.room_id, now_ms, &timer_view) != ESP_OK) {
                continue;
            }
            if (timer_view.session_active) {
                out->active_session_count++;
            }
            if (timer_view.hint_active) {
                out->active_hint_count++;
            }
        }
    }
    orch_scratch_unlock();
    return ESP_OK;
}

void orchestrator_registry_invalidate(void)
{
    if (orch_cache_lock() != ESP_OK) {
        orch_cache_mark_invalidate_pending();
        return;
    }
    s_cache_valid = false;
    (void)orch_cache_take_invalidate_pending();
    orch_cache_unlock();
}

esp_err_t orchestrator_registry_list_rooms(orch_room_entry_t *out_rooms,
                                           size_t max_rooms,
                                           size_t *out_count)
{
    size_t catalog_count = 0;
    size_t emitted = 0;

    if (!out_rooms || max_rooms == 0 || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (room_catalog_init() != ESP_OK) {
        return ESP_FAIL;
    }
    if (room_catalog_refresh() != ESP_OK) {
        return ESP_FAIL;
    }
    catalog_count = room_catalog_count();
    if (catalog_count > max_rooms) {
        catalog_count = max_rooms;
    }
    for (size_t i = 0; i < catalog_count; ++i) {
        room_catalog_entry_t room_info = {0};
        if (room_catalog_get(i, &room_info) != ESP_OK) {
            continue;
        }
        orch_room_entry_t *room = &out_rooms[emitted];
        memset(room, 0, sizeof(*room));
        quest_str_copy(room->room_id, sizeof(room->room_id), room_info.room_id);
        quest_str_copy(room->title,
                       sizeof(room->title),
                       room_info.name[0] ? room_info.name : room_info.room_id);
        room->sort_order = (uint16_t)emitted;
        room->device_count = room_info.device_count;
        emitted++;
    }
    *out_count = emitted;
    return ESP_OK;
}

esp_err_t orchestrator_registry_get_room(const char *room_id, orch_room_entry_t *out)
{
    esp_err_t err;
    if (!room_id || !room_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    err = orch_cache_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = orch_cache_ensure_snapshot_locked();
    if (err == ESP_OK) {
        const orch_room_entry_t *room = orch_room_view_find_room(&s_cached_snapshot, room_id);
        if (!room) {
            err = ESP_ERR_NOT_FOUND;
        } else {
            *out = *room;
        }
    }
    orch_cache_unlock();
    return err;
}

esp_err_t orchestrator_registry_get_device(const char *device_id, orch_device_entry_t *out)
{
    esp_err_t err;
    if (!device_id || !device_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    err = orch_cache_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = orch_cache_ensure_snapshot_locked();
    if (err == ESP_OK) {
        err = orch_device_view_get_device(&s_cached_snapshot, device_id, out);
    }
    orch_cache_unlock();
    return err;
}

esp_err_t orchestrator_registry_list_quest_devices(quest_device_t *out_devices,
                                                   size_t max_devices,
                                                   size_t *out_count,
                                                   bool include_system)
{
    if (!out_count || (max_devices > 0 && !out_devices)) {
        return ESP_ERR_INVALID_ARG;
    }
    return quest_device_list(out_devices, max_devices, out_count, include_system);
}

esp_err_t orchestrator_registry_list_control_devices(orch_control_device_entry_t *out_devices,
                                                     size_t max_devices,
                                                     size_t *out_count)
{
    if (!out_devices || max_devices == 0 || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    return orch_device_view_list_control_devices(out_devices, max_devices, out_count);
}

esp_err_t orchestrator_registry_list_device_issues(const char *device_id,
                                                   orch_issue_entry_t *out_issues,
                                                   size_t max_issues,
                                                   size_t *out_count)
{
    esp_err_t err = ESP_OK;
    size_t emitted = 0;

    if (!device_id || !device_id[0] || !out_issues || max_issues == 0 || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    err = orch_cache_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = orch_cache_ensure_snapshot_locked();
    if (err == ESP_OK) {
        for (uint8_t i = 0; i < s_cached_snapshot.issue_count && emitted < max_issues; ++i) {
            const orch_issue_entry_t *issue = &s_cached_snapshot.issues[i];
            if (strcmp(issue->device_id, device_id) != 0) {
                continue;
            }
            out_issues[emitted++] = *issue;
        }
        *out_count = emitted;
    }
    orch_cache_unlock();
    return err;
}

esp_err_t orchestrator_registry_list_room_scenarios(const char *room_id,
                                                    orch_room_scenario_entry_t *out_scenarios,
                                                    size_t max_scenarios,
                                                    size_t *out_count)
{
    esp_err_t err;
    if (!room_id || !room_id[0] || !out_count || !out_scenarios || max_scenarios == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    err = orch_cache_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = orch_cache_ensure_snapshot_locked();
    if (err == ESP_OK) {
        err = orch_room_scenario_view_list(&s_cached_snapshot,
                                           room_id,
                                           out_scenarios,
                                           max_scenarios,
                                           out_count);
    }
    orch_cache_unlock();
    return err;
}

esp_err_t orchestrator_registry_list_room_profiles(const char *room_id,
                                                   orch_room_profile_entry_t *out_profiles,
                                                   size_t max_profiles,
                                                   size_t *out_count)
{
    if (!room_id || !room_id[0] || !out_profiles || max_profiles == 0 || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    return orch_room_profile_view_list(room_id, out_profiles, max_profiles, out_count);
}

esp_err_t orchestrator_registry_list_room_scenario_details(const char *room_id,
                                                           orch_room_scenario_detail_t *out_scenarios,
                                                           size_t max_scenarios,
                                                           size_t *out_count)
{
    if (!room_id || !room_id[0] || !out_count || !out_scenarios || max_scenarios == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return orch_room_scenario_view_list_details(room_id, out_scenarios, max_scenarios, out_count);
}
