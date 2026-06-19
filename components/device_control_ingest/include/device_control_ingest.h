#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "quest_device.h"
#include "quest_common_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_CONTROL_INGEST_MAX_DEVICES QUEST_DEVICE_MAX_DEVICES
#define DEVICE_CONTROL_INGEST_BOOT_ID_MAX_LEN 48
#define DEVICE_CONTROL_INGEST_FW_VERSION_MAX_LEN 32
#define DEVICE_CONTROL_INGEST_MODE_MAX_LEN 24
#define DEVICE_CONTROL_INGEST_STATE_MAX_LEN 32
#define DEVICE_CONTROL_INGEST_HEALTH_MAX_LEN 16
#define DEVICE_CONTROL_INGEST_LEVEL_MAX_LEN 16
#define DEVICE_CONTROL_INGEST_CODE_MAX_LEN 32
#define DEVICE_CONTROL_INGEST_MESSAGE_MAX_LEN 160
#define DEVICE_CONTROL_INGEST_REQUEST_ID_MAX_LEN 48
#define DEVICE_CONTROL_INGEST_COMMAND_MAX_LEN 32
#define DEVICE_CONTROL_INGEST_RESULT_STATUS_MAX_LEN 16
#define DEVICE_CONTROL_INGEST_ERROR_CODE_MAX_LEN 32
#define DEVICE_CONTROL_INGEST_RESULT_DATA_JSON_MAX_LEN 2048
#define DEVICE_CONTROL_INGEST_EVENT_ARGS_JSON_MAX_LEN 512
#define DEVICE_CONTROL_INGEST_CACHED_RESULT_DATA_JSON_MAX_LEN \
    (QUEST_DEVICE_DESCRIPTION_JSON_MAX_LEN + 256)
#define DEVICE_CONTROL_INGEST_DESCRIBE_INTERFACE_DATA_JSON_MAX_LEN \
    DEVICE_CONTROL_INGEST_CACHED_RESULT_DATA_JSON_MAX_LEN
#define DEVICE_CONTROL_INGEST_DEFAULT_ONLINE_TIMEOUT_MS 15000

typedef enum {
    DEVICE_CONTROL_TOPIC_UNKNOWN = 0,
    DEVICE_CONTROL_TOPIC_HEARTBEAT,
    DEVICE_CONTROL_TOPIC_STATUS,
    DEVICE_CONTROL_TOPIC_DIAG,
    DEVICE_CONTROL_TOPIC_RESULT,
    DEVICE_CONTROL_TOPIC_EVENT,
} device_control_topic_t;

typedef struct {
    char device_id[QUEST_ID_MAX_LEN];
    uint64_t last_seen_ms;
    uint64_t last_contract_rx_ms;
    bool has_heartbeat;
    uint64_t heartbeat_ts_ms;
    uint64_t heartbeat_rx_ms;
    uint64_t heartbeat_uptime_ms;
    uint32_t heartbeat_status_seq;
    char heartbeat_boot_id[DEVICE_CONTROL_INGEST_BOOT_ID_MAX_LEN];
    bool has_status;
    uint64_t status_ts_ms;
    uint64_t status_rx_ms;
    char status_boot_id[DEVICE_CONTROL_INGEST_BOOT_ID_MAX_LEN];
    char status_fw_version[DEVICE_CONTROL_INGEST_FW_VERSION_MAX_LEN];
    char status_mode[DEVICE_CONTROL_INGEST_MODE_MAX_LEN];
    char status_state[DEVICE_CONTROL_INGEST_STATE_MAX_LEN];
    char status_health[DEVICE_CONTROL_INGEST_HEALTH_MAX_LEN];
    bool status_runtime_active;
    bool status_driver_nfc_enabled;
    bool status_driver_nfc_ready;
    char status_driver_nfc_health[DEVICE_CONTROL_INGEST_HEALTH_MAX_LEN];
    char status_driver_nfc_state[DEVICE_CONTROL_INGEST_STATE_MAX_LEN];
    char status_driver_nfc_error_code[DEVICE_CONTROL_INGEST_ERROR_CODE_MAX_LEN];
    char status_driver_nfc_reader_id[QUEST_ID_MAX_LEN];
    bool has_diag;
    uint64_t diag_ts_ms;
    uint64_t diag_rx_ms;
    char diag_level[DEVICE_CONTROL_INGEST_LEVEL_MAX_LEN];
    char diag_code[DEVICE_CONTROL_INGEST_CODE_MAX_LEN];
    char diag_message[DEVICE_CONTROL_INGEST_MESSAGE_MAX_LEN];
    bool has_result;
    uint64_t result_ts_ms;
    uint64_t result_rx_ms;
    char result_request_id[DEVICE_CONTROL_INGEST_REQUEST_ID_MAX_LEN];
    char result_command[DEVICE_CONTROL_INGEST_COMMAND_MAX_LEN];
    char result_status[DEVICE_CONTROL_INGEST_RESULT_STATUS_MAX_LEN];
    char result_error_code[DEVICE_CONTROL_INGEST_ERROR_CODE_MAX_LEN];
    char result_message[DEVICE_CONTROL_INGEST_MESSAGE_MAX_LEN];
    char result_data_json[DEVICE_CONTROL_INGEST_RESULT_DATA_JSON_MAX_LEN];
    bool has_event;
    uint64_t event_ts_ms;
    uint64_t event_rx_ms;
    char event_name[DEVICE_CONTROL_INGEST_COMMAND_MAX_LEN];
    char event_args_json[DEVICE_CONTROL_INGEST_EVENT_ARGS_JSON_MAX_LEN];
    uint32_t heartbeat_count;
    uint32_t status_count;
    uint32_t diag_count;
    uint32_t result_count;
    uint32_t event_count;
} device_control_ingest_device_t;

esp_err_t device_control_ingest_init(void);
esp_err_t device_control_ingest_reset(void);
esp_err_t device_control_ingest_handle_mqtt(const char *topic, const char *payload);
esp_err_t device_control_ingest_get_device(const char *device_id, device_control_ingest_device_t *out);
esp_err_t device_control_ingest_get_presence(const char *device_id,
                                             uint64_t now_ms,
                                             uint32_t timeout_ms,
                                             uint64_t *out_last_seen_ms,
                                             bool *out_online);
esp_err_t device_control_ingest_take_describe_interface_data(const char *device_id,
                                                             const char *request_id,
                                                             char *out,
                                                             size_t out_size);
esp_err_t device_control_ingest_take_result_data(const char *device_id,
                                                 const char *request_id,
                                                 const char *command,
                                                 char *out,
                                                 size_t out_size);
size_t device_control_ingest_count(void);
uint32_t device_control_ingest_generation(void);
esp_err_t device_control_ingest_get_last_changed_device_id(char *out_device_id, size_t out_device_id_size);
esp_err_t device_control_ingest_list_devices(device_control_ingest_device_t *out,
                                             size_t max_count,
                                             size_t *out_count);

bool device_control_ingest_is_online(const device_control_ingest_device_t *state,
                                     uint64_t now_ms,
                                     uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
