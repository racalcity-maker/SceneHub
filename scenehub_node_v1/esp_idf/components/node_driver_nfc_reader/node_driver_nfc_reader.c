#include "node_driver_nfc_reader.h"

#include <string.h>

#include "node_board.h"

enum {
    NODE_NFC_POLL_INTERVAL_MIN_MS = 20,
    NODE_NFC_POLL_INTERVAL_MAX_MS = 5000,
    NODE_NFC_DEBOUNCE_MAX_MS = 5000,
    NODE_NFC_I2C_ADDRESS_MIN = 0x08,
    NODE_NFC_I2C_ADDRESS_MAX = 0x77,
};

static bool text_present(const char *text, size_t cap)
{
    size_t len = 0;

    if (!text) {
        return false;
    }
    len = strnlen(text, cap);
    return len > 0 && len < cap;
}

static bool optional_text_valid(const char *text, size_t cap)
{
    return !text || text[0] == '\0' || text_present(text, cap);
}

static bool known_card_valid(const node_nfc_known_card_t *card)
{
    if (!card) {
        return false;
    }
    if (!optional_text_valid(card->name, sizeof(card->name))) {
        return false;
    }
    if (!text_present(card->uid, sizeof(card->uid))) {
        return false;
    }
    if (card->token_id <= 0) {
        return false;
    }
    if (!optional_text_valid(card->event_name, sizeof(card->event_name))) {
        return false;
    }
    return true;
}

static bool duplicate_card_entry(const node_nfc_known_card_t *cards, size_t count, size_t index)
{
    for (size_t i = 0; i < index; ++i) {
        if (strcmp(cards[i].uid, cards[index].uid) == 0) {
            return true;
        }
        if (cards[i].token_id == cards[index].token_id) {
            return true;
        }
    }
    return false;
}

esp_err_t node_driver_nfc_reader_validate_config(const node_nfc_reader_config_t *config,
                                                 node_driver_instance_info_t *out_instance)
{
    node_driver_instance_info_t instance = {0};

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!text_present(config->id, sizeof(config->id)) ||
        !text_present(config->driver_impl, sizeof(config->driver_impl)) ||
        !text_present(config->bus, sizeof(config->bus))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(config->driver_impl, "pn532") != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (strcmp(config->bus, "i2c_1") != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (config->poll_interval_ms < NODE_NFC_POLL_INTERVAL_MIN_MS ||
        config->poll_interval_ms > NODE_NFC_POLL_INTERVAL_MAX_MS ||
        config->debounce_ms > NODE_NFC_DEBOUNCE_MAX_MS ||
        config->known_card_count > NODE_DRIVER_NFC_KNOWN_CARD_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (config->i2c_sda_gpio < 0 || config->i2c_scl_gpio < 0 ||
        config->i2c_sda_gpio == config->i2c_scl_gpio ||
        !node_board_gpio_is_allowed(config->i2c_sda_gpio) ||
        !node_board_gpio_is_allowed(config->i2c_scl_gpio) ||
        config->i2c_address < NODE_NFC_I2C_ADDRESS_MIN ||
        config->i2c_address > NODE_NFC_I2C_ADDRESS_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->reset_gpio >= 0 &&
        (config->reset_gpio == config->i2c_sda_gpio ||
         config->reset_gpio == config->i2c_scl_gpio ||
         !node_board_gpio_is_allowed(config->reset_gpio))) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < config->known_card_count; ++i) {
        if (!known_card_valid(&config->known_cards[i]) ||
            duplicate_card_entry(config->known_cards, config->known_card_count, i)) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    memcpy(instance.id, config->id, sizeof(instance.id));
    instance.kind = NODE_DRIVER_KIND_NFC_READER;
    memcpy(instance.driver_impl, config->driver_impl, sizeof(instance.driver_impl));
    memcpy(instance.bus, config->bus, sizeof(instance.bus));
    instance.enabled = config->enabled;
    if (out_instance) {
        *out_instance = instance;
    }
    return ESP_OK;
}

esp_err_t node_driver_nfc_reader_register_stub(const node_nfc_reader_config_t *config)
{
    node_driver_instance_info_t instance = {0};
    esp_err_t err = node_driver_nfc_reader_validate_config(config, &instance);

    if (err != ESP_OK) {
        return err;
    }
    return node_driver_registry_register_instance(&instance);
}

esp_err_t node_driver_nfc_reader_resolve_card(const node_nfc_reader_config_t *config,
                                              const char *uid,
                                              node_nfc_card_resolution_t *out_resolution)
{
    if (!config || !uid || !out_resolution) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_resolution, 0, sizeof(*out_resolution));
    for (size_t i = 0; i < config->known_card_count; ++i) {
        if (strcmp(config->known_cards[i].uid, uid) == 0) {
            out_resolution->matched = true;
            out_resolution->token_id = config->known_cards[i].token_id;
            memcpy(out_resolution->event_name,
                   config->known_cards[i].event_name,
                   sizeof(out_resolution->event_name));
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
