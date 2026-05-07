#include "room_scenario.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sd_storage.h"

#define ROOM_SCENARIO_FILE_MAX_BYTES (256 * 1024)

static SemaphoreHandle_t s_persist_lock = NULL;
static portMUX_TYPE s_persist_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t room_scenario_ensure_persist_lock(void)
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

static esp_err_t room_scenario_persist_lock(void)
{
    esp_err_t err = room_scenario_ensure_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_persist_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void room_scenario_persist_unlock(void)
{
    if (s_persist_lock) {
        xSemaphoreGive(s_persist_lock);
    }
}

static char *room_scenario_alloc_bytes(size_t size)
{
    char *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buf;
}

static esp_err_t room_scenario_ensure_sd_for_path(const char *path)
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

static esp_err_t room_scenario_mkdir_parent(const char *path)
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

static esp_err_t room_scenario_make_tmp_path(const char *path, char *tmp, size_t tmp_len)
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

static esp_err_t room_scenario_store_save_to_path_locked(const char *path)
{
    cJSON *root = NULL;
    char *printed = NULL;
    char tmp_path[192] = {0};
    FILE *file = NULL;
    size_t len = 0;
    esp_err_t err = ESP_OK;

    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_scenario_ensure_sd_for_path(path);
    if (err != ESP_OK) {
        return err;
    }
    err = room_scenario_mkdir_parent(path);
    if (err != ESP_OK) {
        return err;
    }
    err = room_scenario_make_tmp_path(path, tmp_path, sizeof(tmp_path));
    if (err != ESP_OK) {
        return err;
    }
    err = room_scenario_store_export_json(&root);
    if (err != ESP_OK) {
        return err;
    }
    printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }
    len = strlen(printed);
    file = fopen(tmp_path, "wb");
    if (!file) {
        cJSON_free(printed);
        return ESP_FAIL;
    }
    if (len > 0 && fwrite(printed, 1, len, file) != len) {
        fclose(file);
        cJSON_free(printed);
        unlink(tmp_path);
        return ESP_FAIL;
    }
    if (fclose(file) != 0) {
        cJSON_free(printed);
        unlink(tmp_path);
        return ESP_FAIL;
    }
    cJSON_free(printed);
    unlink(path);
    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t room_scenario_store_save_to_path(const char *path)
{
    esp_err_t err = room_scenario_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = room_scenario_store_save_to_path_locked(path);
    room_scenario_persist_unlock();
    return err;
}

esp_err_t room_scenario_add_and_save(const room_scenario_t *scenario)
{
    esp_err_t err = room_scenario_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = room_scenario_add(scenario);
    if (err == ESP_OK) {
        err = room_scenario_store_save_to_path_locked(ROOM_SCENARIO_STORAGE_PATH);
    }
    room_scenario_persist_unlock();
    return err;
}

esp_err_t room_scenario_delete_and_save(const char *scenario_id)
{
    esp_err_t err = room_scenario_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = room_scenario_delete(scenario_id);
    if (err == ESP_OK) {
        err = room_scenario_store_save_to_path_locked(ROOM_SCENARIO_STORAGE_PATH);
    }
    room_scenario_persist_unlock();
    return err;
}

esp_err_t room_scenario_store_import_json_and_save(const cJSON *root)
{
    esp_err_t err = room_scenario_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = room_scenario_store_import_json(root);
    if (err == ESP_OK) {
        err = room_scenario_store_save_to_path_locked(ROOM_SCENARIO_STORAGE_PATH);
    }
    room_scenario_persist_unlock();
    return err;
}

static esp_err_t room_scenario_store_load_from_path_locked(const char *path)
{
    FILE *file = NULL;
    char *buf = NULL;
    long file_size = 0;
    size_t bytes_read = 0;
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;

    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_scenario_ensure_sd_for_path(path);
    if (err != ESP_OK) {
        return err;
    }
    file = fopen(path, "rb");
    if (!file) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    file_size = ftell(file);
    if (file_size < 0 || file_size > ROOM_SCENARIO_FILE_MAX_BYTES) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    buf = room_scenario_alloc_bytes((size_t)file_size + 1);
    if (!buf) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    bytes_read = fread(buf, 1, (size_t)file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size) {
        heap_caps_free(buf);
        return ESP_FAIL;
    }
    buf[bytes_read] = '\0';
    root = cJSON_ParseWithLength(buf, bytes_read);
    heap_caps_free(buf);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_scenario_store_import_json(root);
    cJSON_Delete(root);
    return err;
}

esp_err_t room_scenario_store_load_from_path(const char *path)
{
    esp_err_t err = room_scenario_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = room_scenario_store_load_from_path_locked(path);
    room_scenario_persist_unlock();
    return err;
}

esp_err_t room_scenario_store_save(void)
{
    return room_scenario_store_save_to_path(ROOM_SCENARIO_STORAGE_PATH);
}

esp_err_t room_scenario_store_load(void)
{
    return room_scenario_store_load_from_path(ROOM_SCENARIO_STORAGE_PATH);
}
