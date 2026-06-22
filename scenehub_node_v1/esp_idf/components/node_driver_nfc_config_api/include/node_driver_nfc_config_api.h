#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "node_config.h"
#include "node_driver_nfc_contract.h"

esp_err_t node_driver_nfc_config_api_load_factory_config(const node_config_t *node_config,
                                                         node_nfc_reader_config_t *out_config);
esp_err_t node_driver_nfc_config_api_save_known_cards(const node_nfc_known_card_t *cards,
                                                      size_t count);
esp_err_t node_driver_nfc_config_api_register_factory_stub(const node_config_t *node_config);
