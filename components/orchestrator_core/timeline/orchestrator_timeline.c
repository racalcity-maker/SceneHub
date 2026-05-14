#include "orchestrator_timeline.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define ORCH_TIMELINE_STATE_CACHE_CAPACITY 64
#define ORCH_TIMELINE_STATE_KEY_MAX_LEN 32
#define ORCH_TIMELINE_JOB_POOL_LEN 8

typedef struct {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lock_storage;
    size_t head;
    size_t count;
    bool ready;
    bool handler_registered;
} orchestrator_timeline_state_t;

typedef struct {
    bool in_use;
    char device_id[QUEST_ID_MAX_LEN];
    char key[ORCH_TIMELINE_STATE_KEY_MAX_LEN];
    char value[ORCH_TIMELINE_DETAILS_MAX_LEN];
} timeline_state_cache_entry_t;

EXT_RAM_BSS_ATTR static orchestrator_timeline_entry_t s_timeline_entries[ORCH_TIMELINE_CAPACITY];
EXT_RAM_BSS_ATTR static timeline_state_cache_entry_t s_state_cache[ORCH_TIMELINE_STATE_CACHE_CAPACITY];
static EXT_RAM_BSS_ATTR scenehub_event_t s_timeline_job_pool[ORCH_TIMELINE_JOB_POOL_LEN];
static bool s_timeline_job_pool_in_use[ORCH_TIMELINE_JOB_POOL_LEN];
static portMUX_TYPE s_timeline_job_pool_lock = portMUX_INITIALIZER_UNLOCKED;
static orchestrator_timeline_state_t s_timeline = {0};

static void timeline_handle_event(const scenehub_event_t *message);
static void timeline_process_event(const scenehub_event_t *message);
static void timeline_process_event_job(void *ctx);

static scenehub_event_t *timeline_job_alloc(void)
{
    scenehub_event_t *slot = NULL;
    portENTER_CRITICAL(&s_timeline_job_pool_lock);
    for (size_t i = 0; i < ORCH_TIMELINE_JOB_POOL_LEN; ++i) {
        if (!s_timeline_job_pool_in_use[i]) {
            s_timeline_job_pool_in_use[i] = true;
            slot = &s_timeline_job_pool[i];
            break;
        }
    }
    portEXIT_CRITICAL(&s_timeline_job_pool_lock);
    if (slot) {
        memset(slot, 0, sizeof(*slot));
    }
    return slot;
}

static void timeline_job_free(scenehub_event_t *slot)
{
    ptrdiff_t index = 0;
    if (!slot) {
        return;
    }
    index = slot - s_timeline_job_pool;
    if (index < 0 || index >= ORCH_TIMELINE_JOB_POOL_LEN) {
        return;
    }
    portENTER_CRITICAL(&s_timeline_job_pool_lock);
    s_timeline_job_pool_in_use[index] = false;
    portEXIT_CRITICAL(&s_timeline_job_pool_lock);
}

static uint64_t timeline_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static const char *timeline_type_str(orchestrator_timeline_type_t type)
{
    switch (type) {
    case ORCH_TIMELINE_TYPE_DEVICE_STATUS:
        return "device_status";
    case ORCH_TIMELINE_TYPE_RUNTIME_CHANGED:
        return "runtime_changed";
    case ORCH_TIMELINE_TYPE_SCENARIO_TRIGGERED:
        return "scenario_triggered";
    case ORCH_TIMELINE_TYPE_TIMER_CHANGED:
        return "timer_changed";
    case ORCH_TIMELINE_TYPE_DEVICE_ACTION:
        return "device_action";
    case ORCH_TIMELINE_TYPE_ACTION_FAILED:
        return "action_failed";
    case ORCH_TIMELINE_TYPE_CONFIG_CHANGED:
        return "config_changed";
    case ORCH_TIMELINE_TYPE_EVENT:
    default:
        return "event";
    }
}

static const char *timeline_severity_str(orchestrator_timeline_severity_t severity)
{
    switch (severity) {
    case ORCH_TIMELINE_SEVERITY_WARNING:
        return "warning";
    case ORCH_TIMELINE_SEVERITY_ERROR:
        return "error";
    case ORCH_TIMELINE_SEVERITY_INFO:
    default:
        return "info";
    }
}

static void timeline_str_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static void timeline_details_topic_payload(char *dst, size_t dst_size, const char *topic, const char *payload)
{
    size_t used = 0;
    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (topic && topic[0]) {
        timeline_str_copy(dst, dst_size, topic);
        used = strlen(dst);
    }
    if (used + 3 < dst_size) {
        memcpy(dst + used, " / ", 3);
        used += 3;
        dst[used] = '\0';
    }
    if (payload && payload[0] && used < dst_size - 1) {
        timeline_str_copy(dst + used, dst_size - used, payload);
    }
}

static orchestrator_timeline_severity_t timeline_status_severity(const char *connectivity, const char *health)
{
    if ((connectivity && strcmp(connectivity, "offline") == 0) ||
        (health && (strcmp(health, "fault") == 0 || strcmp(health, "error") == 0))) {
        return ORCH_TIMELINE_SEVERITY_ERROR;
    }
    return ORCH_TIMELINE_SEVERITY_INFO;
}

