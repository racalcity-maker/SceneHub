#include "node_rule_compile.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "node_json.h"
#include "node_rule_schema.h"
#include "node_rule_store.h"
#include "node_runtime_mode.h"
#include "sdkconfig.h"

typedef struct {
    node_rule_compiled_bundle_t *bundle;
    const node_config_t *config;
    const char *error_code;
} node_rule_compile_context_t;

static node_rule_compiled_bundle_t *s_active_bundle;

static void *alloc_rule_owner_buffer(size_t size)
{
    void *ptr = NULL;

    if (size == 0) {
        return NULL;
    }
#if CONFIG_SPIRAM
    ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, size);
        return ptr;
    }
#endif
    ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

static void clear_bundle(node_rule_compiled_bundle_t *bundle)
{
    if (!bundle) {
        return;
    }
    memset(bundle, 0, sizeof(*bundle));
    bundle->status = NODE_RULE_COMPILE_STATUS_INACTIVE;
}

static bool ensure_active_bundle_storage(void)
{
    if (s_active_bundle) {
        return true;
    }

    s_active_bundle = (node_rule_compiled_bundle_t *)alloc_rule_owner_buffer(sizeof(*s_active_bundle));
    if (!s_active_bundle) {
        return false;
    }
    clear_bundle(s_active_bundle);
    return true;
}

static void write_error_code(char *out_error_code, size_t out_error_code_size, const char *code)
{
    if (!out_error_code || out_error_code_size == 0) {
        return;
    }
    snprintf(out_error_code, out_error_code_size, "%s", code ? code : "");
}

static int logical_channel_from_name(const char *name, const char *prefix)
{
    size_t prefix_len = 0;
    char *end = NULL;
    long value = 0;

    if (!name || !prefix) {
        return -1;
    }
    prefix_len = strlen(prefix);
    if (strncmp(name, prefix, prefix_len) != 0) {
        return -1;
    }
    value = strtol(name + prefix_len, &end, 10);
    if (!end || *end != '\0' || value <= 0 || value > 255) {
        return -1;
    }
    return (int)value;
}

static bool normalize_name(const char *src, char *out, size_t out_size)
{
    size_t written = 0;
    bool last_underscore = false;

    if (!src || !out || out_size == 0) {
        return false;
    }

    for (; *src; ++src) {
        unsigned char ch = (unsigned char)*src;
        if (isalnum(ch)) {
            if (written + 1 >= out_size) {
                return false;
            }
            out[written++] = (char)tolower(ch);
            last_underscore = false;
            continue;
        }
        if (!last_underscore && written > 0) {
            if (written + 1 >= out_size) {
                return false;
            }
            out[written++] = '_';
            last_underscore = true;
        }
    }
    while (written > 0 && out[written - 1] == '_') {
        --written;
    }
    out[written] = '\0';
    return written > 0;
}

static bool label_matches_name(const char *label, const char *expected)
{
    char normalized[32];

    if (!label || !expected) {
        return false;
    }
    if (!normalize_name(label, normalized, sizeof(normalized))) {
        return false;
    }
    return strcmp(normalized, expected) == 0;
}

static uint16_t find_name_index(const char *items, size_t stride, size_t count, const char *name)
{
    if (!items || !name) {
        return UINT16_MAX;
    }
    for (size_t i = 0; i < count; ++i) {
        const char *slot = items + (i * stride);
        if (strcmp(slot, name) == 0) {
            return (uint16_t)i;
        }
    }
    return UINT16_MAX;
}

static uint16_t find_state_index(const node_rule_compiled_bundle_t *bundle, const char *name)
{
    return bundle ? find_name_index((const char *)bundle->state_keys,
                                    sizeof(bundle->state_keys[0]),
                                    bundle->state_key_count,
                                    name)
                  : UINT16_MAX;
}

static uint16_t find_phase_index(const node_rule_compiled_bundle_t *bundle, const char *name)
{
    return bundle ? find_name_index((const char *)bundle->phase_names,
                                    sizeof(bundle->phase_names[0]),
                                    bundle->phase_count,
                                    name)
                  : UINT16_MAX;
}

static uint16_t find_timer_index(const node_rule_compiled_bundle_t *bundle, const char *name)
{
    return bundle ? find_name_index((const char *)bundle->timer_names,
                                    sizeof(bundle->timer_names[0]),
                                    bundle->timer_count,
                                    name)
                  : UINT16_MAX;
}

static bool read_scalar_value(const cJSON *item, node_rule_scalar_value_t *out_value)
{
    if (!out_value) {
        return false;
    }
    out_value->type = NODE_RULE_SCALAR_TYPE_NONE;
    out_value->int_value = 0;

    if (cJSON_IsBool(item)) {
        out_value->type = NODE_RULE_SCALAR_TYPE_BOOL;
        out_value->int_value = cJSON_IsTrue(item) ? 1 : 0;
        return true;
    }
    if (cJSON_IsNumber(item)) {
        out_value->type = NODE_RULE_SCALAR_TYPE_INT32;
        out_value->int_value = (int32_t)item->valuedouble;
        return true;
    }
    return false;
}

static bool append_jsonf(char *buf, size_t cap, size_t *len, const char *fmt, ...)
{
    va_list args;
    int written = 0;

    if (!buf || !len || !fmt || *len >= cap) {
        return false;
    }

    va_start(args, fmt);
    written = vsnprintf(buf + *len, cap - *len, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= cap - *len) {
        return false;
    }
    *len += (size_t)written;
    return true;
}

static bool build_emit_args_json(const cJSON *args, char *out_json, size_t out_json_size)
{
    size_t len = 0;
    cJSON *item = NULL;
    bool first = true;
    char escaped_key[96];
    char escaped_value[96];

    if (!out_json || out_json_size == 0) {
        return false;
    }
    if (!args) {
        snprintf(out_json, out_json_size, "{}");
        return true;
    }
    if (!cJSON_IsObject(args)) {
        return false;
    }
    if (!append_jsonf(out_json, out_json_size, &len, "{")) {
        return false;
    }
    cJSON_ArrayForEach(item, (cJSON *)args) {
        if (!item->string || !node_json_escape_string(escaped_key, sizeof(escaped_key), item->string)) {
            return false;
        }
        if (cJSON_IsBool(item)) {
            if (!append_jsonf(out_json,
                              out_json_size,
                              &len,
                              "%s\"%s\":%s",
                              first ? "" : ",",
                              escaped_key,
                              cJSON_IsTrue(item) ? "true" : "false")) {
                return false;
            }
        } else if (cJSON_IsNumber(item)) {
            if (!append_jsonf(out_json,
                              out_json_size,
                              &len,
                              "%s\"%s\":%ld",
                              first ? "" : ",",
                              escaped_key,
                              (long)item->valuedouble)) {
                return false;
            }
        } else if (cJSON_IsString(item) && item->valuestring) {
            if (!node_json_escape_string(escaped_value, sizeof(escaped_value), item->valuestring) ||
                !append_jsonf(out_json,
                              out_json_size,
                              &len,
                              "%s\"%s\":\"%s\"",
                              first ? "" : ",",
                              escaped_key,
                              escaped_value)) {
                return false;
            }
        } else {
            return false;
        }
        first = false;
    }
    return append_jsonf(out_json, out_json_size, &len, "}");
}

static bool write_payload_json(char *out_json, size_t out_json_size, const char *fmt, ...)
{
    va_list args;
    int written = 0;

    if (!out_json || out_json_size == 0 || !fmt) {
        return false;
    }
    va_start(args, fmt);
    written = vsnprintf(out_json, out_json_size, fmt, args);
    va_end(args);
    return written >= 0 && (size_t)written < out_json_size;
}

