#include "node_driver_nfc_config_api.h"

#include "node_driver_nfc_reader.h"

esp_err_t node_driver_nfc_config_api_load_factory_config(const node_config_t *node_config,
                                                         node_nfc_reader_config_t *out_config)
{
    return node_driver_nfc_reader_load_factory_config(node_config, out_config);
}

esp_err_t node_driver_nfc_config_api_save_known_cards(const node_nfc_known_card_t *cards,
                                                      size_t count)
{
    return node_driver_nfc_reader_save_known_cards(cards, count);
}

esp_err_t node_driver_nfc_config_api_register_factory_stub(const node_config_t *node_config)
{
    return node_driver_nfc_reader_register_factory_stub(node_config);
}
