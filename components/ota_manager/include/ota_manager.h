#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_ota_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_MANAGER_VERSION_MAX         32
#define OTA_MANAGER_PARTITION_LABEL_MAX 16
#define OTA_MANAGER_ERROR_MAX           96
#define OTA_MANAGER_PHASE_MAX           24

#define OTA_MANAGER_PHASE_IDLE              "idle"
#define OTA_MANAGER_PHASE_UPLOADING         "uploading"
#define OTA_MANAGER_PHASE_REBOOT_REQUIRED   "reboot_required"
#define OTA_MANAGER_PHASE_REBOOTING         "rebooting"
#define OTA_MANAGER_PHASE_VERIFY_WAIT_READY "verify_wait_ready"
#define OTA_MANAGER_PHASE_VERIFY_PENDING    "verify_pending"

typedef struct {
    bool rollback_supported;
    bool pending_verify;
    bool in_progress;
    bool system_ready;
    bool reboot_required;
    bool last_success;
    size_t bytes_written;
    size_t total_bytes;
    char running_partition[OTA_MANAGER_PARTITION_LABEL_MAX];
    char boot_partition[OTA_MANAGER_PARTITION_LABEL_MAX];
    char app_version[OTA_MANAGER_VERSION_MAX];
    char phase[OTA_MANAGER_PHASE_MAX];
    char last_error[OTA_MANAGER_ERROR_MAX];
} ota_manager_status_t;

typedef struct {
    const esp_partition_t *(*get_running_partition)(void);
    const esp_partition_t *(*get_boot_partition)(void);
    const esp_app_desc_t *(*get_app_description)(void);
    esp_err_t (*get_state_partition)(const esp_partition_t *partition, esp_ota_img_states_t *out_state);
    const esp_partition_t *(*get_next_update_partition)(const esp_partition_t *start_from);
    esp_err_t (*begin)(const esp_partition_t *partition, size_t image_size, esp_ota_handle_t *out_handle);
    esp_err_t (*write)(esp_ota_handle_t handle, const void *data, size_t len);
    esp_err_t (*end)(esp_ota_handle_t handle);
    esp_err_t (*set_boot_partition)(const esp_partition_t *partition);
    esp_err_t (*abort)(esp_ota_handle_t handle);
    esp_err_t (*mark_app_valid_cancel_rollback)(void);
    esp_err_t (*start_confirm_task)(void);
    esp_err_t (*start_reboot_task)(void);
} ota_manager_backend_t;

esp_err_t ota_manager_init(void);
esp_err_t ota_manager_notify_boot(void);
void ota_manager_notify_system_ready(void);
void ota_manager_get_status(ota_manager_status_t *out);
bool ota_manager_is_busy(void);
esp_err_t ota_manager_begin_upload(size_t image_size);
esp_err_t ota_manager_write_chunk(const void *data, size_t len);
esp_err_t ota_manager_finish_upload(void);
esp_err_t ota_manager_request_reboot(void);
void ota_manager_abort_upload(void);
void ota_manager_set_backend_for_test(const ota_manager_backend_t *backend);
void ota_manager_reset_for_test(void);

#ifdef __cplusplus
}
#endif
