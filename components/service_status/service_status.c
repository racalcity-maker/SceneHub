#include "service_status.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    SemaphoreHandle_t mutex;
    StaticSemaphore_t mutex_storage;
    service_status_entry_t entries[SERVICE_STATUS_COUNT];
} service_status_state_t;

static service_status_state_t s_status = {0};

static bool service_status_lock(void)
{
    return s_status.mutex && xSemaphoreTake(s_status.mutex, portMAX_DELAY) == pdTRUE;
}

static void service_status_unlock(void)
{
    if (s_status.mutex) {
        xSemaphoreGive(s_status.mutex);
    }
}

esp_err_t service_status_init(void)
{
    if (!s_status.mutex) {
        s_status.mutex = xSemaphoreCreateMutexStatic(&s_status.mutex_storage);
        if (!s_status.mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (service_status_lock()) {
        memset(s_status.entries, 0, sizeof(s_status.entries));
        service_status_unlock();
    }
    return ESP_OK;
}

void service_status_mark_init(service_status_id_t id, esp_err_t err)
{
    if (id < 0 || id >= SERVICE_STATUS_COUNT) {
        return;
    }
    if (service_status_lock()) {
        s_status.entries[id].init_attempted = true;
        s_status.entries[id].init_ok = (err == ESP_OK);
        s_status.entries[id].last_error = err;
        if (err == ESP_OK) {
            s_status.entries[id].fault = false;
        } else {
            s_status.entries[id].fault = true;
        }
        if (err != ESP_OK) {
            s_status.entries[id].start_attempted = false;
            s_status.entries[id].start_ok = false;
        }
        service_status_unlock();
    }
}

void service_status_mark_start(service_status_id_t id, esp_err_t err)
{
    if (id < 0 || id >= SERVICE_STATUS_COUNT) {
        return;
    }
    if (service_status_lock()) {
        s_status.entries[id].start_attempted = true;
        s_status.entries[id].start_ok = (err == ESP_OK);
        s_status.entries[id].last_error = err;
        if (err == ESP_OK) {
            s_status.entries[id].fault = false;
        } else {
            s_status.entries[id].fault = true;
        }
        service_status_unlock();
    }
}

void service_status_mark_fault(service_status_id_t id, esp_err_t err)
{
    if (id < 0 || id >= SERVICE_STATUS_COUNT) {
        return;
    }
    if (service_status_lock()) {
        s_status.entries[id].fault = (err != ESP_OK);
        s_status.entries[id].last_error = err;
        service_status_unlock();
    }
}

bool service_status_get(service_status_id_t id, service_status_entry_t *out)
{
    if (id < 0 || id >= SERVICE_STATUS_COUNT || !out) {
        return false;
    }
    if (!service_status_lock()) {
        return false;
    }
    *out = s_status.entries[id];
    service_status_unlock();
    return true;
}

const char *service_status_name(service_status_id_t id)
{
    switch (id) {
    case SERVICE_STATUS_NETWORK:
        return "network";
    case SERVICE_STATUS_MQTT:
        return "mqtt";
    case SERVICE_STATUS_AUDIO:
        return "audio";
    case SERVICE_STATUS_WEB_UI:
        return "web_ui";
    case SERVICE_STATUS_EVENT_BUS:
        return "event_bus";
    case SERVICE_STATUS_HARDWARE_IO:
        return "hardware_io";
    default:
        return "unknown";
    }
}

void service_status_update_event_bus(uint32_t posted,
                                     uint32_t dispatched,
                                     uint32_t dropped,
                                     uint32_t queue_waiting,
                                     uint32_t slow_handlers,
                                     uint32_t max_handler_ms,
                                     uint32_t job_posted,
                                     uint32_t job_dispatched,
                                     uint32_t job_dropped,
                                     uint32_t job_queue_waiting)
{
    if (service_status_lock()) {
        service_status_entry_t *entry = &s_status.entries[SERVICE_STATUS_EVENT_BUS];
        entry->event_posted = posted;
        entry->event_dispatched = dispatched;
        entry->event_dropped = dropped;
        entry->event_queue_waiting = queue_waiting;
        entry->event_slow_handlers = slow_handlers;
        entry->event_max_handler_ms = max_handler_ms;
        entry->event_job_posted = job_posted;
        entry->event_job_dispatched = job_dispatched;
        entry->event_job_dropped = job_dropped;
        entry->event_job_queue_waiting = job_queue_waiting;
        service_status_unlock();
    }
}
