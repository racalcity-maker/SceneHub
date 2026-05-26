#include "event_bus.h"

#include <stddef.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "service_status.h"

#ifndef CONFIG_SCENEHUB_EVENT_BUS_SKIP_EVENT_VALIDATION
#define CONFIG_SCENEHUB_EVENT_BUS_SKIP_EVENT_VALIDATION 0
#endif

#define EVENT_BUS_QUEUE_LEN 128
#define EVENT_BUS_JOB_QUEUE_LEN 32
#define EVENT_BUS_MAX_HANDLERS 12
#define EVENT_BUS_HANDLER_WARN_MS 20
#define EVENT_BUS_JOB_TASK_STACK_BYTES 8192
#define EVENT_BUS_STATUS_PUBLISH_MIN_US 500000ULL

typedef struct {
    event_bus_job_fn_t fn;
    void *ctx;
} event_bus_job_t;

static const char *TAG = "event_bus";

static QueueHandle_t s_queue = NULL;
static QueueHandle_t s_job_queue = NULL;
static TaskHandle_t s_task = NULL;
static TaskHandle_t s_job_task = NULL;
static EXT_RAM_BSS_ATTR StackType_t s_job_task_stack[EVENT_BUS_JOB_TASK_STACK_BYTES];
static StaticTask_t s_job_task_tcb;
static bool s_initialized = false;
static scenehub_event_t *s_message_pool = NULL;
static bool s_message_pool_in_use[EVENT_BUS_QUEUE_LEN];

static event_bus_handler_t s_handlers[EVENT_BUS_MAX_HANDLERS];
static size_t s_handler_count = 0;
static portMUX_TYPE s_handler_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_pool_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t s_posted_count = 0;
static uint32_t s_dispatched_count = 0;
static uint32_t s_drop_count = 0;
static uint32_t s_slow_handler_count = 0;
static uint32_t s_max_handler_ms = 0;
static uint32_t s_job_posted_count = 0;
static uint32_t s_job_dispatched_count = 0;
static uint32_t s_job_drop_count = 0;
static uint64_t s_last_status_publish_us = 0;

static uint32_t queue_waiting(QueueHandle_t queue)
{
    if (!queue) {
        return 0;
    }
    return (uint32_t)uxQueueMessagesWaiting(queue);
}

static esp_err_t ensure_message_pool(void)
{
    if (s_message_pool) {
        return ESP_OK;
    }

    s_message_pool = heap_caps_calloc(EVENT_BUS_QUEUE_LEN,
                                      sizeof(scenehub_event_t),
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_message_pool) {
        ESP_LOGI(TAG, "event message pool allocated in PSRAM (%u bytes)",
                 (unsigned)(EVENT_BUS_QUEUE_LEN * sizeof(scenehub_event_t)));
        return ESP_OK;
    }

    s_message_pool = heap_caps_calloc(EVENT_BUS_QUEUE_LEN,
                                      sizeof(scenehub_event_t),
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_message_pool) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGW(TAG, "event message pool allocated in internal RAM (%u bytes)",
             (unsigned)(EVENT_BUS_QUEUE_LEN * sizeof(scenehub_event_t)));
    return ESP_OK;
}

static scenehub_event_t *message_pool_alloc(void)
{
    scenehub_event_t *slot = NULL;

    if (!s_message_pool) {
        return NULL;
    }

    portENTER_CRITICAL(&s_pool_lock);
    for (size_t i = 0; i < EVENT_BUS_QUEUE_LEN; ++i) {
        if (!s_message_pool_in_use[i]) {
            s_message_pool_in_use[i] = true;
            slot = &s_message_pool[i];
            break;
        }
    }
    portEXIT_CRITICAL(&s_pool_lock);

    if (slot) {
        memset(slot, 0, sizeof(*slot));
    }
    return slot;
}

