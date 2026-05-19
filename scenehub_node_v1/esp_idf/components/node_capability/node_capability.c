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
                  "%s{\"channel\":%u,\"label\":\"%s\",\"active_low\":%s}",
                  first ? "" : ",",
                  (unsigned)pin->channel,
                  safe_text(pin->label),
                  pin->active_low ? "true" : "false");
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
                  "%s{\"channel\":%u,\"label\":\"%s\",\"active_low\":%s,\"event\":\"input.changed\"}",
                  first ? "" : ",",
                  (unsigned)pin->channel,
                  safe_text(pin->label),
                  pin->active_low ? "true" : "false");
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
                  "%s{\"channel\":%u,\"label\":\"%s\",\"active_low\":%s}",
                  first ? "" : ",",
                  (unsigned)pin->channel,
                  safe_text(pin->label),
                  pin->active_low ? "true" : "false");
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
                  "%s{\"strip\":%u,\"pixels\":%u,\"label\":\"%s\"}",
                  first ? "" : ",",
                  (unsigned)pin->channel,
                  (unsigned)pin->pixel_count,
                  safe_text(pin->label));
        first = false;
    }
    jw_append(w, "]");
}

static void append_command_templates(json_writer_t *w)
{
    jw_append(w, "\"command_templates\":[");

    jw_append(w, "{\"id\":\"relay.set\",\"label\":\"Relay set\",\"target\":\"relays\",\"command\":\"relay.set\",\"args_schema_ref\":\"output_set\",");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"relay.pulse\",\"label\":\"Relay pulse\",\"target\":\"relays\",\"command\":\"relay.pulse\",\"args_schema_ref\":\"pulse\",");
    append_policy(w, true, true, false, true);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"mosfet.set\",\"label\":\"MOSFET set\",\"target\":\"mosfets\",\"command\":\"mosfet.set\",\"args_schema_ref\":\"output_set\",");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"mosfet.effect\",\"label\":\"MOSFET effect\",\"target\":\"mosfets\",\"command\":\"mosfet.effect\",\"args_schema_ref\":\"effect\",");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"io.set\",\"label\":\"Output set\",\"target\":\"outputs\",\"command\":\"io.set\",\"args_schema_ref\":\"output_set\",");
    append_policy(w, true, true, false, false);
    jw_append(w, "},");

    jw_append(w, "{\"id\":\"led.effect\",\"label\":\"LED effect\",\"target\":\"led_strips\",\"command\":\"led.effect\",\"args_schema_ref\":\"led_effect\",");
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

static void append_schemas(json_writer_t *w)
{
    jw_append(w,
              "\"schemas\":{"
              "\"output_set\":["
              "{\"key\":\"channel\",\"label\":\"Channel\",\"type\":\"resource_channel\",\"optional\":false},"
              "{\"key\":\"on\",\"label\":\"On\",\"type\":\"checkbox\",\"optional\":false}"
              "],"
              "\"pulse\":["
              "{\"key\":\"channel\",\"label\":\"Channel\",\"type\":\"resource_channel\",\"optional\":false},"
              "{\"key\":\"duration_ms\",\"label\":\"Duration ms\",\"type\":\"number\",\"optional\":false}"
              "],"
              "\"effect\":["
              "{\"key\":\"channel\",\"label\":\"Channel\",\"type\":\"resource_channel\",\"optional\":false},"
              "{\"key\":\"effect\",\"label\":\"Effect\",\"type\":\"select\",\"optional\":false,"
              "\"options\":[\"set\",\"pulse\",\"blink\",\"fade_in\",\"fade_out\",\"breathe\"]},"
              "{\"key\":\"duration_ms\",\"label\":\"Duration ms\",\"type\":\"number\",\"optional\":true},"
              "{\"key\":\"repeat\",\"label\":\"Repeat\",\"type\":\"number\",\"optional\":true}"
              "],"
              "\"led_effect\":["
              "{\"key\":\"strip\",\"label\":\"Strip\",\"type\":\"resource_channel\",\"optional\":false},"
              "{\"key\":\"effect\",\"label\":\"Effect\",\"type\":\"select\",\"optional\":false,"
              "\"options\":[\"solid\",\"blink\",\"fade\",\"breathe\",\"rainbow\"]},"
              "{\"key\":\"duration_ms\",\"label\":\"Duration ms\",\"type\":\"number\",\"optional\":true},"
              "{\"key\":\"repeat\",\"label\":\"Repeat\",\"type\":\"number\",\"optional\":true}"
              "],"
              "\"input_event\":["
              "{\"key\":\"channel\",\"label\":\"Channel\",\"type\":\"resource_channel\",\"optional\":false},"
              "{\"key\":\"value\",\"label\":\"Value\",\"type\":\"number\",\"optional\":true}"
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
              "\"compatibility\":{\"flat_commands\":false},"
              "\"device\":{\"id\":\"%s\",\"name\":\"%s\",\"kind\":\"scenehub_node\",\"fw_version\":\"0.1.0\"},"
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
