#include "node_rule_engine.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "node_hardware_io.h"
#include "node_rule_action_port.h"
#include "node_rule_compile.h"
#include "node_runtime_mode.h"
#include "sdkconfig.h"

static const char *TAG = "node_rule_engine";

enum {
    NODE_RULE_ENGINE_QUEUE_LEN = 16,
    NODE_RULE_ENGINE_POLL_MS = 50,
};

typedef enum {
    NODE_RULE_ENGINE_REQ_EVENT = 0,
    NODE_RULE_ENGINE_REQ_RESET,
    NODE_RULE_ENGINE_REQ_SET_ENABLED,
    NODE_RULE_ENGINE_REQ_PAUSE,
    NODE_RULE_ENGINE_REQ_RESUME,
} node_rule_engine_request_kind_t;

typedef struct {
    node_rule_engine_request_kind_t kind;
    bool enabled;
    node_rule_event_t event;
    TaskHandle_t reply_task;
    esp_err_t *reply_err;
} node_rule_engine_request_t;

typedef struct {
    bool active;
    node_rule_compiled_timer_mode_t mode;
    uint32_t duration_ms;
    uint32_t interval_ms;
    uint32_t next_fire_ms;
} node_rule_timer_slot_t;

typedef struct {
    bool initialized;
    bool stable_active;
    bool pending_valid;
    bool pending_active;
    uint32_t pending_since_ms;
    uint32_t stable_since_ms;
    uint32_t fired_hold_rule_mask;
} node_rule_input_slot_t;

typedef struct {
    bool enabled;
    uint8_t channel;
    node_pin_role_t role;
    uint32_t debounce_ms;
} node_rule_engine_input_config_t;

typedef struct {
    bool rules_enabled;
    node_rule_engine_input_config_t inputs[NODE_UNIVERSAL_IO_MAX];
} node_rule_engine_runtime_config_t;

static bool s_initialized;
static bool s_paused;
static node_rule_engine_runtime_config_t s_engine_config;
static node_rule_scalar_value_t s_state_values[NODE_RULE_MAX_STATE_KEYS];
static uint16_t s_phase_index = UINT16_MAX;
static node_rule_timer_slot_t s_timer_slots[NODE_RULE_MAX_TIMERS];
static node_rule_input_slot_t s_input_slots[NODE_UNIVERSAL_IO_MAX];
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;

static StaticTask_t s_engine_task_storage;
static StackType_t *s_engine_task_stack_mem;
static TaskHandle_t s_engine_task;
static StaticQueue_t s_engine_queue_storage;
static uint8_t s_engine_queue_buffer[NODE_RULE_ENGINE_QUEUE_LEN * sizeof(node_rule_engine_request_t)];
static QueueHandle_t s_engine_queue;
static char s_local_event_json[128];

static esp_err_t route_event_to_engine(const node_rule_event_t *event);

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static bool running_on_owner_task(void)
{
    return s_engine_task && xTaskGetCurrentTaskHandle() == s_engine_task;
}

static bool ensure_engine_queue(void)
{
    if (s_engine_queue) {
        return true;
    }
    s_engine_queue = xQueueCreateStatic(NODE_RULE_ENGINE_QUEUE_LEN,
                                        sizeof(node_rule_engine_request_t),
                                        s_engine_queue_buffer,
                                        &s_engine_queue_storage);
    return s_engine_queue != NULL;
}

static StackType_t *allocate_engine_task_stack(void)
{
    size_t stack_bytes = 4096U * sizeof(StackType_t);

    if (s_engine_task_stack_mem) {
        return s_engine_task_stack_mem;
    }
#if CONFIG_SPIRAM && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    s_engine_task_stack_mem = (StackType_t *)heap_caps_malloc(stack_bytes,
                                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_engine_task_stack_mem) {
        memset(s_engine_task_stack_mem, 0, stack_bytes);
        ESP_LOGI(TAG, "rule engine stack source=psram bytes=%u", (unsigned)stack_bytes);
        return s_engine_task_stack_mem;
    }
    ESP_LOGW(TAG, "rule engine stack psram alloc failed; using internal heap fallback");
