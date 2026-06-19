#include "node_rule_store.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs.h"

typedef struct {
    uint32_t store_version;
    uint32_t rule_version;
    uint32_t generation;
    uint32_t raw_size;
    uint8_t has_bundle;
    char bundle_id[NODE_RULE_BUNDLE_ID_MAX_LEN + 1];
    char mode[NODE_RULE_MODE_MAX_LEN];
} node_rule_store_persisted_metadata_t;

enum {
    NODE_RULE_STORE_VERSION = 1,
};

static const char *TAG = "node_rule_store";
static const char *NVS_NS = "node_rules";
static const char *NVS_KEY_META = "meta";
static const char *NVS_KEY_BUNDLE = "bundle";
static const char *STORE_BASE_PATH = "/storage";
static const char *STORE_PATH = "/storage/node_rules.bin";
static const char *STORE_TMP_PATH = "/storage/node_rules.tmp";
static const char *STORE_BAK_PATH = "/storage/node_rules.bak";

static bool s_storage_mounted;

static void clear_entry(node_rule_store_entry_t *entry)
{
    if (!entry) {
        return;
    }
    memset(entry, 0, sizeof(*entry));
}

static void persisted_from_metadata(node_rule_store_persisted_metadata_t *persisted,
                                    const node_rule_bundle_metadata_t *metadata,
                                    size_t raw_size)
{
    if (!persisted || !metadata) {
        return;
    }

    memset(persisted, 0, sizeof(*persisted));
    persisted->store_version = NODE_RULE_STORE_VERSION;
    persisted->rule_version = metadata->version;
    persisted->generation = metadata->generation;
    persisted->raw_size = (uint32_t)raw_size;
    persisted->has_bundle = metadata->has_bundle ? 1U : 0U;
    memcpy(persisted->bundle_id, metadata->bundle_id, sizeof(persisted->bundle_id));
    memcpy(persisted->mode, metadata->mode, sizeof(persisted->mode));
}

static void metadata_from_persisted(node_rule_bundle_metadata_t *metadata,
                                    const node_rule_store_persisted_metadata_t *persisted)
{
    if (!metadata || !persisted) {
        return;
    }

    memset(metadata, 0, sizeof(*metadata));
    metadata->has_bundle = persisted->has_bundle != 0;
    metadata->version = persisted->rule_version;
    metadata->generation = persisted->generation;
    metadata->raw_size = (size_t)persisted->raw_size;
    memcpy(metadata->bundle_id, persisted->bundle_id, sizeof(metadata->bundle_id));
    memcpy(metadata->mode, persisted->mode, sizeof(metadata->mode));
}

static esp_err_t ensure_store_mounted(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = STORE_BASE_PATH,
        .partition_label = "storage",
        .max_files = 4,
        .format_if_mount_failed = true,
    };

    if (s_storage_mounted) {
        return ESP_OK;
    }

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_storage_mounted = true;
        return ESP_OK;
    }

    ESP_LOGW(TAG, "storage mount failed: %s", esp_err_to_name(err));
    return err;
}

static esp_err_t legacy_nvs_load(node_rule_store_entry_t *out_entry)
{
    nvs_handle_t handle = 0;
    node_rule_store_persisted_metadata_t persisted = {0};
    size_t metadata_size = sizeof(persisted);
    size_t raw_size = 0;
    esp_err_t err = ESP_OK;

    err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, NVS_KEY_META, &persisted, &metadata_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    if (metadata_size != sizeof(persisted) || persisted.store_version != NODE_RULE_STORE_VERSION) {
        nvs_close(handle);
        return ESP_ERR_INVALID_VERSION;
    }

    metadata_from_persisted(&out_entry->metadata, &persisted);
    if (!out_entry->metadata.has_bundle || out_entry->metadata.raw_size == 0 ||
        out_entry->metadata.raw_size > NODE_RULE_BUNDLE_MAX_LEN) {
        out_entry->metadata.has_bundle = false;
        out_entry->metadata.raw_size = 0;
        nvs_close(handle);
        return ESP_OK;
    }

    raw_size = out_entry->metadata.raw_size;
    err = nvs_get_blob(handle, NVS_KEY_BUNDLE, out_entry->raw_json, &raw_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        clear_entry(out_entry);
        return err;
    }
    if (raw_size != out_entry->metadata.raw_size || raw_size > NODE_RULE_BUNDLE_MAX_LEN) {
        clear_entry(out_entry);
        return ESP_ERR_INVALID_SIZE;
    }

    out_entry->raw_json[raw_size] = '\0';
    return ESP_OK;
}

