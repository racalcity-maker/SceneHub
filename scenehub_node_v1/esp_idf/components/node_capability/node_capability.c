#include "node_capability.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "node_driver_nfc_config_api.h"
#include "node_hardware_io.h"
#include "node_json.h"
#include "node_text.h"
#include "node_runtime_snapshot.h"
#include "node_runtime_mode.h"

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool failed;
} json_writer_t;

static node_runtime_snapshot_t *s_snapshot_scratch;
static StaticSemaphore_t s_snapshot_lock_storage;
static SemaphoreHandle_t s_snapshot_lock;

static void jw_append(json_writer_t *w, const char *fmt, ...)
{
    if (!w || w->failed || !fmt) {
        return;
    }
    if (w->len >= w->cap) {
        w->failed = true;
        return;
    }
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(w->buf + w->len, w->cap - w->len, fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= w->cap - w->len) {
        w->failed = true;
        return;
    }
    w->len += (size_t)n;
}

static void jw_append_json_escaped(json_writer_t *w, const char *value)
{
    if (!w || w->failed) {
        return;
    }
    if (!node_json_append_escaped(w->buf, w->cap, &w->len, value)) {
        w->failed = true;
    }
}

static void jw_append_json_string(json_writer_t *w, const char *value)
{
    if (!w || w->failed) {
        return;
    }
    jw_append(w, "\"");
    jw_append_json_escaped(w, value);
    jw_append(w, "\"");
}

static const char *safe_text(const char *text)
{
    return text ? text : "";
}

static bool bounded_text_present(const char *text, size_t cap)
{
    return cap > 0U && node_text_nonempty_bounded(text, cap - 1U);
}

static bool bounded_identifier_valid(const char *text, size_t cap)
{
    return cap > 0U && node_text_identifier_valid(text, cap - 1U);
}

static node_nfc_reader_config_t *alloc_nfc_reader_config_scratch(void)
{
    node_nfc_reader_config_t *reader =
        (node_nfc_reader_config_t *)heap_caps_malloc(sizeof(*reader),
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!reader) {
        reader = (node_nfc_reader_config_t *)heap_caps_malloc(sizeof(*reader), MALLOC_CAP_8BIT);
    }
    if (reader) {
        memset(reader, 0, sizeof(*reader));
    }
    return reader;
}

static node_runtime_snapshot_t *alloc_runtime_snapshot_scratch(void)
{
    node_runtime_snapshot_t *snapshot =
        (node_runtime_snapshot_t *)heap_caps_malloc(sizeof(*snapshot),
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snapshot) {
        snapshot = (node_runtime_snapshot_t *)heap_caps_malloc(sizeof(*snapshot), MALLOC_CAP_8BIT);
    }
    if (snapshot) {
        memset(snapshot, 0, sizeof(*snapshot));
    }
    return snapshot;
}

static const char *led_chipset_text(node_led_chipset_t chipset)
{
    switch (chipset) {
    case NODE_LED_CHIPSET_WS2812:
        return "ws2812";
    case NODE_LED_CHIPSET_WS2815:
        return "ws2815";
    case NODE_LED_CHIPSET_SK6812:
        return "sk6812";
    default:
        return "ws2812";
    }
}

static const char *led_color_order_text(node_led_color_order_t color_order)
{
    switch (color_order) {
    case NODE_LED_COLOR_ORDER_RGB:
        return "rgb";
    case NODE_LED_COLOR_ORDER_RBG:
        return "rbg";
    case NODE_LED_COLOR_ORDER_GRB:
        return "grb";
    case NODE_LED_COLOR_ORDER_GBR:
        return "gbr";
    case NODE_LED_COLOR_ORDER_BRG:
        return "brg";
    case NODE_LED_COLOR_ORDER_BGR:
        return "bgr";
    default:
        return "grb";
    }
}

static void append_policy(json_writer_t *w, bool manual, bool scenario, bool confirm, bool result_required)
{
    jw_append(w,
              "\"policy\":{\"manual_allowed\":%s,\"scenario_allowed\":%s,"
              "\"requires_confirmation\":%s,\"result_required\":%s,"
              "\"timeout_ms\":3000,\"danger_level\":\"normal\"}",
              manual ? "true" : "false",
              scenario ? "true" : "false",
              confirm ? "true" : "false",
              result_required ? "true" : "false");
}

