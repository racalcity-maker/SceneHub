#include "node_driver_registry.h"

#include <string.h>

static node_driver_registry_snapshot_t s_registry;
static bool s_initialized;

static bool text_present(const char *text, size_t cap)
{
    size_t len = 0;

    if (!text) {
        return false;
    }
    len = strnlen(text, cap);
    return len > 0 && len < cap;
}

static bool driver_instance_valid(const node_driver_instance_info_t *instance)
{
    if (!instance) {
        return false;
    }
    if (instance->kind == NODE_DRIVER_KIND_NONE) {
        return false;
    }
    if (!text_present(instance->id, sizeof(instance->id))) {
        return false;
    }
    if (!text_present(instance->driver_impl, sizeof(instance->driver_impl))) {
        return false;
    }
    if (!text_present(instance->bus, sizeof(instance->bus))) {
        return false;
    }
    return true;
}

esp_err_t node_driver_registry_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    memset(&s_registry, 0, sizeof(s_registry));
    s_initialized = true;
    return ESP_OK;
}

esp_err_t node_driver_registry_clear(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_registry, 0, sizeof(s_registry));
    return ESP_OK;
}

esp_err_t node_driver_registry_register_instance(const node_driver_instance_info_t *instance)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!driver_instance_valid(instance)) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < s_registry.count; ++i) {
        if (strcmp(s_registry.items[i].id, instance->id) == 0) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (s_registry.count >= NODE_DRIVER_INSTANCE_MAX) {
        return ESP_ERR_NO_MEM;
    }

    s_registry.items[s_registry.count++] = *instance;
    return ESP_OK;
}

esp_err_t node_driver_registry_get_snapshot(node_driver_registry_snapshot_t *out_snapshot)
{
    if (!s_initialized || !out_snapshot) {
        return ESP_ERR_INVALID_STATE;
    }
    *out_snapshot = s_registry;
    return ESP_OK;
}

esp_err_t node_driver_registry_find_instance(const char *id, node_driver_instance_info_t *out_instance)
{
    if (!s_initialized || !id || !out_instance) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < s_registry.count; ++i) {
        if (strcmp(s_registry.items[i].id, id) == 0) {
            *out_instance = s_registry.items[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
