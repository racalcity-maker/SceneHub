#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_limits.h"

typedef enum {
    NODE_DRIVER_KIND_NONE = 0,
    NODE_DRIVER_KIND_NFC_READER = 1,
} node_driver_kind_t;

typedef struct {
    char id[NODE_DRIVER_ID_MAX_LEN + 1];
    node_driver_kind_t kind;
    char driver_impl[NODE_DRIVER_IMPL_MAX_LEN];
    char bus[NODE_DRIVER_BUS_MAX_LEN];
    bool enabled;
} node_driver_instance_info_t;

typedef struct {
    size_t count;
    node_driver_instance_info_t items[NODE_DRIVER_INSTANCE_MAX];
} node_driver_registry_snapshot_t;

esp_err_t node_driver_registry_init(void);
esp_err_t node_driver_registry_clear(void);
esp_err_t node_driver_registry_register_instance(const node_driver_instance_info_t *instance);
esp_err_t node_driver_registry_get_snapshot(node_driver_registry_snapshot_t *out_snapshot);
esp_err_t node_driver_registry_find_instance(const char *id, node_driver_instance_info_t *out_instance);
