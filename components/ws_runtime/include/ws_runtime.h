#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t generation;
    uint32_t rooms;
    uint32_t devices;
    uint32_t scenarios;
    uint32_t profiles;
    uint32_t ingest;
    uint32_t session;
    uint32_t static_generation;
    uint32_t runtime_generation;
} ws_runtime_versions_changed_t;

typedef struct {
    const char *slice;
    const char *target_id;
    const char *scope;
    const char *reason;
    uint32_t generation;
} ws_runtime_invalidation_t;

typedef struct {
    const char *reason;
    const char *target_id;
    uint32_t generation;
} ws_runtime_resync_required_t;

bool ws_runtime_available(void);
uint8_t ws_runtime_max_clients(void);

esp_err_t ws_runtime_register_httpd(httpd_handle_t server);

esp_err_t ws_runtime_broadcast_json(const char *json);

esp_err_t ws_runtime_broadcast_versions_changed(const ws_runtime_versions_changed_t *versions);
esp_err_t ws_runtime_broadcast_invalidation(const ws_runtime_invalidation_t *invalidation);
esp_err_t ws_runtime_broadcast_resync_required(const ws_runtime_resync_required_t *resync);

#ifdef __cplusplus
}
#endif
