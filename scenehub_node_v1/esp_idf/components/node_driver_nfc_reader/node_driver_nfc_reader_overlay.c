#include "node_driver_nfc_reader.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "nvs.h"
#include "sdkconfig.h"

enum {
    NODE_NFC_READER_OVERLAY_VERSION = 1,
};

typedef struct {
    uint32_t version;
    uint32_t known_card_count;
    node_nfc_known_card_t known_cards[NODE_DRIVER_NFC_KNOWN_CARD_MAX];
} node_nfc_reader_overlay_v1_t;

static const char *NVS_NS = "node_cfg";
static const char *NVS_KEY = "nfc_cards";

static node_nfc_reader_overlay_v1_t *alloc_overlay_buffer(void)
{
    node_nfc_reader_overlay_v1_t *ptr = NULL;

#if CONFIG_SPIRAM
    ptr = (node_nfc_reader_overlay_v1_t *)heap_caps_malloc(sizeof(*ptr), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, sizeof(*ptr));
        return ptr;
    }
#endif
    ptr = (node_nfc_reader_overlay_v1_t *)heap_caps_malloc(sizeof(*ptr), MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, sizeof(*ptr));
    }
    return ptr;
}

static void apply_overlay_to_config(const node_nfc_reader_overlay_v1_t *overlay, node_nfc_reader_config_t *config)
{
    size_t count = 0;

    if (!overlay || !config || overlay->version != NODE_NFC_READER_OVERLAY_VERSION) {
        return;
    }
    count = overlay->known_card_count;
    if (count > NODE_DRIVER_NFC_KNOWN_CARD_MAX) {
        count = NODE_DRIVER_NFC_KNOWN_CARD_MAX;
    }
    config->known_card_count = count;
    memset(config->known_cards, 0, sizeof(config->known_cards));
    for (size_t i = 0; i < count; ++i) {
        config->known_cards[i] = overlay->known_cards[i];
    }
}

void node_driver_nfc_reader_apply_saved_cards(node_nfc_reader_config_t *config)
{
    nvs_handle_t handle;
    node_nfc_reader_overlay_v1_t *overlay = NULL;
    size_t size = 0;
    esp_err_t err = ESP_OK;

    if (!config) {
        return;
    }
    err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return;
    }
    err = nvs_get_blob(handle, NVS_KEY, NULL, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(node_nfc_reader_overlay_v1_t)) {
        return;
    }

    overlay = alloc_overlay_buffer();
    if (!overlay) {
        return;
    }
    err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        free(overlay);
        return;
    }
    size = sizeof(*overlay);
    err = nvs_get_blob(handle, NVS_KEY, overlay, &size);
    nvs_close(handle);
    if (err != ESP_OK) {
        free(overlay);
        return;
    }

    apply_overlay_to_config(overlay, config);
    free(overlay);
}

esp_err_t node_driver_nfc_reader_save_known_cards(const node_nfc_known_card_t *cards, size_t count)
{
    nvs_handle_t handle;
    node_nfc_reader_overlay_v1_t *overlay = NULL;
    esp_err_t err = ESP_OK;

    if ((!cards && count > 0) || count > NODE_DRIVER_NFC_KNOWN_CARD_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    overlay = alloc_overlay_buffer();
    if (!overlay) {
        return ESP_ERR_NO_MEM;
    }
    overlay->version = NODE_NFC_READER_OVERLAY_VERSION;
    overlay->known_card_count = (uint32_t)count;
    for (size_t i = 0; i < count; ++i) {
        overlay->known_cards[i] = cards[i];
    }

    err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        free(overlay);
        return err;
    }
    err = nvs_set_blob(handle, NVS_KEY, overlay, sizeof(*overlay));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    free(overlay);
    return err;
}
