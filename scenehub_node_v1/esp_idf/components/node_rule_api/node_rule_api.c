#include "node_rule_api.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "node_rule_compile.h"
#include "node_rule_engine.h"
#include "sdkconfig.h"

static void write_error_code(char *out_error_code, size_t out_error_code_size, const char *code)
{
    if (!out_error_code || out_error_code_size == 0) {
        return;
    }
    snprintf(out_error_code, out_error_code_size, "%s", code ? code : "");
}

static void *alloc_rule_admin_buffer(size_t size)
{
    void *ptr = NULL;

    if (size == 0) {
        return NULL;
    }
#if CONFIG_SPIRAM
    ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, size);
        return ptr;
    }
#endif
    ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

esp_err_t node_rule_api_validate_bundle(const char *raw_json,
                                        node_rule_bundle_metadata_t *out_metadata,
                                        char *out_error_code,
                                        size_t out_error_code_size)
{
    return node_rule_api_validate_bundle_for_config(raw_json,
                                                    NULL,
                                                    out_metadata,
                                                    out_error_code,
                                                    out_error_code_size);
}

esp_err_t node_rule_api_validate_bundle_for_config(const char *raw_json,
                                                   const node_config_t *config,
                                                   node_rule_bundle_metadata_t *out_metadata,
                                                   char *out_error_code,
                                                   size_t out_error_code_size)
{
    node_rule_compiled_bundle_t *compiled =
        (node_rule_compiled_bundle_t *)alloc_rule_admin_buffer(sizeof(*compiled));
    if (!compiled) {
        write_error_code(out_error_code, out_error_code_size, "no_mem");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = node_rule_compile_bundle_for_config(raw_json,
                                                        config,
                                                        compiled,
                                                        out_error_code,
                                                        out_error_code_size);
    if (err != ESP_OK) {
        free(compiled);
        return err;
    }
    if (out_metadata) {
        *out_metadata = compiled->metadata;
    }
    free(compiled);
    return ESP_OK;
}

esp_err_t node_rule_api_apply_bundle(const char *raw_json,
                                     node_rule_bundle_metadata_t *out_metadata,
                                     char *out_error_code,
                                     size_t out_error_code_size)
{
    return node_rule_api_apply_bundle_for_config(raw_json,
                                                 NULL,
                                                 out_metadata,
                                                 out_error_code,
                                                 out_error_code_size);
}

esp_err_t node_rule_api_apply_bundle_for_config(const char *raw_json,
                                                const node_config_t *config,
                                                node_rule_bundle_metadata_t *out_metadata,
                                                char *out_error_code,
                                                size_t out_error_code_size)
{
    node_rule_bundle_metadata_t metadata = {0};
    node_rule_compiled_bundle_t *compiled =
        (node_rule_compiled_bundle_t *)alloc_rule_admin_buffer(sizeof(*compiled));
    if (!compiled) {
        write_error_code(out_error_code, out_error_code_size, "no_mem");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = node_rule_compile_bundle_for_config(raw_json,
                                                        config,
                                                        compiled,
                                                        out_error_code,
                                                        out_error_code_size);
    if (err != ESP_OK) {
        free(compiled);
        return err;
    }
    metadata = compiled->metadata;

    err = node_rule_store_save(raw_json, metadata.raw_size, &metadata);
    if (err != ESP_OK) {
        write_error_code(out_error_code, out_error_code_size, "store_failed");
        free(compiled);
        return err;
    }
    if (out_metadata) {
        *out_metadata = metadata;
    }
    write_error_code(out_error_code, out_error_code_size, "");
    free(compiled);
    return ESP_OK;
}

esp_err_t node_rule_api_get_bundle(node_rule_store_entry_t *out_entry)
{
    return node_rule_store_load(out_entry);
}

esp_err_t node_rule_api_clear_bundle(void)
{
    return node_rule_store_clear();
}

esp_err_t node_rule_api_pause(void)
{
    return node_rule_engine_pause();
}

esp_err_t node_rule_api_resume(void)
{
    return node_rule_engine_resume();
}
