#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "scenehub_events.h"

// Handlers run on the event bus task. Keep them short; defer heavy work with event_bus_post_job().
typedef void (*event_bus_handler_t)(const scenehub_event_t *message);
typedef void (*event_bus_job_fn_t)(void *ctx);

typedef struct {
    uint32_t posted;
    uint32_t dispatched;
    uint32_t dropped;
    uint32_t queue_waiting;
    uint32_t handler_count;
    uint32_t slow_handler_count;
    uint32_t max_handler_ms;
    uint32_t job_posted;
    uint32_t job_dispatched;
    uint32_t job_dropped;
    uint32_t job_queue_waiting;
} event_bus_stats_t;

esp_err_t event_bus_init(void);
esp_err_t event_bus_start(void);
esp_err_t event_bus_post(const scenehub_event_t *message, TickType_t timeout);
esp_err_t event_bus_post_priority(const scenehub_event_t *message,
                                  event_bus_priority_t priority,
                                  TickType_t timeout);
esp_err_t event_bus_post_job(event_bus_job_fn_t fn, void *ctx, TickType_t timeout);
esp_err_t event_bus_register_handler(event_bus_handler_t handler);
esp_err_t event_bus_get_stats(event_bus_stats_t *out);