#endif
    s_engine_task_stack_mem = (StackType_t *)heap_caps_malloc(stack_bytes, MALLOC_CAP_8BIT);
    if (s_engine_task_stack_mem) {
        memset(s_engine_task_stack_mem, 0, stack_bytes);
        ESP_LOGI(TAG, "rule engine stack source=internal_heap bytes=%u", (unsigned)stack_bytes);
    }
    return s_engine_task_stack_mem;
}

static void build_runtime_config(const node_config_t *config)
{
    memset(&s_engine_config, 0, sizeof(s_engine_config));
    if (!config) {
        return;
    }

    s_engine_config.rules_enabled = node_runtime_mode_rules_enabled(config);
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        s_engine_config.inputs[i].enabled = config->universal_io[i].enabled;
        s_engine_config.inputs[i].channel = config->universal_io[i].channel;
        s_engine_config.inputs[i].role = config->universal_io[i].role;
        s_engine_config.inputs[i].debounce_ms = config->universal_io[i].debounce_ms;
    }
}

static void reset_runtime_state(const node_rule_compiled_bundle_t *bundle)
{
    memset(s_state_values, 0, sizeof(s_state_values));
    memset(s_timer_slots, 0, sizeof(s_timer_slots));
    memset(s_input_slots, 0, sizeof(s_input_slots));
    s_phase_index = UINT16_MAX;

    if (!bundle) {
        return;
    }
    for (size_t i = 0; i < bundle->state_key_count && i < NODE_RULE_MAX_STATE_KEYS; ++i) {
        s_state_values[i] = bundle->initial_state_values[i];
    }
}

static bool trigger_matches(const node_rule_compiled_rule_t *rule, const node_rule_event_t *event)
{
    if (!rule || !event) {
        return false;
    }

    switch (rule->trigger_kind) {
    case NODE_RULE_TRIGGER_BOOT:
        return event->type == NODE_RULE_EVENT_BOOT;
    case NODE_RULE_TRIGGER_INPUT_EDGE:
        return event->type == NODE_RULE_EVENT_INPUT_EDGE &&
               event->input_channel == rule->input_channel &&
               event->value == rule->trigger_value;
    case NODE_RULE_TRIGGER_INPUT_HOLD:
        return event->type == NODE_RULE_EVENT_INPUT_HOLD &&
               event->input_channel == rule->input_channel &&
               event->value == rule->trigger_value &&
               event->duration_ms == rule->trigger_duration_ms;
    case NODE_RULE_TRIGGER_TIMER:
        return event->type == NODE_RULE_EVENT_TIMER &&
               event->timer_index == rule->timer_index;
    case NODE_RULE_TRIGGER_LOCAL_EVENT:
        return event->type == NODE_RULE_EVENT_LOCAL &&
               strcmp(rule->trigger_event_name, event->event_name) == 0;
    case NODE_RULE_TRIGGER_MQTT_COMMAND:
        return event->type == NODE_RULE_EVENT_MQTT_COMMAND &&
               strcmp(rule->trigger_event_name, event->event_name) == 0;
    default:
        return false;
    }
}

static bool exported_command_exists(const node_rule_compiled_bundle_t *bundle, const char *command_name)
{
    if (!bundle || !command_name || command_name[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < bundle->export_command_count; ++i) {
        if (strcmp(bundle->export_commands[i].id, command_name) == 0) {
            return true;
        }
    }
    return false;
}

static void publish_local_event(const node_rule_event_t *event)
{
    int written = 0;
    esp_err_t err = ESP_OK;

    if (!event ||
        event->type != NODE_RULE_EVENT_LOCAL ||
        event->event_name[0] == '\0') {
        return;
    }

    written = snprintf(s_local_event_json,
                       sizeof(s_local_event_json),
                       "{\"source_id\":\"%s\",\"token_id\":%ld,\"uid\":\"%s\"}",
                       event->source_id,
                       (long)event->token_id,
                       event->uid);
    if (written < 0 || written >= (int)sizeof(s_local_event_json)) {
        ESP_LOGW(TAG, "local event payload truncated event=%s", event->event_name);
        return;
    }

    err = node_rule_action_port_emit_event(event->event_name, s_local_event_json);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG,
                 "local event publish failed event=%s source=%s err=%s",
                 event->event_name,
                 event->source_id,
                 esp_err_to_name(err));
    }
}

