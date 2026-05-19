#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_control_ingest.h"
#include "esp_err.h"
#include "orch_issue_view.h"
#include "orch_runtime_view.h"
#include "quest_common_limits.h"
#include "quest_device.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ORCH_REGISTRY_STATE_MAX_LEN
#define ORCH_REGISTRY_STATE_MAX_LEN 16
#endif
#ifndef ORCH_REGISTRY_ISSUE_ID_MAX_LEN
#define ORCH_REGISTRY_ISSUE_ID_MAX_LEN 96
#endif
#ifndef ORCH_REGISTRY_DEVICE_BADGE_MAX_LEN
#define ORCH_REGISTRY_DEVICE_BADGE_MAX_LEN 16
#endif
#ifndef ORCH_REGISTRY_DEVICE_MAX_BADGES
#define ORCH_REGISTRY_DEVICE_MAX_BADGES 2
#endif
#ifndef ORCH_DEVICE_NODE_KIND_MAX_LEN
#define ORCH_DEVICE_NODE_KIND_MAX_LEN 32
#endif
#ifndef ORCH_DEVICE_CAPABILITY_CONTRACT_MAX_LEN
#define ORCH_DEVICE_CAPABILITY_CONTRACT_MAX_LEN 48
#endif

typedef enum {
    ORCH_CONNECTIVITY_UNKNOWN = 0,
    ORCH_CONNECTIVITY_ONLINE,
    ORCH_CONNECTIVITY_OFFLINE,
} orch_connectivity_t;

typedef enum {
    ORCH_HEALTH_OK = 0,
    ORCH_HEALTH_DEGRADED,
    ORCH_HEALTH_FAULT,
} orch_health_t;

typedef enum {
    ORCH_RUNTIME_STATE_UNKNOWN = 0,
    ORCH_RUNTIME_STATE_IDLE,
    ORCH_RUNTIME_STATE_ARMED,
    ORCH_RUNTIME_STATE_ACTIVE,
    ORCH_RUNTIME_STATE_PAUSED,
    ORCH_RUNTIME_STATE_COMPLETED,
    ORCH_RUNTIME_STATE_TIMEOUT,
    ORCH_RUNTIME_STATE_FAILED,
} orch_runtime_state_t;

typedef struct {
    char device_id[QUEST_ID_MAX_LEN];
    char client_id[QUEST_ID_MAX_LEN];
    char display_name[QUEST_NAME_MAX_LEN];
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char state[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_connectivity_t connectivity;
    char connectivity_text[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_health_t health;
    char health_text[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_runtime_state_t runtime_state;
    char runtime_state_text[ORCH_REGISTRY_STATE_MAX_LEN];
    uint64_t last_seen_ms;
    char fw_version[DEVICE_CONTROL_INGEST_FW_VERSION_MAX_LEN];
    char boot_id[DEVICE_CONTROL_INGEST_BOOT_ID_MAX_LEN];
    char last_diag_code[DEVICE_CONTROL_INGEST_CODE_MAX_LEN];
    char last_diag_message[DEVICE_CONTROL_INGEST_MESSAGE_MAX_LEN];
    char last_result_status[DEVICE_CONTROL_INGEST_RESULT_STATUS_MAX_LEN];
    char last_result_error_code[DEVICE_CONTROL_INGEST_ERROR_CODE_MAX_LEN];
    char badges[ORCH_REGISTRY_DEVICE_MAX_BADGES][ORCH_REGISTRY_DEVICE_BADGE_MAX_LEN];
    uint8_t badge_count;
    bool compact_manifest;
    char node_kind[ORCH_DEVICE_NODE_KIND_MAX_LEN];
    char capability_contract[ORCH_DEVICE_CAPABILITY_CONTRACT_MAX_LEN];
    uint8_t resource_count;
    uint8_t command_template_count;
    uint8_t event_template_count;
    bool has_runtime;
    bool has_fault;
    bool has_degraded;
} orch_device_entry_t;

/* Read-model DTO for device status projections consumed by UI/API serializers. */

typedef struct {
    char device_id[QUEST_ID_MAX_LEN];
    orch_connectivity_t connectivity;
    char connectivity_text[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_health_t health;
    char health_text[ORCH_REGISTRY_STATE_MAX_LEN];
    uint64_t last_seen_ms;
    char fw_version[DEVICE_CONTROL_INGEST_FW_VERSION_MAX_LEN];
    char boot_id[DEVICE_CONTROL_INGEST_BOOT_ID_MAX_LEN];
    char mode[DEVICE_CONTROL_INGEST_MODE_MAX_LEN];
    char state[DEVICE_CONTROL_INGEST_STATE_MAX_LEN];
    bool has_heartbeat;
    bool has_status;
    bool has_diag;
    bool has_result;
} orch_control_device_entry_t;

/* Read-model DTO for observed device-control telemetry, not a storage model. */

typedef struct {
    char id[QUEST_DEVICE_ID_MAX_LEN];
    char client_id[QUEST_DEVICE_CLIENT_ID_MAX_LEN];
    char name[QUEST_DEVICE_NAME_MAX_LEN];
    bool enabled;
    bool system_device;
    quest_device_command_t commands[QUEST_DEVICE_MAX_COMMANDS];
    uint8_t command_count;
    quest_device_event_t events[QUEST_DEVICE_MAX_EVENTS];
    uint8_t event_count;
    bool compact_manifest;
    char node_kind[ORCH_DEVICE_NODE_KIND_MAX_LEN];
    char capability_contract[ORCH_DEVICE_CAPABILITY_CONTRACT_MAX_LEN];
    uint8_t resource_count;
    uint8_t command_template_count;
    uint8_t event_template_count;
    char device_description_json[QUEST_DEVICE_DESCRIPTION_JSON_MAX_LEN];
} orch_quest_device_catalog_entry_t;

/*
 * Read-model catalog adapter for Quest Device lists.
 * Keep this narrow: it prevents Web UI from consuming quest_device_t directly
 * and must not become a second persistent device model.
 */

esp_err_t orchestrator_registry_get_device(const char *device_id, orch_device_entry_t *out);
esp_err_t orchestrator_registry_list_quest_devices(quest_device_t *out_devices,
                                                   size_t max_devices,
                                                   size_t *out_count,
                                                   bool include_system);
esp_err_t orchestrator_registry_list_quest_device_catalog(orch_quest_device_catalog_entry_t *out_devices,
                                                          size_t max_devices,
                                                          size_t *out_count,
                                                          bool include_system);
esp_err_t orchestrator_registry_list_control_devices(orch_control_device_entry_t *out_devices,
                                                     size_t max_devices,
                                                     size_t *out_count);
esp_err_t orchestrator_registry_list_device_issues(const char *device_id,
                                                   orch_issue_entry_t *out_issues,
                                                   size_t max_issues,
                                                   size_t *out_count);

#ifdef __cplusplus
}
#endif