static const node_runtime_snapshot_t *lock_snapshot_scratch(bool *out_locked)
{
    static const node_runtime_snapshot_t empty = {0};

    if (out_locked) {
        *out_locked = false;
    }
    if (!s_snapshot_lock) {
        s_snapshot_lock = xSemaphoreCreateMutexStatic(&s_snapshot_lock_storage);
    }
    if (!s_snapshot_scratch) {
        s_snapshot_scratch = alloc_runtime_snapshot_scratch();
    }
    if (!s_snapshot_lock || !s_snapshot_scratch) {
        return &empty;
    }
    if (xSemaphoreTake(s_snapshot_lock, portMAX_DELAY) != pdTRUE) {
        return &empty;
    }
    if (node_runtime_snapshot_capture(s_snapshot_scratch) != ESP_OK) {
        memset(s_snapshot_scratch, 0, sizeof(*s_snapshot_scratch));
    }
    if (out_locked) {
        *out_locked = true;
    }
    return s_snapshot_scratch;
}

static void unlock_snapshot_scratch(bool locked)
{
    if (locked && s_snapshot_lock) {
        xSemaphoreGive(s_snapshot_lock);
    }
}

static const node_runtime_snapshot_t *safe_snapshot(const node_runtime_snapshot_t *snapshot)
{
    static const node_runtime_snapshot_t empty = {0};
    return snapshot ? snapshot : &empty;
}

static void append_output_resource_array(json_writer_t *w,
                                         const char *key,
                                         const node_output_pin_config_t *pins,
                                         size_t count)
{
    bool first = true;
    jw_append(w, "\"%s\":[", key);
    for (size_t i = 0; i < count; ++i) {
        const node_output_pin_config_t *pin = &pins[i];
        if (!pin->enabled) {
            continue;
        }
        jw_append(w,
                  "%s{\"channel\":%u,\"label\":",
                  first ? "" : ",",
                  (unsigned)pin->channel);
        jw_append_json_string(w, pin->label);
        jw_append(w, "}");
        first = false;
    }
    jw_append(w, "]");
}

static void append_io_resource_arrays(json_writer_t *w, const node_config_t *config)
{
    bool first = true;
    jw_append(w, "\"inputs\":[");
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_universal_pin_config_t *pin = &config->universal_io[i];
        if (!pin->enabled || pin->role != NODE_PIN_UNIVERSAL_INPUT) {
            continue;
        }
        jw_append(w,
                  "%s{\"channel\":%u,\"label\":",
                  first ? "" : ",",
                  (unsigned)pin->channel);
        jw_append_json_string(w, pin->label);
        jw_append(w, ",\"event\":\"input.changed\"}");
        first = false;
    }
    jw_append(w, "],");

    first = true;
    jw_append(w, "\"outputs\":[");
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_universal_pin_config_t *pin = &config->universal_io[i];
        if (!pin->enabled || pin->role != NODE_PIN_UNIVERSAL_OUTPUT) {
            continue;
        }
        jw_append(w,
                  "%s{\"channel\":%u,\"label\":",
                  first ? "" : ",",
                  (unsigned)pin->channel);
        jw_append_json_string(w, pin->label);
        jw_append(w, "}");
        first = false;
    }
    jw_append(w, "]");
}

static void append_led_resources(json_writer_t *w, const node_config_t *config)
{
    bool first = true;
    jw_append(w, "\"led_strips\":[");
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *pin = &config->led_strips[i];
        if (!pin->enabled) {
            continue;
        }
        jw_append(w,
                  "%s{\"strip\":%u,\"pixels\":%u,\"chipset\":\"%s\",\"color_order\":\"%s\",\"rgbw\":%s,\"label\":",
                  first ? "" : ",",
                  (unsigned)pin->channel,
                  (unsigned)pin->pixel_count,
                  led_chipset_text(pin->chipset),
                  led_color_order_text(pin->color_order),
                  pin->rgbw ? "true" : "false");
        jw_append_json_string(w, pin->label);
        jw_append(w, "}");
        first = false;
    }
    jw_append(w, "]");
}

