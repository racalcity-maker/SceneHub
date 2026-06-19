#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mqtt_client.h"
#include "node_config.h"
#include "node_control.h"
#include "node_limits.h"

#define NODE_MQTT_PAYLOAD_MAX NODE_MQTT_PAYLOAD_MAX_LEN
#define NODE_MQTT_TOPIC_MAX 160
#define NODE_MQTT_REQUEST_ID_MAX 48
#define NODE_MQTT_COMMAND_MAX 64
#define NODE_MQTT_ARGS_MAX 1024
#define NODE_MQTT_ADMIN_ARGS_MAX (NODE_RULE_BUNDLE_MAX_LEN + 256)
#define NODE_MQTT_DUP_CACHE_SIZE 4
#define NODE_MQTT_COMMAND_QUEUE_LEN 4
#define NODE_MQTT_ADMIN_QUEUE_LEN 1
#define NODE_MQTT_RESULT_QUEUE_LEN 8
#define NODE_MQTT_RESULT_PUBLISH_RETRIES 4
#define NODE_MQTT_RESULT_RETRY_DELAY_MS 100
#define NODE_MQTT_HEARTBEAT_MS 2000

typedef struct {
    bool used;
    char request_id[NODE_MQTT_REQUEST_ID_MAX];
    char command[NODE_MQTT_COMMAND_MAX];
    char status[16];
    char error_code[32];
} node_mqtt_duplicate_entry_t;

typedef struct {
    bool valid;
    char request_id[NODE_MQTT_REQUEST_ID_MAX];
    char command[NODE_MQTT_COMMAND_MAX];
    char args_json[NODE_MQTT_ARGS_MAX];
} node_mqtt_command_message_t;

typedef struct {
    bool valid;
    char request_id[NODE_MQTT_REQUEST_ID_MAX];
    char command[NODE_MQTT_COMMAND_MAX];
    char args_json[NODE_MQTT_ADMIN_ARGS_MAX];
} node_mqtt_admin_message_t;

typedef struct {
    char request_id[NODE_MQTT_REQUEST_ID_MAX];
    char command[NODE_MQTT_COMMAND_MAX];
    char status[16];
    char error_code[32];
} node_mqtt_deferred_result_t;

extern node_config_t g_node_mqtt_config;
extern esp_mqtt_client_handle_t g_node_mqtt_client;
extern volatile bool g_node_mqtt_connected;

int64_t node_mqtt_now_ms(void);

bool node_mqtt_json_extract_string(const char *json, const char *key, char *out, size_t out_size);
bool node_mqtt_json_copy_object(const char *json, const char *key, char *out, size_t out_size);

const node_mqtt_duplicate_entry_t *node_mqtt_duplicate_find(const char *request_id);
void node_mqtt_duplicate_remember(const char *request_id,
                                  const char *command,
                                  const node_control_result_t *result);

esp_err_t node_mqtt_publish_init(void);
bool node_mqtt_publish_lock(TickType_t timeout_ticks);
void node_mqtt_publish_unlock(void);
esp_err_t node_mqtt_publish_result_fields_locked(const char *request_id,
                                                 const char *command,
                                                 const char *status,
                                                 const char *error_code,
                                                 const char *data_json);
esp_err_t node_mqtt_publish_result_fields_reliable(const char *request_id,
                                                   const char *command,
                                                   const char *status,
                                                   const char *error_code,
                                                   const char *data_json);
esp_err_t node_mqtt_publish_result_locked(const char *request_id,
                                          const char *command,
                                          const node_control_result_t *result);
esp_err_t node_mqtt_publish_result_reliable(const char *request_id,
                                            const char *command,
                                            const node_control_result_t *result);
esp_err_t node_mqtt_publish_status_locked(void);
esp_err_t node_mqtt_publish_event_locked(const char *event_name, const char *args_json);
void node_mqtt_publish_heartbeat_and_status(bool include_status);

bool node_mqtt_parse_command_payload(const char *payload, node_mqtt_command_message_t *out_message);
bool node_mqtt_command_is_admin(const char *command);
bool node_mqtt_parse_admin_command_payload(const char *payload, node_mqtt_admin_message_t *out_message);
void node_mqtt_process_command_message(const node_mqtt_command_message_t *message);
void node_mqtt_process_admin_command_message(const node_mqtt_admin_message_t *message);