static bool input_condition_matches(uint8_t input_channel, int32_t value)
{
    if (input_channel == 0 || input_channel > NODE_UNIVERSAL_IO_MAX) {
        return false;
    }
    if (!s_input_slots[input_channel - 1].initialized) {
        return false;
    }
    return (s_input_slots[input_channel - 1].stable_active ? 1 : 0) == value;
}

static bool condition_matches(const node_rule_compiled_bundle_t *bundle,
                              uint16_t condition_index,
                              const node_rule_event_t *event)
{
    const node_rule_compiled_condition_t *condition = NULL;
    uint16_t child_index = UINT16_MAX;

    if (!bundle || condition_index == UINT16_MAX) {
        return true;
    }
    if (condition_index >= bundle->condition_count) {
        return false;
    }

    condition = &bundle->conditions[condition_index];
    switch (condition->kind) {
    case NODE_RULE_CONDITION_STATE_EQUALS:
        if (condition->state_index >= NODE_RULE_MAX_STATE_KEYS) {
            return false;
        }
        return s_state_values[condition->state_index].type == condition->value.type &&
               s_state_values[condition->state_index].int_value == condition->value.int_value;
    case NODE_RULE_CONDITION_PHASE_IS:
        return s_phase_index == condition->phase_index;
    case NODE_RULE_CONDITION_INPUT_EQUALS:
        return input_condition_matches(condition->input_channel, condition->value.int_value);
    case NODE_RULE_CONDITION_EVENT_FIELD_EQUALS:
        if (!event) {
            return false;
        }
        if (condition->event_field == NODE_RULE_EVENT_FIELD_TOKEN_ID) {
            return event->token_id == condition->value.int_value;
        }
        return false;
    case NODE_RULE_CONDITION_ALL_INPUTS_EQUAL:
        for (size_t i = 0; i < condition->input_count; ++i) {
            if (!input_condition_matches(condition->input_channels[i], condition->value.int_value)) {
                return false;
            }
        }
        return true;
    case NODE_RULE_CONDITION_NOT:
        return !condition_matches(bundle, condition->first_child_index, event);
    case NODE_RULE_CONDITION_ALL:
        child_index = condition->first_child_index;
        for (size_t i = 0; i < condition->child_count && child_index != UINT16_MAX; ++i) {
            if (!condition_matches(bundle, child_index, event)) {
                return false;
            }
            child_index = bundle->conditions[child_index].next_sibling_index;
        }
        return true;
    case NODE_RULE_CONDITION_ANY:
        child_index = condition->first_child_index;
        for (size_t i = 0; i < condition->child_count && child_index != UINT16_MAX; ++i) {
            if (condition_matches(bundle, child_index, event)) {
                return true;
            }
            child_index = bundle->conditions[child_index].next_sibling_index;
        }
        return false;
    case NODE_RULE_CONDITION_NONE:
    default:
        return true;
    }
}

static esp_err_t execute_action_range(const node_rule_compiled_bundle_t *bundle,
                                      uint16_t action_start,
                                      uint16_t action_count,
                                      const node_rule_event_t *event);

static uint16_t next_action_index(const node_rule_compiled_bundle_t *bundle, uint16_t current_index)
{
    const node_rule_compiled_action_t *action = NULL;

    if (!bundle || current_index >= bundle->total_action_count) {
        return current_index;
    }
    action = &bundle->actions[current_index];
    if (action->kind == NODE_RULE_ACTION_CHOOSE && action->next_action_index > current_index) {
        return action->next_action_index;
    }
    return (uint16_t)(current_index + 1);
}

