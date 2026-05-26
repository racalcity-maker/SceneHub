#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "device_control_ingest.h"

typedef struct {
    bool in_use;
    device_control_ingest_device_t state;
} dci_slot_t;

typedef struct {
    bool in_use;
    uint64_t rx_ms;
    char device_id[QUEST_ID_MAX_LEN];
    char request_id[DEVICE_CONTROL_INGEST_REQUEST_ID_MAX_LEN];
    char *data_json;
    size_t data_len;
} dci_describe_interface_cache_entry_t;

typedef struct {
    char device_id[QUEST_ID_MAX_LEN];
    uint64_t last_seen_ms;
    bool has_status;
    char status_state[DEVICE_CONTROL_INGEST_STATE_MAX_LEN];
    char status_health[DEVICE_CONTROL_INGEST_HEALTH_MAX_LEN];
    bool status_runtime_active;
    bool control_is_event;
    bool has_result;
    char result_request_id[DEVICE_CONTROL_INGEST_REQUEST_ID_MAX_LEN];
    char result_command[DEVICE_CONTROL_INGEST_COMMAND_MAX_LEN];
    char result_status[DEVICE_CONTROL_INGEST_RESULT_STATUS_MAX_LEN];
    char event_name[DEVICE_CONTROL_INGEST_COMMAND_MAX_LEN];
    char event_args_json[DEVICE_CONTROL_INGEST_EVENT_ARGS_JSON_MAX_LEN];
} dci_event_snapshot_t;

extern dci_slot_t **dci_s_slots;
extern SemaphoreHandle_t dci_s_lock;
extern StaticSemaphore_t dci_s_lock_storage;
extern portMUX_TYPE dci_s_lock_init_lock;
extern uint32_t dci_s_generation;
extern char dci_s_last_changed_device_id[QUEST_ID_MAX_LEN];
extern dci_describe_interface_cache_entry_t dci_s_describe_interface_cache[];

uint64_t dci_now_ms(void);
dci_slot_t *dci_find_slot_locked(const char *device_id);
dci_slot_t *dci_alloc_slot_locked(const char *device_id);
void dci_clear_describe_interface_cache_locked(void);
esp_err_t dci_store_describe_interface_data_locked(const char *device_id,
                                                   const char *request_id,
                                                   const char *json,
                                                   size_t json_len,
                                                   uint64_t rx_ms);

void dci_capture_event_snapshot(const device_control_ingest_device_t *state,
                                bool control_is_event,
                                dci_event_snapshot_t *out);
void dci_post_status_event(const dci_event_snapshot_t *state);
void dci_post_runtime_event(const dci_event_snapshot_t *state);
void dci_post_control_event(const dci_event_snapshot_t *state);

esp_err_t dci_apply_heartbeat_text_locked(dci_slot_t *slot, const char *json, uint64_t rx_ms);
esp_err_t dci_apply_status_text_locked(dci_slot_t *slot, const char *json, uint64_t rx_ms);
esp_err_t dci_apply_diag_text_locked(dci_slot_t *slot, const char *json, uint64_t rx_ms);
esp_err_t dci_apply_result_text_locked(dci_slot_t *slot, const char *json, uint64_t rx_ms);
esp_err_t dci_apply_event_text_locked(dci_slot_t *slot, const char *json, uint64_t rx_ms);
