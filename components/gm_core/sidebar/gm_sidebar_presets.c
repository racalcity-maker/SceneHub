#include "gm_sidebar_presets_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "scenehub_device_command_resolver.h"

static EXT_RAM_BSS_ATTR gm_sidebar_preset_t s_items[GM_SIDEBAR_PRESET_MAX_ITEMS];
static size_t s_item_count = 0;
static uint32_t s_generation = 0;
static SemaphoreHandle_t s_store_lock = NULL;
static SemaphoreHandle_t s_persist_lock = NULL;
static portMUX_TYPE s_store_lock_init_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_persist_lock_init_lock = portMUX_INITIALIZER_UNLOCKED;

void gm_sidebar_preset_copy(char *dst, size_t dst_size, const char *src)
{
    size_t len = 0;
    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

void gm_sidebar_preset_set_error(char *error, size_t error_size, const char *message)
{
    gm_sidebar_preset_copy(error, error_size, message);
}

esp_err_t gm_sidebar_preset_store_lock(void)
{
    if (!s_store_lock) {
        portENTER_CRITICAL(&s_store_lock_init_lock);
        if (!s_store_lock) {
            s_store_lock = xSemaphoreCreateMutex();
        }
        portEXIT_CRITICAL(&s_store_lock_init_lock);
        if (!s_store_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    return xSemaphoreTake(s_store_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void gm_sidebar_preset_store_unlock(void)
{
    if (s_store_lock) {
        xSemaphoreGive(s_store_lock);
    }
}

esp_err_t gm_sidebar_preset_persist_lock(void)
{
    if (!s_persist_lock) {
        portENTER_CRITICAL(&s_persist_lock_init_lock);
        if (!s_persist_lock) {
            s_persist_lock = xSemaphoreCreateMutex();
        }
        portEXIT_CRITICAL(&s_persist_lock_init_lock);
        if (!s_persist_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    return xSemaphoreTake(s_persist_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void gm_sidebar_preset_persist_unlock(void)
{
    if (s_persist_lock) {
        xSemaphoreGive(s_persist_lock);
    }
}

char *gm_sidebar_preset_alloc_bytes(size_t size)
{
    char *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buf;
}

esp_err_t gm_sidebar_preset_validate_one(const gm_sidebar_preset_t *preset,
                                         char *error,
                                         size_t error_size)
{
    scenehub_resolved_device_command_t resolved = {0};
    esp_err_t err = ESP_OK;
    char resolver_error[64] = {0};

    if (!preset) {
        gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_invalid");
        return ESP_ERR_INVALID_ARG;
    }
    if (!preset->id[0]) {
        gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_id_required");
        return ESP_ERR_INVALID_ARG;
    }
    if (!preset->label[0]) {
        gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_label_required");
        return ESP_ERR_INVALID_ARG;
    }
    if (!preset->device_id[0]) {
        gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_device_required");
        return ESP_ERR_INVALID_ARG;
    }
    if (!preset->resource_key[0]) {
        gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_resource_required");
        return ESP_ERR_INVALID_ARG;
    }
    if (!preset->command_id[0]) {
        gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_command_required");
        return ESP_ERR_INVALID_ARG;
    }

    err = scenehub_device_command_resolve(preset->device_id,
                                          preset->command_id,
                                          preset->params_json,
                                          false,
                                          &resolved,
                                          resolver_error,
                                          sizeof(resolver_error));
    if (err != ESP_OK) {
        gm_sidebar_preset_set_error(error,
                                    error_size,
                                    resolver_error[0] ? resolver_error : "sidebar_preset_command_invalid");
        return err;
    }
    if (!resolved.command.manual_allowed) {
        gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_command_not_manual");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t gm_sidebar_preset_replace_locked(const gm_sidebar_preset_t *items, size_t count)
{
    if (count > GM_SIDEBAR_PRESET_MAX_ITEMS) {
        return ESP_ERR_INVALID_SIZE;
    }
    memset(s_items, 0, sizeof(s_items));
    if (items && count) {
        memcpy(s_items, items, sizeof(*items) * count);
    }
    s_item_count = count;
    ++s_generation;
    return ESP_OK;
}

const gm_sidebar_preset_t *gm_sidebar_preset_items_locked(size_t *out_count)
{
    if (out_count) {
        *out_count = s_item_count;
    }
    return s_items;
}

esp_err_t gm_sidebar_presets_init(void)
{
    esp_err_t err = gm_sidebar_preset_store_lock();
    if (err != ESP_OK) {
        return err;
    }
    gm_sidebar_preset_store_unlock();
    err = gm_sidebar_preset_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    gm_sidebar_preset_persist_unlock();
    return ESP_OK;
}

uint32_t gm_sidebar_preset_generation(void)
{
    uint32_t generation = 0;
    if (gm_sidebar_preset_store_lock() != ESP_OK) {
        return 0;
    }
    generation = s_generation;
    gm_sidebar_preset_store_unlock();
    return generation;
}

esp_err_t gm_sidebar_preset_list(gm_sidebar_preset_t *out,
                                 size_t max_count,
                                 size_t *out_count)
{
    esp_err_t err = gm_sidebar_preset_store_lock();
    if (err != ESP_OK) {
        return err;
    }
    if (out_count) {
        *out_count = s_item_count;
    }
    if (out) {
        if (max_count < s_item_count) {
            gm_sidebar_preset_store_unlock();
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(out, s_items, sizeof(*out) * s_item_count);
    }
    gm_sidebar_preset_store_unlock();
    return ESP_OK;
}

esp_err_t gm_sidebar_preset_replace(const gm_sidebar_preset_t *items, size_t count)
{
    esp_err_t err = gm_sidebar_preset_store_lock();
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < count; ++i) {
        err = gm_sidebar_preset_validate_one(&items[i], NULL, 0);
        if (err != ESP_OK) {
            gm_sidebar_preset_store_unlock();
            return err;
        }
    }
    err = gm_sidebar_preset_replace_locked(items, count);
    gm_sidebar_preset_store_unlock();
    return err;
}

esp_err_t gm_sidebar_preset_replace_and_save(const gm_sidebar_preset_t *items, size_t count)
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
    for (size_t i = 0; i < count; ++i) {
        err = gm_sidebar_preset_validate_one(&items[i], NULL, 0);
        if (err != ESP_OK) {
            gm_sidebar_preset_store_unlock();
            gm_sidebar_preset_persist_unlock();
            return err;
        }
    }
    err = gm_sidebar_preset_replace_locked(items, count);
    if (err == ESP_OK) {
        err = gm_sidebar_preset_save_to_path_locked(GM_SIDEBAR_PRESET_STORAGE_PATH);
    }
    gm_sidebar_preset_store_unlock();
    gm_sidebar_preset_persist_unlock();
    return err;
}

esp_err_t gm_sidebar_preset_validate_entry(const gm_sidebar_preset_t *preset,
                                           char *error,
                                           size_t error_size)
{
    return gm_sidebar_preset_validate_one(preset, error, error_size);
}