static esp_err_t execute_action(const node_rule_compiled_bundle_t *bundle,
                                const node_rule_compiled_action_t *action,
                                const node_rule_event_t *event)
{
    esp_err_t err = ESP_OK;
    uint32_t now = now_ms();
    node_rule_timer_slot_t *slot = NULL;

    if (!bundle || !action) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (action->kind) {
    case NODE_RULE_ACTION_COMMAND:
        return node_rule_action_port_execute_command(action->command, action->payload_json);
    case NODE_RULE_ACTION_SET_STATE:
        if (action->state_index >= NODE_RULE_MAX_STATE_KEYS) {
            return ESP_ERR_INVALID_ARG;
        }
        s_state_values[action->state_index] = action->value;
        return ESP_OK;
    case NODE_RULE_ACTION_SET_PHASE:
        s_phase_index = action->phase_index;
        return ESP_OK;
    case NODE_RULE_ACTION_EMIT_EVENT:
        err = node_rule_action_port_emit_event(action->event_name, action->payload_json);
        return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
    case NODE_RULE_ACTION_START_TIMER:
        if (action->timer_index >= NODE_RULE_MAX_TIMERS) {
            return ESP_ERR_INVALID_ARG;
        }
        slot = &s_timer_slots[action->timer_index];
        if (action->timer_mode == NODE_RULE_TIMER_MODE_COOLDOWN && slot->active) {
            return ESP_OK;
        }
        slot->active = true;
        slot->mode = action->timer_mode;
        slot->duration_ms = action->duration_ms;
        slot->interval_ms = action->interval_ms > 0 ? action->interval_ms : action->duration_ms;
        slot->next_fire_ms = now + (action->duration_ms > 0 ? action->duration_ms : action->interval_ms);
        return ESP_OK;
    case NODE_RULE_ACTION_CANCEL_TIMER:
        if (action->timer_index >= NODE_RULE_MAX_TIMERS) {
            return ESP_ERR_INVALID_ARG;
        }
        memset(&s_timer_slots[action->timer_index], 0, sizeof(s_timer_slots[action->timer_index]));
        return ESP_OK;
    case NODE_RULE_ACTION_CHOOSE:
        if (condition_matches(bundle, action->condition_index, event)) {
            return execute_action_range(bundle, action->then_action_start, action->then_action_count, event);
        }
        if (action->else_action_count > 0) {
            return execute_action_range(bundle, action->else_action_start, action->else_action_count, event);
        }
        return ESP_OK;
    case NODE_RULE_ACTION_NONE:
    default:
        return ESP_OK;
    }
}

static esp_err_t execute_action_range(const node_rule_compiled_bundle_t *bundle,
                                      uint16_t action_start,
                                      uint16_t action_count,
                                      const node_rule_event_t *event)
{
    uint16_t compiled_index = action_start;

    for (size_t action_index = 0; action_index < action_count; ++action_index) {
        esp_err_t err = ESP_OK;

        if (!bundle || compiled_index >= bundle->total_action_count) {
            return ESP_ERR_INVALID_SIZE;
        }
        err = execute_action(bundle, &bundle->actions[compiled_index], event);
        if (err != ESP_OK) {
            return err;
        }
        compiled_index = next_action_index(bundle, compiled_index);
    }
    return ESP_OK;
}

static esp_err_t dispatch_event_inline(const node_rule_event_t *event)
{
    const node_rule_compiled_bundle_t *bundle = NULL;

    if (!s_initialized || !event) {
        return ESP_ERR_INVALID_STATE;
    }
    publish_local_event(event);
    if (s_paused) {
        return ESP_OK;
    }
    if (!s_engine_config.rules_enabled) {
        return ESP_OK;
    }

    bundle = node_rule_compile_peek_active();
    if (!bundle ||
        bundle->status != NODE_RULE_COMPILE_STATUS_READY ||
        !bundle->metadata.has_bundle) {
        return ESP_OK;
    }

    for (size_t i = 0; i < bundle->rule_count; ++i) {
        const node_rule_compiled_rule_t *rule = &bundle->rules[i];
        esp_err_t err = ESP_OK;

        if (!rule->enabled ||
            !trigger_matches(rule, event) ||
            !condition_matches(bundle, rule->condition_index, event)) {
            continue;
        }

        err = execute_action_range(bundle, rule->action_start, rule->action_count, event);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "rule action failed rule=%s err=%s", rule->id, esp_err_to_name(err));
        }
    }

    return ESP_OK;
}

