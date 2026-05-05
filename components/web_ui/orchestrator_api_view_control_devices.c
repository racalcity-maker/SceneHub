#include "orchestrator_api_view.h"

#include <string.h>

static const char *api_control_health_str(const device_control_ingest_device_t *device)
{
    const char *health = NULL;
    if (!device) {
        return "ok";
    }
    health = device->status_health;
    if (health && health[0]) {
        if (strcmp(health, "ok") == 0 || strcmp(health, "normal") == 0) {
            return "ok";
        }
        if (strcmp(health, "warn") == 0 || strcmp(health, "warning") == 0 ||
            strcmp(health, "degraded") == 0) {
            return "degraded";
        }
        if (strcmp(health, "error") == 0 || strcmp(health, "fault") == 0 ||
            strcmp(health, "fatal") == 0) {
            return "fault";
        }
        return health;
    }
    if (device->has_diag && device->diag_level[0]) {
        if (strcmp(device->diag_level, "warn") == 0 || strcmp(device->diag_level, "warning") == 0) {
            return "degraded";
        }
        if (strcmp(device->diag_level, "error") == 0 || strcmp(device->diag_level, "fatal") == 0) {
            return "fault";
        }
    }
    return "ok";
}

static const char *api_control_boot_id(const device_control_ingest_device_t *device)
{
    if (!device) {
        return "";
    }
    if (device->status_boot_id[0]) {
        return device->status_boot_id;
    }
    return device->heartbeat_boot_id;
}

cJSON *orchestrator_api_view_control_devices(const device_control_ingest_device_t *devices,
                                             size_t count,
                                             uint64_t now_ms)
{
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
        const device_control_ingest_device_t *device = &devices[i];
        cJSON *item = cJSON_CreateObject();
        bool online = device_control_ingest_is_online(device,
                                                       now_ms,
                                                       DEVICE_CONTROL_INGEST_DEFAULT_ONLINE_TIMEOUT_MS);
        if (!item) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            return NULL;
        }
        cJSON_AddStringToObject(item, "device_id", device->device_id);
        cJSON_AddStringToObject(item, "connectivity", online ? "online" : "offline");
        cJSON_AddStringToObject(item, "health", api_control_health_str(device));
        cJSON_AddNumberToObject(item, "last_seen_ms", (double)device->last_seen_ms);
        cJSON_AddStringToObject(item, "fw_version", device->status_fw_version);
        cJSON_AddStringToObject(item, "boot_id", api_control_boot_id(device));
        cJSON_AddStringToObject(item, "mode", device->status_mode);
        cJSON_AddStringToObject(item, "state", device->status_state);
        cJSON_AddBoolToObject(item, "has_heartbeat", device->has_heartbeat);
        cJSON_AddBoolToObject(item, "has_status", device->has_status);
        cJSON_AddBoolToObject(item, "has_diag", device->has_diag);
        cJSON_AddBoolToObject(item, "has_result", device->has_result);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "items", items);
    return root;
}
