#include "node_rule_schema.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "node_driver_nfc_reader.h"
#include "node_runtime_mode.h"
#include "sdkconfig.h"

typedef struct {
    const node_config_t *config;
    char emit_names[NODE_RULE_MAX_EMIT_EVENTS][NODE_DRIVER_EVENT_NAME_MAX_LEN];
    size_t emit_count;
    char export_command_names[NODE_RULE_MAX_EXPORT_COMMANDS][NODE_DRIVER_EVENT_NAME_MAX_LEN];
    size_t export_command_count;
    char export_event_names[NODE_RULE_MAX_EXPORT_EVENTS][NODE_DRIVER_EVENT_NAME_MAX_LEN];
    size_t export_event_count;
    char local_event_names[NODE_RULE_MAX_LOCAL_EVENTS][NODE_DRIVER_EVENT_NAME_MAX_LEN];
    size_t local_event_count;
    char driver_ids[NODE_DRIVER_INSTANCE_MAX][NODE_DRIVER_ID_MAX_LEN + 1];
    size_t driver_count;
} node_rule_schema_context_t;

static node_rule_schema_context_t *alloc_schema_context(void)
{
    node_rule_schema_context_t *ctx = NULL;

    #if CONFIG_SPIRAM
    ctx = (node_rule_schema_context_t *)heap_caps_malloc(sizeof(*ctx),
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ctx) {
        memset(ctx, 0, sizeof(*ctx));
        return ctx;
    }
    #endif

    ctx = (node_rule_schema_context_t *)heap_caps_malloc(sizeof(*ctx), MALLOC_CAP_8BIT);
    if (ctx) {
        memset(ctx, 0, sizeof(*ctx));
    }
    return ctx;
}