static void process_timer_events(const node_rule_compiled_bundle_t *bundle)
{
    uint32_t now = now_ms();

    if (!bundle) {
        return;
    }

    for (size_t i = 0; i < bundle->timer_count && i < NODE_RULE_MAX_TIMERS; ++i) {
        node_rule_timer_slot_t *slot = &s_timer_slots[i];
        node_rule_event_t event = {0};

        if (!slot->active || !time_reached(now, slot->next_fire_ms)) {
            continue;
        }

        if (slot->mode == NODE_RULE_TIMER_MODE_REPEAT) {
            slot->next_fire_ms = now + (slot->interval_ms > 0 ? slot->interval_ms : slot->duration_ms);
        } else {
            slot->active = false;
        }

        node_event_router_make_timer_event(&event,
                                           (uint16_t)i,
                                           bundle->timer_names[i],
                                           slot->duration_ms);
        (void)dispatch_event_inline(&event);
    }
}

static uint32_t input_debounce_ms_for_channel(uint8_t channel)
{
    if (channel == 0) {
        return 0;
    }
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_rule_engine_input_config_t *pin = &s_engine_config.inputs[i];

        if (!pin->enabled ||
            pin->role != NODE_PIN_UNIVERSAL_INPUT ||
            pin->channel != channel) {
            continue;
        }
        return pin->debounce_ms;
    }
    return 0;
}

static void publish_input_edge(uint8_t channel, bool active)
{
    node_rule_event_t event = {0};
    esp_err_t err = node_rule_action_port_publish_input_change(channel, active ? 1 : 0);

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG,
                 "input publish failed channel=%u err=%s",
                 (unsigned)channel,
                 esp_err_to_name(err));
    }

    node_event_router_make_input_edge_event(&event, channel, active ? 1 : 0);
    (void)dispatch_event_inline(&event);
}

static void mark_hold_rules_fired(const node_rule_compiled_bundle_t *bundle,
                                  node_rule_input_slot_t *slot,
                                  uint8_t channel,
                                  int32_t value,
                                  uint32_t duration_ms)
{
    if (!bundle || !slot) {
        return;
    }
    for (size_t i = 0; i < bundle->rule_count && i < 32; ++i) {
        const node_rule_compiled_rule_t *rule = &bundle->rules[i];

        if (!rule->enabled ||
            rule->trigger_kind != NODE_RULE_TRIGGER_INPUT_HOLD ||
            rule->input_channel != channel ||
            rule->trigger_value != value ||
            rule->trigger_duration_ms != duration_ms) {
            continue;
        }
        slot->fired_hold_rule_mask |= (1UL << i);
    }
}