static void append_driver_resources(json_writer_t *w, const node_config_t *config)
{
    node_nfc_reader_config_t *reader = alloc_nfc_reader_config_scratch();

    jw_append(w, "\"drivers\":[");
    if (config && reader &&
        node_driver_nfc_config_api_load_factory_config(config, reader) == ESP_OK &&
        bounded_identifier_valid(reader->id, sizeof(reader->id)) &&
        bounded_identifier_valid(reader->driver_impl, sizeof(reader->driver_impl)) &&
        bounded_identifier_valid(reader->bus, sizeof(reader->bus))) {
        jw_append(w, "{\"id\":");
        jw_append_json_string(w, reader->id);
        jw_append(w, ",\"type\":\"nfc_reader\",\"driver\":");
        jw_append_json_string(w, reader->driver_impl);
        jw_append(w, ",\"bus\":");
        jw_append_json_string(w, reader->bus);
        jw_append(w,
                  ",\"enabled\":%s,\"poll_interval_ms\":%lu,\"debounce_ms\":%lu}",
                  reader->enabled ? "true" : "false",
                  (unsigned long)reader->poll_interval_ms,
                  (unsigned long)reader->debounce_ms);
    }
    jw_append(w, "]");
    free(reader);
}

static void append_bundle_command_templates(json_writer_t *w,
                                            const node_runtime_snapshot_t *snapshot,
                                            bool *first)
{
    snapshot = safe_snapshot(snapshot);
    if (!w || !first || snapshot->export_command_count == 0) {
        return;
    }

    for (size_t i = 0; i < snapshot->export_command_count; ++i) {
        const node_runtime_snapshot_export_command_t *command = &snapshot->export_commands[i];

        if (!bounded_identifier_valid(command->id, sizeof(command->id))) {
            continue;
        }
        jw_append(w,
                  "%s{\"id\":",
                  *first ? "" : ",");
        jw_append_json_string(w, command->id);
        jw_append(w, ",\"label\":");
        jw_append_json_string(w, command->label);
        jw_append(w, ",\"target\":\"bundle\",\"command\":");
        jw_append_json_string(w, command->id);
        jw_append(w,
                  ",\"args_schema_ref\":\"none\",\"bundle_export\":true,");
        append_policy(w, true, true, false, true);
        jw_append(w, "}");
        *first = false;
    }
}