static void write_error_code(char *out_error_code, size_t out_error_code_size, const char *code)
{
    if (!out_error_code || out_error_code_size == 0) {
        return;
    }
    snprintf(out_error_code, out_error_code_size, "%s", code ? code : "");
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

static bool read_string_item(const cJSON *item, char *out, size_t out_size)
{
    if (!cJSON_IsString(item) || !item->valuestring || !out || out_size == 0) {
        return false;
    }
    snprintf(out, out_size, "%s", item->valuestring);
    return out[0] != '\0';
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

static bool append_name(char (*items)[NODE_DRIVER_EVENT_NAME_MAX_LEN], size_t *count, size_t cap, const char *name)
{
    if (!items || !count || !name || !nonempty_text(name, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1)) {
        return false;
    }
    for (size_t i = 0; i < *count; ++i) {
        if (strcmp(items[i], name) == 0) {
            return false;
        }
    }
    if (*count >= cap) {
        return false;
    }
    snprintf(items[*count], NODE_DRIVER_EVENT_NAME_MAX_LEN, "%s", name);
    ++(*count);
    return true;
}

static bool append_driver_id(node_rule_schema_context_t *ctx, const char *id)
{
    if (!ctx || !id || !nonempty_text(id, NODE_DRIVER_ID_MAX_LEN)) {
        return false;
    }
    for (size_t i = 0; i < ctx->driver_count; ++i) {
        if (strcmp(ctx->driver_ids[i], id) == 0) {
            return false;
        }
    }
    if (ctx->driver_count >= NODE_DRIVER_INSTANCE_MAX) {
        return false;
    }
    snprintf(ctx->driver_ids[ctx->driver_count], sizeof(ctx->driver_ids[ctx->driver_count]), "%s", id);
    ++ctx->driver_count;
    return true;
}

static bool name_in_list(char (*items)[NODE_DRIVER_EVENT_NAME_MAX_LEN], size_t count, const char *name)
{
    if (!name) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(items[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool driver_id_exists(const node_rule_schema_context_t *ctx, const char *id)
{
    if (!ctx || !id) {
        return false;
    }
    for (size_t i = 0; i < ctx->driver_count; ++i) {
        if (strcmp(ctx->driver_ids[i], id) == 0) {
            return true;
        }
    }
    return false;
}

static bool export_command_allowed(const node_rule_schema_context_t *ctx, const char *id)
{
    if (!ctx || !id || ctx->export_command_count == 0) {
        return true;
    }
    return name_in_list((char (*)[NODE_DRIVER_EVENT_NAME_MAX_LEN])ctx->export_command_names,
                        ctx->export_command_count,
                        id);
}

static bool scalar_state_value_valid(const cJSON *item)
{
    return cJSON_IsBool(item) || cJSON_IsNumber(item) || cJSON_IsString(item);
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

static bool config_has_input(const node_config_t *config, const char *name)
{
    int channel = 0;

    if (!config || !name) {
        return true;
    }
    channel = logical_channel_from_name(name, "input_");
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_universal_pin_config_t *pin = &config->universal_io[i];
        if (!pin->enabled || pin->role != NODE_PIN_UNIVERSAL_INPUT) {
            continue;
        }
        if ((channel > 0 && pin->channel == (uint8_t)channel) ||
            label_matches_name(pin->label, name)) {
            return true;
        }
    }
    return false;
}

static bool config_has_output_pin(const node_output_pin_config_t *pins, size_t count, const char *name, const char *prefix)
{
    int channel = logical_channel_from_name(name, prefix);

    if (!pins || !name || !prefix) {
        return true;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!pins[i].enabled) {
            continue;
        }
        if ((channel > 0 && pins[i].channel == (uint8_t)channel) ||
            label_matches_name(pins[i].label, name)) {
            return true;
        }
    }
    return false;
}

static bool config_has_io_output(const node_config_t *config, const char *name)
{
    int channel = 0;

    if (!config || !name) {
        return true;
    }
    channel = logical_channel_from_name(name, "io_");
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_universal_pin_config_t *pin = &config->universal_io[i];
        if (!pin->enabled || pin->role != NODE_PIN_UNIVERSAL_OUTPUT) {
            continue;
        }
        if ((channel > 0 && pin->channel == (uint8_t)channel) ||
            label_matches_name(pin->label, name)) {
            return true;
        }
    }
    return false;
}

static bool config_has_led_output(const node_config_t *config, const char *name)
{
    int led_channel = 0;
    int strip_channel = 0;
    int led_strip_channel = 0;

    if (!config || !name) {
        return true;
    }
    led_channel = logical_channel_from_name(name, "led_");
    strip_channel = logical_channel_from_name(name, "strip_");
    led_strip_channel = logical_channel_from_name(name, "led_strip_");
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *pin = &config->led_strips[i];
        if (!pin->enabled) {
            continue;
        }
        if ((led_channel > 0 && pin->channel == (uint8_t)led_channel) ||
            (strip_channel > 0 && pin->channel == (uint8_t)strip_channel) ||
            (led_strip_channel > 0 && pin->channel == (uint8_t)led_strip_channel) ||
            label_matches_name(pin->label, name)) {
            return true;
        }
    }
    return false;
}

static bool command_output_valid(const node_rule_schema_context_t *ctx, const char *command, const char *output)
{
    const node_config_t *config = ctx ? ctx->config : NULL;

    if (!command || !output) {
        return false;
    }
    if (strncmp(command, "relay.", 6) == 0) {
        return config_has_output_pin(config ? config->relays : NULL, NODE_RELAY_MAX, output, "relay_");
    }
    if (strncmp(command, "mosfet.", 7) == 0) {
        return config_has_output_pin(config ? config->mosfets : NULL, NODE_MOSFET_MAX, output, "mosfet_");
    }
    if (strncmp(command, "io.", 3) == 0) {
        return config_has_io_output(config, output);
    }
    if (strncmp(command, "led.", 4) == 0) {
        return config_has_led_output(config, output);
    }
    return false;
}

static bool command_name_supported(const char *command)
{
    static const char *const supported[] = {
        "relay.set",
        "relay.pulse",
        "relay.all_off",
        "mosfet.set",
        "mosfet.fade",
        "mosfet.pulse",
        "mosfet.blink",
        "mosfet.breathe",
        "mosfet.all_off",
        "mosfet.effect",
        "io.set",
        "io.all_off",
        "node.all_off",
        "led.off",
        "led.solid",
        "led.blink",
        "led.breathe",
        "led.effect",
    };

    for (size_t i = 0; i < sizeof(supported) / sizeof(supported[0]); ++i) {
        if (strcmp(command, supported[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool local_event_allowed(const node_rule_schema_context_t *ctx, const char *event_name)
{
    if (!ctx || !event_name || ctx->local_event_count == 0) {
        return true;
    }
    return name_in_list((char (*)[NODE_DRIVER_EVENT_NAME_MAX_LEN])ctx->local_event_names,
                        ctx->local_event_count,
                        event_name);
}

static bool emit_event_declared(const node_rule_schema_context_t *ctx, const char *event_name)
{
    if (!ctx || !event_name) {
        return false;
    }
    if (ctx->emit_count == 0) {
        return true;
    }
    return name_in_list((char (*)[NODE_DRIVER_EVENT_NAME_MAX_LEN])ctx->emit_names,
                        ctx->emit_count,
                        event_name);
}

static bool validate_inputs_array(const node_rule_schema_context_t *ctx, const cJSON *array)
{
    cJSON *item = NULL;

    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) <= 0 ||
        cJSON_GetArraySize(array) > NODE_RULE_MAX_GROUP_INPUTS) {
        return false;
    }
    cJSON_ArrayForEach(item, (cJSON *)array) {
        if (!cJSON_IsString(item) || !nonempty_text(item->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1)) {
            return false;
        }
        if (!config_has_input(ctx ? ctx->config : NULL, item->valuestring)) {
            return false;
        }
    }
    return true;
}

static bool logical_resource_exists(const node_rule_schema_context_t *ctx, const char *name)
{
    const node_config_t *config = ctx ? ctx->config : NULL;

    if (!name || !config) {
        return true;
    }
    return config_has_input(config, name) ||
           config_has_output_pin(config->relays, NODE_RELAY_MAX, name, "relay_") ||
           config_has_output_pin(config->mosfets, NODE_MOSFET_MAX, name, "mosfet_") ||
           config_has_io_output(config, name) ||
           config_has_led_output(config, name) ||
           driver_id_exists(ctx, name);
}

static bool validate_claims_array(const node_rule_schema_context_t *ctx, const cJSON *claims)
{
    cJSON *item = NULL;

    if (!claims) {
        return true;
    }
    if (!cJSON_IsArray(claims) ||
        cJSON_GetArraySize(claims) <= 0 ||
        cJSON_GetArraySize(claims) > NODE_RULE_MAX_EXPORT_CLAIMS) {
        return false;
    }
    cJSON_ArrayForEach(item, (cJSON *)claims) {
        if (!cJSON_IsString(item) ||
            !nonempty_text(item->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1) ||
            !logical_resource_exists(ctx, item->valuestring)) {
            return false;
        }
    }
    return true;
}

static bool validate_exports_object(node_rule_schema_context_t *ctx, const cJSON *exports)
{
    const cJSON *commands = NULL;
    const cJSON *events = NULL;
    cJSON *item = NULL;

    if (!exports) {
        return true;
    }
    if (!cJSON_IsObject(exports)) {
        return false;
    }

    commands = cJSON_GetObjectItemCaseSensitive(exports, "commands");
    if (commands) {
        if (!cJSON_IsArray(commands) || cJSON_GetArraySize(commands) > NODE_RULE_MAX_EXPORT_COMMANDS) {
            return false;
        }
        cJSON_ArrayForEach(item, (cJSON *)commands) {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
            const cJSON *label = cJSON_GetObjectItemCaseSensitive(item, "label");
            const cJSON *kind = cJSON_GetObjectItemCaseSensitive(item, "kind");
            const cJSON *claims = cJSON_GetObjectItemCaseSensitive(item, "claims");

            if (!cJSON_IsObject(item) ||
                !cJSON_IsString(id) ||
                !nonempty_text(id->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1) ||
                !cJSON_IsString(label) ||
                !nonempty_text(label->valuestring, NODE_RULE_EXPORT_LABEL_MAX_LEN) ||
                (kind && (!cJSON_IsString(kind) || strcmp(kind->valuestring, "runtime_command") != 0)) ||
                !validate_claims_array(ctx, claims) ||
                !append_name(ctx->export_command_names,
                             &ctx->export_command_count,
                             NODE_RULE_MAX_EXPORT_COMMANDS,
                             id->valuestring)) {
                return false;
            }
        }
    }

    events = cJSON_GetObjectItemCaseSensitive(exports, "events");
    if (events) {
        if (!cJSON_IsArray(events) || cJSON_GetArraySize(events) > NODE_RULE_MAX_EXPORT_EVENTS) {
            return false;
        }
        cJSON_ArrayForEach(item, (cJSON *)events) {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
            const cJSON *label = cJSON_GetObjectItemCaseSensitive(item, "label");

            if (!cJSON_IsObject(item) ||
                !cJSON_IsString(id) ||
                !nonempty_text(id->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1) ||
                !emit_event_declared(ctx, id->valuestring) ||
                !cJSON_IsString(label) ||
                !nonempty_text(label->valuestring, NODE_RULE_EXPORT_LABEL_MAX_LEN) ||
                !append_name(ctx->export_event_names,
                             &ctx->export_event_count,
                             NODE_RULE_MAX_EXPORT_EVENTS,
                             id->valuestring)) {
                return false;
            }
        }
    }
    return true;
}

static bool validate_condition(const node_rule_schema_context_t *ctx, const cJSON *condition, unsigned depth);
static bool validate_actions_array(const node_rule_schema_context_t *ctx, const cJSON *actions, unsigned depth);

static bool validate_trigger(const node_rule_schema_context_t *ctx, const cJSON *trigger)
{
    const cJSON *kind = NULL;
    const cJSON *input = NULL;
    const cJSON *value = NULL;
    const cJSON *inputs = NULL;
    const cJSON *event = NULL;
    const cJSON *timer = NULL;
    const cJSON *duration = NULL;

    if (!cJSON_IsObject(trigger)) {
        return false;
    }

    kind = cJSON_GetObjectItemCaseSensitive(trigger, "kind");
    if (!cJSON_IsString(kind) || !kind->valuestring) {
        return false;
    }

    if (strcmp(kind->valuestring, "boot") == 0) {
        return true;
    }
    if (strcmp(kind->valuestring, "input_edge") == 0) {
        input = cJSON_GetObjectItemCaseSensitive(trigger, "input");
        value = cJSON_GetObjectItemCaseSensitive(trigger, "to");
        return cJSON_IsString(input) &&
               config_has_input(ctx ? ctx->config : NULL, input->valuestring) &&
               cJSON_IsNumber(value);
    }
    if (strcmp(kind->valuestring, "input_level") == 0) {
        input = cJSON_GetObjectItemCaseSensitive(trigger, "input");
        value = cJSON_GetObjectItemCaseSensitive(trigger, "value");
        return cJSON_IsString(input) &&
               config_has_input(ctx ? ctx->config : NULL, input->valuestring) &&
               cJSON_IsNumber(value);
    }
    if (strcmp(kind->valuestring, "input_hold") == 0) {
        input = cJSON_GetObjectItemCaseSensitive(trigger, "input");
        value = cJSON_GetObjectItemCaseSensitive(trigger, "value");
        duration = cJSON_GetObjectItemCaseSensitive(trigger, "duration_ms");
        return cJSON_IsString(input) &&
               config_has_input(ctx ? ctx->config : NULL, input->valuestring) &&
               cJSON_IsNumber(value) &&
               cJSON_IsNumber(duration) &&
               duration->valuedouble > 0;
    }
    if (strcmp(kind->valuestring, "all_inputs_level") == 0) {
        inputs = cJSON_GetObjectItemCaseSensitive(trigger, "inputs");
        value = cJSON_GetObjectItemCaseSensitive(trigger, "value");
        return validate_inputs_array(ctx, inputs) && cJSON_IsNumber(value);
    }
    if (strcmp(kind->valuestring, "timer") == 0) {
        timer = cJSON_GetObjectItemCaseSensitive(trigger, "timer");
        return cJSON_IsString(timer) && nonempty_text(timer->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1);
    }
    if (strcmp(kind->valuestring, "local_event") == 0) {
        event = cJSON_GetObjectItemCaseSensitive(trigger, "event");
        return cJSON_IsString(event) &&
               nonempty_text(event->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1) &&
               local_event_allowed(ctx, event->valuestring);
    }
    if (strcmp(kind->valuestring, "state_changed") == 0) {
        const cJSON *key = cJSON_GetObjectItemCaseSensitive(trigger, "key");
        return cJSON_IsString(key) && nonempty_text(key->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1);
    }
    if (strcmp(kind->valuestring, "mqtt_command") == 0) {
        const cJSON *command = cJSON_GetObjectItemCaseSensitive(trigger, "command");
        return cJSON_IsString(command) &&
               nonempty_text(command->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1) &&
               export_command_allowed(ctx, command->valuestring);
    }

    return false;
}

static bool validate_condition(const node_rule_schema_context_t *ctx, const cJSON *condition, unsigned depth)
{
    const cJSON *kind = NULL;

    if (!cJSON_IsObject(condition) || depth > NODE_RULE_MAX_ACTION_NESTING) {
        return false;
    }
    kind = cJSON_GetObjectItemCaseSensitive(condition, "kind");
    if (!cJSON_IsString(kind) || !kind->valuestring) {
        return false;
    }

    if (strcmp(kind->valuestring, "state_equals") == 0) {
        const cJSON *key = cJSON_GetObjectItemCaseSensitive(condition, "key");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(condition, "value");
        return cJSON_IsString(key) &&
               nonempty_text(key->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1) &&
               scalar_state_value_valid(value);
    }
    if (strcmp(kind->valuestring, "input_equals") == 0) {
        const cJSON *input = cJSON_GetObjectItemCaseSensitive(condition, "input");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(condition, "value");
        return cJSON_IsString(input) &&
               config_has_input(ctx ? ctx->config : NULL, input->valuestring) &&
               cJSON_IsNumber(value);
    }
    if (strcmp(kind->valuestring, "event_field_equals") == 0) {
        const cJSON *field = cJSON_GetObjectItemCaseSensitive(condition, "field");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(condition, "value");
        return cJSON_IsString(field) &&
               strcmp(field->valuestring, "token_id") == 0 &&
               cJSON_IsNumber(value);
    }
    if (strcmp(kind->valuestring, "all_inputs_equal") == 0) {
        const cJSON *inputs = cJSON_GetObjectItemCaseSensitive(condition, "inputs");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(condition, "value");
        return validate_inputs_array(ctx, inputs) && cJSON_IsNumber(value);
    }
    if (strcmp(kind->valuestring, "phase_is") == 0) {
        const cJSON *phase = cJSON_GetObjectItemCaseSensitive(condition, "phase");
        return cJSON_IsString(phase) && nonempty_text(phase->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1);
    }
    if (strcmp(kind->valuestring, "not") == 0) {
        const cJSON *inner = cJSON_GetObjectItemCaseSensitive(condition, "condition");
        return validate_condition(ctx, inner, depth + 1);
    }
    if (strcmp(kind->valuestring, "all") == 0 || strcmp(kind->valuestring, "any") == 0) {
        const cJSON *items = cJSON_GetObjectItemCaseSensitive(condition, "conditions");
        cJSON *item = NULL;

        if (!cJSON_IsArray(items) || cJSON_GetArraySize(items) <= 0) {
            return false;
        }
        cJSON_ArrayForEach(item, (cJSON *)items) {
            if (!validate_condition(ctx, item, depth + 1)) {
                return false;
            }
        }
        return true;
    }

    return false;
}

static bool validate_command_action(const node_rule_schema_context_t *ctx, const cJSON *action)
{
    const cJSON *command = cJSON_GetObjectItemCaseSensitive(action, "command");
    const cJSON *args = cJSON_GetObjectItemCaseSensitive(action, "args");
    const cJSON *output = NULL;
    const cJSON *driver = NULL;

    if (!cJSON_IsString(command) || !command->valuestring || !command_name_supported(command->valuestring)) {
        return false;
    }
    if (strcmp(command->valuestring, "node.all_off") == 0 ||
        strcmp(command->valuestring, "relay.all_off") == 0 ||
        strcmp(command->valuestring, "mosfet.all_off") == 0 ||
        strcmp(command->valuestring, "io.all_off") == 0) {
        return !args || cJSON_IsObject(args);
    }
    if (!cJSON_IsObject(args)) {
        return false;
    }

    output = cJSON_GetObjectItemCaseSensitive(args, "output");
    if (cJSON_IsString(output) && output->valuestring) {
        return command_output_valid(ctx, command->valuestring, output->valuestring);
    }

    driver = cJSON_GetObjectItemCaseSensitive(args, "driver");
    if (cJSON_IsString(driver) && driver->valuestring) {
        return driver_id_exists(ctx, driver->valuestring);
    }
    return false;
}

static bool validate_action(const node_rule_schema_context_t *ctx, const cJSON *action, unsigned depth)
{
    const cJSON *kind = NULL;

    if (!cJSON_IsObject(action) || depth > NODE_RULE_MAX_ACTION_NESTING) {
        return false;
    }
    kind = cJSON_GetObjectItemCaseSensitive(action, "kind");
    if (!cJSON_IsString(kind) || !kind->valuestring) {
        return false;
    }

    if (strcmp(kind->valuestring, "command") == 0) {
        return validate_command_action(ctx, action);
    }
    if (strcmp(kind->valuestring, "set_state") == 0) {
        const cJSON *key = cJSON_GetObjectItemCaseSensitive(action, "key");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(action, "value");
        return cJSON_IsString(key) &&
               nonempty_text(key->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1) &&
               scalar_state_value_valid(value);
    }
    if (strcmp(kind->valuestring, "set_phase") == 0) {
        const cJSON *phase = cJSON_GetObjectItemCaseSensitive(action, "phase");
        return cJSON_IsString(phase) && nonempty_text(phase->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1);
    }
    if (strcmp(kind->valuestring, "emit_event") == 0) {
        const cJSON *event = cJSON_GetObjectItemCaseSensitive(action, "event");
        const cJSON *args = cJSON_GetObjectItemCaseSensitive(action, "args");
        return cJSON_IsString(event) &&
               emit_event_declared(ctx, event->valuestring) &&
               (!args || cJSON_IsObject(args));
    }
    if (strcmp(kind->valuestring, "start_timer") == 0) {
        const cJSON *timer = cJSON_GetObjectItemCaseSensitive(action, "timer");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(action, "name");
        const cJSON *mode = cJSON_GetObjectItemCaseSensitive(action, "mode");
        const cJSON *duration_ms = cJSON_GetObjectItemCaseSensitive(action, "duration_ms");
        const cJSON *interval_ms = cJSON_GetObjectItemCaseSensitive(action, "interval_ms");
        const cJSON *timer_name = cJSON_IsString(timer) ? timer : name;

        if (!cJSON_IsString(timer_name) ||
            !nonempty_text(timer_name->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1)) {
            return false;
        }
        if (mode && (!cJSON_IsString(mode) ||
                     (strcmp(mode->valuestring, "oneshot") != 0 &&
                      strcmp(mode->valuestring, "repeat") != 0 &&
                      strcmp(mode->valuestring, "cooldown") != 0))) {
            return false;
        }
        if (cJSON_IsNumber(duration_ms)) {
            return duration_ms->valuedouble > 0;
        }
        return cJSON_IsNumber(interval_ms) && interval_ms->valuedouble > 0;
    }
    if (strcmp(kind->valuestring, "cancel_timer") == 0) {
        const cJSON *timer = cJSON_GetObjectItemCaseSensitive(action, "timer");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(action, "name");
        const cJSON *timer_name = cJSON_IsString(timer) ? timer : name;
        return cJSON_IsString(timer_name) &&
               nonempty_text(timer_name->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1);
    }
    if (strcmp(kind->valuestring, "choose") == 0) {
        const cJSON *condition = cJSON_GetObjectItemCaseSensitive(action, "condition");
        const cJSON *if_condition = cJSON_GetObjectItemCaseSensitive(action, "if");
        const cJSON *then_actions = cJSON_GetObjectItemCaseSensitive(action, "then");
        const cJSON *else_actions = cJSON_GetObjectItemCaseSensitive(action, "else");
        const cJSON *selected_condition = cJSON_IsObject(condition) ? condition : if_condition;

        return validate_condition(ctx, selected_condition, depth + 1) &&
               validate_actions_array(ctx, then_actions, depth + 1) &&
               (!else_actions || validate_actions_array(ctx, else_actions, depth + 1));
    }
    if (strcmp(kind->valuestring, "sequence") == 0) {
        const cJSON *actions = cJSON_GetObjectItemCaseSensitive(action, "actions");
        return validate_actions_array(ctx, actions, depth + 1);
    }

    return false;
}

static bool validate_actions_array(const node_rule_schema_context_t *ctx, const cJSON *actions, unsigned depth)
{
    cJSON *item = NULL;
    int count = 0;

    if (!cJSON_IsArray(actions)) {
        return false;
    }
    count = cJSON_GetArraySize(actions);
    if (count <= 0 || count > NODE_RULE_MAX_ACTIONS_PER_RULE) {
        return false;
    }
    cJSON_ArrayForEach(item, (cJSON *)actions) {
        if (!validate_action(ctx, item, depth)) {
            return false;
        }
    }
    return true;
}

static bool collect_emit_names(node_rule_schema_context_t *ctx, const cJSON *emits)
{
    cJSON *item = NULL;

    if (!emits) {
        return true;
    }
    if (!cJSON_IsArray(emits) || cJSON_GetArraySize(emits) > NODE_RULE_MAX_EMIT_EVENTS) {
        return false;
    }
    cJSON_ArrayForEach(item, (cJSON *)emits) {
        if (!cJSON_IsString(item) || !append_name(ctx->emit_names, &ctx->emit_count, NODE_RULE_MAX_EMIT_EVENTS, item->valuestring)) {
            return false;
        }
    }
    return true;
}

static bool validate_initial_state(const cJSON *initial_state)
{
    cJSON *item = NULL;
    size_t count = 0;

    if (!initial_state) {
        return true;
    }
    if (!cJSON_IsObject(initial_state)) {
        return false;
    }
    cJSON_ArrayForEach(item, (cJSON *)initial_state) {
        if (!item->string || !scalar_state_value_valid(item)) {
            return false;
        }
        ++count;
    }
    return count <= NODE_RULE_MAX_STATE_KEYS;
}

static bool validate_limits_object(const cJSON *limits)
{
    cJSON *item = NULL;

    if (!limits) {
        return true;
    }
    if (!cJSON_IsObject(limits)) {
        return false;
    }
    cJSON_ArrayForEach(item, (cJSON *)limits) {
        if (!item->string || !cJSON_IsNumber(item) || item->valuedouble < 0) {
            return false;
        }
    }
    return true;
}

static bool collect_driver_events_for_reader(node_rule_schema_context_t *ctx,
                                             const node_nfc_reader_config_t *config)
{
    char event_name[NODE_DRIVER_EVENT_NAME_MAX_LEN];

    if (!ctx || !config) {
        return false;
    }
    if (!append_driver_id(ctx, config->id)) {
        return false;
    }

    snprintf(event_name, sizeof(event_name), "%s_card_seen", config->id);
    if (!append_name(ctx->local_event_names, &ctx->local_event_count, NODE_RULE_MAX_LOCAL_EVENTS, event_name)) {
        return false;
    }
    snprintf(event_name, sizeof(event_name), "%s_card_removed", config->id);
    if (!append_name(ctx->local_event_names, &ctx->local_event_count, NODE_RULE_MAX_LOCAL_EVENTS, event_name)) {
        return false;
    }
    snprintf(event_name, sizeof(event_name), "%s_unknown_card", config->id);
    if (!append_name(ctx->local_event_names, &ctx->local_event_count, NODE_RULE_MAX_LOCAL_EVENTS, event_name)) {
        return false;
    }

    for (size_t i = 0; i < config->known_card_count; ++i) {
        if (config->known_cards[i].event_name[0] == '\0') {
            continue;
        }
        if (!append_name(ctx->local_event_names,
                         &ctx->local_event_count,
                         NODE_RULE_MAX_LOCAL_EVENTS,
                         config->known_cards[i].event_name)) {
            return false;
        }
    }
    return true;
}

static bool validate_driver_array(node_rule_schema_context_t *ctx, const cJSON *drivers)
{
    cJSON *driver = NULL;

    if (!drivers) {
        return true;
    }
    if (!cJSON_IsArray(drivers) || cJSON_GetArraySize(drivers) > NODE_DRIVER_INSTANCE_MAX) {
        return false;
    }

    cJSON_ArrayForEach(driver, (cJSON *)drivers) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(driver, "id");
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(driver, "type");
        const cJSON *impl = cJSON_GetObjectItemCaseSensitive(driver, "driver");
        const cJSON *bus = cJSON_GetObjectItemCaseSensitive(driver, "bus");
        const cJSON *cfg = cJSON_GetObjectItemCaseSensitive(driver, "config");
        const cJSON *poll_interval_ms = NULL;
        const cJSON *debounce_ms = NULL;
        const cJSON *known_cards = NULL;
        cJSON *card = NULL;
        node_nfc_reader_config_t reader = {0};
        node_driver_instance_info_t instance = {0};

        if (!cJSON_IsString(id) || !cJSON_IsString(type) || !cJSON_IsString(impl) ||
            !cJSON_IsString(bus) || !cJSON_IsObject(cfg)) {
            return false;
        }
        if (strcmp(type->valuestring, "nfc_reader") != 0 || strcmp(impl->valuestring, "pn532") != 0) {
            return false;
        }

        snprintf(reader.id, sizeof(reader.id), "%s", id->valuestring);
        snprintf(reader.driver_impl, sizeof(reader.driver_impl), "%s", impl->valuestring);
        snprintf(reader.bus, sizeof(reader.bus), "%s", bus->valuestring);
        reader.enabled = true;
        reader.i2c_address = 0x24;

        poll_interval_ms = cJSON_GetObjectItemCaseSensitive(cfg, "poll_interval_ms");
        debounce_ms = cJSON_GetObjectItemCaseSensitive(cfg, "debounce_ms");
        if (!cJSON_IsNumber(poll_interval_ms) || !cJSON_IsNumber(debounce_ms)) {
            return false;
        }
        reader.poll_interval_ms = (uint32_t)poll_interval_ms->valuedouble;
        reader.debounce_ms = (uint32_t)debounce_ms->valuedouble;
        reader.i2c_sda_gpio = 1;
        reader.i2c_scl_gpio = 2;

        known_cards = cJSON_GetObjectItemCaseSensitive(cfg, "known_cards");
        if (known_cards) {
            if (!cJSON_IsArray(known_cards) || cJSON_GetArraySize(known_cards) > NODE_DRIVER_NFC_KNOWN_CARD_MAX) {
                return false;
            }
            cJSON_ArrayForEach(card, (cJSON *)known_cards) {
                const cJSON *uid = cJSON_GetObjectItemCaseSensitive(card, "uid");
                const cJSON *token_id = cJSON_GetObjectItemCaseSensitive(card, "token_id");
                const cJSON *event = cJSON_GetObjectItemCaseSensitive(card, "event");
                size_t index = reader.known_card_count;

                if (index >= NODE_DRIVER_NFC_KNOWN_CARD_MAX ||
                    !cJSON_IsString(uid) || !cJSON_IsNumber(token_id) ||
                    (event && !cJSON_IsString(event))) {
                    return false;
                }
                snprintf(reader.known_cards[index].uid, sizeof(reader.known_cards[index].uid), "%s", uid->valuestring);
                reader.known_cards[index].token_id = (int32_t)token_id->valuedouble;
                if (cJSON_IsString(event)) {
                    snprintf(reader.known_cards[index].event_name,
                             sizeof(reader.known_cards[index].event_name),
                             "%s",
                             event->valuestring);
                }
                ++reader.known_card_count;
            }
        }

        if (node_driver_nfc_reader_validate_config(&reader, &instance) != ESP_OK) {
            return false;
        }
        if (!collect_driver_events_for_reader(ctx, &reader)) {
            return false;
        }
    }
    return true;
}

static bool validate_rule_object(const node_rule_schema_context_t *ctx, const cJSON *rule)
{
    const cJSON *id = NULL;
    const cJSON *enabled = NULL;
    const cJSON *trigger = NULL;
    const cJSON *conditions = NULL;
    const cJSON *actions = NULL;

    if (!cJSON_IsObject(rule)) {
        return false;
    }
    id = cJSON_GetObjectItemCaseSensitive(rule, "id");
    enabled = cJSON_GetObjectItemCaseSensitive(rule, "enabled");
    trigger = cJSON_GetObjectItemCaseSensitive(rule, "trigger");
    conditions = cJSON_GetObjectItemCaseSensitive(rule, "conditions");
    actions = cJSON_GetObjectItemCaseSensitive(rule, "actions");

    if (!cJSON_IsString(id) || !nonempty_text(id->valuestring, NODE_DRIVER_EVENT_NAME_MAX_LEN - 1)) {
        return false;
    }
    if (enabled && !cJSON_IsBool(enabled)) {
        return false;
    }
    if (!validate_trigger(ctx, trigger)) {
        return false;
    }
    if (conditions && !validate_condition(ctx, conditions, 1)) {
        return false;
    }
    return validate_actions_array(ctx, actions, 1);
}

static esp_err_t validate_bundle_impl(const char *raw_json,
                                      const node_config_t *config,
                                      node_rule_bundle_metadata_t *out_metadata,
                                      char *out_error_code,
                                      size_t out_error_code_size)
{
    node_rule_schema_context_t *ctx = NULL;
    node_rule_bundle_metadata_t metadata = {0};
    node_operation_mode_t mode = NODE_OPERATION_MODE_SCENEHUB;
    cJSON *root = NULL;
    cJSON *rules = NULL;
    cJSON *rule = NULL;

    if (!raw_json) {
        write_error_code(out_error_code, out_error_code_size, "invalid_request");
        return ESP_ERR_INVALID_ARG;
    }

    ctx = alloc_schema_context();
    if (!ctx) {
        write_error_code(out_error_code, out_error_code_size, "no_mem");
        return ESP_ERR_NO_MEM;
    }
    ctx->config = config;

    metadata.raw_size = strlen(raw_json);
    if (metadata.raw_size == 0 || metadata.raw_size > NODE_RULE_BUNDLE_MAX_LEN) {
        write_error_code(out_error_code, out_error_code_size, "bundle_too_large");
        free(ctx);
        return ESP_ERR_INVALID_SIZE;
    }

    root = cJSON_Parse(raw_json);
    if (!cJSON_IsObject(root)) {
        write_error_code(out_error_code, out_error_code_size, "invalid_bundle_json");
        cJSON_Delete(root);
        free(ctx);
        return ESP_ERR_INVALID_ARG;
    }

    if (!read_string_item(cJSON_GetObjectItemCaseSensitive(root, "bundle_id"),
                          metadata.bundle_id,
                          sizeof(metadata.bundle_id))) {
        write_error_code(out_error_code, out_error_code_size, "missing_bundle_id");
        cJSON_Delete(root);
        free(ctx);
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(root, "version"))) {
        write_error_code(out_error_code, out_error_code_size, "missing_version");
        cJSON_Delete(root);
        free(ctx);
        return ESP_ERR_INVALID_ARG;
    }
    metadata.version = (uint32_t)cJSON_GetObjectItemCaseSensitive(root, "version")->valuedouble;
    if (metadata.version != 2U) {
        write_error_code(out_error_code, out_error_code_size, "unsupported_version");
        cJSON_Delete(root);
        free(ctx);
        return ESP_ERR_INVALID_VERSION;
    }
    if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(root, "generation"))) {
        write_error_code(out_error_code, out_error_code_size, "missing_generation");
        cJSON_Delete(root);
        free(ctx);
        return ESP_ERR_INVALID_ARG;
    }
    metadata.generation = (uint32_t)cJSON_GetObjectItemCaseSensitive(root, "generation")->valuedouble;
    if (metadata.generation == 0U) {
        write_error_code(out_error_code, out_error_code_size, "missing_generation");
        cJSON_Delete(root);
        free(ctx);
        return ESP_ERR_INVALID_ARG;
    }
    if (!read_string_item(cJSON_GetObjectItemCaseSensitive(root, "mode"),
                          metadata.mode,
                          sizeof(metadata.mode))) {
        write_error_code(out_error_code, out_error_code_size, "missing_mode");
        cJSON_Delete(root);
        free(ctx);
        return ESP_ERR_INVALID_ARG;
    }
    if (!node_runtime_mode_from_name(metadata.mode, &mode)) {
        write_error_code(out_error_code, out_error_code_size, "invalid_mode");
        cJSON_Delete(root);
        free(ctx);
        return ESP_ERR_INVALID_ARG;
    }

    if (!collect_emit_names(ctx, cJSON_GetObjectItemCaseSensitive(root, "emits")) ||
        !validate_initial_state(cJSON_GetObjectItemCaseSensitive(root, "initial_state")) ||
        !validate_limits_object(cJSON_GetObjectItemCaseSensitive(root, "limits")) ||
        !validate_driver_array(ctx, cJSON_GetObjectItemCaseSensitive(root, "drivers")) ||
        !validate_exports_object(ctx, cJSON_GetObjectItemCaseSensitive(root, "exports"))) {
        write_error_code(out_error_code, out_error_code_size, "invalid_bundle_shape");
        cJSON_Delete(root);
        free(ctx);
        return ESP_ERR_INVALID_ARG;
    }

    rules = cJSON_GetObjectItemCaseSensitive(root, "rules");
    if (!cJSON_IsArray(rules)) {
        write_error_code(out_error_code, out_error_code_size, "missing_rules");
        cJSON_Delete(root);
        free(ctx);
        return ESP_ERR_INVALID_ARG;
    }
    if (cJSON_GetArraySize(rules) > NODE_RULE_MAX_RULES) {
        write_error_code(out_error_code, out_error_code_size, "too_many_rules");
        cJSON_Delete(root);
        free(ctx);
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON_ArrayForEach(rule, rules) {
        if (!validate_rule_object(ctx, rule)) {
            write_error_code(out_error_code, out_error_code_size, "invalid_rule");
            cJSON_Delete(root);
            free(ctx);
            return ESP_ERR_INVALID_ARG;
        }
    }

    metadata.has_bundle = true;
    if (out_metadata) {
        *out_metadata = metadata;
    }
    write_error_code(out_error_code, out_error_code_size, "");
    cJSON_Delete(root);
    free(ctx);
    return ESP_OK;
}

esp_err_t node_rule_schema_validate_bundle(const char *raw_json,
                                           node_rule_bundle_metadata_t *out_metadata,
                                           char *out_error_code,
                                           size_t out_error_code_size)
{
    return validate_bundle_impl(raw_json, NULL, out_metadata, out_error_code, out_error_code_size);
}

esp_err_t node_rule_schema_validate_bundle_for_config(const char *raw_json,
                                                      const node_config_t *config,
                                                      node_rule_bundle_metadata_t *out_metadata,
                                                      char *out_error_code,
                                                      size_t out_error_code_size)
{
    return validate_bundle_impl(raw_json, config, out_metadata, out_error_code, out_error_code_size);
}
