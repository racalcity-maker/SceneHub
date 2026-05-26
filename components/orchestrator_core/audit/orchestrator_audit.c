#include "orchestrator_audit.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lock_storage;
    size_t head;
    size_t count;
    bool ready;
} orchestrator_audit_state_t;

EXT_RAM_BSS_ATTR static orchestrator_audit_entry_t s_audit_entries[ORCH_AUDIT_CAPACITY];
static orchestrator_audit_state_t s_audit = {0};
static portMUX_TYPE s_audit_lock_init_lock = portMUX_INITIALIZER_UNLOCKED;

static void audit_str_copy(char *dst, size_t dst_size, const char *src)
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

static uint64_t audit_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

esp_err_t orchestrator_audit_init(void)
{
    if (s_audit.ready) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_audit_lock_init_lock);
    if (!s_audit.lock) {
        s_audit.lock = xSemaphoreCreateMutexStatic(&s_audit.lock_storage);
    }
    portEXIT_CRITICAL(&s_audit_lock_init_lock);
    if (!s_audit.lock) {
        return ESP_ERR_NO_MEM;
    }
    memset(s_audit_entries, 0, sizeof(s_audit_entries));
    s_audit.head = 0;
    s_audit.count = 0;
    s_audit.ready = true;
    return ESP_OK;
}

esp_err_t orchestrator_audit_log_device_action(const char *source,
                                               const char *device_id,
                                               const char *action_id,
                                               const char *request_id,
                                               bool success,
                                               const char *error_code)
{
    orchestrator_audit_entry_t *entry = NULL;
    if (!source || !source[0] || !device_id || !device_id[0] || !action_id || !action_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = orchestrator_audit_init();
    if (err != ESP_OK) {
        return err;
    }
    if (xSemaphoreTake(s_audit.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    entry = &s_audit_entries[s_audit.head];
    memset(entry, 0, sizeof(*entry));
    entry->timestamp_ms = audit_now_ms();
    audit_str_copy(entry->source, sizeof(entry->source), source);
    audit_str_copy(entry->device_id, sizeof(entry->device_id), device_id);
    audit_str_copy(entry->action_id, sizeof(entry->action_id), action_id);
    audit_str_copy(entry->request_id, sizeof(entry->request_id), request_id);
    entry->success = success;
    audit_str_copy(entry->error_code, sizeof(entry->error_code), error_code);

    s_audit.head = (s_audit.head + 1) % ORCH_AUDIT_CAPACITY;
    if (s_audit.count < ORCH_AUDIT_CAPACITY) {
        s_audit.count++;
    }
    xSemaphoreGive(s_audit.lock);
    return ESP_OK;
}

esp_err_t orchestrator_audit_list_recent(size_t max_items,
                                         orchestrator_audit_entry_t *out_items,
                                         size_t *out_count)
{
    size_t limit = 0;
    if (!out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    esp_err_t err = orchestrator_audit_init();
    if (err != ESP_OK) {
        return err;
    }
    if (max_items > 0 && !out_items) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_audit.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    limit = s_audit.count;
    if (max_items > 0 && limit > max_items) {
        limit = max_items;
    }
    for (size_t i = 0; i < limit; ++i) {
        size_t idx = (s_audit.head + ORCH_AUDIT_CAPACITY - 1 - i) % ORCH_AUDIT_CAPACITY;
        out_items[i] = s_audit_entries[idx];
    }
    *out_count = limit;
    xSemaphoreGive(s_audit.lock);
    return ESP_OK;
}

void orchestrator_audit_reset(void)
{
    if (!s_audit.ready) {
        return;
    }
    if (xSemaphoreTake(s_audit.lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    memset(s_audit_entries, 0, sizeof(s_audit_entries));
    s_audit.head = 0;
    s_audit.count = 0;
    xSemaphoreGive(s_audit.lock);
}
