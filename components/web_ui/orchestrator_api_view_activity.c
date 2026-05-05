#include "orchestrator_api_view.h"

cJSON *orchestrator_api_view_audit_recent(const orchestrator_audit_entry_t *entries, size_t count)
{
    if (!entries && count > 0) {
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
        const orchestrator_audit_entry_t *entry = &entries[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            return NULL;
        }
        cJSON_AddNumberToObject(item, "timestamp_ms", (double)entry->timestamp_ms);
        cJSON_AddStringToObject(item, "source", entry->source);
        cJSON_AddStringToObject(item, "device_id", entry->device_id);
        cJSON_AddStringToObject(item, "action_id", entry->action_id);
        cJSON_AddBoolToObject(item, "success", entry->success);
        cJSON_AddStringToObject(item, "error_code", entry->error_code);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "items", items);
    return root;
}

static const char *api_timeline_type_str(orchestrator_timeline_type_t type)
{
    switch (type) {
    case ORCH_TIMELINE_TYPE_DEVICE_STATUS:
        return "device_status";
    case ORCH_TIMELINE_TYPE_RUNTIME_CHANGED:
        return "runtime_changed";
    case ORCH_TIMELINE_TYPE_SCENARIO_TRIGGERED:
        return "scenario_triggered";
    case ORCH_TIMELINE_TYPE_TIMER_CHANGED:
        return "timer_changed";
    case ORCH_TIMELINE_TYPE_DEVICE_ACTION:
        return "device_action";
    case ORCH_TIMELINE_TYPE_ACTION_FAILED:
        return "action_failed";
    case ORCH_TIMELINE_TYPE_CONFIG_CHANGED:
        return "config_changed";
    case ORCH_TIMELINE_TYPE_EVENT:
    default:
        return "event";
    }
}

static const char *api_timeline_severity_str(orchestrator_timeline_severity_t severity)
{
    switch (severity) {
    case ORCH_TIMELINE_SEVERITY_WARNING:
        return "warning";
    case ORCH_TIMELINE_SEVERITY_ERROR:
        return "error";
    case ORCH_TIMELINE_SEVERITY_INFO:
    default:
        return "info";
    }
}

cJSON *orchestrator_api_view_timeline_recent(const orchestrator_timeline_entry_t *entries, size_t count)
{
    if (!entries && count > 0) {
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
        const orchestrator_timeline_entry_t *entry = &entries[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            return NULL;
        }
        cJSON_AddNumberToObject(item, "timestamp_ms", (double)entry->timestamp_ms);
        cJSON_AddStringToObject(item, "type", api_timeline_type_str(entry->type));
        cJSON_AddStringToObject(item, "severity", api_timeline_severity_str(entry->severity));
        cJSON_AddStringToObject(item, "source", entry->source);
        cJSON_AddStringToObject(item, "room_id", entry->room_id);
        cJSON_AddStringToObject(item, "device_id", entry->device_id);
        cJSON_AddStringToObject(item, "title", entry->title);
        cJSON_AddStringToObject(item, "details", entry->details);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "items", items);
    return root;
}