static bool resolve_output_channel(const node_config_t *config,
                                   const char *command,
                                   const char *output_name,
                                   uint8_t *out_channel)
{
    int channel = -1;

    if (!command || !output_name || !out_channel) {
        return false;
    }

    if (strncmp(command, "relay.", 6) == 0) {
        channel = logical_channel_from_name(output_name, "relay_");
        if (channel <= 0) {
            return false;
        }
        if (!config) {
            *out_channel = (uint8_t)channel;
            return true;
        }
        for (size_t i = 0; i < NODE_RELAY_MAX; ++i) {
            if (config->relays[i].enabled &&
                ((config->relays[i].channel == (uint8_t)channel && channel > 0) ||
                 label_matches_name(config->relays[i].label, output_name))) {
                *out_channel = config->relays[i].channel;
                return true;
            }
        }
        return false;
    }
    if (strncmp(command, "mosfet.", 7) == 0) {
        channel = logical_channel_from_name(output_name, "mosfet_");
        if (channel <= 0) {
            return false;
        }
        if (!config) {
            *out_channel = (uint8_t)channel;
            return true;
        }
        for (size_t i = 0; i < NODE_MOSFET_MAX; ++i) {
            if (config->mosfets[i].enabled &&
                ((config->mosfets[i].channel == (uint8_t)channel && channel > 0) ||
                 label_matches_name(config->mosfets[i].label, output_name))) {
                *out_channel = config->mosfets[i].channel;
                return true;
            }
        }
        return false;
    }
    if (strncmp(command, "io.", 3) == 0) {
        channel = logical_channel_from_name(output_name, "output_");
        if (channel <= 0) {
            channel = logical_channel_from_name(output_name, "io_");
        }
        if (channel <= 0) {
            return false;
        }
        if (!config) {
            *out_channel = (uint8_t)channel;
            return true;
        }
        for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
            if (config->universal_io[i].enabled &&
                config->universal_io[i].role == NODE_PIN_UNIVERSAL_OUTPUT &&
                ((config->universal_io[i].channel == (uint8_t)channel && channel > 0) ||
                 label_matches_name(config->universal_io[i].label, output_name))) {
                *out_channel = config->universal_io[i].channel;
                return true;
            }
        }
        return false;
    }
    if (strncmp(command, "led.", 4) == 0) {
        channel = logical_channel_from_name(output_name, "strip_");
        if (channel <= 0) {
            channel = logical_channel_from_name(output_name, "led_strip_");
        }
        if (channel <= 0) {
            return false;
        }
        if (!config) {
            *out_channel = (uint8_t)channel;
            return true;
        }
        for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
            if (config->led_strips[i].enabled &&
                ((config->led_strips[i].channel == (uint8_t)channel && channel > 0) ||
                 label_matches_name(config->led_strips[i].label, output_name))) {
                *out_channel = config->led_strips[i].channel;
                return true;
            }
        }
        return false;
    }

    return false;
}

static bool resolve_input_channel(const node_config_t *config,
                                  const char *input_name,
                                  uint8_t *out_channel)
{
    int channel = 0;

    if (!input_name || !out_channel) {
        return false;
    }

    channel = logical_channel_from_name(input_name, "input_");
    if (!config) {
        if (channel <= 0) {
            return false;
        }
        *out_channel = (uint8_t)channel;
        return true;
    }

    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        if (!config->universal_io[i].enabled ||
            config->universal_io[i].role != NODE_PIN_UNIVERSAL_INPUT) {
            continue;
        }
        if ((channel > 0 && config->universal_io[i].channel == (uint8_t)channel) ||
            label_matches_name(config->universal_io[i].label, input_name)) {
            *out_channel = config->universal_io[i].channel;
            return true;
        }
    }
    return false;
}

