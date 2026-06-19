#include "orchestrator_registry_internal.h"

#include <stdio.h>
#include <string.h>

#include "scenehub_command_result.h"

static void orch_issue_builder_append_text(char *dst, size_t dst_size, const char *text)
{
    size_t used = 0;

    if (!dst || dst_size == 0 || !text || !text[0]) {
        return;
    }
    used = strlen(dst);
    if (used >= dst_size - 1) {
        return;
    }
    quest_str_copy(dst + used, dst_size - used, text);
}

static bool orch_issue_builder_room_has_related_issue(const orch_room_entry_t *room,
                                                      const char *issue_id)
{
    if (!room || !issue_id || !issue_id[0]) {
        return false;
    }
    for (uint8_t i = 0;
         i < room->related_issue_count && i < ORCH_REGISTRY_MAX_ISSUES;
         ++i) {
        if (strcmp(room->related_issue_ids[i], issue_id) == 0) {
            return true;
        }
    }
    return false;
}

static void orch_issue_builder_room_add_related_issue(orch_room_entry_t *room,
                                                      const char *issue_id)
{
    if (!room || !issue_id || !issue_id[0] ||
        room->related_issue_count >= ORCH_REGISTRY_MAX_ISSUES ||
        orch_issue_builder_room_has_related_issue(room, issue_id)) {
        return;
    }
    quest_str_copy(room->related_issue_ids[room->related_issue_count],
                   sizeof(room->related_issue_ids[room->related_issue_count]),
                   issue_id);
    room->related_issue_count++;
}

