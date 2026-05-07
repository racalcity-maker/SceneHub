#include "command_executor_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "quest_common_utils.h"
#include "scenehub_command_result.h"

#define COMMAND_EXECUTOR_MAX_PENDING 24

typedef struct {
    bool in_use;
    uint64_t deadline_ms;
    char request_id[COMMAND_EXECUTOR_REQUEST_ID_MAX_LEN];
    char source_id[ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN];
    char command[COMMAND_EXECUTOR_COMMAND_MAX_LEN];
} command_executor_pending_t;

EXT_RAM_BSS_ATTR static command_executor_pending_t s_pending[COMMAND_EXECUTOR_MAX_PENDING];
static SemaphoreHandle_t s_pending_lock = NULL;
static StaticSemaphore_t s_pending_lock_storage;

static esp_err_t ce_pending_lock_init(void)
{
    if (!s_pending_lock) {
        s_pending_lock = xSemaphoreCreateMutexStatic(&s_pending_lock_storage);
        if (!s_pending_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static bool ce_time_reached(uint64_t now_ms, uint64_t deadline_ms)
{
    return deadline_ms > 0 && (int64_t)(now_ms - deadline_ms) >= 0;
}

esp_err_t command_executor_track_pending(const char *request_id,
                                         const char *source_id,
                                         const char *command,
                                         uint32_t timeout_ms)
{
    command_executor_pending_t *free_slot = NULL;
    uint64_t now_ms = command_executor_now_ms();
    esp_err_t err = ce_pending_lock_init();
    if (err != ESP_OK) {
        return err;
    }
    if (!request_id || !request_id[0] || !source_id || !source_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_pending_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < COMMAND_EXECUTOR_MAX_PENDING; ++i) {
        command_executor_pending_t *slot = &s_pending[i];
        if (slot->in_use && strcmp(slot->request_id, request_id) == 0) {
            free_slot = slot;
            break;
        }
        if (!slot->in_use && !free_slot) {
            free_slot = slot;
        }
    }
    if (!free_slot) {
        xSemaphoreGive(s_pending_lock);
        return ESP_ERR_NO_MEM;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->in_use = true;
    free_slot->deadline_ms = now_ms + (timeout_ms ? timeout_ms : QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS);
    quest_str_copy(free_slot->request_id, sizeof(free_slot->request_id), request_id);
    quest_str_copy(free_slot->source_id, sizeof(free_slot->source_id), source_id);
    quest_str_copy(free_slot->command, sizeof(free_slot->command), command);
    xSemaphoreGive(s_pending_lock);
    return ESP_OK;
}

void command_executor_clear_pending(const char *request_id)
{
    if (!request_id || !request_id[0] || ce_pending_lock_init() != ESP_OK) {
        return;
    }
    if (xSemaphoreTake(s_pending_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    for (size_t i = 0; i < COMMAND_EXECUTOR_MAX_PENDING; ++i) {
        if (s_pending[i].in_use && strcmp(s_pending[i].request_id, request_id) == 0) {
            memset(&s_pending[i], 0, sizeof(s_pending[i]));
            break;
        }
    }
    xSemaphoreGive(s_pending_lock);
}

void command_executor_cancel_request(const char *request_id)
{
    command_executor_clear_pending(request_id);
}

void command_executor_on_event(const event_bus_message_t *message)
{
    if (!message ||
        message->type != EVENT_DEVICE_CONTROL ||
        message->payload_type != EVENT_BUS_PAYLOAD_DEVICE_CONTROL ||
        strcmp(message->data.device_control.source, "result") != 0) {
        return;
    }
    if (!scenehub_command_result_is_terminal(message->payload)) {
        return;
    }
    command_executor_clear_pending(message->data.device_control.action_id);
}

size_t command_executor_poll_timeouts(event_bus_message_t *out_events, size_t max_events)
{
    size_t expired_count = 0;
    uint64_t now_ms = command_executor_now_ms();

    if (!out_events || max_events == 0 || ce_pending_lock_init() != ESP_OK) {
        return 0;
    }
    if (xSemaphoreTake(s_pending_lock, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    for (size_t i = 0; i < COMMAND_EXECUTOR_MAX_PENDING && expired_count < max_events; ++i) {
        if (!s_pending[i].in_use || !ce_time_reached(now_ms, s_pending[i].deadline_ms)) {
            continue;
        }
        event_bus_message_t *msg = &out_events[expired_count++];
        memset(msg, 0, sizeof(*msg));
        msg->type = EVENT_DEVICE_CONTROL;
        msg->payload_type = EVENT_BUS_PAYLOAD_DEVICE_CONTROL;
        quest_str_copy(msg->payload, sizeof(msg->payload), SCENEHUB_COMMAND_RESULT_TIMEOUT);
        quest_str_copy(msg->data.device_control.device_id,
                       sizeof(msg->data.device_control.device_id),
                       s_pending[i].source_id);
        quest_str_copy(msg->data.device_control.action_id,
                       sizeof(msg->data.device_control.action_id),
                       s_pending[i].request_id);
        quest_str_copy(msg->data.device_control.source,
                       sizeof(msg->data.device_control.source),
                       "result");
        msg->data.device_control.timestamp_ms = now_ms;
        memset(&s_pending[i], 0, sizeof(s_pending[i]));
    }
    xSemaphoreGive(s_pending_lock);
    return expired_count;
}

void command_executor_reset_pending(void)
{
    if (ce_pending_lock_init() != ESP_OK) {
        memset(s_pending, 0, sizeof(s_pending));
        return;
    }
    if (xSemaphoreTake(s_pending_lock, portMAX_DELAY) == pdTRUE) {
        memset(s_pending, 0, sizeof(s_pending));
        xSemaphoreGive(s_pending_lock);
    }
}