static void append_command_templates(json_writer_t *w, const node_runtime_snapshot_t *snapshot)
{
    bool first = true;

    jw_append(w, "\"command_templates\":[");
    append_bundle_command_templates(w, snapshot, &first);

    jw_append(w, "%s{\"id\":\"relay.set\",\"label\":\"Relay set\",\"target\":\"relays\",\"command\":\"relay.set\",\"args_schema_ref\":\"output_set\",\"default_args\":{\"on\":true},", first ? "" : ",");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");
    first = false;

    jw_append(w, "{\"id\":\"relay.pulse\",\"label\":\"Relay pulse\",\"target\":\"relays\",\"command\":\"relay.pulse\",\"args_schema_ref\":\"pulse\",\"default_args\":{\"duration_ms\":300},");
    append_policy(w, true, true, false, true);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"relay.effect\",\"label\":\"Relay effect\",\"target\":\"relays\",\"command\":\"relay.effect\",\"args_schema_ref\":\"relay_effect\",\"default_args\":{\"effect\":\"broken_fluorescent\"},");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"relay.all_off\",\"label\":\"Relay all off\",\"target\":\"relays\",\"command\":\"relay.all_off\",\"args_schema_ref\":\"none\",");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"mosfet.set\",\"label\":\"MOSFET set\",\"target\":\"mosfets\",\"command\":\"mosfet.set\",\"args_schema_ref\":\"mosfet_set\",\"default_args\":{\"value\":255},");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"mosfet.fade\",\"label\":\"MOSFET fade\",\"target\":\"mosfets\",\"command\":\"mosfet.fade\",\"args_schema_ref\":\"mosfet_fade\",\"default_args\":{\"target\":255},");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"mosfet.pulse\",\"label\":\"MOSFET pulse\",\"target\":\"mosfets\",\"command\":\"mosfet.pulse\",\"args_schema_ref\":\"mosfet_pulse\",\"default_args\":{\"value\":255},");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"mosfet.blink\",\"label\":\"MOSFET blink\",\"target\":\"mosfets\",\"command\":\"mosfet.blink\",\"args_schema_ref\":\"mosfet_blink\",\"default_args\":{\"value\":255},");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"mosfet.breathe\",\"label\":\"MOSFET breathe\",\"target\":\"mosfets\",\"command\":\"mosfet.breathe\",\"args_schema_ref\":\"mosfet_breathe\",\"default_args\":{},");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"mosfet.all_off\",\"label\":\"MOSFET all off\",\"target\":\"mosfets\",\"command\":\"mosfet.all_off\",\"args_schema_ref\":\"none\",");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"mosfet.effect\",\"label\":\"MOSFET effect alias\",\"target\":\"mosfets\",\"command\":\"mosfet.effect\",\"args_schema_ref\":\"mosfet_effect\",\"default_args\":{\"effect\":\"set\",\"value\":255},");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"io.set\",\"label\":\"Output set\",\"target\":\"outputs\",\"command\":\"io.set\",\"args_schema_ref\":\"output_set\",\"default_args\":{\"on\":true},");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"io.all_off\",\"label\":\"Output all off\",\"target\":\"outputs\",\"command\":\"io.all_off\",\"args_schema_ref\":\"none\",");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"node.all_off\",\"label\":\"Node all off\",\"target\":\"device\",\"command\":\"node.all_off\",\"args_schema_ref\":\"none\",");
    append_policy(w, true, true, true, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"led.off\",\"label\":\"LED off\",\"target\":\"led_strips\",\"command\":\"led.off\",\"args_schema_ref\":\"led_strip_only\",");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"led.solid\",\"label\":\"LED solid\",\"target\":\"led_strips\",\"command\":\"led.solid\",\"args_schema_ref\":\"led_solid\",\"default_args\":{\"color\":\"#ffffff\"},");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"led.blink\",\"label\":\"LED blink\",\"target\":\"led_strips\",\"command\":\"led.blink\",\"args_schema_ref\":\"led_blink\",\"default_args\":{\"color\":\"#ffffff\",\"times\":1},");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"led.breathe\",\"label\":\"LED breathe\",\"target\":\"led_strips\",\"command\":\"led.breathe\",\"args_schema_ref\":\"led_breathe\",\"default_args\":{\"color\":\"#ffffff\"},");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"led.effect\",\"label\":\"LED effect\",\"target\":\"led_strips\",\"command\":\"led.effect\",\"args_schema_ref\":\"led_effect\",\"default_args\":{\"effect\":\"rainbow\"},");
    append_policy(w, true, true, false, false);
    jw_append(w, "}");

    jw_append(w, "],");
}

static void append_admin_command_templates(json_writer_t *w)
{
    jw_append(w, "\"admin_command_templates\":[");

    jw_append(w,
              "{\"id\":\"node.rules.validate\",\"label\":\"Validate node rules\",\"command\":\"node.rules.validate\",");
    append_policy(w, false, false, true, true);
    jw_append(w, "},");

    jw_append(w,
              "{\"id\":\"node.rules.apply\",\"label\":\"Apply node rules\",\"command\":\"node.rules.apply\",");
    append_policy(w, false, false, true, true);
    jw_append(w, "},");

    jw_append(w,
              "{\"id\":\"node.rules.get\",\"label\":\"Get node rules\",\"command\":\"node.rules.get\",");
    append_policy(w, false, false, false, true);
    jw_append(w, "},");

    jw_append(w,
              "{\"id\":\"node.rules.clear\",\"label\":\"Clear node rules\",\"command\":\"node.rules.clear\",");
    append_policy(w, false, false, true, true);
    jw_append(w, "},");

    jw_append(w,
              "{\"id\":\"node.rules.pause\",\"label\":\"Pause node rules\",\"command\":\"node.rules.pause\",");
    append_policy(w, false, false, false, true);
    jw_append(w, "},");

    jw_append(w,
              "{\"id\":\"node.rules.resume\",\"label\":\"Resume node rules\",\"command\":\"node.rules.resume\",");
    append_policy(w, false, false, false, true);
    jw_append(w, "},");

    jw_append(w,
              "{\"id\":\"node.reboot\",\"label\":\"Reboot node\",\"command\":\"node.reboot\",");
    append_policy(w, false, false, true, true);
    jw_append(w, "},");

    jw_append(w,
              "{\"id\":\"node.nfc.reinit\",\"label\":\"Reinit NFC reader\",\"command\":\"node.nfc.reinit\",");
    append_policy(w, false, false, false, true);
    jw_append(w, "}],");
}

