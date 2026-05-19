#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    NODE_RESET_BUTTON_EVENT_NONE = 0,
    NODE_RESET_BUTTON_EVENT_WIFI_RESET,
    NODE_RESET_BUTTON_EVENT_FACTORY_RESET,
} node_reset_button_event_t;

typedef void (*node_reset_button_callback_t)(node_reset_button_event_t event, void *ctx);

typedef struct {
    int gpio;
    bool active_low;
    node_reset_button_callback_t callback;
    void *callback_ctx;
} node_reset_button_config_t;

esp_err_t node_reset_button_start(const node_reset_button_config_t *config);
