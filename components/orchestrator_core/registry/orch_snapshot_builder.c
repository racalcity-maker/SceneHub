#include "orchestrator_registry_internal.h"

#include <string.h>

#include "esp_heap_caps.h"

void orch_collect_services(orch_registry_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }
    for (int id = 0; id < SERVICE_STATUS_COUNT && snapshot->service_count < 8; ++id) {
        service_status_entry_t entry = {0};
        if (!service_status_get((service_status_id_t)id, &entry)) {
            continue;
        }
        orch_service_entry_t *dst = &snapshot->services[snapshot->service_count++];
        memset(dst, 0, sizeof(*dst));
        quest_str_copy(dst->service_id, sizeof(dst->service_id), service_status_name((service_status_id_t)id));
        dst->init_attempted = entry.init_attempted;
        dst->init_ok = entry.init_ok;
        dst->start_attempted = entry.start_attempted;
        dst->start_ok = entry.start_ok;
        dst->health = ORCH_HEALTH_OK;
        if ((entry.init_attempted && !entry.init_ok) || (entry.start_attempted && !entry.start_ok)) {
            dst->health = ORCH_HEALTH_DEGRADED;
            snapshot->has_degraded = true;
        }
    }
}

esp_err_t orch_snapshot_builder_build_uncached(orch_registry_snapshot_t *out)
{
    quest_device_t *devices = NULL;
    size_t count = 0;
    size_t capacity = 0;
    esp_err_t err = ESP_OK;

    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    orch_collect_services(out);
    bool services_degraded = out->has_degraded;

    out->generation = quest_device_generation() ^
                      (room_catalog_generation() << 1) ^
                      (room_scenario_generation() << 2) ^
                      (gm_game_profile_generation() << 3);

    err = quest_device_list(NULL, 0, &count, false);
    if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        return err;
    }
    capacity = count;
    if (capacity > ORCH_REGISTRY_MAX_DEVICES) {
        capacity = ORCH_REGISTRY_MAX_DEVICES;
    }
    if (capacity == 0) {
        orch_room_view_collect_rooms(out);
        orch_room_view_enrich_from_sessions(out);
        orch_room_scenario_view_collect_all(out);
        orch_issue_builder_collect_system(out);
        orch_issue_builder_collect_rooms(out);
        return ESP_OK;
    }
    devices = orch_snapshot_alloc(sizeof(*devices) * capacity);
    if (!devices) {
        return ESP_ERR_NO_MEM;
    }
    err = quest_device_list(devices, capacity, &count, false);
    if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        heap_caps_free(devices);
        return err;
    }
    if (count > capacity) {
        count = capacity;
    }
    out->device_count = (uint8_t)count;
    for (uint8_t i = 0; i < out->device_count; ++i) {
        orch_device_view_fill_device(&devices[i], services_degraded, &out->devices[i]);
        if (out->devices[i].has_fault) {
            out->has_fault = true;
        }
        if (out->devices[i].has_degraded) {
            out->has_degraded = true;
        }
    }
    heap_caps_free(devices);

    orch_room_view_collect_rooms(out);
    orch_room_view_enrich_from_sessions(out);
    orch_room_scenario_view_collect_all(out);
    orch_issue_builder_collect_system(out);
    orch_issue_builder_collect_devices(out);
    orch_issue_builder_collect_rooms(out);

    return ESP_OK;
}
