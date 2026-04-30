#include "orchestrator_registry_internal.h"

#include <string.h>

#include "esp_heap_caps.h"

static void *orch_device_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static const char *orch_observed_client_id(const quest_device_t *dev)
{
    if (!dev) {
        return "";
    }
    if (dev->client_id[0]) {
        return dev->client_id;
    }
    return dev->id;
}

static void orch_apply_control_ingest(const quest_device_t *dev, orch_device_entry_t *dst)
{
    device_control_ingest_device_t *ingest = NULL;
    uint64_t now_ms = orch_now_ms();
    if (!dev || !dst) {
        return;
    }
    ingest = orch_device_alloc(sizeof(*ingest));
    if (!ingest) {
        return;
    }
    if (device_control_ingest_get_device(orch_observed_client_id(dev), ingest) != ESP_OK) {
        heap_caps_free(ingest);
        dst->connectivity = ORCH_CONNECTIVITY_OFFLINE;
        dst->health = ORCH_HEALTH_FAULT;
        dst->has_fault = true;
        quest_str_copy(dst->state, sizeof(dst->state), "not observed");
        return;
    }

    dst->last_seen_ms = ingest->last_seen_ms;
    quest_str_copy(dst->fw_version, sizeof(dst->fw_version), ingest->status_fw_version);
    if (ingest->status_boot_id[0]) {
        quest_str_copy(dst->boot_id, sizeof(dst->boot_id), ingest->status_boot_id);
    } else {
        quest_str_copy(dst->boot_id, sizeof(dst->boot_id), ingest->heartbeat_boot_id);
    }
    quest_str_copy(dst->last_diag_code, sizeof(dst->last_diag_code), ingest->diag_code);
    quest_str_copy(dst->last_diag_message, sizeof(dst->last_diag_message), ingest->diag_message);
    quest_str_copy(dst->last_result_status, sizeof(dst->last_result_status), ingest->result_status);
    quest_str_copy(dst->last_result_error_code,
                sizeof(dst->last_result_error_code),
                ingest->result_error_code);

    dst->connectivity = device_control_ingest_is_online(ingest,
                                                         now_ms,
                                                         DEVICE_CONTROL_INGEST_DEFAULT_ONLINE_TIMEOUT_MS)
                            ? ORCH_CONNECTIVITY_ONLINE
                            : ORCH_CONNECTIVITY_OFFLINE;
    if (dst->connectivity == ORCH_CONNECTIVITY_OFFLINE) {
        dst->health = ORCH_HEALTH_FAULT;
        dst->has_fault = true;
        quest_str_copy(dst->state, sizeof(dst->state), "offline");
    }

    if (dst->connectivity != ORCH_CONNECTIVITY_OFFLINE &&
        ingest->has_status && ingest->status_state[0]) {
        quest_str_copy(dst->state, sizeof(dst->state), ingest->status_state);
    }
    if (ingest->has_status) {
        dst->has_runtime = ingest->status_runtime_active;
        dst->runtime_state = ingest->status_runtime_active ? ORCH_RUNTIME_STATE_ACTIVE : ORCH_RUNTIME_STATE_IDLE;
    }

    if (ingest->has_status) {
        orch_promote_health(dst, orch_health_from_status_text(ingest->status_health));
    }
    if (ingest->has_diag) {
        orch_promote_health(dst, orch_health_from_diag_level(ingest->diag_level));
    }
    if (ingest->has_result && strcmp(ingest->result_status, "error") == 0) {
        orch_promote_health(dst, ORCH_HEALTH_DEGRADED);
    }
    heap_caps_free(ingest);
}

void orch_device_view_fill_device(const quest_device_t *dev,
                                  bool services_degraded,
                                  orch_device_entry_t *dst)
{
    if (!dev || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    quest_str_copy(dst->device_id, sizeof(dst->device_id), dev->id);
    quest_str_copy(dst->client_id, sizeof(dst->client_id), orch_observed_client_id(dev));
    quest_str_copy(dst->display_name, sizeof(dst->display_name), dev->name[0] ? dev->name : dev->id);
    quest_str_copy(dst->room_id, sizeof(dst->room_id), orch_default_room_id());
    dst->connectivity = ORCH_CONNECTIVITY_UNKNOWN;
    dst->health = ORCH_HEALTH_OK;
    dst->runtime_state = ORCH_RUNTIME_STATE_UNKNOWN;
    quest_str_copy(dst->state, sizeof(dst->state), "unknown");

    if (!dev->enabled) {
        dst->health = ORCH_HEALTH_DEGRADED;
        dst->has_degraded = true;
        quest_str_copy(dst->state, sizeof(dst->state), "disabled");
        return;
    }

    orch_apply_control_ingest(dev, dst);

    if (dst->health != ORCH_HEALTH_FAULT && services_degraded) {
        dst->health = ORCH_HEALTH_DEGRADED;
        dst->has_degraded = true;
    }
    if (dst->health == ORCH_HEALTH_FAULT) {
        dst->has_fault = true;
    }
}

esp_err_t orch_device_view_get_device(const orch_registry_snapshot_t *snapshot,
                                      const char *device_id,
                                      orch_device_entry_t *out)
{
    if (!snapshot || !device_id || !device_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < snapshot->device_count; ++i) {
        if (strcmp(snapshot->devices[i].device_id, device_id) == 0) {
            *out = snapshot->devices[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