static void orch_issue_builder_room_promote_health(orch_room_entry_t *room,
                                                   orch_issue_severity_t severity)
{
    orch_health_t impact = ORCH_HEALTH_OK;
    if (!room) {
        return;
    }
    impact = orch_health_from_severity(severity);
    if (impact == ORCH_HEALTH_FAULT) {
        room->health = ORCH_HEALTH_FAULT;
    } else if (impact == ORCH_HEALTH_DEGRADED && room->health != ORCH_HEALTH_FAULT) {
        room->health = ORCH_HEALTH_DEGRADED;
    }
    quest_str_copy(room->health_text, sizeof(room->health_text), orch_health_str(room->health));
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
    quest_str_copy(issue->scope_text, sizeof(issue->scope_text), orch_issue_scope_str(scope));
    quest_str_copy(issue->severity_text,
                   sizeof(issue->severity_text),
                   orch_issue_severity_str(severity));
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
        if (service->health == ORCH_HEALTH_OK) {
            continue;
        }
        char title[ORCH_REGISTRY_ISSUE_TITLE_MAX_LEN] = {0};
        char details[ORCH_REGISTRY_ISSUE_DETAILS_MAX_LEN] = {0};
        bool fault = service->health == ORCH_HEALTH_FAULT;
        snprintf(title, sizeof(title), "%s %s", service->service_id, fault ? "fault" : "degraded");
        if (service->last_error != ESP_OK) {
            snprintf(details,
                     sizeof(details),
                     "Service %s reported error %d.",
                     service->service_id,
                     (int)service->last_error);
        } else {
            snprintf(details,
                     sizeof(details),
                     "Service %s did not initialize or start cleanly.",
                     service->service_id);
        }
        orch_issue_builder_add_issue(snapshot,
                                     ORCH_ISSUE_SCOPE_SYSTEM,
                                     fault ? ORCH_ISSUE_SEVERITY_ERROR : ORCH_ISSUE_SEVERITY_WARNING,
                                     "",
                                     "",
                                     fault ? "service_fault" : "service_degraded",
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
    ingest = orch_scratch_ingest();
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
            if (ingest->has_result &&
                scenehub_command_result_is_failure(ingest->result_status)) {
                orch_issue_builder_add_issue(snapshot,
                                             ORCH_ISSUE_SCOPE_DEVICE,
                                             ORCH_ISSUE_SEVERITY_WARNING,
                                             device->room_id,
                                             device->device_id,
                                             ingest->result_error_code[0] ? ingest->result_error_code : "command_error",
                                             "Device command failed",
                                             ingest->result_message[0] ? ingest->result_message : "Device returned command error result.");
            }
            if (ingest->has_status &&
                ingest->status_driver_nfc_enabled &&
                ingest->status_driver_nfc_health[0] &&
                strcmp(ingest->status_driver_nfc_health, "ok") != 0) {
                char title[ORCH_REGISTRY_ISSUE_TITLE_MAX_LEN] = {0};
                char details[ORCH_REGISTRY_ISSUE_DETAILS_MAX_LEN] = {0};
                const char *device_name = device->display_name[0] ? device->display_name : device->device_id;
                const char *reader_id = ingest->status_driver_nfc_reader_id[0]
                                            ? ingest->status_driver_nfc_reader_id
                                            : "nfc_reader";
                const char *reader_state = ingest->status_driver_nfc_state[0]
                                               ? ingest->status_driver_nfc_state
                                               : "unknown";
                const char *reader_health = ingest->status_driver_nfc_health[0]
                                                ? ingest->status_driver_nfc_health
                                                : "unknown";
                const char *reader_code = ingest->status_driver_nfc_error_code[0]
                                              ? ingest->status_driver_nfc_error_code
                                              : "none";
                snprintf(title,
                         sizeof(title),
                         "NFC reader %s",
                         strcmp(ingest->status_driver_nfc_health, "degraded") == 0 ? "degraded" : "error");
                quest_str_copy(details, sizeof(details), "Device ");
                orch_issue_builder_append_text(details, sizeof(details), device_name);
                orch_issue_builder_append_text(details, sizeof(details), " reader ");
                orch_issue_builder_append_text(details, sizeof(details), reader_id);
                orch_issue_builder_append_text(details, sizeof(details), " state=");
                orch_issue_builder_append_text(details, sizeof(details), reader_state);
                orch_issue_builder_append_text(details, sizeof(details), " health=");
                orch_issue_builder_append_text(details, sizeof(details), reader_health);
                orch_issue_builder_append_text(details, sizeof(details), " code=");
                orch_issue_builder_append_text(details, sizeof(details), reader_code);
                orch_issue_builder_append_text(details, sizeof(details), ".");
                orch_issue_builder_add_issue(snapshot,
                                             ORCH_ISSUE_SCOPE_DEVICE,
                                             ORCH_ISSUE_SEVERITY_WARNING,
                                             device->room_id,
                                             device->device_id,
                                             ingest->status_driver_nfc_error_code[0]
                                                 ? ingest->status_driver_nfc_error_code
                                                 : "nfc_reader_issue",
                                             title,
                                             details);
            }
        }
    }
}

void orch_issue_builder_collect_rooms(orch_registry_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }
    for (uint8_t room_index = 0; room_index < snapshot->room_count; ++room_index) {
        orch_room_entry_t *room = &snapshot->rooms[room_index];
        room->issue_count = 0;
        room->related_issue_count = 0;
        memset(room->related_issue_ids, 0, sizeof(room->related_issue_ids));
    }
    for (uint8_t i = 0; i < snapshot->issue_count; ++i) {
        const orch_issue_entry_t *issue = &snapshot->issues[i];
        if (!issue->active) {
            continue;
        }
        for (uint8_t room_index = 0; room_index < snapshot->room_count; ++room_index) {
            orch_room_entry_t *room = &snapshot->rooms[room_index];
            bool matches_room = issue->room_id[0] &&
                                strcmp(issue->room_id, room->room_id) == 0;
            bool matches_device = issue->device_id[0] &&
                                  orch_room_view_has_scenario_device(room, issue->device_id);
            if (!matches_room && !matches_device) {
                continue;
            }
            if (!orch_issue_builder_room_has_related_issue(room, issue->issue_id)) {
                room->issue_count++;
                orch_issue_builder_room_add_related_issue(room, issue->issue_id);
            }
            orch_issue_builder_room_promote_health(room, issue->severity);
        }
    }
}
