#include "node_capability.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "node_driver_nfc_reader.h"
#include "node_fallback_runtime.h"
#include "node_rule_compile.h"
#include "node_rule_engine.h"
#include "node_runtime_mode.h"

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool failed;
} json_writer_t;

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

static const char *safe_text(const char *text)
{
    return text ? text : "";
}

static bool bounded_text_present(const char *text, size_t cap)
{
    size_t len = 0;

    if (!text) {
        return false;
    }
    len = strnlen(text, cap);
    return len > 0 && len < cap;
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

static void append_claims_array(json_writer_t *w, const node_rule_exported_command_t *command)
{
    bool first = true;

    jw_append(w, "\"claims\":[");
    if (command) {
        for (size_t i = 0; i < command->claim_count && i < NODE_RULE_MAX_EXPORT_CLAIMS; ++i) {
            if (!bounded_text_present(command->claims[i], sizeof(command->claims[i]))) {
                continue;
            }
            jw_append(w, "%s\"%s\"", first ? "" : ",", safe_text(command->claims[i]));
            first = false;
        }
    }
    jw_append(w, "]");
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
                  "%s{\"channel\":%u,\"label\":\"%s\"}",
                  first ? "" : ",",
                  (unsigned)pin->channel,
                  safe_text(pin->label));
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
                  "%s{\"channel\":%u,\"label\":\"%s\",\"event\":\"input.changed\"}",
                  first ? "" : ",",
                  (unsigned)pin->channel,
                  safe_text(pin->label));
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
                  "%s{\"channel\":%u,\"label\":\"%s\"}",
                  first ? "" : ",",
                  (unsigned)pin->channel,
                  safe_text(pin->label));
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
                  "%s{\"strip\":%u,\"pixels\":%u,\"chipset\":\"%s\",\"color_order\":\"%s\",\"rgbw\":%s,\"label\":\"%s\"}",
                  first ? "" : ",",
                  (unsigned)pin->channel,
                  (unsigned)pin->pixel_count,
                  led_chipset_text(pin->chipset),
                  led_color_order_text(pin->color_order),
                  pin->rgbw ? "true" : "false",
                  safe_text(pin->label));
        first = false;
    }
    jw_append(w, "]");
}

static void append_driver_resources(json_writer_t *w, const node_config_t *config)
{
    node_nfc_reader_config_t *reader = alloc_nfc_reader_config_scratch();

    jw_append(w, "\"drivers\":[");
    if (config && reader && node_driver_nfc_reader_load_factory_config(config, reader) == ESP_OK) {
        jw_append(w,
                  "{\"id\":\"%s\",\"type\":\"nfc_reader\",\"driver\":\"%s\",\"bus\":\"%s\","
                  "\"enabled\":%s,\"i2c_sda_gpio\":%d,\"i2c_scl_gpio\":%d,\"reset_gpio\":%d,"
                  "\"i2c_address\":%u,\"poll_interval_ms\":%lu,\"debounce_ms\":%lu,"
                  "\"known_card_count\":%lu,\"known_cards\":[",
                  safe_text(reader->id),
                  safe_text(reader->driver_impl),
                  safe_text(reader->bus),
                  reader->enabled ? "true" : "false",
                  reader->i2c_sda_gpio,
                  reader->i2c_scl_gpio,
                  reader->reset_gpio,
                  (unsigned)reader->i2c_address,
                  (unsigned long)reader->poll_interval_ms,
                  (unsigned long)reader->debounce_ms,
                  (unsigned long)reader->known_card_count);

        bool first = true;
        for (size_t i = 0; i < reader->known_card_count; ++i) {
            const node_nfc_known_card_t *card = &reader->known_cards[i];

            if (!bounded_text_present(card->uid, sizeof(card->uid))) {
                continue;
            }
            jw_append(w,
                      "%s{\"uid\":\"%s\",\"token_id\":%ld",
                      first ? "" : ",",
                      safe_text(card->uid),
                      (long)card->token_id);
            if (bounded_text_present(card->name, sizeof(card->name))) {
                jw_append(w, ",\"name\":\"%s\"", safe_text(card->name));
            }
            if (bounded_text_present(card->event_name, sizeof(card->event_name))) {
                jw_append(w, ",\"event\":\"%s\"", safe_text(card->event_name));
            }
            jw_append(w, "}");
            first = false;
        }
        jw_append(w, "]}");
    }
    jw_append(w, "]");
    free(reader);
}

