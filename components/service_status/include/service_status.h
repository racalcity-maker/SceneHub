#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    SERVICE_STATUS_NETWORK = 0,
    SERVICE_STATUS_MQTT,
    SERVICE_STATUS_AUDIO,
    SERVICE_STATUS_WEB_UI,
    SERVICE_STATUS_EVENT_BUS,
    SERVICE_STATUS_HARDWARE_IO,
    SERVICE_STATUS_COUNT,
} service_status_id_t;

typedef struct {
    bool init_attempted;
    bool init_ok;
    bool start_attempted;
    bool start_ok;
    bool fault;
    esp_err_t last_error;
    uint32_t event_posted;
    uint32_t event_dispatched;
    uint32_t event_dropped;
    uint32_t event_queue_waiting;
    uint32_t event_slow_handlers;
    uint32_t event_max_handler_ms;
    uint32_t event_job_posted;
    uint32_t event_job_dispatched;
    uint32_t event_job_dropped;
    uint32_t event_job_queue_waiting;
} service_status_entry_t;

esp_err_t service_status_init(void);
void service_status_mark_init(service_status_id_t id, esp_err_t err);
void service_status_mark_start(service_status_id_t id, esp_err_t err);
void service_status_mark_fault(service_status_id_t id, esp_err_t err);
bool service_status_get(service_status_id_t id, service_status_entry_t *out);
const char *service_status_name(service_status_id_t id);
void service_status_update_event_bus(uint32_t posted,
                                     uint32_t dispatched,
                                     uint32_t dropped,
                                     uint32_t queue_waiting,
                                     uint32_t slow_handlers,
                                     uint32_t max_handler_ms,
                                     uint32_t job_posted,
                                     uint32_t job_dispatched,
                                     uint32_t job_dropped,
                                     uint32_t job_queue_waiting);
