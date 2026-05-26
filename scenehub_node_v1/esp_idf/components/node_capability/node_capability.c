#include "node_capability.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

static void append_command_templates(json_writer_t *w)
{
    jw_append(w, "\"command_templates\":[");

    jw_append(w, "{\"id\":\"relay.set\",\"label\":\"Relay set\",\"target\":\"relays\",\"command\":\"relay.set\",\"args_schema_ref\":\"output_set\",\"default_args\":{\"on\":true},");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

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

static void append_event_templates(json_writer_t *w)
{
    jw_append(w,
              "\"event_templates\":["
              "{\"id\":\"input.changed\",\"label\":\"Input changed\",\"source\":\"inputs\","
              "\"event\":\"input.changed\",\"args_schema_ref\":\"input_event\"}"
              "],");
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
              "\"input_event\":["
              "{\"key\":\"channel\",\"type\":\"resource_channel\"},"
              "{\"key\":\"value\",\"type\":\"number\",\"optional\":true}"
              "],"
              "\"none\":["
              "]"
              "}");
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
    jw_append(&w, "},");

    append_command_templates(&w);
    append_event_templates(&w);
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
