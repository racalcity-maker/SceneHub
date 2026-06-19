#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_config.h"
#include "node_driver_registry.h"

typedef struct {
    char name[24];
    char uid[NODE_DRIVER_UID_TEXT_MAX_LEN];
    int32_t token_id;
    char event_name[NODE_DRIVER_EVENT_NAME_MAX_LEN];
} node_nfc_known_card_t;

typedef struct {
    char id[NODE_DRIVER_ID_MAX_LEN + 1];
    char driver_impl[NODE_DRIVER_IMPL_MAX_LEN];
    char bus[NODE_DRIVER_BUS_MAX_LEN];
    bool enabled;
    int i2c_sda_gpio;
    int i2c_scl_gpio;
    int reset_gpio;
    uint8_t i2c_address;
    uint32_t poll_interval_ms;
    uint32_t debounce_ms;
    size_t known_card_count;
    node_nfc_known_card_t known_cards[NODE_DRIVER_NFC_KNOWN_CARD_MAX];
} node_nfc_reader_config_t;

typedef struct {
    bool matched;
    int32_t token_id;
    char event_name[NODE_DRIVER_EVENT_NAME_MAX_LEN];
} node_nfc_card_resolution_t;

esp_err_t node_driver_nfc_reader_validate_config(const node_nfc_reader_config_t *config,
                                                 node_driver_instance_info_t *out_instance);
esp_err_t node_driver_nfc_reader_register_stub(const node_nfc_reader_config_t *config);
esp_err_t node_driver_nfc_reader_resolve_card(const node_nfc_reader_config_t *config,
                                              const char *uid,
                                              node_nfc_card_resolution_t *out_resolution);
esp_err_t node_driver_nfc_reader_load_factory_config(const node_config_t *node_config,
                                                     node_nfc_reader_config_t *out_config);
esp_err_t node_driver_nfc_reader_register_factory_stub(const node_config_t *node_config);
esp_err_t node_driver_nfc_reader_save_known_cards(const node_nfc_known_card_t *cards, size_t count);