static void message_pool_free(scenehub_event_t *message)
{
    if (!message || !s_message_pool) {
        return;
    }

    ptrdiff_t index = message - s_message_pool;
    if (index < 0 || index >= EVENT_BUS_QUEUE_LEN) {
        return;
    }

    portENTER_CRITICAL(&s_pool_lock);
    s_message_pool_in_use[index] = false;
    portEXIT_CRITICAL(&s_pool_lock);
}

static void stats_increment(uint32_t *counter)
{
    portENTER_CRITICAL(&s_stats_lock);
    (*counter)++;
    portEXIT_CRITICAL(&s_stats_lock);
}

static void stats_record_handler_duration(uint32_t elapsed_ms)
{
    portENTER_CRITICAL(&s_stats_lock);
    if (elapsed_ms > s_max_handler_ms) {
        s_max_handler_ms = elapsed_ms;
    }
    if (elapsed_ms > EVENT_BUS_HANDLER_WARN_MS) {
        s_slow_handler_count++;
    }
    portEXIT_CRITICAL(&s_stats_lock);
}

esp_err_t event_bus_get_stats(event_bus_stats_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_stats_lock);
    out->posted = s_posted_count;
    out->dispatched = s_dispatched_count;
    out->dropped = s_drop_count;
    out->slow_handler_count = s_slow_handler_count;
    out->max_handler_ms = s_max_handler_ms;
    out->job_posted = s_job_posted_count;
    out->job_dispatched = s_job_dispatched_count;
    out->job_dropped = s_job_drop_count;
    portEXIT_CRITICAL(&s_stats_lock);

    out->queue_waiting = queue_waiting(s_queue);
    out->job_queue_waiting = queue_waiting(s_job_queue);

    portENTER_CRITICAL(&s_handler_lock);
    out->handler_count = (uint32_t)s_handler_count;
    portEXIT_CRITICAL(&s_handler_lock);

    return ESP_OK;
}

static void publish_status_snapshot(void)
{
    event_bus_stats_t stats = {0};
    if (event_bus_get_stats(&stats) != ESP_OK) {
        return;
    }

    service_status_update_event_bus(stats.posted,
                                    stats.dispatched,
                                    stats.dropped,
                                    stats.queue_waiting,
                                    stats.slow_handler_count,
                                    stats.max_handler_ms,
                                    stats.job_posted,
                                    stats.job_dispatched,
                                    stats.job_dropped,
                                    stats.job_queue_waiting);
}

static void publish_status_now(void)
{
    portENTER_CRITICAL(&s_stats_lock);
    s_last_status_publish_us = (uint64_t)esp_timer_get_time();
    portEXIT_CRITICAL(&s_stats_lock);
    publish_status_snapshot();
}

static void publish_status_if_due(void)
{
    uint64_t now_us = (uint64_t)esp_timer_get_time();
    bool should_publish = false;

    portENTER_CRITICAL(&s_stats_lock);
    if (s_last_status_publish_us == 0 ||
        now_us <= s_last_status_publish_us ||
        (now_us - s_last_status_publish_us) >= EVENT_BUS_STATUS_PUBLISH_MIN_US) {
        s_last_status_publish_us = now_us;
        should_publish = true;
    }
    portEXIT_CRITICAL(&s_stats_lock);

    if (should_publish) {
        publish_status_snapshot();
    }
}

static void dispatch_message(const scenehub_event_t *message)
{
    event_bus_handler_t handlers[EVENT_BUS_MAX_HANDLERS] = {0};
    size_t handler_count = 0;

    portENTER_CRITICAL(&s_handler_lock);
    handler_count = s_handler_count;
    if (handler_count > EVENT_BUS_MAX_HANDLERS) {
        handler_count = EVENT_BUS_MAX_HANDLERS;
    }
    memcpy(handlers, s_handlers, handler_count * sizeof(handlers[0]));
    portEXIT_CRITICAL(&s_handler_lock);

    for (size_t i = 0; i < handler_count; ++i) {
        if (handlers[i]) {
            int64_t start_us = esp_timer_get_time();
            handlers[i](message);
            uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
            stats_record_handler_duration(elapsed_ms);
            if (elapsed_ms > EVENT_BUS_HANDLER_WARN_MS) {
                ESP_LOGW(TAG, "slow handler type=%s elapsed_ms=%lu",
                         scenehub_event_type_to_string(message ? message->type : SCENEHUB_EVENT_NONE),
                         (unsigned long)elapsed_ms);
            }
        }
    }
}