static void process_input_hold_events(const node_rule_compiled_bundle_t *bundle,
                                      uint8_t channel,
                                      node_rule_input_slot_t *slot,
                                      uint32_t now_ms_value)
{
    uint32_t dispatched_durations[NODE_RULE_MAX_RULES] = {0};
    size_t dispatched_count = 0;

    if (!bundle || !slot || !slot->initialized) {
        return;
    }

    for (size_t i = 0; i < bundle->rule_count && i < 32; ++i) {
        const node_rule_compiled_rule_t *rule = &bundle->rules[i];
        uint32_t held_ms = now_ms_value - slot->stable_since_ms;
        node_rule_event_t event = {0};
        bool already_dispatched = false;

        if (!rule->enabled ||
            rule->trigger_kind != NODE_RULE_TRIGGER_INPUT_HOLD ||
            rule->input_channel != channel ||
            rule->trigger_value != (slot->stable_active ? 1 : 0) ||
            held_ms < rule->trigger_duration_ms ||
            (slot->fired_hold_rule_mask & (1UL << i)) != 0) {
            continue;
        }

        for (size_t j = 0; j < dispatched_count; ++j) {
            if (dispatched_durations[j] == rule->trigger_duration_ms) {
                already_dispatched = true;
                break;
            }
        }
        if (already_dispatched) {
            continue;
        }

        node_event_router_make_input_hold_event(&event,
                                                channel,
                                                slot->stable_active ? 1 : 0,
                                                rule->trigger_duration_ms);
        (void)dispatch_event_inline(&event);
        mark_hold_rules_fired(bundle,
                              slot,
                              channel,
                              slot->stable_active ? 1 : 0,
                              rule->trigger_duration_ms);
        dispatched_durations[dispatched_count++] = rule->trigger_duration_ms;
    }
}

static void process_input_sample(const node_rule_compiled_bundle_t *bundle,
                                 uint8_t channel,
                                 bool active,
                                 uint32_t now_ms_value)
{
    node_rule_input_slot_t *slot = NULL;
    uint32_t debounce_ms = 0;

    if (channel == 0 || channel > NODE_UNIVERSAL_IO_MAX) {
        return;
    }
    slot = &s_input_slots[channel - 1];
    debounce_ms = input_debounce_ms_for_channel(channel);

    if (!slot->initialized) {
        slot->initialized = true;
        slot->stable_active = active;
        slot->stable_since_ms = now_ms_value;
        slot->pending_valid = false;
        return;
    }

    if (active != slot->stable_active) {
        if (debounce_ms == 0) {
            slot->stable_active = active;
            slot->stable_since_ms = now_ms_value;
            slot->pending_valid = false;
            slot->fired_hold_rule_mask = 0;
            publish_input_edge(channel, active);
        } else if (!slot->pending_valid || slot->pending_active != active) {
            slot->pending_valid = true;
            slot->pending_active = active;
            slot->pending_since_ms = now_ms_value;
        } else if ((now_ms_value - slot->pending_since_ms) >= debounce_ms) {
            slot->stable_active = slot->pending_active;
            slot->stable_since_ms = now_ms_value;
            slot->pending_valid = false;
            slot->fired_hold_rule_mask = 0;
            publish_input_edge(channel, slot->stable_active);
        }
        return;
    }

    slot->pending_valid = false;
    process_input_hold_events(bundle, channel, slot, now_ms_value);
}

static void poll_inputs_and_timers(void)
{
    const node_rule_compiled_bundle_t *bundle = node_rule_compile_peek_active();
    uint32_t now_ms_value = now_ms();

    for (uint8_t channel = 1; channel <= NODE_UNIVERSAL_IO_MAX; ++channel) {
        bool active = false;
        esp_err_t err = node_hardware_io_read_input(channel, &active);

        if (err == ESP_ERR_NOT_FOUND) {
            continue;
        }
        if (err != ESP_OK) {
            continue;
        }
        process_input_sample(bundle, channel, active, now_ms_value);
    }
    process_timer_events(bundle);
}

static esp_err_t handle_reset_inline(void)
{
    const node_rule_compiled_bundle_t *bundle = node_rule_compile_peek_active();

    reset_runtime_state(bundle);
    return ESP_OK;
}

static esp_err_t handle_pause_inline(void)
{
    const node_rule_compiled_bundle_t *bundle = node_rule_compile_peek_active();

    portENTER_CRITICAL(&s_status_lock);
    s_paused = true;
    portEXIT_CRITICAL(&s_status_lock);
    reset_runtime_state(bundle);
    ESP_LOGI(TAG, "rules paused");
    return ESP_OK;
}

