#include "orchestrator_registry_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "event_bus.h"

EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_cached_snapshot;
static SemaphoreHandle_t s_cache_mutex = NULL;
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

static void orch_registry_event_handler(const event_bus_message_t *message);

static esp_err_t orch_cache_ensure_mutex(void)
{
    if (s_cache_mutex) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_cache_mutex_init_lock);
    if (!s_cache_mutex) {
        s_cache_mutex = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&s_cache_mutex_init_lock);
    return s_cache_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t orch_cache_lock(void)
{
    esp_err_t err = orch_cache_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }
    return (xSemaphoreTake(s_cache_mutex, portMAX_DELAY) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void orch_cache_unlock(void)
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

static void orch_registry_event_handler(const event_bus_message_t *message)
{
    if (!message) {
        return;
    }
    switch (message->type) {
    case EVENT_DEVICE_CONFIG_CHANGED:
    case EVENT_DEVICE_STATUS:
    case EVENT_DEVICE_RUNTIME:
    case EVENT_DEVICE_CONTROL:
    case EVENT_RUNTIME_CONTROL:
        orch_cache_mark_invalidate_pending();
        break;
    default:
        break;
    }
}

static esp_err_t orch_cache_ensure_snapshot_locked(void)
{
    uint32_t source_generation = 0;
    uint32_t ingest_generation = device_control_ingest_generation();
    gm_room_session_scenario_tick();
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