static bool known_card_event_duplicate(const node_nfc_reader_config_t *reader, size_t index)
{
    char base_seen[NODE_DRIVER_EVENT_NAME_MAX_LEN] = {0};
    char base_removed[NODE_DRIVER_EVENT_NAME_MAX_LEN] = {0};

    if (!reader || index >= reader->known_card_count) {
        return true;
    }
    if (!bounded_identifier_valid(reader->known_cards[index].event_name,
                                  sizeof(reader->known_cards[index].event_name))) {
        return true;
    }
    snprintf(base_seen, sizeof(base_seen), "%s_card_seen", safe_text(reader->id));
    snprintf(base_removed, sizeof(base_removed), "%s_card_removed", safe_text(reader->id));
    if (strcmp(reader->known_cards[index].event_name, base_seen) == 0 ||
        strcmp(reader->known_cards[index].event_name, base_removed) == 0) {
        return true;
    }
    for (size_t i = 0; i < index; ++i) {
        if (strcmp(reader->known_cards[i].event_name, reader->known_cards[index].event_name) == 0) {
            return true;
        }
    }
    return false;
}

static void append_driver_event_templates(json_writer_t *w, const node_config_t *config, bool *first)
{
    node_nfc_reader_config_t *reader = alloc_nfc_reader_config_scratch();
    char base_seen[NODE_DRIVER_EVENT_NAME_MAX_LEN] = {0};
    char base_removed[NODE_DRIVER_EVENT_NAME_MAX_LEN] = {0};

    if (!w || !first || !config || !reader ||
        node_driver_nfc_config_api_load_factory_config(config, reader) != ESP_OK ||
        !bounded_identifier_valid(reader->id, sizeof(reader->id))) {
        free(reader);
        return;
    }
    snprintf(base_seen, sizeof(base_seen), "%s_card_seen", safe_text(reader->id));
    snprintf(base_removed, sizeof(base_removed), "%s_card_removed", safe_text(reader->id));

    jw_append(w,
              "%s{\"id\":",
              *first ? "" : ",");
    jw_append_json_string(w, base_seen);
    jw_append(w, ",\"label\":");
    jw_append_json_string(w, base_seen);
    jw_append(w, ",\"source\":\"drivers\",\"event\":");
    jw_append_json_string(w, base_seen);
    jw_append(w, ",\"args_schema_ref\":\"driver_local_event\"}");
    *first = false;

    jw_append(w,
              "%s{\"id\":",
              *first ? "" : ",");
    jw_append_json_string(w, base_removed);
    jw_append(w, ",\"label\":");
    jw_append_json_string(w, base_removed);
    jw_append(w, ",\"source\":\"drivers\",\"event\":");
    jw_append_json_string(w, base_removed);
    jw_append(w, ",\"args_schema_ref\":\"driver_local_event\"}");
    *first = false;

    for (size_t i = 0; i < reader->known_card_count; ++i) {
        const node_nfc_known_card_t *card = &reader->known_cards[i];

        if (known_card_event_duplicate(reader, i)) {
            continue;
        }
        jw_append(w,
                  "%s{\"id\":",
                  *first ? "" : ",");
        jw_append_json_string(w, card->event_name);
        jw_append(w, ",\"label\":");
        jw_append(w, "\"");
        if (bounded_text_present(card->name, sizeof(card->name))) {
            jw_append_json_escaped(w, reader->id);
            jw_append(w, " ");
            jw_append_json_escaped(w, card->name);
        } else if (card->token_id != 0) {
            jw_append_json_escaped(w, reader->id);
            jw_append(w, " token %ld", (long)card->token_id);
        } else {
            jw_append_json_escaped(w, card->event_name);
        }
        jw_append(w, "\",\"source\":\"drivers\",\"event\":");
        jw_append_json_string(w, card->event_name);
        jw_append(w, ",\"args_schema_ref\":\"driver_local_event\"}");
        *first = false;
    }

    free(reader);
}