static esp_err_t handle_set_enabled_inline(bool enabled)
{
    portENTER_CRITICAL(&s_status_lock);
    s_engine_config.rules_enabled = enabled;
    portEXIT_CRITICAL(&s_status_lock);
    ESP_LOGI(TAG, "rules runtime enabled=%d", enabled);
    return ESP_OK;
}

static esp_err_t handle_resume_inline(void)
{
    node_rule_event_t boot_event = {0};
    const node_rule_compiled_bundle_t *bundle = node_rule_compile_peek_active();

    reset_runtime_state(bundle);
    portENTER_CRITICAL(&s_status_lock);
    s_paused = false;
    portEXIT_CRITICAL(&s_status_lock);
    ESP_LOGI(TAG, "rules resumed");
    if (!s_engine_config.rules_enabled ||
        !bundle ||
        bundle->status != NODE_RULE_COMPILE_STATUS_READY ||
        !bundle->metadata.has_bundle) {
        return ESP_OK;
    }

    node_event_router_make_boot_event(&boot_event);
    return dispatch_event_inline(&boot_event);
}

static void handle_request(const node_rule_engine_request_t *request)
{
    esp_err_t err = ESP_OK;

    if (!request) {
        return;
    }

    switch (request->kind) {
    case NODE_RULE_ENGINE_REQ_EVENT:
        err = dispatch_event_inline(&request->event);
        break;
    case NODE_RULE_ENGINE_REQ_RESET:
        err = handle_reset_inline();
        break;
    case NODE_RULE_ENGINE_REQ_SET_ENABLED:
        err = handle_set_enabled_inline(request->enabled);
        break;
    case NODE_RULE_ENGINE_REQ_PAUSE:
        err = handle_pause_inline();
        break;
    case NODE_RULE_ENGINE_REQ_RESUME:
        err = handle_resume_inline();
        break;
    default:
        err = ESP_ERR_INVALID_ARG;
        break;
    }

    if (request->reply_err) {
        *request->reply_err = err;
    }
    if (request->reply_task) {
        xTaskNotifyGive(request->reply_task);
    }
}

static void engine_task(void *arg)
{
    (void)arg;

    while (true) {
        node_rule_engine_request_t request = {0};

        if (xQueueReceive(s_engine_queue, &request, pdMS_TO_TICKS(NODE_RULE_ENGINE_POLL_MS)) == pdTRUE) {
            handle_request(&request);
            while (xQueueReceive(s_engine_queue, &request, 0) == pdTRUE) {
                handle_request(&request);
            }
        }
        poll_inputs_and_timers();
    }
}

static bool ensure_engine_task(void)
{
    if (s_engine_task) {
        return true;
    }
    StackType_t *task_stack = allocate_engine_task_stack();

    if (!task_stack) {
        return false;
    }
    s_engine_task = xTaskCreateStatic(engine_task,
                                      "node_rule_eng",
                                      4096,
                                      NULL,
                                      tskIDLE_PRIORITY + 1,
                                      task_stack,
                                      &s_engine_task_storage);
    return s_engine_task != NULL;
}