static bool timeline_state_changed(const char *device_id, const char *key, const char *value)
{
    timeline_state_cache_entry_t *empty = NULL;
    timeline_state_cache_entry_t *target = NULL;
    if (!device_id || !device_id[0] || !key || !key[0] || !value) {
        return true;
    }
    if (!s_timeline.ready || xSemaphoreTake(s_timeline.lock, 0) != pdTRUE) {
        return true;
    }
    for (size_t i = 0; i < ORCH_TIMELINE_STATE_CACHE_CAPACITY; ++i) {
        timeline_state_cache_entry_t *entry = &s_state_cache[i];
        if (!entry->in_use) {
            if (!empty) {
                empty = entry;
            }
            continue;
        }
        if (strcmp(entry->device_id, device_id) == 0 && strcmp(entry->key, key) == 0) {
            target = entry;
            break;
        }
    }
    if (target && strcmp(target->value, value) == 0) {
        xSemaphoreGive(s_timeline.lock);
        return false;
    }
    if (!target) {
        target = empty ? empty : &s_state_cache[0];
        memset(target, 0, sizeof(*target));
        target->in_use = true;
        timeline_str_copy(target->device_id, sizeof(target->device_id), device_id);
        timeline_str_copy(target->key, sizeof(target->key), key);
    }
    timeline_str_copy(target->value, sizeof(target->value), value);
    xSemaphoreGive(s_timeline.lock);
    return true;
}

esp_err_t orchestrator_timeline_init(void)
{
    if (!s_timeline.ready) {
        s_timeline.lock = xSemaphoreCreateMutexStatic(&s_timeline.lock_storage);
        if (!s_timeline.lock) {
            return ESP_ERR_NO_MEM;
        }
        memset(s_timeline_entries, 0, sizeof(s_timeline_entries));
        memset(s_state_cache, 0, sizeof(s_state_cache));
        s_timeline.head = 0;
        s_timeline.count = 0;
        s_timeline.ready = true;
    }

    if (!s_timeline.handler_registered) {
        esp_err_t err = event_bus_register_handler(timeline_handle_event);
        if (err != ESP_OK) {
            return err;
        }
        s_timeline.handler_registered = true;
    }
    return ESP_OK;
}

