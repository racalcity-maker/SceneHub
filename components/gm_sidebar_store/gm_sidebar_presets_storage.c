#include "gm_sidebar_presets_internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "sd_storage.h"

#define GM_SIDEBAR_PRESET_FILE_MAX_BYTES (64 * 1024)

static esp_err_t gm_sidebar_preset_ensure_sd_for_path(const char *path)
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

static esp_err_t gm_sidebar_preset_mkdir_parent(const char *path)
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

static esp_err_t gm_sidebar_preset_make_tmp_path(const char *path, char *tmp, size_t tmp_len)
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

esp_err_t gm_sidebar_preset_save_to_path_locked(const char *path)
{
    cJSON *root = NULL;
    char *printed = NULL;
    FILE *f = NULL;
    char tmp[192] = {0};
    size_t len = 0;
    esp_err_t err = ESP_OK;
    uint32_t started_ms = sd_storage_trace_now_ms();
    char detail[64] = {0};
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_sidebar_preset_ensure_sd_for_path(path);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_sidebar_preset_mkdir_parent(path);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_sidebar_preset_make_tmp_path(path, tmp, sizeof(tmp));
    if (err != ESP_OK) {
        return err;
    }
    err = gm_sidebar_preset_export_json_locked(&root);
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
    len = strlen(printed);
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
    snprintf(detail, sizeof(detail), "bytes=%u", (unsigned)len);
    sd_storage_trace_log("gm_sidebar", "save", path, sd_storage_trace_now_ms() - started_ms, detail);
    return ESP_OK;
}

esp_err_t gm_sidebar_preset_load_from_path_locked(const char *path)
{
    FILE *f = NULL;
    char *buf = NULL;
    long size = 0;
    size_t bytes_read = 0;
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;
    uint32_t started_ms = sd_storage_trace_now_ms();
    char detail[64] = {0};
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_sidebar_preset_ensure_sd_for_path(path);
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
    if (size < 0 || size > GM_SIDEBAR_PRESET_FILE_MAX_BYTES) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(f);
    buf = gm_sidebar_preset_alloc_bytes((size_t)size + 1);
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
    err = gm_sidebar_preset_import_json_locked(root, NULL, 0);
    cJSON_Delete(root);
    snprintf(detail, sizeof(detail), "bytes=%u result=%s", (unsigned)bytes_read, esp_err_to_name(err));
    sd_storage_trace_log("gm_sidebar", "load", path, sd_storage_trace_now_ms() - started_ms, detail);
    return err;
}

esp_err_t gm_sidebar_preset_save(void)
{
    return gm_sidebar_preset_save_to_path(GM_SIDEBAR_PRESET_STORAGE_PATH);
}

esp_err_t gm_sidebar_preset_load(void)
{
    return gm_sidebar_preset_load_from_path(GM_SIDEBAR_PRESET_STORAGE_PATH);
}

esp_err_t gm_sidebar_preset_save_to_path(const char *path)
{
    esp_err_t err = gm_sidebar_preset_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_sidebar_preset_store_lock();
    if (err != ESP_OK) {
        gm_sidebar_preset_persist_unlock();
        return err;
    }
    err = gm_sidebar_preset_save_to_path_locked(path);
    gm_sidebar_preset_store_unlock();
    gm_sidebar_preset_persist_unlock();
    return err;
}

esp_err_t gm_sidebar_preset_load_from_path(const char *path)
{
    esp_err_t err = gm_sidebar_preset_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_sidebar_preset_store_lock();
    if (err != ESP_OK) {
        gm_sidebar_preset_persist_unlock();
        return err;
    }
    err = gm_sidebar_preset_load_from_path_locked(path);
    gm_sidebar_preset_store_unlock();
    gm_sidebar_preset_persist_unlock();
    return err;
}