static esp_err_t legacy_nvs_clear(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, NVS_KEY_BUNDLE);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        esp_err_t meta_err = nvs_erase_key(handle, NVS_KEY_META);
        if (meta_err != ESP_OK && meta_err != ESP_ERR_NVS_NOT_FOUND) {
            err = meta_err;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t load_from_file(node_rule_store_entry_t *out_entry)
{
    FILE *fp = NULL;
    node_rule_store_persisted_metadata_t persisted = {0};
    size_t raw_size = 0;

    if (!out_entry) {
        return ESP_ERR_INVALID_ARG;
    }

    fp = fopen(STORE_PATH, "rb");
    if (!fp) {
        return ESP_ERR_NOT_FOUND;
    }

    if (fread(&persisted, sizeof(persisted), 1, fp) != 1) {
        fclose(fp);
        return ESP_ERR_INVALID_SIZE;
    }
    if (persisted.store_version != NODE_RULE_STORE_VERSION) {
        fclose(fp);
        return ESP_ERR_INVALID_VERSION;
    }

    metadata_from_persisted(&out_entry->metadata, &persisted);
    if (!out_entry->metadata.has_bundle || out_entry->metadata.raw_size == 0 ||
        out_entry->metadata.raw_size > NODE_RULE_BUNDLE_MAX_LEN) {
        fclose(fp);
        out_entry->metadata.has_bundle = false;
        out_entry->metadata.raw_size = 0;
        return ESP_OK;
    }

    raw_size = out_entry->metadata.raw_size;
    if (fread(out_entry->raw_json, 1, raw_size, fp) != raw_size) {
        fclose(fp);
        clear_entry(out_entry);
        return ESP_ERR_INVALID_SIZE;
    }

    fclose(fp);
    out_entry->raw_json[raw_size] = '\0';
    return ESP_OK;
}

static esp_err_t write_store_file(const char *path,
                                  const node_rule_store_persisted_metadata_t *persisted,
                                  const char *raw_json,
                                  size_t raw_size)
{
    FILE *fp = NULL;

    if (!path || !persisted || !raw_json || raw_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        return ESP_FAIL;
    }

    if (fwrite(persisted, sizeof(*persisted), 1, fp) != 1 ||
        fwrite(raw_json, 1, raw_size, fp) != raw_size) {
        fclose(fp);
        remove(path);
        return ESP_FAIL;
    }

    fclose(fp);
    return ESP_OK;
}

esp_err_t node_rule_store_load(node_rule_store_entry_t *out_entry)
{
    esp_err_t err = ESP_OK;

    if (!out_entry) {
        return ESP_ERR_INVALID_ARG;
    }

    clear_entry(out_entry);

    err = ensure_store_mounted();
    if (err == ESP_OK) {
        err = load_from_file(out_entry);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        if (err != ESP_ERR_NOT_FOUND) {
            return err;
        }
    }

    return legacy_nvs_load(out_entry);
}

esp_err_t node_rule_store_save(const char *raw_json,
                               size_t raw_size,
                               const node_rule_bundle_metadata_t *metadata)
{
    node_rule_store_persisted_metadata_t persisted = {0};
    esp_err_t err = ESP_OK;

    if (!raw_json || !metadata || raw_size == 0 || raw_size > NODE_RULE_BUNDLE_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    err = ensure_store_mounted();
    if (err != ESP_OK) {
        return err;
    }

    persisted_from_metadata(&persisted, metadata, raw_size);
    err = write_store_file(STORE_TMP_PATH, &persisted, raw_json, raw_size);
    if (err != ESP_OK) {
        return err;
    }

    remove(STORE_BAK_PATH);
    if (rename(STORE_PATH, STORE_BAK_PATH) != 0) {
        remove(STORE_BAK_PATH);
    }
    if (rename(STORE_TMP_PATH, STORE_PATH) != 0) {
        rename(STORE_BAK_PATH, STORE_PATH);
        remove(STORE_TMP_PATH);
        return ESP_FAIL;
    }
    remove(STORE_BAK_PATH);

    err = legacy_nvs_clear();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "legacy NVS cleanup failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

esp_err_t node_rule_store_clear(void)
{
    esp_err_t err = ensure_store_mounted();

    if (err == ESP_OK) {
        remove(STORE_TMP_PATH);
        remove(STORE_BAK_PATH);
        if (remove(STORE_PATH) != 0) {
            /* Missing file is not an error. */
        }
    }

    err = legacy_nvs_clear();
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    return err;
}
