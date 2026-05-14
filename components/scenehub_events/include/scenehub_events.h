#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "quest_common_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCENEHUB_EVENT_DEVICE_ID_MAX_LEN                64
#define SCENEHUB_EVENT_CONNECTIVITY_MAX_LEN             16
#define SCENEHUB_EVENT_HEALTH_MAX_LEN                   16
#define SCENEHUB_EVENT_STATE_MAX_LEN                    32
#define SCENEHUB_EVENT_RUNTIME_TYPE_MAX_LEN             32
#define SCENEHUB_EVENT_ACTION_ID_MAX_LEN                96
#define SCENEHUB_EVENT_DEVICE_CONTROL_SOURCE_MAX_LEN    16
#define SCENEHUB_EVENT_TEXT_PAYLOAD_MAX_LEN             256

#define SCENEHUB_DEVICE_CONTROL_SOURCE_EVENT            "event"
#define SCENEHUB_DEVICE_CONTROL_SOURCE_RESULT           "result"

typedef enum {
    SCENEHUB_EVENT_NONE = 0,
    SCENEHUB_EVENT_CARD_OK,
    SCENEHUB_EVENT_CARD_BAD,
    SCENEHUB_EVENT_RELAY_CMD,
    SCENEHUB_EVENT_AUDIO_PLAY,
    SCENEHUB_EVENT_AUDIO_FINISHED,
    SCENEHUB_EVENT_VOLUME_SET,
    SCENEHUB_EVENT_WEB_COMMAND,
    SCENEHUB_EVENT_SYSTEM_STATUS,
    SCENEHUB_EVENT_SCENARIO_TRIGGER,
    SCENEHUB_EVENT_DEVICE_CONFIG_CHANGED,
    SCENEHUB_EVENT_MQTT_MESSAGE,
    SCENEHUB_EVENT_FLAG_CHANGED,
    SCENEHUB_EVENT_DEVICE_STATUS,
    SCENEHUB_EVENT_DEVICE_RUNTIME,
    SCENEHUB_EVENT_DEVICE_CONTROL,
    SCENEHUB_EVENT_RUNTIME_CONTROL,
} scenehub_event_type_t;

typedef enum {
    EVENT_BUS_PRIORITY_NORMAL = 0,
    EVENT_BUS_PRIORITY_HIGH,
} event_bus_priority_t;

typedef enum {
    SCENEHUB_EVENT_PAYLOAD_TEXT = 0,
    SCENEHUB_EVENT_PAYLOAD_DEVICE_STATUS,
    SCENEHUB_EVENT_PAYLOAD_DEVICE_RUNTIME,
    SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL,
} scenehub_event_payload_type_t;

typedef struct {
    char device_id[SCENEHUB_EVENT_DEVICE_ID_MAX_LEN];
    char connectivity[SCENEHUB_EVENT_CONNECTIVITY_MAX_LEN];
    char health[SCENEHUB_EVENT_HEALTH_MAX_LEN];
    char state[SCENEHUB_EVENT_STATE_MAX_LEN];
    uint64_t timestamp_ms;
} scenehub_event_device_status_payload_t;

typedef struct {
    char device_id[SCENEHUB_EVENT_DEVICE_ID_MAX_LEN];
    char runtime_type[SCENEHUB_EVENT_RUNTIME_TYPE_MAX_LEN];
    char state[SCENEHUB_EVENT_STATE_MAX_LEN];
    bool active;
    uint64_t timestamp_ms;
} scenehub_event_device_runtime_payload_t;

typedef struct {
    char device_id[SCENEHUB_EVENT_DEVICE_ID_MAX_LEN];
    char action_id[SCENEHUB_EVENT_ACTION_ID_MAX_LEN];
    char source[SCENEHUB_EVENT_DEVICE_CONTROL_SOURCE_MAX_LEN];
    char args_json[QUEST_PAYLOAD_MAX_LEN];
    uint64_t timestamp_ms;
} scenehub_event_device_control_payload_t;

typedef struct {
    scenehub_event_type_t type;
    // Transport-owned compatibility field; delivery policy stays in event_bus.
    event_bus_priority_t priority;
    scenehub_event_payload_type_t payload_type;
    char topic[QUEST_TOPIC_MAX_LEN];
    char payload[SCENEHUB_EVENT_TEXT_PAYLOAD_MAX_LEN];
    union {
        scenehub_event_device_status_payload_t device_status;
        scenehub_event_device_runtime_payload_t device_runtime;
        scenehub_event_device_control_payload_t device_control;
    } data;
} scenehub_event_t;

const char *scenehub_event_type_to_string(scenehub_event_type_t type);
const char *scenehub_event_payload_type_to_string(scenehub_event_payload_type_t type);
const char *scenehub_event_source_id(const scenehub_event_t *event);

bool scenehub_event_is_valid(const scenehub_event_t *event);
bool scenehub_event_is_device_status(const scenehub_event_t *event);
bool scenehub_event_is_device_runtime(const scenehub_event_t *event);
bool scenehub_event_is_device_control(const scenehub_event_t *event);
bool scenehub_event_is_device_control_event(const scenehub_event_t *event);
bool scenehub_event_is_device_control_result(const scenehub_event_t *event);
bool scenehub_event_matches_device(const scenehub_event_t *event, const char *device_id);
bool scenehub_event_matches_action(const scenehub_event_t *event, const char *action_id);

esp_err_t scenehub_event_make_text(scenehub_event_t *out,
                                   scenehub_event_type_t type,
                                   const char *topic,
                                   const char *payload);
esp_err_t scenehub_event_make_flag_changed(scenehub_event_t *out,
                                           const char *flag_name,
                                           bool value);
esp_err_t scenehub_event_make_device_status(scenehub_event_t *out,
                                            const char *device_id,
                                            const char *connectivity,
                                            const char *health,
                                            const char *state,
                                            uint64_t timestamp_ms);
esp_err_t scenehub_event_make_device_runtime(scenehub_event_t *out,
                                             const char *device_id,
                                             const char *runtime_type,
                                             const char *state,
                                             bool active,
                                             uint64_t timestamp_ms);
esp_err_t scenehub_event_make_device_control_event(scenehub_event_t *out,
                                                   const char *device_id,
                                                   const char *action_id,
                                                   const char *args_json,
                                                   uint64_t timestamp_ms);
esp_err_t scenehub_event_make_device_control_result(scenehub_event_t *out,
                                                    const char *device_id,
                                                    const char *request_id,
                                                    const char *result_payload,
                                                    uint64_t timestamp_ms);

#ifdef __cplusplus
}
#endif
