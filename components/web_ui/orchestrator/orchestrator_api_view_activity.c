#include "orchestrator/orchestrator_api_view.h"

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
        cJSON_AddStringToObject(item, "request_id", entry->request_id);
        cJSON_AddBoolToObject(item, "success", entry->success);
        cJSON_AddStringToObject(item, "error_code", entry->error_code);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "items", items);
    return root;
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
        cJSON_AddStringToObject(item, "type", entry->type_text);
        cJSON_AddStringToObject(item, "severity", entry->severity_text);
        cJSON_AddStringToObject(item, "source", entry->source);
        cJSON_AddStringToObject(item, "room_id", entry->room_id);
        cJSON_AddStringToObject(item, "device_id", entry->device_id);
        cJSON_AddStringToObject(item, "request_id", entry->request_id);
        cJSON_AddStringToObject(item, "title", entry->title);
        cJSON_AddStringToObject(item, "details", entry->details);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "items", items);
    return root;
}
