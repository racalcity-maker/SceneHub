#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "node_config.h"

typedef enum {
    NODE_PROVISIONING_MODE_STA = 0,
    NODE_PROVISIONING_MODE_AP,
} node_provisioning_mode_t;

typedef struct {
    node_provisioning_mode_t mode;
    bool ap_started;
    bool web_started;
    bool sta_got_ip;
    bool sta_disconnected;
    uint16_t sta_disconnect_reason;
} node_provisioning_status_t;

typedef void (*node_provisioning_got_ip_cb_t)(const node_config_t *config, void *ctx);

typedef struct {
    node_provisioning_got_ip_cb_t got_ip_cb;
    void *got_ip_ctx;
} node_provisioning_callbacks_t;

esp_err_t node_provisioning_start(const node_config_t *config, const node_provisioning_callbacks_t *callbacks);
node_provisioning_status_t node_provisioning_get_status(void);