static void append_bundle_event_templates(json_writer_t *w,
                                          const node_runtime_snapshot_t *snapshot,
                                          bool *first)
{
    snapshot = safe_snapshot(snapshot);
    if (!w || !first || snapshot->export_event_count == 0) {
        return;
    }

    for (size_t i = 0; i < snapshot->export_event_count; ++i) {
        const node_runtime_snapshot_export_event_t *event = &snapshot->export_events[i];

        if (!bounded_identifier_valid(event->id, sizeof(event->id))) {
            continue;
        }
        jw_append(w,
                  "%s{\"id\":",
                  *first ? "" : ",");
        jw_append_json_string(w, event->id);
        jw_append(w, ",\"label\":");
        jw_append_json_string(w, event->label);
        jw_append(w, ",\"source\":\"bundle\",\"event\":");
        jw_append_json_string(w, event->id);
        jw_append(w, ",\"args_schema_ref\":\"none\",\"bundle_export\":true}");
        *first = false;
    }
}

static void append_event_templates(json_writer_t *w,
                                   const node_config_t *config,
                                   const node_runtime_snapshot_t *snapshot)
{
    bool first = true;

    jw_append(w, "\"event_templates\":[");
    append_bundle_event_templates(w, snapshot, &first);
    jw_append(w,
              "%s{\"id\":\"input.changed\",\"label\":\"Input changed\",\"source\":\"inputs\","
              "\"event\":\"input.changed\",\"args_schema_ref\":\"input_event\"}",
              first ? "" : ",");
    first = false;
    jw_append(w,
              "%s{\"id\":\"rules.changed\",\"label\":\"Rules changed\",\"source\":\"runtime\","
              "\"event\":\"rules.changed\",\"args_schema_ref\":\"rules_changed_event\"}",
              first ? "" : ",");
    first = false;
    append_driver_event_templates(w, config, &first);
    jw_append(w, "],");
}

static void append_led_effect_options(json_writer_t *w)
{
    bool first = true;
    for (size_t i = 0; i < node_led_effect_descriptor_count(); ++i) {
        const node_led_effect_descriptor_t *desc = &node_led_effect_descriptors()[i];
        if (!node_led_effect_is_advanced(desc->id)) {
            continue;
        }
        jw_append(w, "%s", first ? "" : ",");
        jw_append_json_string(w, desc->name);
        first = false;
    }
}

static void append_schemas(json_writer_t *w)
{
    jw_append(w,
              "\"schemas\":{"
              "\"output_set\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"on\",\"type\":\"checkbox\"}"
              "],"
              "\"pulse\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"duration_ms\",\"type\":\"number\"}"
              "],"
              "\"relay_effect\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"effect\",\"type\":\"select\",\"options\":[\"broken_fluorescent\"]}"
              "],"
              "\"mosfet_set\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"value\",\"type\":\"number\"}"
              "],"
              "\"mosfet_fade\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"target\",\"type\":\"number\",\"optional\":true}"
              "],"
              "\"mosfet_pulse\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"value\",\"type\":\"number\",\"optional\":true}"
              "],"
              "\"mosfet_blink\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"value\",\"type\":\"number\",\"optional\":true}"
              "],"
              "\"mosfet_breathe\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"}"
              "],"
              "\"mosfet_effect\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"effect\",\"type\":\"select\","
              "\"options\":[\"set\",\"pulse\",\"blink\",\"fade\",\"fade_in\",\"fade_out\",\"breathe\",\"broken_fluorescent\"]},"
              "{\"key\":\"value\",\"type\":\"number\",\"optional\":true},"
              "{\"key\":\"on\",\"type\":\"checkbox\",\"optional\":true}"
              "],"
              "\"led_strip_only\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"}"
              "],"
              "\"led_solid\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"color\",\"type\":\"text\"}"
              "],"
              "\"led_blink\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"color\",\"type\":\"text\"},"
              "{\"key\":\"times\",\"type\":\"number\"}"
              "],"
              "\"led_breathe\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"color\",\"type\":\"text\"}"
              "],"
              "\"led_effect\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"effect\",\"type\":\"select\",\"options\":[");
    append_led_effect_options(w);
    jw_append(w,
              "]}"
              "],"
              "\"rules_changed_event\":["
              "{\"key\":\"op\",\"type\":\"text\"},"
              "{\"key\":\"mode\",\"type\":\"text\",\"optional\":true},"
              "{\"key\":\"bundle_id\",\"type\":\"text\",\"optional\":true},"
              "{\"key\":\"generation\",\"type\":\"number\",\"optional\":true},"
              "{\"key\":\"paused\",\"type\":\"checkbox\",\"optional\":true},"
              "{\"key\":\"has_bundle\",\"type\":\"checkbox\",\"optional\":true}"
              "],"
              "\"driver_local_event\":["
              "{\"key\":\"source_id\",\"type\":\"text\",\"optional\":true},"
              "{\"key\":\"token_id\",\"type\":\"number\",\"optional\":true},"
              "{\"key\":\"uid\",\"type\":\"text\",\"optional\":true}"
              "],"
              "\"input_event\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"value\",\"type\":\"number\",\"optional\":true}"
              "],"
              "\"none\":["
              "]"
              "}");
}

