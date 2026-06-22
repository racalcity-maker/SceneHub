#include "node_driver_nfc_reader.h"

#include <string.h>

esp_err_t node_driver_nfc_reader_register_stub(const node_nfc_reader_config_t *config)
{
    node_driver_instance_info_t instance = {0};
    esp_err_t err = node_driver_nfc_contract_validate_config(config, &instance);

    if (err != ESP_OK) {
        return err;
    }
    return node_driver_registry_register_instance(&instance);
}
