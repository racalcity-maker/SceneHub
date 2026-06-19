#pragma once

#include "esp_err.h"
#include "node_driver_nfc_reader.h"

typedef struct {
    bool started;
    bool enabled;
    bool bus_ready;
    bool session_ready;
    bool ever_ready;
    bool pending_hw_reset;
    uint32_t init_fail_count;
    uint32_t poll_fail_count;
    uint32_t next_init_attempt_ms;
    int last_init_err;
    int last_poll_err;
} node_pn532_i2c_diag_t;

esp_err_t node_driver_pn532_i2c_start(const node_nfc_reader_config_t *config);
esp_err_t node_driver_pn532_i2c_request_reinit(void);
void node_driver_pn532_i2c_get_diag(node_pn532_i2c_diag_t *out_diag);