static void append_bundle_command_templates(json_writer_t *w, const node_rule_compiled_bundle_t *compiled, bool *first)
{
    if (!w || !first || !compiled || compiled->status != NODE_RULE_COMPILE_STATUS_READY) {
        return;
    }

    for (size_t i = 0; i < compiled->export_command_count; ++i) {
        const node_rule_exported_command_t *command = &compiled->export_commands[i];

        if (!bounded_text_present(command->id, sizeof(command->id))) {
            continue;
        }
        jw_append(w,
                  "%s{\"id\":\"%s\",\"label\":\"%s\",\"target\":\"bundle\",\"command\":\"%s\","
                  "\"args_schema_ref\":\"none\",\"bundle_export\":true,",
                  *first ? "" : ",",
                  safe_text(command->id),
                  safe_text(command->label),
                  safe_text(command->id));
        append_claims_array(w, command);
        jw_append(w, ",");
        append_policy(w, true, true, false, true);
        jw_append(w, "}");
        *first = false;
    }
}

static void append_command_templates(json_writer_t *w, const node_rule_compiled_bundle_t *compiled)
{
    bool first = true;

    jw_append(w, "\"command_templates\":[");
    append_bundle_command_templates(w, compiled, &first);

    jw_append(w, "%s{\"id\":\"relay.set\",\"label\":\"Relay set\",\"target\":\"relays\",\"command\":\"relay.set\",\"args_schema_ref\":\"output_set\",\"default_args\":{\"on\":true},", first ? "" : ",");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");
    first = false;

    jw_append(w, "{\"id\":\"relay.pulse\",\"label\":\"Relay pulse\",\"target\":\"relays\",\"command\":\"relay.pulse\",\"args_schema_ref\":\"pulse\",\"default_args\":{\"duration_ms\":300},");
    append_policy(w, true, true, false, true);
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
    if (!bounded_text_present(reader->known_cards[index].event_name,
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

    if (!w || !first || !config || !reader ||
        node_driver_nfc_reader_load_factory_config(config, reader) != ESP_OK) {
        free(reader);
        return;
    }

    jw_append(w,
              "%s{\"id\":\"%s_card_seen\",\"label\":\"%s card seen\",\"source\":\"drivers\","
              "\"event\":\"%s_card_seen\",\"args_schema_ref\":\"driver_local_event\"}",
              *first ? "" : ",",
              safe_text(reader->id),
              safe_text(reader->id),
              safe_text(reader->id));
    *first = false;

    jw_append(w,
              "%s{\"id\":\"%s_card_removed\",\"label\":\"%s card removed\",\"source\":\"drivers\","
              "\"event\":\"%s_card_removed\",\"args_schema_ref\":\"driver_local_event\"}",
              *first ? "" : ",",
              safe_text(reader->id),
              safe_text(reader->id),
              safe_text(reader->id));
    *first = false;

    for (size_t i = 0; i < reader->known_card_count; ++i) {
        const node_nfc_known_card_t *card = &reader->known_cards[i];

        if (known_card_event_duplicate(reader, i)) {
            continue;
        }
        jw_append(w,
                  "%s{\"id\":\"%s\",\"label\":\"",
                  *first ? "" : ",",
                  safe_text(card->event_name));
        if (bounded_text_present(card->name, sizeof(card->name))) {
            jw_append(w, "%s %s", safe_text(reader->id), safe_text(card->name));
        } else if (card->token_id != 0) {
            jw_append(w, "%s token %ld", safe_text(reader->id), (long)card->token_id);
        } else {
            jw_append(w, "%s", safe_text(card->event_name));
        }
        jw_append(w,
                  "\",\"source\":\"drivers\",\"event\":\"%s\","
                  "\"args_schema_ref\":\"driver_local_event\"}",
                  safe_text(card->event_name));
        *first = false;
    }

    free(reader);
}

static void append_bundle_event_templates(json_writer_t *w, const node_rule_compiled_bundle_t *compiled, bool *first)
{
    if (!w || !first || !compiled || compiled->status != NODE_RULE_COMPILE_STATUS_READY) {
        return;
    }

    for (size_t i = 0; i < compiled->export_event_count; ++i) {
        const node_rule_exported_event_t *event = &compiled->export_events[i];

        if (!bounded_text_present(event->id, sizeof(event->id))) {
            continue;
        }
        jw_append(w,
                  "%s{\"id\":\"%s\",\"label\":\"%s\",\"source\":\"bundle\","
                  "\"event\":\"%s\",\"args_schema_ref\":\"none\",\"bundle_export\":true}",
                  *first ? "" : ",",
                  safe_text(event->id),
                  safe_text(event->label),
                  safe_text(event->id));
        *first = false;
    }
}

static void append_event_templates(json_writer_t *w,
                                   const node_config_t *config,
                                   const node_rule_compiled_bundle_t *compiled)
{
    bool first = true;

    jw_append(w, "\"event_templates\":[");
    append_bundle_event_templates(w, compiled, &first);
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
        jw_append(w, "%s\"%s\"", first ? "" : ",", safe_text(desc->name));
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
              "\"options\":[\"set\",\"pulse\",\"blink\",\"fade\",\"fade_in\",\"fade_out\",\"breathe\"]},"
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

static void append_standalone_events(json_writer_t *w, const node_rule_compiled_bundle_t *compiled)
{
    bool first = true;

    jw_append(w, "[");
    if (compiled && compiled->status == NODE_RULE_COMPILE_STATUS_READY) {
        for (size_t i = 0; i < compiled->emit_count; ++i) {
            jw_append(w,
                      "%s\"%s\"",
                      first ? "" : ",",
                      safe_text(compiled->emit_names[i]));
            first = false;
        }
    }
    jw_append(w, "]");
}

static void append_exported_command_ids(json_writer_t *w, const node_rule_compiled_bundle_t *compiled)
{
    bool first = true;

    jw_append(w, "[");
    if (compiled && compiled->status == NODE_RULE_COMPILE_STATUS_READY) {
        for (size_t i = 0; i < compiled->export_command_count; ++i) {
            if (!bounded_text_present(compiled->export_commands[i].id, sizeof(compiled->export_commands[i].id))) {
                continue;
            }
            jw_append(w, "%s\"%s\"", first ? "" : ",", safe_text(compiled->export_commands[i].id));
            first = false;
        }
    }
    jw_append(w, "]");
}

static void append_exported_event_ids(json_writer_t *w, const node_rule_compiled_bundle_t *compiled)
{
    bool first = true;

    jw_append(w, "[");
    if (compiled && compiled->status == NODE_RULE_COMPILE_STATUS_READY) {
        for (size_t i = 0; i < compiled->export_event_count; ++i) {
            if (!bounded_text_present(compiled->export_events[i].id, sizeof(compiled->export_events[i].id))) {
                continue;
            }
            jw_append(w, "%s\"%s\"", first ? "" : ",", safe_text(compiled->export_events[i].id));
            first = false;
        }
    }
    jw_append(w, "]");
}

static void append_v2_metadata(json_writer_t *w, const node_config_t *config)
{
    node_operation_mode_t mode = node_runtime_mode_normalize((node_operation_mode_t)config->operation_mode);
    const node_rule_compiled_bundle_t *compiled = node_rule_compile_peek_active();
    node_rule_engine_status_t runtime = {0};
    node_fallback_runtime_status_t fallback = {0};
    const char *rules_status = "disabled";

    node_rule_engine_get_status(&runtime);
    node_fallback_runtime_get_status(&fallback);
    if (node_runtime_mode_rules_enabled(config)) {
        rules_status = node_rule_compile_status_name(compiled ? compiled->status : NODE_RULE_COMPILE_STATUS_INACTIVE);
    }

    jw_append(w,
              "\"v2\":{"
              "\"operation_mode\":\"%s\","
              "\"standalone_mqtt_enabled\":%s,"
              "\"rules_supported\":%s,"
              "\"active_bundle_id\":\"%s\","
              "\"rules_generation\":%lu,"
              "\"rules_status\":\"%s\","
              "\"rules_paused\":%s,"
              "\"rules_initialized\":%s,"
              "\"fallback_configured\":%s,"
              "\"fallback_enabled\":%s,"
              "\"fallback_state\":\"%s\","
              "\"fallback_wifi_ready\":%s,"
              "\"fallback_mqtt_connected\":%s,"
              "\"fallback_rules_active\":%s,"
              "\"fallback_timeout_ms\":%lu,"
              "\"fallback_return_delay_ms\":%lu,"
              "\"fallback_return_policy\":\"%s\","
              "\"exported_commands\":",
              node_runtime_mode_name(mode),
              config->standalone_mqtt_enabled ? "true" : "false",
              node_runtime_mode_rules_enabled(config) ? "true" : "false",
              (compiled && compiled->metadata.has_bundle) ? safe_text(compiled->metadata.bundle_id) : "",
              (unsigned long)((compiled && compiled->metadata.has_bundle) ? compiled->metadata.generation : 0U),
              rules_status,
              runtime.paused ? "true" : "false",
              runtime.initialized ? "true" : "false",
              (mode == NODE_OPERATION_MODE_FALLBACK && fallback.fallback_timeout_ms > 0U) ? "true" : "false",
              fallback.enabled ? "true" : "false",
              node_fallback_runtime_state_name(fallback.state),
              fallback.wifi_ready ? "true" : "false",
              fallback.mqtt_connected ? "true" : "false",
              fallback.fallback_rules_active ? "true" : "false",
              (unsigned long)fallback.fallback_timeout_ms,
              (unsigned long)fallback.fallback_return_delay_ms,
              node_fallback_runtime_return_policy_name(fallback.return_policy));
    append_exported_command_ids(w, compiled);
    jw_append(w, ",\"exported_events\":");
    append_exported_event_ids(w, compiled);
    jw_append(w, ",\"standalone_events\":");
    append_standalone_events(w, compiled);
    jw_append(w, ",\"compiled_rules\":%lu,\"compiled_actions\":%lu},",
              (unsigned long)(compiled ? compiled->rule_count : 0U),
              (unsigned long)(compiled ? compiled->total_action_count : 0U));
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
    const node_rule_compiled_bundle_t *compiled = node_rule_compile_peek_active();

    jw_append(&w,
              "{\"manifest_version\":2,\"format\":\"compact_resources\","
              "\"node_kind\":\"scenehub_node\","
              "\"capability_contract\":\"scenehub.node.compact.v1\","
              "\"device\":{\"id\":\"%s\",\"name\":\"%s\",\"kind\":\"scenehub_node\"},"
              "\"resources\":{",
              safe_text(config->node_id),
              safe_text(config->node_name));

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

    append_command_templates(&w, compiled);
    append_admin_command_templates(&w);
    append_event_templates(&w, config, compiled);
    append_v2_metadata(&w, config);
    append_schemas(&w);
    jw_append(&w, "}");

    if (w.failed) {
        out[0] = '\0';
        return ESP_ERR_NO_MEM;
    }
    if (out_written) {
        *out_written = w.len;
    }
    return ESP_OK;
}
