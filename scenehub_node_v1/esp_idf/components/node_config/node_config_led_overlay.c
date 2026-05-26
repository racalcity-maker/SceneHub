#include "node_config_internal.h"

#include <string.h>

#include "nvs.h"

static const char *NVS_NS = "node_cfg";
static const char *NVS_LED_KEY = "led_cfg";

static node_led_editor_config_v1_t s_legacy_led_editor_v1;
static node_led_editor_config_v2_t s_led_editor_overlay;
static node_led_editor_config_v2_t s_led_editor_save;

static void copy_led_editor_v2_to_config(const node_led_editor_config_v2_t *editor, node_config_t *config)
{
    if (!editor || !config) {
        return;
    }
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        config->led_strips[i].blink = editor->led_strips[i].blink;
        config->led_strips[i].breathe = editor->led_strips[i].breathe;
        config->led_strips[i].effects = editor->led_strips[i].effects;
    }
}

static void copy_led_editor_v1_to_config(const node_led_editor_config_v1_t *editor, node_config_t *config)
{
    if (!editor || !config) {
        return;
    }
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        config->led_strips[i].blink = editor->led_strips[i].blink;
        config->led_strips[i].breathe = editor->led_strips[i].breathe;
        for (size_t effect_index = 0; effect_index < NODE_LED_EFFECT_COUNT; ++effect_index) {
            const node_led_effect_preset_v8_t *src = &editor->led_strips[i].effects.items[effect_index];
            node_led_effect_preset_t *dst = &config->led_strips[i].effects.items[effect_index];
            dst->duration_ms = src->duration_ms;
            dst->step_ms = src->step_ms;
            dst->repeat_count = src->repeat_count;
            dst->size = src->size;
            dst->intensity = src->intensity;
            dst->density = src->density;
            dst->fade = src->fade;
            dst->palette_mode = src->palette_mode;
            dst->red = src->red;
            dst->green = src->green;
            dst->blue = src->blue;
            dst->white = src->white;
            dst->red2 = src->red2;
            dst->green2 = src->green2;
            dst->blue2 = src->blue2;
            dst->white2 = src->white2;
            migrate_effect_aux_colors((node_led_effect_id_t)effect_index, dst);
        }
    }
}

static void copy_config_to_led_editor(const node_led_strip_config_t *led_strips,
                                      size_t count,
                                      node_led_editor_config_v2_t *editor)
{
    if (!led_strips || !editor) {
        return;
    }
    memset(editor, 0, sizeof(*editor));
    editor->version = g_node_led_editor_version;
    for (size_t i = 0; i < count && i < NODE_LED_STRIP_MAX; ++i) {
        editor->led_strips[i].blink = led_strips[i].blink;
        editor->led_strips[i].breathe = led_strips[i].breathe;
        editor->led_strips[i].effects = led_strips[i].effects;
    }
}

void node_config_overlay_led_editor(node_config_t *config)
{
    nvs_handle_t handle;
    size_t size = 0;
    esp_err_t err;

    if (!config) {
        return;
    }
    err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return;
    }
    err = nvs_get_blob(handle, NVS_LED_KEY, NULL, &size);
    nvs_close(handle);

    if (err == ESP_OK && size == sizeof(s_led_editor_overlay)) {
        memset(&s_led_editor_overlay, 0, sizeof(s_led_editor_overlay));
        err = nvs_open(NVS_NS, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            size = sizeof(s_led_editor_overlay);
            err = nvs_get_blob(handle, NVS_LED_KEY, &s_led_editor_overlay, &size);
            nvs_close(handle);
        }
        if (err == ESP_OK && s_led_editor_overlay.version == g_node_led_editor_version) {
            copy_led_editor_v2_to_config(&s_led_editor_overlay, config);
            return;
        }
    }

    if (err == ESP_OK && size == sizeof(s_legacy_led_editor_v1)) {
        memset(&s_legacy_led_editor_v1, 0, sizeof(s_legacy_led_editor_v1));
        err = nvs_open(NVS_NS, NVS_READONLY, &handle);
        if (err == ESP_OK) {
            size = sizeof(s_legacy_led_editor_v1);
            err = nvs_get_blob(handle, NVS_LED_KEY, &s_legacy_led_editor_v1, &size);
            nvs_close(handle);
        }
        if (err == ESP_OK && s_legacy_led_editor_v1.version == 1) {
            copy_led_editor_v1_to_config(&s_legacy_led_editor_v1, config);
        }
    }
}

esp_err_t node_config_save_led_editor(const node_led_strip_config_t *led_strips, size_t count)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (!led_strips || count > NODE_LED_STRIP_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_led_editor_save, 0, sizeof(s_led_editor_save));
    copy_config_to_led_editor(led_strips, count, &s_led_editor_save);

    err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, NVS_LED_KEY, &s_led_editor_save, sizeof(s_led_editor_save));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