static esp_err_t submit_request(node_rule_engine_request_t *request, TickType_t timeout_ticks)
{
    esp_err_t err = ESP_OK;

    if (!request || !s_initialized || !s_engine_queue || !s_engine_task) {
        return ESP_ERR_INVALID_STATE;
    }
    if (running_on_owner_task()) {
        handle_request(request);
        return request->reply_err ? *request->reply_err : ESP_OK;
    }

    request->reply_task = xTaskGetCurrentTaskHandle();
    request->reply_err = &err;
    if (xQueueSend(s_engine_queue, request, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (ulTaskNotifyTake(pdTRUE, timeout_ticks) == 0) {
        return ESP_ERR_TIMEOUT;
    }
    return err;
}

static esp_err_t enqueue_event(const node_rule_event_t *event)
{
    node_rule_engine_request_t request = {0};

    if (!event) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !s_engine_queue || !s_engine_task) {
        return ESP_ERR_INVALID_STATE;
    }
    if (running_on_owner_task()) {
        return dispatch_event_inline(event);
    }

    request.kind = NODE_RULE_ENGINE_REQ_EVENT;
    request.event = *event;
    if (xQueueSend(s_engine_queue, &request, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t route_event_to_engine(const node_rule_event_t *event)
{
    return enqueue_event(event);
}

esp_err_t node_rule_engine_dispatch_event(const node_rule_event_t *event)
{
    return enqueue_event(event);
}

esp_err_t node_rule_engine_dispatch_local_event(const char *event_name,
                                                const char *source_id,
                                                int32_t token_id,
                                                const char *uid)
{
    node_rule_event_t event = {0};

    if (!s_initialized || !event_name || event_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    node_event_router_make_local_event(&event, event_name, source_id, token_id, uid);
    return enqueue_event(&event);
}

esp_err_t node_rule_engine_dispatch_mqtt_command(const char *command_name)
{
    node_rule_event_t event = {0};
    const node_rule_compiled_bundle_t *bundle = NULL;

    if (!s_initialized || !command_name || command_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    bundle = node_rule_compile_peek_active();
    if (!bundle ||
        bundle->status != NODE_RULE_COMPILE_STATUS_READY ||
        !bundle->metadata.has_bundle) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!exported_command_exists(bundle, command_name)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_paused || !s_engine_config.rules_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    node_event_router_make_mqtt_command_event(&event, command_name);
    return enqueue_event(&event);
}

esp_err_t node_rule_engine_reset(void)
{
    node_rule_engine_request_t request = {.kind = NODE_RULE_ENGINE_REQ_RESET};

    return submit_request(&request, pdMS_TO_TICKS(1000));
}

esp_err_t node_rule_engine_set_runtime_enabled(bool enabled)
{
    node_rule_engine_request_t request = {
        .kind = NODE_RULE_ENGINE_REQ_SET_ENABLED,
        .enabled = enabled,
    };

    return submit_request(&request, pdMS_TO_TICKS(1000));
}

esp_err_t node_rule_engine_pause(void)
{
    node_rule_engine_request_t request = {.kind = NODE_RULE_ENGINE_REQ_PAUSE};

    return submit_request(&request, pdMS_TO_TICKS(1000));
}

esp_err_t node_rule_engine_resume(void)
{
    node_rule_engine_request_t request = {.kind = NODE_RULE_ENGINE_REQ_RESUME};

    return submit_request(&request, pdMS_TO_TICKS(1000));
}

void node_rule_engine_get_status(node_rule_engine_status_t *out_status)
{
    if (!out_status) {
        return;
    }

    memset(out_status, 0, sizeof(*out_status));
    portENTER_CRITICAL(&s_status_lock);
    out_status->initialized = s_initialized;
    out_status->paused = s_paused;
    out_status->rules_enabled_by_mode = s_engine_config.rules_enabled;
    portEXIT_CRITICAL(&s_status_lock);
}

esp_err_t node_rule_engine_init(const node_config_t *config)
{
    node_rule_event_t boot_event = {0};
    const node_rule_compiled_bundle_t *bundle = NULL;
    esp_err_t err = ESP_OK;

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ensure_engine_queue() || !ensure_engine_task()) {
        return ESP_ERR_NO_MEM;
    }

    build_runtime_config(config);
    portENTER_CRITICAL(&s_status_lock);
    s_initialized = true;
    s_paused = false;
    portEXIT_CRITICAL(&s_status_lock);
    err = node_event_router_set_sink(route_event_to_engine);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_status_lock);
        s_initialized = false;
        portEXIT_CRITICAL(&s_status_lock);
        return err;
    }

    err = node_rule_engine_reset();
    if (err != ESP_OK) {
        return err;
    }

    bundle = node_rule_compile_peek_active();
    if (!s_engine_config.rules_enabled ||
        !bundle ||
        bundle->status != NODE_RULE_COMPILE_STATUS_READY ||
        !bundle->metadata.has_bundle) {
        return ESP_OK;
    }

    node_event_router_make_boot_event(&boot_event);
    return enqueue_event(&boot_event);
}