static void event_bus_task(void *param)
{
    (void)param;

    scenehub_event_t *message = NULL;
    while (true) {
        if (xQueueReceive(s_queue, &message, portMAX_DELAY) == pdTRUE) {
            dispatch_message(message);
            message_pool_free(message);
            message = NULL;
            stats_increment(&s_dispatched_count);
            publish_status_if_due();
        }
    }
}

static void event_bus_job_task(void *param)
{
    (void)param;

    event_bus_job_t job;
    while (true) {
        if (xQueueReceive(s_job_queue, &job, portMAX_DELAY) == pdTRUE) {
            if (job.fn) {
                job.fn(job.ctx);
            }
            stats_increment(&s_job_dispatched_count);
            publish_status_if_due();
        }
    }
}

esp_err_t event_bus_init(void)
{
    if (s_initialized) {
        publish_status_now();
        return ESP_OK;
    }

    esp_err_t pool_err = ensure_message_pool();
    if (pool_err != ESP_OK) {
        return pool_err;
    }

    if (!s_queue) {
        s_queue = xQueueCreateWithCaps(EVENT_BUS_QUEUE_LEN,
                                       sizeof(scenehub_event_t *),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_queue) {
            s_queue = xQueueCreate(EVENT_BUS_QUEUE_LEN, sizeof(scenehub_event_t *));
        }
        if (!s_queue) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_job_queue) {
        s_job_queue = xQueueCreateWithCaps(EVENT_BUS_JOB_QUEUE_LEN,
                                           sizeof(event_bus_job_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_job_queue) {
            s_job_queue = xQueueCreate(EVENT_BUS_JOB_QUEUE_LEN, sizeof(event_bus_job_t));
        }
        if (!s_job_queue) {
            return ESP_ERR_NO_MEM;
        }
    }

    portENTER_CRITICAL(&s_pool_lock);
    memset(s_message_pool_in_use, 0, sizeof(s_message_pool_in_use));
    portEXIT_CRITICAL(&s_pool_lock);

    portENTER_CRITICAL(&s_handler_lock);
    memset(s_handlers, 0, sizeof(s_handlers));
    s_handler_count = 0;
    portEXIT_CRITICAL(&s_handler_lock);

    portENTER_CRITICAL(&s_stats_lock);
    s_posted_count = 0;
    s_dispatched_count = 0;
    s_drop_count = 0;
    s_slow_handler_count = 0;
    s_max_handler_ms = 0;
    s_job_posted_count = 0;
    s_job_dispatched_count = 0;
    s_job_drop_count = 0;
    s_last_status_publish_us = 0;
    portEXIT_CRITICAL(&s_stats_lock);

    publish_status_now();
    s_initialized = true;
    return ESP_OK;
}

esp_err_t event_bus_start(void)
{
    if (!s_queue || !s_job_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_task) {
        BaseType_t ok = xTaskCreate(event_bus_task, "event_bus", 4096, NULL, 10, &s_task);
        if (ok != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_job_task) {
        s_job_task = xTaskCreateStatic(event_bus_job_task,
                                       "event_bus_job",
                                       EVENT_BUS_JOB_TASK_STACK_BYTES,
                                       NULL,
                                       8,
                                       s_job_task_stack,
                                       &s_job_task_tcb);
        if (!s_job_task) {
            return ESP_ERR_NO_MEM;
        }
    }

    publish_status_now();
    return ESP_OK;
}

esp_err_t event_bus_post(const scenehub_event_t *message, TickType_t timeout)
{
    event_bus_priority_t priority = EVENT_BUS_PRIORITY_NORMAL;
    if (message) {
        priority = message->priority;
    }
    if (priority != EVENT_BUS_PRIORITY_HIGH) {
        priority = EVENT_BUS_PRIORITY_NORMAL;
    }
    return event_bus_post_priority(message, priority, timeout);
}

esp_err_t event_bus_post_priority(const scenehub_event_t *message,
                                  event_bus_priority_t priority,
                                  TickType_t timeout)
{
    if (!message) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
#if !CONFIG_SCENEHUB_EVENT_BUS_SKIP_EVENT_VALIDATION
    if (!scenehub_event_is_valid(message)) {
        stats_increment(&s_drop_count);
        ESP_LOGW(TAG,
                 "drop invalid event type=%s payload_type=%s topic=%s",
                 scenehub_event_type_to_string(message->type),
                 scenehub_event_payload_type_to_string(message->payload_type),
                 message->topic);
        publish_status_now();
        return ESP_ERR_INVALID_ARG;
    }
#endif

    scenehub_event_t *queued = message_pool_alloc();
    if (!queued) {
        stats_increment(&s_drop_count);
        ESP_LOGW(TAG, "drop event type=%s topic=%s: message pool exhausted",
                 scenehub_event_type_to_string(message->type),
                 message->topic);
        publish_status_now();
        return ESP_ERR_NO_MEM;
    }

    *queued = *message;
    if (priority != EVENT_BUS_PRIORITY_HIGH) {
        priority = EVENT_BUS_PRIORITY_NORMAL;
    }
    queued->priority = priority;

    BaseType_t ok = pdFALSE;
    if (priority == EVENT_BUS_PRIORITY_HIGH) {
        ok = xQueueSendToFront(s_queue, &queued, timeout);
    } else {
        ok = xQueueSend(s_queue, &queued, timeout);
    }

    if (ok != pdTRUE) {
        scenehub_event_type_t type = queued->type;
        const char *topic = queued->topic;
        stats_increment(&s_drop_count);
        ESP_LOGW(TAG, "drop event type=%s topic=%s", scenehub_event_type_to_string(type), topic);
        message_pool_free(queued);
        publish_status_now();
        return ESP_ERR_TIMEOUT;
    }

    stats_increment(&s_posted_count);
    publish_status_if_due();
    return ESP_OK;
}

esp_err_t event_bus_post_job(event_bus_job_fn_t fn, void *ctx, TickType_t timeout)
{
    if (!fn) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_job_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    event_bus_job_t job = {
        .fn = fn,
        .ctx = ctx,
    };

    if (xQueueSend(s_job_queue, &job, timeout) != pdTRUE) {
        stats_increment(&s_job_drop_count);
        ESP_LOGW(TAG, "drop event bus job");
        publish_status_now();
        return ESP_ERR_TIMEOUT;
    }

    stats_increment(&s_job_posted_count);
    publish_status_if_due();
    return ESP_OK;
}

esp_err_t event_bus_register_handler(event_bus_handler_t handler)
{
    if (!handler) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_handler_lock);
    for (size_t i = 0; i < s_handler_count; ++i) {
        if (s_handlers[i] == handler) {
            portEXIT_CRITICAL(&s_handler_lock);
            publish_status_now();
            return ESP_OK;
        }
    }
    if (s_handler_count >= EVENT_BUS_MAX_HANDLERS) {
        portEXIT_CRITICAL(&s_handler_lock);
        return ESP_ERR_NO_MEM;
    }

    s_handlers[s_handler_count++] = handler;
    portEXIT_CRITICAL(&s_handler_lock);

    publish_status_now();
    return ESP_OK;
}
