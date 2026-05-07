#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "quest_common_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUEST_DEVICE_MAX_DEVICES 20
#define QUEST_DEVICE_MAX_COMMANDS 8
#define QUEST_DEVICE_MAX_EVENTS 20
#define QUEST_DEVICE_MAX_COMMAND_PARAMS 4
#define QUEST_DEVICE_ID_MAX_LEN QUEST_ID_MAX_LEN
#define QUEST_DEVICE_CLIENT_ID_MAX_LEN QUEST_ID_MAX_LEN
#define QUEST_DEVICE_NAME_MAX_LEN QUEST_NAME_MAX_LEN
#define QUEST_DEVICE_COMMAND_ID_MAX_LEN QUEST_ID_MAX_LEN
#define QUEST_DEVICE_EVENT_ID_MAX_LEN QUEST_ID_MAX_LEN
#define QUEST_DEVICE_PARAM_KEY_MAX_LEN 32
#define QUEST_DEVICE_COMMAND_NAME_MAX_LEN 48
#define QUEST_DEVICE_CAPABILITY_MAX_LEN 32
#define QUEST_DEVICE_DANGER_LEVEL_MAX_LEN 16
#define QUEST_DEVICE_DEFAULT_ARGS_JSON_MAX_LEN QUEST_PAYLOAD_MAX_LEN
#define QUEST_DEVICE_MATCH_JSON_MAX_LEN QUEST_PAYLOAD_MAX_LEN
#define QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS 3000
#define QUEST_DEVICE_STORAGE_PATH "/sdcard/quest/quest_devices.json"
#define QUEST_DEVICE_SYSTEM_AUDIO_ID "system_audio"
#define QUEST_DEVICE_SYSTEM_RELAY_ID "system_relay"
#define QUEST_DEVICE_SYSTEM_MOSFET_ID "system_mosfet"
#define QUEST_DEVICE_SYSTEM_INPUT_ID "system_input"
#define QUEST_DEVICE_SYSTEM_GPIO_ID "system_gpio"

typedef enum {
    QUEST_DEVICE_COMMAND_PARAM_TEXT = 0,
    QUEST_DEVICE_COMMAND_PARAM_NUMBER,
    QUEST_DEVICE_COMMAND_PARAM_CHECKBOX,
    QUEST_DEVICE_COMMAND_PARAM_AUDIO_FILE_SELECT,
} quest_device_command_param_type_t;

typedef struct {
    char key[QUEST_DEVICE_PARAM_KEY_MAX_LEN];
    char label[QUEST_DEVICE_NAME_MAX_LEN];
    quest_device_command_param_type_t type;
    bool optional;
} quest_device_command_param_t;

typedef struct {
    char id[QUEST_DEVICE_COMMAND_ID_MAX_LEN];
    char label[QUEST_DEVICE_NAME_MAX_LEN];
    char capability[QUEST_DEVICE_CAPABILITY_MAX_LEN];
    char command[QUEST_DEVICE_COMMAND_NAME_MAX_LEN];
    char default_args_json[QUEST_DEVICE_DEFAULT_ARGS_JSON_MAX_LEN];
    bool manual_allowed;
    bool scenario_allowed;
    bool requires_confirmation;
    bool result_required;
    uint32_t timeout_ms;
    char danger_level[QUEST_DEVICE_DANGER_LEVEL_MAX_LEN];
    quest_device_command_param_t params[QUEST_DEVICE_MAX_COMMAND_PARAMS];
    uint8_t param_count;
} quest_device_command_t;

typedef struct {
    char id[QUEST_DEVICE_EVENT_ID_MAX_LEN];
    char label[QUEST_DEVICE_NAME_MAX_LEN];
    char capability[QUEST_DEVICE_CAPABILITY_MAX_LEN];
    char event[QUEST_DEVICE_COMMAND_NAME_MAX_LEN];
    char match_json[QUEST_DEVICE_MATCH_JSON_MAX_LEN];
} quest_device_event_t;

typedef struct {
    char id[QUEST_DEVICE_ID_MAX_LEN];
    char client_id[QUEST_DEVICE_CLIENT_ID_MAX_LEN];
    char name[QUEST_DEVICE_NAME_MAX_LEN];
    bool enabled;
    bool system_device;
    quest_device_command_t commands[QUEST_DEVICE_MAX_COMMANDS];
    uint8_t command_count;
    quest_device_event_t events[QUEST_DEVICE_MAX_EVENTS];
    uint8_t event_count;
} quest_device_t;

esp_err_t quest_device_init(void);
esp_err_t quest_device_upsert(const quest_device_t *device);
esp_err_t quest_device_upsert_and_save(const quest_device_t *device);
esp_err_t quest_device_delete(const char *device_id);
esp_err_t quest_device_delete_and_save(const char *device_id);
esp_err_t quest_device_get(const char *device_id, quest_device_t *out);
esp_err_t quest_device_get_command(const char *device_id,
                                   const char *command_id,
                                   quest_device_command_t *out);
esp_err_t quest_device_get_event(const char *device_id,
                                 const char *event_id,
                                 quest_device_event_t *out);
esp_err_t quest_device_list(quest_device_t *out,
                            size_t max_count,
                            size_t *out_count,
                            bool include_system);
esp_err_t quest_device_clear(void);
uint32_t quest_device_generation(void);

const char *quest_device_command_param_type_to_str(quest_device_command_param_type_t type);
esp_err_t quest_device_command_param_type_from_str(const char *s,
                                                   quest_device_command_param_type_t *out);

esp_err_t quest_device_to_json(const quest_device_t *device, cJSON *out);
esp_err_t quest_device_from_json(const cJSON *json, quest_device_t *out);
esp_err_t quest_device_export_json(cJSON **out);
esp_err_t quest_device_import_json(const cJSON *root);
esp_err_t quest_device_import_json_and_save(const cJSON *root);
esp_err_t quest_device_save(void);
esp_err_t quest_device_load(void);
esp_err_t quest_device_save_to_path(const char *path);
esp_err_t quest_device_load_from_path(const char *path);

#ifdef __cplusplus
}
#endif
