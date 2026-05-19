#include <string.h>

#include "unity.h"

#include "ota_manager.h"

static esp_partition_t s_ota_running_partition;
static esp_partition_t s_ota_boot_partition;
static esp_partition_t s_ota_update_partition;
static esp_app_desc_t s_ota_app_desc;
static ota_manager_backend_t s_ota_backend;
static esp_ota_img_states_t s_ota_image_state;
static esp_err_t s_ota_begin_err;
static esp_err_t s_ota_write_err;
static esp_err_t s_ota_end_err;
static esp_err_t s_ota_set_boot_err;
static esp_err_t s_ota_confirm_task_err;
static esp_err_t s_ota_reboot_task_err;
static bool s_ota_has_update_partition;
static esp_ota_handle_t s_ota_handle;
static size_t s_ota_begin_size;
static size_t s_ota_written;
static int s_ota_begin_count;
static int s_ota_write_count;
static int s_ota_end_count;
static int s_ota_set_boot_count;
static int s_ota_abort_count;
static int s_ota_confirm_task_count;
static int s_ota_reboot_task_count;

static const esp_partition_t *fake_get_running_partition(void)
{
    return &s_ota_running_partition;
}

static const esp_partition_t *fake_get_boot_partition(void)
{
    return &s_ota_boot_partition;
}

static const esp_app_desc_t *fake_get_app_description(void)
{
    return &s_ota_app_desc;
}

static esp_err_t fake_get_state_partition(const esp_partition_t *partition, esp_ota_img_states_t *out_state)
{
    (void)partition;
    if (!out_state) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_state = s_ota_image_state;
    return ESP_OK;
}

static const esp_partition_t *fake_get_next_update_partition(const esp_partition_t *start_from)
{
    (void)start_from;
    return s_ota_has_update_partition ? &s_ota_update_partition : NULL;
}

static esp_err_t fake_begin(const esp_partition_t *partition, size_t image_size, esp_ota_handle_t *out_handle)
{
    TEST_ASSERT_EQUAL_PTR(&s_ota_update_partition, partition);
    s_ota_begin_count++;
    s_ota_begin_size = image_size;
    if (s_ota_begin_err != ESP_OK) {
        return s_ota_begin_err;
    }
    if (!out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_handle = s_ota_handle;
    return ESP_OK;
}

static esp_err_t fake_write(esp_ota_handle_t handle, const void *data, size_t len)
{
    TEST_ASSERT_EQUAL(s_ota_handle, handle);
    TEST_ASSERT_NOT_NULL(data);
    s_ota_write_count++;
    if (s_ota_write_err != ESP_OK) {
        return s_ota_write_err;
    }
    s_ota_written += len;
    return ESP_OK;
}

static esp_err_t fake_end(esp_ota_handle_t handle)
{
    TEST_ASSERT_EQUAL(s_ota_handle, handle);
    s_ota_end_count++;
    return s_ota_end_err;
}

static esp_err_t fake_set_boot_partition(const esp_partition_t *partition)
{
    TEST_ASSERT_EQUAL_PTR(&s_ota_update_partition, partition);
    s_ota_set_boot_count++;
    return s_ota_set_boot_err;
}

static esp_err_t fake_abort(esp_ota_handle_t handle)
{
    TEST_ASSERT_EQUAL(s_ota_handle, handle);
    s_ota_abort_count++;
    return ESP_OK;
}

static esp_err_t fake_mark_app_valid_cancel_rollback(void)
{
    return ESP_OK;
}

static esp_err_t fake_start_confirm_task(void)
{
    s_ota_confirm_task_count++;
    return s_ota_confirm_task_err;
}

static esp_err_t fake_start_reboot_task(void)
{
    s_ota_reboot_task_count++;
    return s_ota_reboot_task_err;
}

static void ota_test_copy(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static void ota_test_bootstrap(void)
{
    ota_manager_status_t status = {0};

    ota_manager_reset_for_test();
    memset(&s_ota_running_partition, 0, sizeof(s_ota_running_partition));
    memset(&s_ota_boot_partition, 0, sizeof(s_ota_boot_partition));
    memset(&s_ota_update_partition, 0, sizeof(s_ota_update_partition));
    memset(&s_ota_app_desc, 0, sizeof(s_ota_app_desc));
    memset(&s_ota_backend, 0, sizeof(s_ota_backend));
    s_ota_image_state = ESP_OTA_IMG_VALID;
    s_ota_begin_err = ESP_OK;
    s_ota_write_err = ESP_OK;
    s_ota_end_err = ESP_OK;
    s_ota_set_boot_err = ESP_OK;
    s_ota_confirm_task_err = ESP_OK;
    s_ota_reboot_task_err = ESP_OK;
    s_ota_has_update_partition = true;
    s_ota_handle = 0x1234;
    s_ota_begin_size = 0;
    s_ota_written = 0;
    s_ota_begin_count = 0;
    s_ota_write_count = 0;
    s_ota_end_count = 0;
    s_ota_set_boot_count = 0;
    s_ota_abort_count = 0;
    s_ota_confirm_task_count = 0;
    s_ota_reboot_task_count = 0;

    ota_test_copy(s_ota_running_partition.label, sizeof(s_ota_running_partition.label), "factory");
    ota_test_copy(s_ota_boot_partition.label, sizeof(s_ota_boot_partition.label), "factory");
    ota_test_copy(s_ota_update_partition.label, sizeof(s_ota_update_partition.label), "ota_0");
    ota_test_copy(s_ota_app_desc.version, sizeof(s_ota_app_desc.version), "9.8.7");
    s_ota_update_partition.size = 4096;

    s_ota_backend.get_running_partition = fake_get_running_partition;
    s_ota_backend.get_boot_partition = fake_get_boot_partition;
    s_ota_backend.get_app_description = fake_get_app_description;
    s_ota_backend.get_state_partition = fake_get_state_partition;
    s_ota_backend.get_next_update_partition = fake_get_next_update_partition;
    s_ota_backend.begin = fake_begin;
    s_ota_backend.write = fake_write;
    s_ota_backend.end = fake_end;
    s_ota_backend.set_boot_partition = fake_set_boot_partition;
    s_ota_backend.abort = fake_abort;
    s_ota_backend.mark_app_valid_cancel_rollback = fake_mark_app_valid_cancel_rollback;
    s_ota_backend.start_confirm_task = fake_start_confirm_task;
    s_ota_backend.start_reboot_task = fake_start_reboot_task;
    ota_manager_set_backend_for_test(&s_ota_backend);

    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_init());
    ota_manager_get_status(&status);
    TEST_ASSERT_EQUAL_STRING(OTA_MANAGER_PHASE_IDLE, status.phase);
    TEST_ASSERT_TRUE(status.last_success);
    TEST_ASSERT_EQUAL_STRING("factory", status.running_partition);
    TEST_ASSERT_EQUAL_STRING("factory", status.boot_partition);
    TEST_ASSERT_EQUAL_STRING("9.8.7", status.app_version);
}

static void test_ota_manager_rejects_operations_before_init(void)
{
    uint8_t byte = 0xAA;

    ota_manager_reset_for_test();
    ota_manager_set_backend_for_test(&s_ota_backend);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_manager_notify_boot());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_manager_begin_upload(128));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_manager_request_reboot());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_manager_write_chunk(&byte, sizeof(byte)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_manager_finish_upload());
}