static void append_v2_metadata(json_writer_t *w,
                               const node_config_t *config,
                               const node_runtime_snapshot_t *snapshot)
{
    node_operation_mode_t mode = node_runtime_mode_normalize((node_operation_mode_t)config->operation_mode);
    const char *rules_status = "disabled";
    bool fallback_configured = false;

    snapshot = safe_snapshot(snapshot);
    if (node_runtime_mode_rules_enabled(config)) {
        rules_status = safe_text(snapshot->compile_status);
    }
    fallback_configured =
        (mode == NODE_OPERATION_MODE_FALLBACK && snapshot->fallback_timeout_ms > 0U);

    jw_append(w, "\"v2\":{\"operation_mode\":");
    jw_append_json_string(w, node_runtime_mode_name(mode));
    jw_append(w,
              ",\"standalone_mqtt_enabled\":%s,"
              "\"rules_supported\":%s,"
              "\"active_bundle_id\":",
              config->standalone_mqtt_enabled ? "true" : "false",
              node_runtime_mode_rules_enabled(config) ? "true" : "false");
    jw_append_json_string(w, snapshot->has_bundle ? snapshot->bundle_id : "");
    jw_append(w, ",\"rules_generation\":%lu,\"rules_status\":",
              (unsigned long)(snapshot->has_bundle ? snapshot->generation : 0U));
    jw_append_json_string(w, rules_status);
    jw_append(w,
              ",\"rules_paused\":%s,"
              "\"rules_initialized\":%s,"
              "\"fallback_configured\":%s,"
              "\"fallback_enabled\":%s,"
              "\"fallback_state\":",
              snapshot->rules_paused ? "true" : "false",
              snapshot->rules_initialized ? "true" : "false",
              fallback_configured ? "true" : "false",
              snapshot->fallback_enabled ? "true" : "false");
    jw_append_json_string(w, snapshot->fallback_state);
    jw_append(w, "},");
}

