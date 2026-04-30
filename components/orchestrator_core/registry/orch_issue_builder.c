#include "orchestrator_registry_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"

static void *orch_issue_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

void orch_issue_builder_add_issue(orch_registry_snapshot_t *snapshot,
                                  orch_issue_scope_t scope,
                                  orch_issue_severity_t severity,
                                  const char *room_id,
                                  const char *device_id,
                                  const char *code,
                                  const char *title,
                                  const char *details)
{
    if (!snapshot || snapshot->issue_count >= ORCH_REGISTRY_MAX_ISSUES) {
        return;
    }
    orch_issue_entry_t *issue = &snapshot->issues[snapshot->issue_count++];
    memset(issue, 0, sizeof(*issue));
    issue->scope = scope;
    issue->severity = severity;
    issue->active = true;
    quest_str_copy(issue->room_id, sizeof(issue->room_id), room_id);
    quest_str_copy(issue->device_id, sizeof(issue->device_id), device_id);
    quest_str_copy(issue->code, sizeof(issue->code), code);
    quest_str_copy(issue->title, sizeof(issue->title), title);
    quest_str_copy(issue->details, sizeof(issue->details), details);
    snprintf(issue->issue_id,
             sizeof(issue->issue_id),
             "%s:%s:%s",
             (scope == ORCH_ISSUE_SCOPE_SYSTEM) ? "system" : (scope == ORCH_ISSUE_SCOPE_ROOM) ? "room" : "device",
             code ? code : "issue",
             device_id && device_id[0] ? device_id : (room_id && room_id[0] ? room_id : "global"));

    orch_health_t impact = orch_health_from_severity(severity);
    if (impact == ORCH_HEALTH_FAULT) {
        snapshot->has_fault = true;
    } else if (impact == ORCH_HEALTH_DEGRADED) {
        snapshot->has_degraded = true;
    }
}

void orch_issue_builder_collect_system(orch_registry_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }
    for (uint8_t i = 0; i < snapshot->service_count; ++i) {
        const orch_service_entry_t *service = &snapshot->services[i];
        if (service->health != ORCH_HEALTH_DEGRADED) {
            continue;
        }
        char title[ORCH_REGISTRY_ISSUE_TITLE_MAX_LEN] = {0};
        char details[ORCH_REGISTRY_ISSUE_DETAILS_MAX_LEN] = {0};
        snprintf(title, sizeof(title), "%s degraded", service->service_id);
        snprintf(details,
                 sizeof(details),
                 "Service %s did not initialize or start cleanly.",
                 service->service_id);
        orch_issue_builder_add_issue(snapshot,
                                     ORCH_ISSUE_SCOPE_SYSTEM,
                                     ORCH_ISSUE_SEVERITY_WARNING,
                                     "",
                                     "",
                                     "service_degraded",
                                     title,
                                     details);
    }
}

void orch_issue_builder_collect_devices(orch_registry_snapshot_t *snapshot)
{
    device_control_ingest_device_t *ingest = NULL;
    if (!snapshot) {
        return;
    }
    ingest = orch_issue_alloc(sizeof(*ingest));
    for (uint8_t i = 0; i < snapshot->device_count; ++i) {
        const orch_device_entry_t *device = &snapshot->devices[i];
        if (device->connectivity == ORCH_CONNECTIVITY_OFFLINE) {
            char title[ORCH_REGISTRY_ISSUE_TITLE_MAX_LEN] = {0};
            snprintf(title,
                     sizeof(title),
                     "%s offline",
                     device->display_name[0] ? device->display_name : device->device_id);
            orch_issue_builder_add_issue(snapshot,
                                         ORCH_ISSUE_SCOPE_DEVICE,
                                         ORCH_ISSUE_SEVERITY_ERROR,
                                         device->room_id,
                                         device->device_id,
                                         "device_offline",
                                         title,
                                         "No fresh heartbeat/status/result was received from the observed device.");
        }

        if (ingest) {
            memset(ingest, 0, sizeof(*ingest));
        }
        const char *client_id = device->client_id[0] ? device->client_id : device->device_id;
        if (ingest && device_control_ingest_get_device(client_id, ingest) == ESP_OK) {
            orch_health_t diag_health = orch_health_from_diag_level(ingest->diag_level);
            if (ingest->has_diag && diag_health != ORCH_HEALTH_OK) {
                orch_issue_builder_add_issue(snapshot,
                                             ORCH_ISSUE_SCOPE_DEVICE,
                                             diag_health == ORCH_HEALTH_FAULT ? ORCH_ISSUE_SEVERITY_ERROR
                                                                               : ORCH_ISSUE_SEVERITY_WARNING,
                                             device->room_id,
                                             device->device_id,
                                             ingest->diag_code[0] ? ingest->diag_code : "device_diag",
                                             "Device diagnostics",
                                             ingest->diag_message[0] ? ingest->diag_message : "Device reported diagnostics event.");
            }
            if (ingest->has_result && strcmp(ingest->result_status, "error") == 0) {
                orch_issue_builder_add_issue(snapshot,
                                             ORCH_ISSUE_SCOPE_DEVICE,
                                             ORCH_ISSUE_SEVERITY_WARNING,
                                             device->room_id,
                                             device->device_id,
                                             ingest->result_error_code[0] ? ingest->result_error_code : "command_error",
                                             "Device command failed",
                                             ingest->result_message[0] ? ingest->result_message : "Device returned command error result.");
            }
        }
    }
    heap_caps_free(ingest);
}

void orch_issue_builder_collect_rooms(orch_registry_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }
    for (uint8_t i = 0; i < snapshot->issue_count; ++i) {
        const orch_issue_entry_t *issue = &snapshot->issues[i];
        if (!issue->active || !issue->room_id[0]) {
            continue;
        }
        orch_room_entry_t *room = orch_room_view_find_room(snapshot, issue->room_id);
        if (!room) {
            continue;
        }
        room->issue_count++;
        orch_health_t impact = orch_health_from_severity(issue->severity);
        if (impact == ORCH_HEALTH_FAULT) {
            room->health = ORCH_HEALTH_FAULT;
        } else if (impact == ORCH_HEALTH_DEGRADED && room->health != ORCH_HEALTH_FAULT) {
            room->health = ORCH_HEALTH_DEGRADED;
        }
    }
}