static void test_ota_manager_upload_happy_path_requires_reboot(void)
{
    uint8_t chunk_a[3] = {1, 2, 3};
    uint8_t chunk_b[5] = {4, 5, 6, 7, 8};
    ota_manager_status_t status = {0};

    ota_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_begin_upload(1024));
    TEST_ASSERT_TRUE(ota_manager_is_busy());
    TEST_ASSERT_EQUAL(1, s_ota_begin_count);
    TEST_ASSERT_EQUAL_UINT(1024, s_ota_begin_size);
    ota_manager_get_status(&status);
    TEST_ASSERT_TRUE(status.in_progress);
    TEST_ASSERT_FALSE(status.last_success);
    TEST_ASSERT_EQUAL_STRING(OTA_MANAGER_PHASE_UPLOADING, status.phase);
    TEST_ASSERT_EQUAL_UINT(1024, status.total_bytes);

    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_write_chunk(chunk_a, sizeof(chunk_a)));
    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_write_chunk(chunk_b, sizeof(chunk_b)));
    TEST_ASSERT_EQUAL_UINT(sizeof(chunk_a) + sizeof(chunk_b), s_ota_written);
    ota_manager_get_status(&status);
    TEST_ASSERT_EQUAL_UINT(sizeof(chunk_a) + sizeof(chunk_b), status.bytes_written);

    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_finish_upload());
    TEST_ASSERT_FALSE(ota_manager_is_busy());
    TEST_ASSERT_EQUAL(1, s_ota_end_count);
    TEST_ASSERT_EQUAL(1, s_ota_set_boot_count);
    ota_manager_get_status(&status);
    TEST_ASSERT_FALSE(status.in_progress);
    TEST_ASSERT_TRUE(status.reboot_required);
    TEST_ASSERT_TRUE(status.last_success);
    TEST_ASSERT_EQUAL_STRING(OTA_MANAGER_PHASE_REBOOT_REQUIRED, status.phase);

    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_request_reboot());
    TEST_ASSERT_EQUAL(1, s_ota_reboot_task_count);
    ota_manager_get_status(&status);
    TEST_ASSERT_EQUAL_STRING(OTA_MANAGER_PHASE_REBOOTING, status.phase);
}

static void test_ota_manager_rejects_invalid_sizes_and_missing_slot(void)
{
    ota_manager_status_t status = {0};

    ota_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, ota_manager_begin_upload(0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, ota_manager_begin_upload(s_ota_update_partition.size + 1));
    TEST_ASSERT_EQUAL(0, s_ota_begin_count);
    ota_manager_get_status(&status);
    TEST_ASSERT_FALSE(status.last_success);
    TEST_ASSERT_EQUAL_STRING("image too large", status.last_error);

    s_ota_has_update_partition = false;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ota_manager_begin_upload(128));
    ota_manager_get_status(&status);
    TEST_ASSERT_EQUAL_STRING("no OTA slot available", status.last_error);
}