esp_err_t orchestrator_timeline_log(orchestrator_timeline_type_t type,
                                    orchestrator_timeline_severity_t severity,
                                    const char *source,
                                    const char *room_id,
                                    const char *device_id,
                                    const char *title,
                                    const char *details)
{
    orchestrator_timeline_entry_t *entry = NULL;
    esp_err_t err = orchestrator_timeline_init();
    if (err != ESP_OK) {
        return err;
    }
    if (xSemaphoreTake(s_timeline.lock, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    entry = &s_timeline_entries[s_timeline.head];
    memset(entry, 0, sizeof(*entry));
    entry->timestamp_ms = timeline_now_ms();
    entry->type = type;
    timeline_str_copy(entry->type_text, sizeof(entry->type_text), timeline_type_str(type));
    entry->severity = severity;
    timeline_str_copy(entry->severity_text, sizeof(entry->severity_text), timeline_severity_str(severity));
    timeline_str_copy(entry->source, sizeof(entry->source), source && source[0] ? source : "system");
    timeline_str_copy(entry->room_id, sizeof(entry->room_id), room_id);
    timeline_str_copy(entry->device_id, sizeof(entry->device_id), device_id);
    timeline_str_copy(entry->title, sizeof(entry->title), title && title[0] ? title : "Event");
    timeline_str_copy(entry->details, sizeof(entry->details), details);

    s_timeline.head = (s_timeline.head + 1) % ORCH_TIMELINE_CAPACITY;
    if (s_timeline.count < ORCH_TIMELINE_CAPACITY) {
        s_timeline.count++;
    }
    xSemaphoreGive(s_timeline.lock);
    return ESP_OK;
}

esp_err_t orchestrator_timeline_list_recent(size_t max_items,
                                            orchestrator_timeline_entry_t *out_items,
                                            size_t *out_count)
{
    size_t limit = 0;
    if (!out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    esp_err_t err = orchestrator_timeline_init();
    if (err != ESP_OK) {
        return err;
    }
    if (max_items > 0 && !out_items) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_timeline.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    limit = s_timeline.count;
    if (max_items > 0 && limit > max_items) {
        limit = max_items;
    }
    for (size_t i = 0; i < limit; ++i) {
        size_t idx = (s_timeline.head + ORCH_TIMELINE_CAPACITY - 1 - i) % ORCH_TIMELINE_CAPACITY;
        out_items[i] = s_timeline_entries[idx];
    }
    *out_count = limit;
    xSemaphoreGive(s_timeline.lock);
    return ESP_OK;
}

void orchestrator_timeline_reset(void)
{
    if (!s_timeline.ready) {
        return;
    }
    if (xSemaphoreTake(s_timeline.lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    memset(s_timeline_entries, 0, sizeof(s_timeline_entries));
    memset(s_state_cache, 0, sizeof(s_state_cache));
    s_timeline.head = 0;
    s_timeline.count = 0;
    xSemaphoreGive(s_timeline.lock);
}

static void timeline_log_device_status(const scenehub_event_t *message)
{
    char details[ORCH_TIMELINE_DETAILS_MAX_LEN] = {0};
    const scenehub_event_device_status_payload_t *status = &message->data.device_status;
    const char *connectivity = status->connectivity[0] ? status->connectivity : "unknown";
    const char *health = status->health[0] ? status->health : "unknown";
    const char *state = status->state[0] ? status->state : "unknown";
    const char *title = (strcmp(connectivity, "online") == 0) ? "Device online" :
                        ((strcmp(connectivity, "offline") == 0) ? "Device offline" : "Device status");
    snprintf(details, sizeof(details), "%s / %s / %s", connectivity, health, state);
    if (!timeline_state_changed(status->device_id, "status", details)) {
        return;
    }
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_DEVICE_STATUS,
                                    timeline_status_severity(connectivity, health),
                                    "timeline",
                                    "",
                                    status->device_id,
                                    title,
                                    details);
}

static void timeline_log_runtime_changed(const scenehub_event_t *message)
{
    char details[ORCH_TIMELINE_DETAILS_MAX_LEN] = {0};
    const scenehub_event_device_runtime_payload_t *runtime = &message->data.device_runtime;
    if (strcmp(runtime->runtime_type, "control_status") == 0) {
        return;
    }
    snprintf(details,
             sizeof(details),
             "%s / %s / %s",
             runtime->runtime_type[0] ? runtime->runtime_type : "runtime",
             runtime->state[0] ? runtime->state : "unknown",
             runtime->active ? "active" : "idle");
    if (!timeline_state_changed(runtime->device_id, runtime->runtime_type, details)) {
        return;
    }
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_RUNTIME_CHANGED,
                                    ORCH_TIMELINE_SEVERITY_INFO,
                                    "timeline",
                                    "",
                                    runtime->device_id,
                                    "Runtime changed",
                                    details);
}

static void timeline_log_device_control(const scenehub_event_t *message)
{
    char details[ORCH_TIMELINE_DETAILS_MAX_LEN] = {0};
    const scenehub_event_device_control_payload_t *control = &message->data.device_control;
    snprintf(details,
             sizeof(details),
             "%s / %s",
             control->action_id[0] ? control->action_id : "action",
             control->source[0] ? control->source : "unknown");
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_DEVICE_ACTION,
                                    ORCH_TIMELINE_SEVERITY_INFO,
                                    "timeline",
                                    "",
                                    control->device_id,
                                    "Device control",
                                    details);
}

static void timeline_process_event(const scenehub_event_t *message)
{
    char details[ORCH_TIMELINE_DETAILS_MAX_LEN] = {0};
    if (!message) {
        return;
    }

    switch (message->type) {
    case SCENEHUB_EVENT_DEVICE_STATUS:
        if (scenehub_event_is_device_status(message)) {
            timeline_log_device_status(message);
        }
        break;
    case SCENEHUB_EVENT_DEVICE_RUNTIME:
        if (scenehub_event_is_device_runtime(message)) {
            timeline_log_runtime_changed(message);
        }
        break;
    case SCENEHUB_EVENT_DEVICE_CONTROL:
        if (scenehub_event_is_device_control(message)) {
            timeline_log_device_control(message);
        }
        break;
    case SCENEHUB_EVENT_SCENARIO_TRIGGER:
        timeline_str_copy(details, sizeof(details), message->payload);
        (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_SCENARIO_TRIGGERED,
                                        ORCH_TIMELINE_SEVERITY_INFO,
                                        "timeline",
                                        "",
                                        message->topic,
                                        "Scenario triggered",
                                        details);
        break;
    case SCENEHUB_EVENT_RUNTIME_CONTROL:
        timeline_details_topic_payload(details, sizeof(details), message->topic, message->payload);
        (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_EVENT,
                                        ORCH_TIMELINE_SEVERITY_INFO,
                                        "timeline",
                                        "",
                                        "",
                                        "Runtime control",
                                        details);
        break;
    case SCENEHUB_EVENT_DEVICE_CONFIG_CHANGED:
        (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_CONFIG_CHANGED,
                                        ORCH_TIMELINE_SEVERITY_INFO,
                                        "timeline",
                                        "",
                                        "",
                                        "Device config changed",
                                        message->payload);
        break;
    default:
        break;
    }
}

static void timeline_handle_event(const scenehub_event_t *message)
{
    scenehub_event_t *copy = NULL;
    if (!message) {
        return;
    }
    copy = timeline_job_alloc();
    if (!copy) {
        return;
    }
    *copy = *message;
    if (event_bus_post_job(timeline_process_event_job, copy, 0) != ESP_OK) {
        timeline_job_free(copy);
    }
}

static void timeline_process_event_job(void *ctx)
{
    scenehub_event_t *message = (scenehub_event_t *)ctx;
    if (!message) {
        return;
    }
    timeline_process_event(message);
    timeline_job_free(message);
}
