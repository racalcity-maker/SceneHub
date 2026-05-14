#include "error_monitor.h"

#include <stddef.h>

#include "status_led.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "error_monitor";
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock = NULL;
static bool s_wifi_ok = false;
static bool s_sd_present = false;
static bool s_sd_fault = false;

#define ERROR_MONITOR_JOB_POOL_LEN 4

typedef enum {
    ERROR_MONITOR_EVENT_SD_OK = 0,
    ERROR_MONITOR_EVENT_SD_BAD,
} error_monitor_event_t;

typedef struct {
    bool in_use;
    error_monitor_event_t event;
} error_monitor_job_t;

static error_monitor_job_t s_job_pool[ERROR_MONITOR_JOB_POOL_LEN];
static portMUX_TYPE s_job_pool_lock = portMUX_INITIALIZER_UNLOCKED;

static void update_led_locked(void);
static void error_monitor_process_event_job(void *ctx);

static error_monitor_job_t *error_monitor_job_alloc(void)
{
    error_monitor_job_t *job = NULL;
    portENTER_CRITICAL(&s_job_pool_lock);
    for (size_t i = 0; i < ERROR_MONITOR_JOB_POOL_LEN; ++i) {
        if (!s_job_pool[i].in_use) {
            s_job_pool[i].in_use = true;
            job = &s_job_pool[i];
            break;
        }
    }
    portEXIT_CRITICAL(&s_job_pool_lock);
    return job;
}

static void error_monitor_job_free(error_monitor_job_t *job)
{
    if (!job) {
        return;
    }
    portENTER_CRITICAL(&s_job_pool_lock);
    job->in_use = false;
    portEXIT_CRITICAL(&s_job_pool_lock);
}

static void on_event(const scenehub_event_t *msg)
{
    error_monitor_job_t *job = NULL;
    if (!msg || !s_lock) {
        return;
    }
    switch (msg->type) {
    case SCENEHUB_EVENT_CARD_OK:
    case SCENEHUB_EVENT_CARD_BAD:
        break;
    default:
        return;
    }
    job = error_monitor_job_alloc();
    if (!job) {
        ESP_LOGW(TAG, "job pool full, dropped type=%s", scenehub_event_type_to_string(msg->type));
        return;
    }
    job->event = (msg->type == SCENEHUB_EVENT_CARD_OK) ? ERROR_MONITOR_EVENT_SD_OK
                                                       : ERROR_MONITOR_EVENT_SD_BAD;
    if (event_bus_post_job(error_monitor_process_event_job, job, 0) != ESP_OK) {
        error_monitor_job_free(job);
        ESP_LOGW(TAG, "job queue full, dropped type=%s", scenehub_event_type_to_string(msg->type));
    }
}

static void update_led_locked(void)
{
    status_led_pattern_t pattern = STATUS_LED_PATTERN_OFF;
    if (!s_sd_present || s_sd_fault) {
        pattern = STATUS_LED_PATTERN_BLINK_RED;
    } else if (!s_wifi_ok) {
        pattern = STATUS_LED_PATTERN_SOLID_RED;
    } else {
        pattern = STATUS_LED_PATTERN_SOLID_GREEN;
    }
    status_led_set_pattern(pattern);
}

static void error_monitor_process_event_job(void *ctx)
{
    error_monitor_job_t *job = (error_monitor_job_t *)ctx;
    if (!job || !s_lock) {
        error_monitor_job_free(job);
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (job->event == ERROR_MONITOR_EVENT_SD_OK) {
        s_sd_present = true;
        s_sd_fault = false;
    } else {
        s_sd_present = false;
        s_sd_fault = true;
    }
    update_led_locked();
    xSemaphoreGive(s_lock);
    error_monitor_job_free(job);
}

esp_err_t error_monitor_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_RETURN_ON_ERROR(status_led_init(), TAG, "led init");
    ESP_RETURN_ON_ERROR(event_bus_register_handler(on_event), TAG, "event reg");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_wifi_ok = false;
    s_sd_present = false;
    s_sd_fault = false;
    update_led_locked();
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "error monitor ready");
    return ESP_OK;
}

void error_monitor_set_wifi_connected(bool connected)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_wifi_ok = connected;
    update_led_locked();
    xSemaphoreGive(s_lock);
}

void error_monitor_report_sd_fault(void)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_sd_present = false;
    s_sd_fault = true;
    update_led_locked();
    xSemaphoreGive(s_lock);
}

void error_monitor_report_audio_fault(void)
{
    status_led_flash_warning(3);
}