static bool build_command_args_json(const node_config_t *config,
                                    const cJSON *action,
                                    char *out_json,
                                    size_t out_json_size)
{
    const cJSON *command = cJSON_GetObjectItemCaseSensitive(action, "command");
    const cJSON *args = cJSON_GetObjectItemCaseSensitive(action, "args");
    const cJSON *output = NULL;
    uint8_t channel = 0;

    if (!cJSON_IsString(command) || !command->valuestring || !out_json || out_json_size == 0) {
        return false;
    }

    if (strcmp(command->valuestring, "node.all_off") == 0 ||
        strcmp(command->valuestring, "relay.all_off") == 0 ||
        strcmp(command->valuestring, "mosfet.all_off") == 0 ||
        strcmp(command->valuestring, "io.all_off") == 0) {
        return write_payload_json(out_json, out_json_size, "{}");
    }
    if (!cJSON_IsObject(args)) {
        return false;
    }

    output = cJSON_GetObjectItemCaseSensitive(args, "output");
    if (!cJSON_IsString(output) || !resolve_output_channel(config, command->valuestring, output->valuestring, &channel)) {
        return false;
    }

    if (strcmp(command->valuestring, "relay.set") == 0 || strcmp(command->valuestring, "io.set") == 0) {
        const cJSON *on = cJSON_GetObjectItemCaseSensitive(args, "on");
        return cJSON_IsBool(on) &&
               write_payload_json(out_json, out_json_size, "{\"channel\":%u,\"on\":%s}",
                                  (unsigned)channel,
                                  cJSON_IsTrue(on) ? "true" : "false");
    }
    if (strcmp(command->valuestring, "relay.pulse") == 0) {
        const cJSON *duration_ms = cJSON_GetObjectItemCaseSensitive(args, "duration_ms");
        return cJSON_IsNumber(duration_ms) &&
               write_payload_json(out_json, out_json_size, "{\"channel\":%u,\"duration_ms\":%lu}",
                                  (unsigned)channel,
                                  (unsigned long)duration_ms->valuedouble);
    }
    if (strcmp(command->valuestring, "mosfet.set") == 0) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(args, "value");
        return cJSON_IsNumber(value) &&
               write_payload_json(out_json, out_json_size, "{\"channel\":%u,\"value\":%ld}",
                                  (unsigned)channel,
                                  (long)value->valuedouble);
    }
    if (strcmp(command->valuestring, "mosfet.fade") == 0) {
        const cJSON *target = cJSON_GetObjectItemCaseSensitive(args, "target");
        const cJSON *duration_ms = cJSON_GetObjectItemCaseSensitive(args, "duration_ms");
        if (cJSON_IsNumber(target) && cJSON_IsNumber(duration_ms)) {
            return write_payload_json(out_json, out_json_size, "{\"channel\":%u,\"target\":%ld,\"duration_ms\":%lu}",
                                      (unsigned)channel,
                                      (long)target->valuedouble,
                                      (unsigned long)duration_ms->valuedouble);
        }
        if (cJSON_IsNumber(target)) {
            return write_payload_json(out_json, out_json_size, "{\"channel\":%u,\"target\":%ld}",
                                      (unsigned)channel,
                                      (long)target->valuedouble);
        }
        return false;
    }
    if (strcmp(command->valuestring, "mosfet.pulse") == 0) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(args, "value");
        const cJSON *duration_ms = cJSON_GetObjectItemCaseSensitive(args, "duration_ms");
        if (cJSON_IsNumber(value) && cJSON_IsNumber(duration_ms)) {
            return write_payload_json(out_json, out_json_size, "{\"channel\":%u,\"value\":%ld,\"duration_ms\":%lu}",
                                      (unsigned)channel,
                                      (long)value->valuedouble,
                                      (unsigned long)duration_ms->valuedouble);
        }
        if (cJSON_IsNumber(value)) {
            return write_payload_json(out_json, out_json_size, "{\"channel\":%u,\"value\":%ld}",
                                      (unsigned)channel,
                                      (long)value->valuedouble);
        }
        return false;
    }
    if (strcmp(command->valuestring, "mosfet.blink") == 0) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(args, "value");
        const cJSON *final_value = cJSON_GetObjectItemCaseSensitive(args, "final_value");
        const cJSON *on_ms = cJSON_GetObjectItemCaseSensitive(args, "on_ms");
        const cJSON *off_ms = cJSON_GetObjectItemCaseSensitive(args, "off_ms");
        const cJSON *count = cJSON_GetObjectItemCaseSensitive(args, "count");
        size_t len = 0;

        if (!append_jsonf(out_json, out_json_size, &len, "{\"channel\":%u", (unsigned)channel)) {
            return false;
        }
        if (cJSON_IsNumber(value) && !append_jsonf(out_json, out_json_size, &len, ",\"value\":%ld", (long)value->valuedouble)) {
            return false;
        }
        if (cJSON_IsNumber(final_value) &&
            !append_jsonf(out_json, out_json_size, &len, ",\"final_value\":%ld", (long)final_value->valuedouble)) {
            return false;
        }
        if (cJSON_IsNumber(on_ms) &&
            !append_jsonf(out_json, out_json_size, &len, ",\"on_ms\":%lu", (unsigned long)on_ms->valuedouble)) {
            return false;
        }
        if (cJSON_IsNumber(off_ms) &&
            !append_jsonf(out_json, out_json_size, &len, ",\"off_ms\":%lu", (unsigned long)off_ms->valuedouble)) {
            return false;
        }
        if (cJSON_IsNumber(count) &&
            !append_jsonf(out_json, out_json_size, &len, ",\"count\":%ld", (long)count->valuedouble)) {
            return false;
        }
        return append_jsonf(out_json, out_json_size, &len, "}");
    }
    if (strcmp(command->valuestring, "mosfet.breathe") == 0) {
        const cJSON *min = cJSON_GetObjectItemCaseSensitive(args, "min");
        const cJSON *max = cJSON_GetObjectItemCaseSensitive(args, "max");
        const cJSON *final_value = cJSON_GetObjectItemCaseSensitive(args, "final_value");
        const cJSON *fade_ms = cJSON_GetObjectItemCaseSensitive(args, "fade_ms");
        const cJSON *hold_ms = cJSON_GetObjectItemCaseSensitive(args, "hold_ms");
        const cJSON *count = cJSON_GetObjectItemCaseSensitive(args, "count");
        size_t len = 0;

        if (!append_jsonf(out_json, out_json_size, &len, "{\"channel\":%u", (unsigned)channel)) {
            return false;
        }
        if (cJSON_IsNumber(min) && !append_jsonf(out_json, out_json_size, &len, ",\"min\":%ld", (long)min->valuedouble)) {
            return false;
        }
        if (cJSON_IsNumber(max) && !append_jsonf(out_json, out_json_size, &len, ",\"max\":%ld", (long)max->valuedouble)) {
            return false;
        }
        if (cJSON_IsNumber(final_value) &&
            !append_jsonf(out_json, out_json_size, &len, ",\"final_value\":%ld", (long)final_value->valuedouble)) {
            return false;
        }
        if (cJSON_IsNumber(fade_ms) &&
            !append_jsonf(out_json, out_json_size, &len, ",\"fade_ms\":%lu", (unsigned long)fade_ms->valuedouble)) {
            return false;
        }
        if (cJSON_IsNumber(hold_ms) &&
            !append_jsonf(out_json, out_json_size, &len, ",\"hold_ms\":%lu", (unsigned long)hold_ms->valuedouble)) {
            return false;
        }
        if (cJSON_IsNumber(count) &&
            !append_jsonf(out_json, out_json_size, &len, ",\"count\":%ld", (long)count->valuedouble)) {
            return false;
        }
        return append_jsonf(out_json, out_json_size, &len, "}");
    }
    if (strcmp(command->valuestring, "mosfet.effect") == 0) {
        const cJSON *effect = cJSON_GetObjectItemCaseSensitive(args, "effect");
        size_t len = 0;

        if (!cJSON_IsString(effect)) {
            return false;
        }
        if (!append_jsonf(out_json,
                          out_json_size,
                          &len,
                          "{\"channel\":%u,\"effect\":\"%s\"",
                          (unsigned)channel,
                          effect->valuestring)) {
            return false;
        }
        {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(args, "value");
            const cJSON *target = cJSON_GetObjectItemCaseSensitive(args, "target");
            const cJSON *min = cJSON_GetObjectItemCaseSensitive(args, "min");
            const cJSON *max = cJSON_GetObjectItemCaseSensitive(args, "max");
            const cJSON *final_value = cJSON_GetObjectItemCaseSensitive(args, "final_value");
            const cJSON *duration_ms = cJSON_GetObjectItemCaseSensitive(args, "duration_ms");
            const cJSON *repeat = cJSON_GetObjectItemCaseSensitive(args, "repeat");
            const cJSON *on = cJSON_GetObjectItemCaseSensitive(args, "on");
            if (cJSON_IsNumber(value) && !append_jsonf(out_json, out_json_size, &len, ",\"value\":%ld", (long)value->valuedouble)) {
                return false;
            }
            if (cJSON_IsNumber(target) && !append_jsonf(out_json, out_json_size, &len, ",\"target\":%ld", (long)target->valuedouble)) {
                return false;
            }
            if (cJSON_IsNumber(min) && !append_jsonf(out_json, out_json_size, &len, ",\"min\":%ld", (long)min->valuedouble)) {
                return false;
            }
            if (cJSON_IsNumber(max) && !append_jsonf(out_json, out_json_size, &len, ",\"max\":%ld", (long)max->valuedouble)) {
                return false;
            }
            if (cJSON_IsNumber(final_value) &&
                !append_jsonf(out_json, out_json_size, &len, ",\"final_value\":%ld", (long)final_value->valuedouble)) {
                return false;
            }
            if (cJSON_IsNumber(duration_ms) &&
                !append_jsonf(out_json, out_json_size, &len, ",\"duration_ms\":%lu", (unsigned long)duration_ms->valuedouble)) {
                return false;
            }
            if (cJSON_IsNumber(repeat) &&
                !append_jsonf(out_json, out_json_size, &len, ",\"repeat\":%lu", (unsigned long)repeat->valuedouble)) {
                return false;
            }
            if (cJSON_IsBool(on) &&
                !append_jsonf(out_json, out_json_size, &len, ",\"on\":%s", cJSON_IsTrue(on) ? "true" : "false")) {
                return false;
            }
        }
        return append_jsonf(out_json, out_json_size, &len, "}");
    }
    if (strcmp(command->valuestring, "led.off") == 0) {
        return write_payload_json(out_json, out_json_size, "{\"channel\":%u}", (unsigned)channel);
    }
    if (strcmp(command->valuestring, "led.solid") == 0) {
        const cJSON *color = cJSON_GetObjectItemCaseSensitive(args, "color");
        return cJSON_IsString(color) &&
               write_payload_json(out_json, out_json_size, "{\"channel\":%u,\"color\":\"%s\"}",
                                  (unsigned)channel,
                                  color->valuestring);
    }
    if (strcmp(command->valuestring, "led.blink") == 0) {
        const cJSON *color = cJSON_GetObjectItemCaseSensitive(args, "color");
        const cJSON *times = cJSON_GetObjectItemCaseSensitive(args, "times");
        if (!cJSON_IsString(color)) {
            return false;
        }
        if (cJSON_IsNumber(times)) {
            return write_payload_json(out_json, out_json_size, "{\"channel\":%u,\"color\":\"%s\",\"times\":%ld}",
                                      (unsigned)channel,
                                      color->valuestring,
                                      (long)times->valuedouble);
        }
        return write_payload_json(out_json, out_json_size, "{\"channel\":%u,\"color\":\"%s\"}",
                                  (unsigned)channel,
                                  color->valuestring);
    }
    if (strcmp(command->valuestring, "led.breathe") == 0) {
        const cJSON *color = cJSON_GetObjectItemCaseSensitive(args, "color");
        return cJSON_IsString(color) &&
               write_payload_json(out_json, out_json_size, "{\"channel\":%u,\"color\":\"%s\"}",
                                  (unsigned)channel,
                                  color->valuestring);
    }
    if (strcmp(command->valuestring, "led.effect") == 0) {
        const cJSON *effect = cJSON_GetObjectItemCaseSensitive(args, "effect");
        return cJSON_IsString(effect) &&
               write_payload_json(out_json, out_json_size, "{\"channel\":%u,\"effect\":\"%s\"}",
                                  (unsigned)channel,
                                  effect->valuestring);
    }

    return false;
}

