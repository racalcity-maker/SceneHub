#include "quest_device.h"
#include "quest_device_internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sd_storage.h"

#define QUEST_DEVICE_FILE_MAX_BYTES (160 * 1024)

static SemaphoreHandle_t s_persist_lock = NULL;
static portMUX_TYPE s_persist_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t quest_device_save_to_path_locked(const char *path);
static esp_err_t quest_device_load_from_path_locked(const char *path);

esp_err_t quest_device_storage_init(void)
{
    if (s_persist_lock) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_persist_init_lock);
    if (!s_persist_lock) {
        s_persist_lock = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&s_persist_init_lock);
    return s_persist_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t qd_persist_lock(void)
{
    esp_err_t err = quest_device_storage_init();
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_persist_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void qd_persist_unlock(void)
{
    if (s_persist_lock) {
        xSemaphoreGive(s_persist_lock);
    }
}

static char *qd_alloc_bytes(size_t size)
{
    char *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buf;
}

static esp_err_t qd_ensure_sd_for_path(const char *path)
{
    const char *root = sd_storage_root_path();
    size_t root_len = strlen(root);
    if (!path || strncmp(path, root, root_len) != 0 ||
        (path[root_len] != '\0' && path[root_len] != '/')) {
        return ESP_OK;
    }
    esp_err_t err = sd_storage_init();
    if (err != ESP_OK) {
        return err;
    }
    if (!sd_storage_available()) {
        err = sd_storage_mount();
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t qd_mkdir_parent(const char *path)
{
    char dir[160] = {0};
    const char *slash = NULL;
    size_t len = 0;
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    slash = strrchr(path, '/');
    if (!slash || slash == path) {
        return ESP_OK;
    }
    len = (size_t)(slash - path);
    if (len >= sizeof(dir)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
    for (char *p = dir + 1; *p; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
            *p = '/';
            return ESP_FAIL;
        }
        *p = '/';
    }
    if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t qd_make_tmp_path(const char *path, char *tmp, size_t tmp_len)
{
    int written = 0;
    if (!path || !path[0] || !tmp || tmp_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    written = snprintf(tmp, tmp_len, "%s.tmp", path);
    if (written < 0 || (size_t)written >= tmp_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t quest_device_save(void)
{
    return quest_device_save_to_path(QUEST_DEVICE_STORAGE_PATH);
}

esp_err_t quest_device_load(void)
{
    return quest_device_load_from_path(QUEST_DEVICE_STORAGE_PATH);
}

esp_err_t quest_device_save_to_path(const char *path)
{
    esp_err_t err = qd_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = quest_device_save_to_path_locked(path);
    qd_persist_unlock();
    return err;
}

static esp_err_t quest_device_save_to_path_locked(const char *path)
{
    cJSON *root = NULL;
    char *printed = NULL;
    FILE *f = NULL;
    char tmp[192] = {0};
    esp_err_t err = ESP_OK;
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = qd_ensure_sd_for_path(path);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_mkdir_parent(path);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_make_tmp_path(path, tmp, sizeof(tmp));
    if (err != ESP_OK) {
        return err;
    }
    err = quest_device_export_json(&root);
    if (err != ESP_OK) {
        return err;
    }
    printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }
    f = fopen(tmp, "wb");
    if (!f) {
        cJSON_free(printed);
        return ESP_FAIL;
    }
    size_t len = strlen(printed);
    if (fwrite(printed, 1, len, f) != len) {
        fclose(f);
        unlink(tmp);
        cJSON_free(printed);
        return ESP_FAIL;
    }
    if (fclose(f) != 0) {
        unlink(tmp);
        cJSON_free(printed);
        return ESP_FAIL;
    }
    cJSON_free(printed);
    unlink(path);
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t quest_device_upsert_and_save(const quest_device_t *device)
{
    esp_err_t err = qd_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = quest_device_upsert(device);
    if (err == ESP_OK) {
        err = quest_device_save_to_path_locked(QUEST_DEVICE_STORAGE_PATH);
    }
    qd_persist_unlock();
    return err;
}

esp_err_t quest_device_delete_and_save(const char *device_id)
{
    esp_err_t err = qd_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = quest_device_delete(device_id);
    if (err == ESP_OK) {
        err = quest_device_save_to_path_locked(QUEST_DEVICE_STORAGE_PATH);
    }
    qd_persist_unlock();
    return err;
}

esp_err_t quest_device_import_json_and_save(const cJSON *root)
{
    esp_err_t err = qd_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = quest_device_import_json(root);
    if (err == ESP_OK) {
        err = quest_device_save_to_path_locked(QUEST_DEVICE_STORAGE_PATH);
    }
    qd_persist_unlock();
    return err;
}

esp_err_t quest_device_load_from_path(const char *path)
{
    esp_err_t err = qd_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = quest_device_load_from_path_locked(path);
    qd_persist_unlock();
    return err;
}

static esp_err_t quest_device_load_from_path_locked(const char *path)
{
    FILE *f = NULL;
    char *buf = NULL;
    long size = 0;
    size_t bytes_read = 0;
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = qd_ensure_sd_for_path(path);
    if (err != ESP_OK) {
        return err;
    }
    f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    size = ftell(f);
    if (size < 0 || size > QUEST_DEVICE_FILE_MAX_BYTES) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(f);
    buf = qd_alloc_bytes((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    bytes_read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (bytes_read != (size_t)size) {
        heap_caps_free(buf);
        return ESP_FAIL;
    }
    buf[bytes_read] = '\0';
    root = cJSON_ParseWithLength(buf, bytes_read);
    heap_caps_free(buf);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    err = quest_device_import_json(root);
    cJSON_Delete(root);
    return err;
}