esp_err_t node_capability_write_device_description(const node_config_t *config,
                                                   char *out,
                                                   size_t out_size,
                                                   size_t *out_written)
{
    if (!config || !out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    json_writer_t w = {.buf = out, .cap = out_size, .len = 0, .failed = false};
    bool snapshot_locked = false;
    const node_runtime_snapshot_t *snapshot = safe_snapshot(lock_snapshot_scratch(&snapshot_locked));

    jw_append(&w,
              "{\"manifest_version\":2,\"format\":\"compact_resources\","
              "\"node_kind\":\"scenehub_node\","
              "\"capability_contract\":\"scenehub.node.compact.v1\","
              "\"device\":{\"id\":");
    jw_append_json_string(&w, config->node_id);
    jw_append(&w, ",\"name\":");
    jw_append_json_string(&w, config->node_name);
    jw_append(&w, ",\"kind\":\"scenehub_node\"},\"resources\":{");

    append_output_resource_array(&w, "relays", config->relays, NODE_RELAY_MAX);
    jw_append(&w, ",");
    append_output_resource_array(&w, "mosfets", config->mosfets, NODE_MOSFET_MAX);
    jw_append(&w, ",");
    append_io_resource_arrays(&w, config);
    jw_append(&w, ",");
    append_led_resources(&w, config);
    jw_append(&w, ",");
    append_driver_resources(&w, config);
    jw_append(&w, "},");

    append_command_templates(&w, snapshot);
    append_admin_command_templates(&w);
    append_event_templates(&w, config, snapshot);
    append_v2_metadata(&w, config, snapshot);
    append_schemas(&w);
    jw_append(&w, "}");

    if (w.failed) {
        unlock_snapshot_scratch(snapshot_locked);
        out[0] = '\0';
        return ESP_ERR_NO_MEM;
    }
    if (out_written) {
        *out_written = w.len;
    }
    unlock_snapshot_scratch(snapshot_locked);
    return ESP_OK;
}

esp_err_t node_capability_write_node_status_json(const node_config_t *config,
                                                 char *out,
                                                 size_t out_size,
                                                 size_t *out_written)
{
    node_hardware_io_status_t status = {0};
    const node_runtime_snapshot_t *snapshot = NULL;
    const char *operation_mode = NULL;
    bool snapshot_locked = false;
    json_writer_t w = {.buf = out, .cap = out_size, .len = 0, .failed = false};

    if (!config || !out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    status = node_hardware_io_get_status();
    snapshot = safe_snapshot(lock_snapshot_scratch(&snapshot_locked));
    operation_mode = node_runtime_mode_name((node_operation_mode_t)config->operation_mode);

    jw_append(&w, "{\"operation_mode\":");
    jw_append_json_string(&w, operation_mode);
    jw_append(&w,
              ",\"standalone_mqtt_enabled\":%s,\"hardware\":{\"relays\":%u,\"mosfets\":%u,\"universal_inputs\":%u,"
              "\"universal_outputs\":%u,\"led_strips\":%u},"
              "\"rules\":{\"supported\":%s,\"enabled_by_mode\":%s,\"initialized\":%s,\"paused\":%s,"
              "\"compile_status\":",
              config->standalone_mqtt_enabled ? "true" : "false",
              (unsigned)status.configured_relays,
              (unsigned)status.configured_mosfets,
              (unsigned)status.configured_universal_inputs,
              (unsigned)status.configured_universal_outputs,
              (unsigned)status.configured_led_strips,
              node_runtime_mode_rules_enabled(config) ? "true" : "false",
              snapshot->rules_enabled_by_mode ? "true" : "false",
              snapshot->rules_initialized ? "true" : "false",
              snapshot->rules_paused ? "true" : "false");
    jw_append_json_string(&w, snapshot->compile_status);
    jw_append(&w,
              ",\"has_bundle\":%s,\"bundle_id\":",
              snapshot->has_bundle ? "true" : "false");
    jw_append_json_string(&w, snapshot->has_bundle ? snapshot->bundle_id : "");
    jw_append(&w,
              ",\"generation\":%lu,\"compiled_rules\":%lu,\"compiled_actions\":%lu},"
              "\"fallback\":{\"configured\":%s,\"initialized\":%s,\"enabled\":%s,\"state\":",
              (unsigned long)(snapshot->has_bundle ? snapshot->generation : 0U),
              (unsigned long)snapshot->compiled_rules,
              (unsigned long)snapshot->compiled_actions,
              (config->operation_mode == NODE_OPERATION_MODE_FALLBACK &&
               snapshot->fallback_timeout_ms > 0U)
                  ? "true"
                  : "false",
              snapshot->fallback_initialized ? "true" : "false",
              snapshot->fallback_enabled ? "true" : "false");
    jw_append_json_string(&w, snapshot->fallback_state);
    jw_append(&w,
              ",\"wifi_ready\":%s,\"mqtt_connected\":%s,\"rules_active\":%s,"
              "\"timeout_ms\":%lu,\"return_delay_ms\":%lu,\"return_policy\":",
              snapshot->fallback_wifi_ready ? "true" : "false",
              snapshot->fallback_mqtt_connected ? "true" : "false",
              snapshot->fallback_rules_active ? "true" : "false",
              (unsigned long)snapshot->fallback_timeout_ms,
              (unsigned long)snapshot->fallback_return_delay_ms);
    jw_append_json_string(&w, snapshot->fallback_return_policy);
    jw_append(&w, "}}");
    if (w.failed) {
        unlock_snapshot_scratch(snapshot_locked);
        if (out_size > 0) {
            out[0] = '\0';
        }
        return ESP_ERR_NO_MEM;
    }
    if (out_written) {
        *out_written = w.len;
    }
    unlock_snapshot_scratch(snapshot_locked);
    return ESP_OK;
}
