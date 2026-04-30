#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "quest_common_limits.h"

typedef enum {
    EVENT_NONE = 0,
    EVENT_CARD_OK,
    EVENT_CARD_BAD,
    EVENT_RELAY_CMD,
    EVENT_AUDIO_PLAY,
    EVENT_AUDIO_FINISHED,
    EVENT_VOLUME_SET,
    EVENT_WEB_COMMAND,
    EVENT_SYSTEM_STATUS,
    EVENT_SCENARIO_TRIGGER,
    EVENT_DEVICE_CONFIG_CHANGED,
    EVENT_MQTT_MESSAGE,
    EVENT_FLAG_CHANGED,
    EVENT_DEVICE_STATUS,
    EVENT_DEVICE_RUNTIME,
    EVENT_DEVICE_CONTROL,
    EVENT_RUNTIME_CONTROL,
} event_bus_type_t;

typedef enum {
    EVENT_BUS_PRIORITY_NORMAL = 0,
    EVENT_BUS_PRIORITY_HIGH,
} event_bus_priority_t;

typedef enum {
    EVENT_BUS_PAYLOAD_TEXT = 0,
    EVENT_BUS_PAYLOAD_DEVICE_STATUS,
    EVENT_BUS_PAYLOAD_DEVICE_RUNTIME,
    EVENT_BUS_PAYLOAD_DEVICE_CONTROL,
} event_bus_payload_type_t;

typedef struct {
    char device_id[64];
    char connectivity[16];
    char health[16];
    char state[32];
    uint64_t timestamp_ms;
} event_bus_device_status_payload_t;

typedef struct {
    char device_id[64];
    char runtime_type[32];
    char state[32];
    bool active;
    uint64_t timestamp_ms;
} event_bus_device_runtime_payload_t;

typedef struct {
    char device_id[64];
    char action_id[96];
    char source[16];
    uint64_t timestamp_ms;
} event_bus_device_control_payload_t;

typedef struct {
    event_bus_type_t type;
    event_bus_priority_t priority;
    event_bus_payload_type_t payload_type;
    char topic[QUEST_TOPIC_MAX_LEN];
    char payload[256];
    union {
        event_bus_device_status_payload_t device_status;
        event_bus_device_runtime_payload_t device_runtime;
        event_bus_device_control_payload_t device_control;
    } data;
} event_bus_message_t;

// Handlers run on the event bus task. Keep them short; defer heavy work with event_bus_post_job().
typedef void (*event_bus_handler_t)(const event_bus_message_t *message);
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
esp_err_t event_bus_post(const event_bus_message_t *message, TickType_t timeout);
esp_err_t event_bus_post_priority(const event_bus_message_t *message,
                                  event_bus_priority_t priority,
                                  TickType_t timeout);
esp_err_t event_bus_post_job(event_bus_job_fn_t fn, void *ctx, TickType_t timeout);
esp_err_t event_bus_register_handler(event_bus_handler_t handler);
esp_err_t event_bus_get_stats(event_bus_stats_t *out);
