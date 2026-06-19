#include "orchestrator/orchestrator_api_view.h"

cJSON *orchestrator_api_view_control_devices(const orch_control_device_entry_t *devices,
                                             size_t count,
                                             uint64_t now_ms)
{
    (void)now_ms;
    if (!devices && count > 0) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();
    if (!root || !items) {
        if (root) {
            cJSON_Delete(root);
        }
        if (items) {
            cJSON_Delete(items);
        }
        return NULL;
    }

    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "count", (double)count);
    for (size_t i = 0; i < count; ++i) {
        const orch_control_device_entry_t *device = &devices[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            return NULL;
        }
        cJSON_AddStringToObject(item, "device_id", device->device_id);
        cJSON_AddStringToObject(item, "connectivity", device->connectivity_text);
        cJSON_AddStringToObject(item, "health", device->health_text);
        cJSON_AddNumberToObject(item, "last_seen_ms", (double)device->last_seen_ms);
        cJSON_AddStringToObject(item, "fw_version", device->fw_version);
        cJSON_AddStringToObject(item, "boot_id", device->boot_id);
        cJSON_AddStringToObject(item, "mode", device->mode);
        cJSON_AddStringToObject(item, "state", device->state);
        cJSON_AddBoolToObject(item, "runtime_driver_enabled", device->runtime_driver_enabled);
        cJSON_AddBoolToObject(item, "runtime_driver_ready", device->runtime_driver_ready);
        cJSON_AddStringToObject(item, "runtime_driver_id", device->runtime_driver_id);
        cJSON_AddStringToObject(item, "runtime_driver_health", device->runtime_driver_health);
        cJSON_AddStringToObject(item, "runtime_driver_state", device->runtime_driver_state);
        cJSON_AddStringToObject(item, "runtime_driver_error_code", device->runtime_driver_error_code);
        cJSON_AddBoolToObject(item, "has_heartbeat", device->has_heartbeat);
        cJSON_AddBoolToObject(item, "has_status", device->has_status);
        cJSON_AddBoolToObject(item, "has_diag", device->has_diag);
        cJSON_AddBoolToObject(item, "has_result", device->has_result);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "items", items);
    return root;
}