static esp_err_t compile_error_to_esp_err(const char *code)
{
    if (!code || code[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strncmp(code, "too_many_", 9) == 0 ||
        strcmp(code, "action_nesting_too_deep") == 0 ||
        strcmp(code, "condition_nesting_too_deep") == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_ERR_INVALID_ARG;
}

static bool nonempty_text(const char *text, size_t max_len)
{
    size_t len = 0;

    if (!text) {
        return false;
    }
    len = strnlen(text, max_len + 1);
    return len > 0 && len <= max_len;
}

static bool name_exists(const char *items, size_t stride, size_t count, const char *name)
{
    if (!items || !name) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const char *slot = items + (i * stride);
        if (strcmp(slot, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool append_unique_name(char *items,
                               size_t stride,
                               size_t *count,
                               size_t cap,
                               const char *name,
                               size_t max_len,
                               const char *overflow_error,
                               node_rule_compile_context_t *ctx)
{
    char *slot = NULL;

    if (!items || !count || !name || !ctx || !ctx->bundle) {
        if (ctx) {
            ctx->error_code = "compile_failed";
        }
        return false;
    }
    if (!nonempty_text(name, max_len)) {
        ctx->error_code = "invalid_bundle_shape";
        return false;
    }
    if (name_exists(items, stride, *count, name)) {
        return true;
    }
    if (*count >= cap) {
        ctx->error_code = overflow_error;
        return false;
    }
    slot = items + (*count * stride);
    snprintf(slot, stride, "%s", name);
    ++(*count);
    return true;
}

static node_rule_compiled_trigger_kind_t trigger_kind_from_name(const char *kind)
{
    if (!kind) {
        return NODE_RULE_TRIGGER_NONE;
    }
    if (strcmp(kind, "boot") == 0) {
        return NODE_RULE_TRIGGER_BOOT;
    }
    if (strcmp(kind, "input_edge") == 0) {
        return NODE_RULE_TRIGGER_INPUT_EDGE;
    }
    if (strcmp(kind, "input_level") == 0) {
        return NODE_RULE_TRIGGER_INPUT_LEVEL;
    }
    if (strcmp(kind, "input_hold") == 0) {
        return NODE_RULE_TRIGGER_INPUT_HOLD;
    }
    if (strcmp(kind, "all_inputs_level") == 0) {
        return NODE_RULE_TRIGGER_ALL_INPUTS_LEVEL;
    }
    if (strcmp(kind, "timer") == 0) {
        return NODE_RULE_TRIGGER_TIMER;
    }
    if (strcmp(kind, "local_event") == 0) {
        return NODE_RULE_TRIGGER_LOCAL_EVENT;
    }
    if (strcmp(kind, "state_changed") == 0) {
        return NODE_RULE_TRIGGER_STATE_CHANGED;
    }
    if (strcmp(kind, "mqtt_command") == 0) {
        return NODE_RULE_TRIGGER_MQTT_COMMAND;
    }
    return NODE_RULE_TRIGGER_NONE;
}

static bool append_state_key(node_rule_compile_context_t *ctx, const char *name)
{
    return append_unique_name((char *)ctx->bundle->state_keys,
                              sizeof(ctx->bundle->state_keys[0]),
                              &ctx->bundle->state_key_count,
                              NODE_RULE_MAX_STATE_KEYS,
                              name,
                              NODE_RULE_STATE_KEY_MAX_LEN,
                              "too_many_state_keys",
                              ctx);
}

static bool append_timer_name(node_rule_compile_context_t *ctx, const char *name)
{
    return append_unique_name((char *)ctx->bundle->timer_names,
                              sizeof(ctx->bundle->timer_names[0]),
                              &ctx->bundle->timer_count,
                              NODE_RULE_MAX_TIMERS,
                              name,
                              NODE_RULE_TIMER_NAME_MAX_LEN,
                              "too_many_timers",
                              ctx);
}

static bool append_phase_name(node_rule_compile_context_t *ctx, const char *name)
{
    return append_unique_name((char *)ctx->bundle->phase_names,
                              sizeof(ctx->bundle->phase_names[0]),
                              &ctx->bundle->phase_count,
                              NODE_RULE_MAX_PHASES,
                              name,
                              NODE_RULE_PHASE_NAME_MAX_LEN,
                              "too_many_phases",
                              ctx);
}

static bool append_emit_name(node_rule_compile_context_t *ctx, const char *name)
{
    return append_unique_name((char *)ctx->bundle->emit_names,
                              sizeof(ctx->bundle->emit_names[0]),
                              &ctx->bundle->emit_count,
                              NODE_RULE_MAX_EMIT_EVENTS,
                              name,
                              NODE_DRIVER_EVENT_NAME_MAX_LEN - 1,
                              "too_many_emit_events",
                              ctx);
}

static bool collect_exports(node_rule_compile_context_t *ctx, const cJSON *exports)
{
    const cJSON *commands = NULL;
    const cJSON *events = NULL;
    cJSON *item = NULL;

    if (!ctx || !ctx->bundle) {
        if (ctx) {
            ctx->error_code = "compile_failed";
        }
        return false;
    }
    if (!exports) {
        return true;
    }
    if (!cJSON_IsObject(exports)) {
        ctx->error_code = "invalid_bundle_shape";
        return false;
    }

    commands = cJSON_GetObjectItemCaseSensitive(exports, "commands");
    if (commands) {
        if (!cJSON_IsArray(commands)) {
            ctx->error_code = "invalid_bundle_shape";
            return false;
        }
        cJSON_ArrayForEach(item, (cJSON *)commands) {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
            const cJSON *label = cJSON_GetObjectItemCaseSensitive(item, "label");
            const cJSON *claims = cJSON_GetObjectItemCaseSensitive(item, "claims");
            cJSON *claim = NULL;
            node_rule_exported_command_t *out = NULL;

            if (!cJSON_IsString(id) ||
                !cJSON_IsString(label) ||
                ctx->bundle->export_command_count >= NODE_RULE_MAX_EXPORT_COMMANDS) {
                ctx->error_code = "invalid_bundle_shape";
                return false;
            }
            out = &ctx->bundle->export_commands[ctx->bundle->export_command_count++];
            snprintf(out->id, sizeof(out->id), "%s", id->valuestring);
            snprintf(out->label, sizeof(out->label), "%s", label->valuestring);

            if (claims) {
                if (!cJSON_IsArray(claims)) {
                    ctx->error_code = "invalid_bundle_shape";
                    return false;
                }
                cJSON_ArrayForEach(claim, (cJSON *)claims) {
                    if (!cJSON_IsString(claim) || out->claim_count >= NODE_RULE_MAX_EXPORT_CLAIMS) {
                        ctx->error_code = "invalid_bundle_shape";
                        return false;
                    }
                    snprintf(out->claims[out->claim_count],
                             sizeof(out->claims[out->claim_count]),
                             "%s",
                             claim->valuestring);
                    ++out->claim_count;
                }
            }
        }
    }

    events = cJSON_GetObjectItemCaseSensitive(exports, "events");
    if (events) {
        if (!cJSON_IsArray(events)) {
            ctx->error_code = "invalid_bundle_shape";
            return false;
        }
        cJSON_ArrayForEach(item, (cJSON *)events) {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
            const cJSON *label = cJSON_GetObjectItemCaseSensitive(item, "label");
            node_rule_exported_event_t *out = NULL;

            if (!cJSON_IsString(id) ||
                !cJSON_IsString(label) ||
                ctx->bundle->export_event_count >= NODE_RULE_MAX_EXPORT_EVENTS) {
                ctx->error_code = "invalid_bundle_shape";
                return false;
            }
            out = &ctx->bundle->export_events[ctx->bundle->export_event_count++];
            snprintf(out->id, sizeof(out->id), "%s", id->valuestring);
            snprintf(out->label, sizeof(out->label), "%s", label->valuestring);
        }
    }
    return true;
}

static node_rule_compiled_timer_mode_t timer_mode_from_name(const char *name)
{
    if (!name || strcmp(name, "oneshot") == 0) {
        return NODE_RULE_TIMER_MODE_ONESHOT;
    }
    if (strcmp(name, "repeat") == 0) {
        return NODE_RULE_TIMER_MODE_REPEAT;
    }
    if (strcmp(name, "cooldown") == 0) {
        return NODE_RULE_TIMER_MODE_COOLDOWN;
    }
    return NODE_RULE_TIMER_MODE_ONESHOT;
}

static bool read_timer_name(const cJSON *item, const cJSON *fallback, const char **out_name)
{
    const cJSON *selected = cJSON_IsString(item) ? item : fallback;

    if (out_name) {
        *out_name = NULL;
    }
    if (!out_name || !cJSON_IsString(selected) || !selected->valuestring) {
        return false;
    }
    *out_name = selected->valuestring;
    return true;
}

static bool read_timer_delay_fields(const cJSON *action,
                                    uint32_t *out_duration_ms,
                                    uint32_t *out_interval_ms)
{
    const cJSON *duration_ms = NULL;
    const cJSON *interval_ms = NULL;

    if (out_duration_ms) {
        *out_duration_ms = 0;
    }
    if (out_interval_ms) {
        *out_interval_ms = 0;
    }
    if (!cJSON_IsObject(action) || !out_duration_ms || !out_interval_ms) {
        return false;
    }

    duration_ms = cJSON_GetObjectItemCaseSensitive(action, "duration_ms");
    interval_ms = cJSON_GetObjectItemCaseSensitive(action, "interval_ms");
    if (cJSON_IsNumber(duration_ms) && duration_ms->valuedouble > 0) {
        *out_duration_ms = (uint32_t)duration_ms->valuedouble;
    }
    if (cJSON_IsNumber(interval_ms) && interval_ms->valuedouble > 0) {
        *out_interval_ms = (uint32_t)interval_ms->valuedouble;
    }
    return *out_duration_ms > 0 || *out_interval_ms > 0;
}

static bool compile_condition(node_rule_compile_context_t *ctx,
                              const cJSON *condition,
                              uint16_t *out_condition_index);
static bool compile_condition_children(node_rule_compile_context_t *ctx,
                                       const cJSON *conditions,
                                       uint16_t *out_first_child_index,
                                       uint8_t *out_child_count);
static bool compile_action(node_rule_compile_context_t *ctx, const cJSON *action);
static bool collect_condition(node_rule_compile_context_t *ctx, const cJSON *condition, unsigned depth);
static bool collect_actions_array(node_rule_compile_context_t *ctx,
                                  const cJSON *actions,
                                  unsigned depth,
                                  size_t *out_action_count);

static bool compile_condition_children(node_rule_compile_context_t *ctx,
                                       const cJSON *conditions,
                                       uint16_t *out_first_child_index,
                                       uint8_t *out_child_count)
{
    cJSON *item = NULL;
    uint16_t first_child_index = UINT16_MAX;
    uint16_t prev_child_index = UINT16_MAX;
    uint8_t child_count = 0;

    if (out_first_child_index) {
        *out_first_child_index = UINT16_MAX;
    }
    if (out_child_count) {
        *out_child_count = 0;
    }
    if (!ctx || !ctx->bundle || !cJSON_IsArray(conditions) || !out_first_child_index || !out_child_count) {
        if (ctx) {
            ctx->error_code = "invalid_rule";
        }
        return false;
    }

    cJSON_ArrayForEach(item, (cJSON *)conditions) {
        uint16_t child_index = UINT16_MAX;

        if (!compile_condition(ctx, item, &child_index)) {
            return false;
        }
        if (first_child_index == UINT16_MAX) {
            first_child_index = child_index;
        }
        if (prev_child_index != UINT16_MAX && prev_child_index < ctx->bundle->condition_count) {
            ctx->bundle->conditions[prev_child_index].next_sibling_index = child_index;
        }
        prev_child_index = child_index;
        ++child_count;
    }

    *out_first_child_index = first_child_index;
    *out_child_count = child_count;
    return true;
}

static bool compile_condition(node_rule_compile_context_t *ctx,
                              const cJSON *condition,
                              uint16_t *out_condition_index)
{
    const cJSON *kind = NULL;
    node_rule_compiled_condition_t *out = NULL;
    uint16_t parent_index = UINT16_MAX;

    if (out_condition_index) {
        *out_condition_index = UINT16_MAX;
    }
    if (!ctx || !ctx->bundle || !condition || !out_condition_index || !cJSON_IsObject(condition)) {
        if (ctx) {
            ctx->error_code = "invalid_rule";
        }
        return false;
    }
    if (ctx->bundle->condition_count >= NODE_RULE_MAX_ACTIONS_TOTAL) {
        ctx->error_code = "too_many_conditions";
        return false;
    }

    kind = cJSON_GetObjectItemCaseSensitive(condition, "kind");
    if (!cJSON_IsString(kind) || !kind->valuestring) {
        ctx->error_code = "invalid_rule";
        return false;
    }

    parent_index = (uint16_t)ctx->bundle->condition_count;
    out = &ctx->bundle->conditions[parent_index];
    memset(out, 0, sizeof(*out));
    out->state_index = UINT16_MAX;
    out->phase_index = UINT16_MAX;
    out->first_child_index = UINT16_MAX;
    out->next_sibling_index = UINT16_MAX;

    if (strcmp(kind->valuestring, "state_equals") == 0) {
        const cJSON *key = cJSON_GetObjectItemCaseSensitive(condition, "key");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(condition, "value");
        if (!cJSON_IsString(key) || !read_scalar_value(value, &out->value)) {
            ctx->error_code = "unsupported_state_type";
            return false;
        }
        if (!append_state_key(ctx, key->valuestring)) {
            return false;
        }
        out->state_index = find_state_index(ctx->bundle, key->valuestring);
        if (out->state_index == UINT16_MAX) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        out->kind = NODE_RULE_CONDITION_STATE_EQUALS;
    } else if (strcmp(kind->valuestring, "phase_is") == 0) {
        const cJSON *phase = cJSON_GetObjectItemCaseSensitive(condition, "phase");
        if (!cJSON_IsString(phase)) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        if (!append_phase_name(ctx, phase->valuestring)) {
            return false;
        }
        out->phase_index = find_phase_index(ctx->bundle, phase->valuestring);
        if (out->phase_index == UINT16_MAX) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        out->kind = NODE_RULE_CONDITION_PHASE_IS;
    } else if (strcmp(kind->valuestring, "input_equals") == 0) {
        const cJSON *input = cJSON_GetObjectItemCaseSensitive(condition, "input");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(condition, "value");

        if (!cJSON_IsString(input) || !cJSON_IsNumber(value)) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        if (!resolve_input_channel(ctx->config, input->valuestring, &out->input_channel)) {
            ctx->error_code = "runtime_condition_not_supported";
            return false;
        }
        out->value.type = NODE_RULE_SCALAR_TYPE_INT32;
        out->value.int_value = (int32_t)value->valuedouble;
        out->kind = NODE_RULE_CONDITION_INPUT_EQUALS;
    } else if (strcmp(kind->valuestring, "event_field_equals") == 0) {
        const cJSON *field = cJSON_GetObjectItemCaseSensitive(condition, "field");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(condition, "value");

        if (!cJSON_IsString(field) || !cJSON_IsNumber(value) || strcmp(field->valuestring, "token_id") != 0) {
            ctx->error_code = "runtime_condition_not_supported";
            return false;
        }
        out->event_field = NODE_RULE_EVENT_FIELD_TOKEN_ID;
        out->value.type = NODE_RULE_SCALAR_TYPE_INT32;
        out->value.int_value = (int32_t)value->valuedouble;
        out->kind = NODE_RULE_CONDITION_EVENT_FIELD_EQUALS;
    } else if (strcmp(kind->valuestring, "all_inputs_equal") == 0) {
        const cJSON *inputs = cJSON_GetObjectItemCaseSensitive(condition, "inputs");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(condition, "value");
        cJSON *item = NULL;

        if (!cJSON_IsArray(inputs) || !cJSON_IsNumber(value)) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        cJSON_ArrayForEach(item, (cJSON *)inputs) {
            uint8_t input_channel = 0;

            if (out->input_count >= NODE_RULE_MAX_GROUP_INPUTS ||
                !cJSON_IsString(item) ||
                !resolve_input_channel(ctx->config, item->valuestring, &input_channel)) {
                ctx->error_code = "runtime_condition_not_supported";
                return false;
            }
            out->input_channels[out->input_count++] = input_channel;
        }
        out->value.type = NODE_RULE_SCALAR_TYPE_INT32;
        out->value.int_value = (int32_t)value->valuedouble;
        out->kind = NODE_RULE_CONDITION_ALL_INPUTS_EQUAL;
    } else if (strcmp(kind->valuestring, "not") == 0) {
        const cJSON *inner = cJSON_GetObjectItemCaseSensitive(condition, "condition");

        if (!cJSON_IsObject(inner)) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        ++ctx->bundle->condition_count;
        if (!compile_condition(ctx, inner, &out->first_child_index)) {
            return false;
        }
        out->child_count = 1;
        out->kind = NODE_RULE_CONDITION_NOT;
        *out_condition_index = parent_index;
        return true;
    } else if (strcmp(kind->valuestring, "all") == 0 || strcmp(kind->valuestring, "any") == 0) {
        const cJSON *conditions = cJSON_GetObjectItemCaseSensitive(condition, "conditions");
        bool is_all = strcmp(kind->valuestring, "all") == 0;

        if (!cJSON_IsArray(conditions)) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        ++ctx->bundle->condition_count;
        if (!compile_condition_children(ctx, conditions, &out->first_child_index, &out->child_count)) {
            return false;
        }
        out->kind = is_all ? NODE_RULE_CONDITION_ALL : NODE_RULE_CONDITION_ANY;
        *out_condition_index = parent_index;
        return true;
    } else {
        ctx->error_code = "runtime_condition_not_supported";
        return false;
    }

    *out_condition_index = parent_index;
    ++ctx->bundle->condition_count;
    return true;
}

static bool compile_action(node_rule_compile_context_t *ctx, const cJSON *action)
{
    const cJSON *kind = NULL;
    node_rule_compiled_action_t *out = NULL;

    if (!ctx || !ctx->bundle || !cJSON_IsObject(action)) {
        if (ctx) {
            ctx->error_code = "invalid_rule";
        }
        return false;
    }
    if (ctx->bundle->total_action_count >= NODE_RULE_MAX_ACTIONS_TOTAL) {
        ctx->error_code = "too_many_actions_total";
        return false;
    }

    kind = cJSON_GetObjectItemCaseSensitive(action, "kind");
    if (!cJSON_IsString(kind) || !kind->valuestring) {
        ctx->error_code = "invalid_rule";
        return false;
    }

    out = &ctx->bundle->actions[ctx->bundle->total_action_count];
    memset(out, 0, sizeof(*out));
    out->state_index = UINT16_MAX;
    out->phase_index = UINT16_MAX;
    out->timer_index = UINT16_MAX;
    out->condition_index = UINT16_MAX;

    if (strcmp(kind->valuestring, "command") == 0) {
        const cJSON *command = cJSON_GetObjectItemCaseSensitive(action, "command");
        if (!cJSON_IsString(command) ||
            !build_command_args_json(ctx->config, action, out->payload_json, sizeof(out->payload_json))) {
            ctx->error_code = "runtime_command_not_supported";
            return false;
        }
        snprintf(out->command, sizeof(out->command), "%s", command->valuestring);
        out->kind = NODE_RULE_ACTION_COMMAND;
    } else if (strcmp(kind->valuestring, "set_state") == 0) {
        const cJSON *key = cJSON_GetObjectItemCaseSensitive(action, "key");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(action, "value");
        if (!cJSON_IsString(key) || !read_scalar_value(value, &out->value)) {
            ctx->error_code = "unsupported_state_type";
            return false;
        }
        if (!append_state_key(ctx, key->valuestring)) {
            return false;
        }
        out->state_index = find_state_index(ctx->bundle, key->valuestring);
        if (out->state_index == UINT16_MAX) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        out->kind = NODE_RULE_ACTION_SET_STATE;
    } else if (strcmp(kind->valuestring, "set_phase") == 0) {
        const cJSON *phase = cJSON_GetObjectItemCaseSensitive(action, "phase");
        if (!cJSON_IsString(phase)) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        if (!append_phase_name(ctx, phase->valuestring)) {
            return false;
        }
        out->phase_index = find_phase_index(ctx->bundle, phase->valuestring);
        if (out->phase_index == UINT16_MAX) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        out->kind = NODE_RULE_ACTION_SET_PHASE;
    } else if (strcmp(kind->valuestring, "emit_event") == 0) {
        const cJSON *event = cJSON_GetObjectItemCaseSensitive(action, "event");
        const cJSON *args = cJSON_GetObjectItemCaseSensitive(action, "args");
        if (!cJSON_IsString(event) || !build_emit_args_json(args, out->payload_json, sizeof(out->payload_json))) {
            ctx->error_code = "runtime_emit_not_supported";
            return false;
        }
        snprintf(out->event_name, sizeof(out->event_name), "%s", event->valuestring);
        out->kind = NODE_RULE_ACTION_EMIT_EVENT;
    } else if (strcmp(kind->valuestring, "start_timer") == 0) {
        const cJSON *timer = cJSON_GetObjectItemCaseSensitive(action, "timer");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(action, "name");
        const cJSON *mode = cJSON_GetObjectItemCaseSensitive(action, "mode");
        const char *timer_name = NULL;

        if (!read_timer_name(timer, name, &timer_name) ||
            !append_timer_name(ctx, timer_name) ||
            !read_timer_delay_fields(action, &out->duration_ms, &out->interval_ms)) {
            ctx->error_code = "runtime_timer_not_supported";
            return false;
        }
        out->timer_index = find_timer_index(ctx->bundle, timer_name);
        if (out->timer_index == UINT16_MAX) {
            ctx->error_code = "runtime_timer_not_supported";
            return false;
        }
        out->timer_mode = timer_mode_from_name(cJSON_IsString(mode) ? mode->valuestring : NULL);
        if (out->timer_mode == NODE_RULE_TIMER_MODE_REPEAT && out->interval_ms == 0) {
            out->interval_ms = out->duration_ms;
        }
        if (out->duration_ms == 0) {
            out->duration_ms = out->interval_ms;
        }
        out->kind = NODE_RULE_ACTION_START_TIMER;
    } else if (strcmp(kind->valuestring, "cancel_timer") == 0) {
        const cJSON *timer = cJSON_GetObjectItemCaseSensitive(action, "timer");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(action, "name");
        const char *timer_name = NULL;

        if (!read_timer_name(timer, name, &timer_name) || !append_timer_name(ctx, timer_name)) {
            ctx->error_code = "runtime_timer_not_supported";
            return false;
        }
        out->timer_index = find_timer_index(ctx->bundle, timer_name);
        if (out->timer_index == UINT16_MAX) {
            ctx->error_code = "runtime_timer_not_supported";
            return false;
        }
        out->kind = NODE_RULE_ACTION_CANCEL_TIMER;
    } else if (strcmp(kind->valuestring, "choose") == 0) {
        const cJSON *condition = cJSON_GetObjectItemCaseSensitive(action, "condition");
        const cJSON *if_condition = cJSON_GetObjectItemCaseSensitive(action, "if");
        const cJSON *then_actions = cJSON_GetObjectItemCaseSensitive(action, "then");
        const cJSON *else_actions = cJSON_GetObjectItemCaseSensitive(action, "else");
        const cJSON *selected_condition = cJSON_IsObject(condition) ? condition : if_condition;
        size_t then_count = 0;
        size_t else_count = 0;

        if (!cJSON_IsObject(selected_condition) || !cJSON_IsArray(then_actions)) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        out->kind = NODE_RULE_ACTION_CHOOSE;
        ++ctx->bundle->total_action_count;

        if (!compile_condition(ctx, selected_condition, &out->condition_index)) {
            return false;
        }
        out->then_action_start = (uint16_t)ctx->bundle->total_action_count;
        if (!collect_actions_array(ctx, then_actions, 1, &then_count)) {
            return false;
        }
        out->then_action_count = (uint16_t)then_count;
        if (else_actions) {
            if (!cJSON_IsArray(else_actions)) {
                ctx->error_code = "invalid_rule";
                return false;
            }
            out->else_action_start = (uint16_t)ctx->bundle->total_action_count;
            if (!collect_actions_array(ctx, else_actions, 1, &else_count)) {
                return false;
            }
            out->else_action_count = (uint16_t)else_count;
        }
        out->next_action_index = (uint16_t)ctx->bundle->total_action_count;
        return true;
    } else {
        ctx->error_code = "runtime_action_not_supported";
        return false;
    }

    ++ctx->bundle->total_action_count;
    return true;
}

static bool collect_condition(node_rule_compile_context_t *ctx, const cJSON *condition, unsigned depth)
{
    const cJSON *kind = NULL;

    if (!ctx || !ctx->bundle || !cJSON_IsObject(condition)) {
        if (ctx) {
            ctx->error_code = "invalid_rule";
        }
        return false;
    }
    if (depth > NODE_RULE_MAX_ACTION_NESTING) {
        ctx->error_code = "condition_nesting_too_deep";
        return false;
    }

    kind = cJSON_GetObjectItemCaseSensitive(condition, "kind");
    if (!cJSON_IsString(kind) || !kind->valuestring) {
        ctx->error_code = "invalid_rule";
        return false;
    }

    if (strcmp(kind->valuestring, "state_equals") == 0) {
        const cJSON *key = cJSON_GetObjectItemCaseSensitive(condition, "key");
        return cJSON_IsString(key) && append_state_key(ctx, key->valuestring);
    }
    if (strcmp(kind->valuestring, "phase_is") == 0) {
        const cJSON *phase = cJSON_GetObjectItemCaseSensitive(condition, "phase");
        return cJSON_IsString(phase) && append_phase_name(ctx, phase->valuestring);
    }
    if (strcmp(kind->valuestring, "event_field_equals") == 0) {
        return true;
    }
    if (strcmp(kind->valuestring, "not") == 0) {
        return collect_condition(ctx,
                                 cJSON_GetObjectItemCaseSensitive(condition, "condition"),
                                 depth + 1);
    }
    if (strcmp(kind->valuestring, "all") == 0 || strcmp(kind->valuestring, "any") == 0) {
        const cJSON *conditions = cJSON_GetObjectItemCaseSensitive(condition, "conditions");
        cJSON *item = NULL;

        if (!cJSON_IsArray(conditions)) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        cJSON_ArrayForEach(item, (cJSON *)conditions) {
            if (!collect_condition(ctx, item, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}

static bool collect_action(node_rule_compile_context_t *ctx,
                           const cJSON *action,
                           unsigned depth,
                           size_t *out_action_count)
{
    const cJSON *kind = NULL;

    if (!ctx || !ctx->bundle || !out_action_count || !cJSON_IsObject(action)) {
        if (ctx) {
            ctx->error_code = "invalid_rule";
        }
        return false;
    }
    if (depth > NODE_RULE_MAX_ACTION_NESTING) {
        ctx->error_code = "action_nesting_too_deep";
        return false;
    }
    kind = cJSON_GetObjectItemCaseSensitive(action, "kind");
    if (!cJSON_IsString(kind) || !kind->valuestring) {
        ctx->error_code = "invalid_rule";
        return false;
    }
    if (strcmp(kind->valuestring, "sequence") == 0) {
        const cJSON *actions = cJSON_GetObjectItemCaseSensitive(action, "actions");
        return collect_actions_array(ctx, actions, depth + 1, out_action_count);
    }
    if (!compile_action(ctx, action)) {
        return false;
    }
    ++(*out_action_count);
    return true;
}

static bool collect_actions_array(node_rule_compile_context_t *ctx,
                                  const cJSON *actions,
                                  unsigned depth,
                                  size_t *out_action_count)
{
    cJSON *item = NULL;

    if (!ctx || !ctx->bundle || !out_action_count || !cJSON_IsArray(actions)) {
        if (ctx) {
            ctx->error_code = "invalid_rule";
        }
        return false;
    }
    cJSON_ArrayForEach(item, (cJSON *)actions) {
        if (!collect_action(ctx, item, depth, out_action_count)) {
            return false;
        }
    }
    return true;
}

static bool collect_initial_state(node_rule_compile_context_t *ctx, const cJSON *initial_state)
{
    cJSON *item = NULL;

    if (!initial_state) {
        return true;
    }
    if (!cJSON_IsObject(initial_state)) {
        ctx->error_code = "invalid_bundle_shape";
        return false;
    }
    cJSON_ArrayForEach(item, (cJSON *)initial_state) {
        uint16_t index = UINT16_MAX;

        if (!item->string || !append_state_key(ctx, item->string)) {
            return false;
        }
        index = find_state_index(ctx->bundle, item->string);
        if (index == UINT16_MAX || index >= NODE_RULE_MAX_STATE_KEYS) {
            ctx->error_code = "invalid_bundle_shape";
            return false;
        }
        if (!read_scalar_value(item, &ctx->bundle->initial_state_values[index])) {
            ctx->error_code = "unsupported_state_type";
            return false;
        }
        if (ctx->bundle->initial_state_count < ctx->bundle->state_key_count) {
            ctx->bundle->initial_state_count = ctx->bundle->state_key_count;
        }
    }
    return true;
}

static bool collect_emit_names(node_rule_compile_context_t *ctx, const cJSON *emits)
{
    cJSON *item = NULL;

    if (!emits) {
        return true;
    }
    if (!cJSON_IsArray(emits)) {
        ctx->error_code = "invalid_bundle_shape";
        return false;
    }
    cJSON_ArrayForEach(item, (cJSON *)emits) {
        if (!cJSON_IsString(item) || !append_emit_name(ctx, item->valuestring)) {
            return false;
        }
    }
    return true;
}

static bool collect_rule(node_rule_compile_context_t *ctx, const cJSON *rule)
{
    node_rule_compiled_rule_t *out_rule = NULL;
    const cJSON *id = NULL;
    const cJSON *enabled = NULL;
    const cJSON *trigger = NULL;
    const cJSON *trigger_kind = NULL;
    const cJSON *conditions = NULL;
    const cJSON *actions = NULL;
    size_t action_count = 0;

    if (!ctx || !ctx->bundle || !cJSON_IsObject(rule)) {
        if (ctx) {
            ctx->error_code = "invalid_rule";
        }
        return false;
    }
    if (ctx->bundle->rule_count >= NODE_RULE_MAX_RULES) {
        ctx->error_code = "too_many_rules";
        return false;
    }

    out_rule = &ctx->bundle->rules[ctx->bundle->rule_count];
    id = cJSON_GetObjectItemCaseSensitive(rule, "id");
    enabled = cJSON_GetObjectItemCaseSensitive(rule, "enabled");
    trigger = cJSON_GetObjectItemCaseSensitive(rule, "trigger");
    conditions = cJSON_GetObjectItemCaseSensitive(rule, "conditions");
    actions = cJSON_GetObjectItemCaseSensitive(rule, "actions");

    if (!cJSON_IsString(id) || !nonempty_text(id->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1)) {
        ctx->error_code = "invalid_rule";
        return false;
    }
    memset(out_rule, 0, sizeof(*out_rule));
    out_rule->timer_index = UINT16_MAX;
    out_rule->condition_index = UINT16_MAX;
    out_rule->action_start = (uint16_t)ctx->bundle->total_action_count;
    snprintf(out_rule->id, sizeof(out_rule->id), "%s", id->valuestring);
    out_rule->enabled = !cJSON_IsBool(enabled) || cJSON_IsTrue(enabled);
    out_rule->has_conditions = cJSON_IsObject(conditions);
    if (out_rule->enabled) {
        ++ctx->bundle->enabled_rule_count;
    }

    trigger_kind = cJSON_GetObjectItemCaseSensitive(trigger, "kind");
    if (!cJSON_IsString(trigger_kind) || !trigger_kind->valuestring) {
        ctx->error_code = "invalid_rule";
        return false;
    }
    out_rule->trigger_kind = trigger_kind_from_name(trigger_kind->valuestring);
    if (out_rule->trigger_kind == NODE_RULE_TRIGGER_NONE) {
        ctx->error_code = "invalid_rule";
        return false;
    }

    if (out_rule->trigger_kind == NODE_RULE_TRIGGER_BOOT) {
        /* no extra fields */
    } else if (out_rule->trigger_kind == NODE_RULE_TRIGGER_INPUT_EDGE) {
        const cJSON *input = cJSON_GetObjectItemCaseSensitive(trigger, "input");
        const cJSON *to = cJSON_GetObjectItemCaseSensitive(trigger, "to");

        if (!cJSON_IsString(input) || !cJSON_IsNumber(to)) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        if (!resolve_input_channel(ctx->config, input->valuestring, &out_rule->input_channel)) {
            ctx->error_code = "runtime_trigger_not_supported";
            return false;
        }
        out_rule->trigger_value = (int32_t)to->valuedouble;
    } else if (out_rule->trigger_kind == NODE_RULE_TRIGGER_INPUT_HOLD) {
        const cJSON *input = cJSON_GetObjectItemCaseSensitive(trigger, "input");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(trigger, "value");
        const cJSON *duration_ms = cJSON_GetObjectItemCaseSensitive(trigger, "duration_ms");

        if (!cJSON_IsString(input) || !cJSON_IsNumber(value) || !cJSON_IsNumber(duration_ms)) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        if (!resolve_input_channel(ctx->config, input->valuestring, &out_rule->input_channel)) {
            ctx->error_code = "runtime_trigger_not_supported";
            return false;
        }
        out_rule->trigger_value = (int32_t)value->valuedouble;
        out_rule->trigger_duration_ms = (uint32_t)duration_ms->valuedouble;
    } else if (out_rule->trigger_kind == NODE_RULE_TRIGGER_TIMER) {
        const cJSON *timer = cJSON_GetObjectItemCaseSensitive(trigger, "timer");
        if (!cJSON_IsString(timer) || !append_timer_name(ctx, timer->valuestring)) {
            ctx->error_code = "runtime_trigger_not_supported";
            return false;
        }
        out_rule->timer_index = find_timer_index(ctx->bundle, timer->valuestring);
        if (out_rule->timer_index == UINT16_MAX) {
            ctx->error_code = "runtime_trigger_not_supported";
            return false;
        }
    } else if (out_rule->trigger_kind == NODE_RULE_TRIGGER_LOCAL_EVENT) {
        const cJSON *event = cJSON_GetObjectItemCaseSensitive(trigger, "event");
        if (!cJSON_IsString(event)) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        snprintf(out_rule->trigger_event_name, sizeof(out_rule->trigger_event_name), "%s", event->valuestring);
    } else if (out_rule->trigger_kind == NODE_RULE_TRIGGER_MQTT_COMMAND) {
        const cJSON *command = cJSON_GetObjectItemCaseSensitive(trigger, "command");
        if (!cJSON_IsString(command)) {
            ctx->error_code = "invalid_rule";
            return false;
        }
        snprintf(out_rule->trigger_event_name, sizeof(out_rule->trigger_event_name), "%s", command->valuestring);
    } else {
        ctx->error_code = "runtime_trigger_not_supported";
        return false;
    }

    if (conditions) {
        if (!collect_condition(ctx, conditions, 1)) {
            return false;
        }
        if (!compile_condition(ctx, conditions, &out_rule->condition_index)) {
            return false;
        }
    }
    if (!collect_actions_array(ctx, actions, 1, &action_count)) {
        return false;
    }

    out_rule->action_count = (uint16_t)action_count;
    if (action_count > ctx->bundle->max_rule_action_count) {
        ctx->bundle->max_rule_action_count = action_count;
    }

    ++ctx->bundle->rule_count;
    return true;
}

static esp_err_t compile_bundle_impl(const char *raw_json,
                                     const node_config_t *config,
                                     node_rule_compiled_bundle_t *out_bundle,
                                     char *out_error_code,
                                     size_t out_error_code_size)
{
    node_rule_compile_context_t ctx = {0};
    node_rule_bundle_metadata_t metadata = {0};
    cJSON *root = NULL;
    cJSON *rules = NULL;
    cJSON *rule = NULL;
    esp_err_t err = ESP_OK;

    if (!raw_json || !out_bundle) {
        write_error_code(out_error_code, out_error_code_size, "invalid_request");
        return ESP_ERR_INVALID_ARG;
    }

    clear_bundle(out_bundle);
    ctx.bundle = out_bundle;
    ctx.config = config;

    err = node_rule_schema_validate_bundle_for_config(raw_json,
                                                      config,
                                                      &metadata,
                                                      out_error_code,
                                                      out_error_code_size);
    if (err != ESP_OK) {
        return err;
    }

    root = cJSON_Parse(raw_json);
    if (!cJSON_IsObject(root)) {
        write_error_code(out_error_code, out_error_code_size, "invalid_bundle_json");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    out_bundle->metadata = metadata;
    out_bundle->status = NODE_RULE_COMPILE_STATUS_READY;

    if (!collect_emit_names(&ctx, cJSON_GetObjectItemCaseSensitive(root, "emits")) ||
        !collect_exports(&ctx, cJSON_GetObjectItemCaseSensitive(root, "exports")) ||
        !collect_initial_state(&ctx, cJSON_GetObjectItemCaseSensitive(root, "initial_state"))) {
        write_error_code(out_error_code, out_error_code_size, ctx.error_code);
        cJSON_Delete(root);
        return compile_error_to_esp_err(ctx.error_code);
    }

    rules = cJSON_GetObjectItemCaseSensitive(root, "rules");
    out_bundle->driver_count = cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root, "drivers"))
                                   ? (size_t)cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(root, "drivers"))
                                   : 0U;

    cJSON_ArrayForEach(rule, rules) {
        if (!collect_rule(&ctx, rule)) {
            write_error_code(out_error_code, out_error_code_size, ctx.error_code);
            cJSON_Delete(root);
            return compile_error_to_esp_err(ctx.error_code);
        }
    }

    write_error_code(out_error_code, out_error_code_size, "");
    cJSON_Delete(root);
    return ESP_OK;
}

const char *node_rule_compile_status_name(node_rule_compile_status_t status)
{
    switch (status) {
    case NODE_RULE_COMPILE_STATUS_READY:
        return "ready";
    case NODE_RULE_COMPILE_STATUS_ERROR:
        return "error";
    case NODE_RULE_COMPILE_STATUS_INACTIVE:
    default:
        return "inactive";
    }
}

esp_err_t node_rule_compile_bundle_for_config(const char *raw_json,
                                              const node_config_t *config,
                                              node_rule_compiled_bundle_t *out_bundle,
                                              char *out_error_code,
                                              size_t out_error_code_size)
{
    return compile_bundle_impl(raw_json, config, out_bundle, out_error_code, out_error_code_size);
}

esp_err_t node_rule_compile_bootstrap(const node_config_t *config)
{
    node_rule_store_entry_t *entry = NULL;
    node_rule_compiled_bundle_t *compiled = NULL;
    char error_code[NODE_RULE_API_ERROR_MAX_LEN] = {0};
    esp_err_t err = ESP_OK;

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ensure_active_bundle_storage()) {
        return ESP_ERR_NO_MEM;
    }
    clear_bundle(s_active_bundle);
    if (!node_runtime_mode_rules_enabled(config)) {
        return ESP_OK;
    }

    entry = (node_rule_store_entry_t *)alloc_rule_owner_buffer(sizeof(*entry));
    compiled = (node_rule_compiled_bundle_t *)alloc_rule_owner_buffer(sizeof(*compiled));
    if (!entry || !compiled) {
        free(compiled);
        free(entry);
        return ESP_ERR_NO_MEM;
    }

    err = node_rule_store_load(entry);
    if (err != ESP_OK) {
        free(compiled);
        free(entry);
        return err;
    }
    if (!entry->metadata.has_bundle || entry->metadata.raw_size == 0) {
        free(compiled);
        free(entry);
        return ESP_OK;
    }

    err = node_rule_compile_bundle_for_config(entry->raw_json,
                                              config,
                                              compiled,
                                              error_code,
                                              sizeof(error_code));
    if (err != ESP_OK) {
        clear_bundle(s_active_bundle);
        s_active_bundle->status = NODE_RULE_COMPILE_STATUS_ERROR;
        s_active_bundle->metadata = entry->metadata;
        write_error_code(s_active_bundle->error_code, sizeof(s_active_bundle->error_code), error_code);
        free(compiled);
        free(entry);
        return err;
    }

    *s_active_bundle = *compiled;
    free(compiled);
    free(entry);
    return ESP_OK;
}

void node_rule_compile_get_active(node_rule_compiled_bundle_t *out_bundle)
{
    if (!out_bundle) {
        return;
    }
    if (!s_active_bundle) {
        clear_bundle(out_bundle);
        return;
    }
    *out_bundle = *s_active_bundle;
}

const node_rule_compiled_bundle_t *node_rule_compile_peek_active(void)
{
    return s_active_bundle;
}

bool node_rule_compile_has_ready_bundle(void)
{
    return s_active_bundle &&
           s_active_bundle->status == NODE_RULE_COMPILE_STATUS_READY &&
           s_active_bundle->metadata.has_bundle;
}
