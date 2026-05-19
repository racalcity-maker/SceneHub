#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "mqtt_client.h"
#include "node_config.h"
#include "node_control.h"
#include "node_limits.h"

#define NODE_MQTT_PAYLOAD_MAX NODE_MQTT_PAYLOAD_MAX_LEN
#define NODE_MQTT_TOPIC_MAX 160
#define NODE_MQTT_REQUEST_ID_MAX 48
#define NODE_MQTT_COMMAND_MAX 64
#define NODE_MQTT_ARGS_MAX 1024
#define NODE_MQTT_DUP_CACHE_SIZE 4
#define NODE_MQTT_HEARTBEAT_MS 2000

typedef struct {
    bool used;
    char request_id[NODE_MQTT_REQUEST_ID_MAX];
    char command[NODE_MQTT_COMMAND_MAX];
    char status[16];
    char error_code[32];
} node_mqtt_duplicate_entry_t;

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
esp_err_t node_mqtt_publish_result_locked(const char *request_id,
                                          const char *command,
                                          const node_control_result_t *result);
void node_mqtt_publish_heartbeat_and_status(bool include_status);

void node_mqtt_handle_command_payload(const char *payload);
