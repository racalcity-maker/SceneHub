#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_config.h"

typedef struct {
    bool initialized;
    bool started;
    bool enabled;
    bool driver_ready;
    bool card_present;
    uint32_t poll_interval_ms;
    uint32_t debounce_ms;
    uint32_t seen_count;
    int32_t token_id;
    char reader_id[NODE_DRIVER_ID_MAX_LEN + 1];
    char uid[NODE_DRIVER_UID_TEXT_MAX_LEN];
    char last_seen_uid[NODE_DRIVER_UID_TEXT_MAX_LEN];
    char health[16];
    char state[16];
    char error_code[32];
} node_nfc_driver_status_t;

void node_driver_nfc_api_get_status(node_nfc_driver_status_t *out_status);
esp_err_t node_driver_nfc_api_reinit(void);
esp_err_t node_driver_nfc_api_reload(const node_config_t *node_config);