static void test_ota_manager_rejects_double_begin_and_write_before_begin(void)
{
    uint8_t byte = 0xCC;

    ota_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_manager_write_chunk(&byte, sizeof(byte)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_manager_finish_upload());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ota_manager_write_chunk(NULL, sizeof(byte)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ota_manager_write_chunk(&byte, 0));

    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_begin_upload(256));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_manager_begin_upload(256));
    TEST_ASSERT_EQUAL(1, s_ota_begin_count);
}

static void test_ota_manager_abort_resets_upload_state(void)
{
    uint8_t byte = 0xDD;
    ota_manager_status_t status = {0};

    ota_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_begin_upload(256));
    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_write_chunk(&byte, sizeof(byte)));
    ota_manager_abort_upload();
    TEST_ASSERT_EQUAL(1, s_ota_abort_count);
    TEST_ASSERT_FALSE(ota_manager_is_busy());
    ota_manager_get_status(&status);
    TEST_ASSERT_FALSE(status.in_progress);
    TEST_ASSERT_FALSE(status.reboot_required);
    TEST_ASSERT_EQUAL_UINT(0, status.bytes_written);
    TEST_ASSERT_EQUAL_STRING(OTA_MANAGER_PHASE_IDLE, status.phase);
    TEST_ASSERT_EQUAL_STRING("ota aborted", status.last_error);
}

static void test_ota_manager_finish_failures_clear_upload_state(void)
{
    ota_manager_status_t status = {0};

    ota_test_bootstrap();
    s_ota_end_err = ESP_ERR_OTA_VALIDATE_FAILED;
    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_begin_upload(256));
    TEST_ASSERT_EQUAL(ESP_ERR_OTA_VALIDATE_FAILED, ota_manager_finish_upload());
    ota_manager_get_status(&status);
    TEST_ASSERT_FALSE(status.in_progress);
    TEST_ASSERT_FALSE(status.reboot_required);
    TEST_ASSERT_EQUAL_STRING("invalid image", status.last_error);

    ota_test_bootstrap();
    s_ota_set_boot_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_begin_upload(256));
    TEST_ASSERT_EQUAL(ESP_FAIL, ota_manager_finish_upload());
    ota_manager_get_status(&status);
    TEST_ASSERT_FALSE(status.in_progress);
    TEST_ASSERT_FALSE(status.reboot_required);
    TEST_ASSERT_EQUAL_STRING("set boot partition failed", status.last_error);
}

static void test_ota_manager_notify_boot_pending_verify_tracks_ready_phase(void)
{
    ota_manager_status_t status = {0};

    ota_test_bootstrap();
    s_ota_image_state = ESP_OTA_IMG_PENDING_VERIFY;

    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_notify_boot());
    TEST_ASSERT_EQUAL(1, s_ota_confirm_task_count);
    ota_manager_get_status(&status);
    TEST_ASSERT_TRUE(status.pending_verify);
    TEST_ASSERT_FALSE(status.system_ready);
    TEST_ASSERT_EQUAL_STRING(OTA_MANAGER_PHASE_VERIFY_WAIT_READY, status.phase);

    ota_manager_notify_system_ready();
    ota_manager_get_status(&status);
    TEST_ASSERT_TRUE(status.pending_verify);
    TEST_ASSERT_TRUE(status.system_ready);
    TEST_ASSERT_EQUAL_STRING(OTA_MANAGER_PHASE_VERIFY_PENDING, status.phase);
    TEST_ASSERT_EQUAL(1, s_ota_confirm_task_count);
}

static void test_ota_manager_reboot_only_after_valid_finish(void)
{
    ota_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_manager_request_reboot());
    TEST_ASSERT_EQUAL(0, s_ota_reboot_task_count);

    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_begin_upload(256));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ota_manager_request_reboot());
    TEST_ASSERT_EQUAL(ESP_OK, ota_manager_finish_upload());
    s_ota_reboot_task_err = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, ota_manager_request_reboot());
    TEST_ASSERT_EQUAL(1, s_ota_reboot_task_count);
}

void register_ota_manager_tests(void)
{
    RUN_TEST(test_ota_manager_rejects_operations_before_init);
    RUN_TEST(test_ota_manager_upload_happy_path_requires_reboot);
    RUN_TEST(test_ota_manager_rejects_invalid_sizes_and_missing_slot);
    RUN_TEST(test_ota_manager_rejects_double_begin_and_write_before_begin);
    RUN_TEST(test_ota_manager_abort_resets_upload_state);
    RUN_TEST(test_ota_manager_finish_failures_clear_upload_state);
    RUN_TEST(test_ota_manager_notify_boot_pending_verify_tracks_ready_phase);
    RUN_TEST(test_ota_manager_reboot_only_after_valid_finish);
}
