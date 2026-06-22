#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "node_admin_control.h"
#include "node_config.h"
#include "node_control.h"
#include "node_driver_nfc_config_api.h"
#include "node_led_effects.h"
#include "node_rule_api.h"
#include "node_runtime_snapshot.h"

enum {
    NODE_PROVISIONING_CONFIG_JSON_CAPACITY = 24576,
    NODE_PROVISIONING_POST_BODY_CAPACITY = NODE_RULE_BUNDLE_HTTP_MAX_LEN + 1,
};

typedef struct {
    node_config_t next_config;
    node_config_t get_config;
    node_nfc_reader_config_t nfc_reader_scratch;
    node_nfc_known_card_t nfc_cards_scratch[NODE_DRIVER_NFC_KNOWN_CARD_MAX];
    node_control_result_t preview_result;
    node_runtime_snapshot_t runtime_snapshot_scratch;
} node_provisioning_scratch_t;

extern const char *g_node_provisioning_tag;
extern char *g_node_provisioning_config_json;
extern char *g_node_provisioning_post_body;
extern node_provisioning_scratch_t *g_node_provisioning_scratch;
extern StaticSemaphore_t g_node_provisioning_post_body_mutex_storage;
extern SemaphoreHandle_t g_node_provisioning_post_body_mutex;
extern StaticSemaphore_t g_node_provisioning_config_json_mutex_storage;
extern SemaphoreHandle_t g_node_provisioning_config_json_mutex;

#define s_config_json g_node_provisioning_config_json
#define s_post_body g_node_provisioning_post_body
#define s_scratch g_node_provisioning_scratch
#define s_next_config (s_scratch->next_config)
#define s_get_config (s_scratch->get_config)
#define s_nfc_reader_scratch (s_scratch->nfc_reader_scratch)
#define s_nfc_cards_scratch (s_scratch->nfc_cards_scratch)
#define s_preview_result (s_scratch->preview_result)

void *alloc_provisioning_buffer(size_t size);
bool ensure_provisioning_scratch(void);
bool ensure_config_json_buffer(void);
bool ensure_post_body_buffer(void);
void drain_request_body(httpd_req_t *req);
char *dup_request_json(const char *src, size_t len);
bool read_request_body(httpd_req_t *req, char *out, size_t out_size);
bool lock_post_body(void);
void unlock_post_body(void);
bool lock_config_json(void);
void unlock_config_json(void);

esp_err_t send_preview_json(httpd_req_t *req,
                            bool ok,
                            const char *status,
                            const char *error_code,
                            const char *command);
esp_err_t send_admin_result_json(httpd_req_t *req,
                                 int http_status,
                                 bool ok,
                                 const char *error_code,
                                 const node_admin_control_result_t *result);
esp_err_t send_rule_result_json(httpd_req_t *req,
                                int http_status,
                                bool ok,
                                const char *error_code,
                                const node_rule_bundle_metadata_t *metadata,
                                const node_admin_control_result_t *result);

bool json_copy_string_field(const char *json, const char *key, char *out, size_t out_size);
bool json_escape_string(char *out, size_t out_size, const char *value);
void format_led_color_text(char *out,
                           size_t out_size,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue,
                           uint8_t white);
esp_err_t append_effect_controls_json(char *out,
                                      size_t out_size,
                                      size_t *io_len,
                                      uint32_t controls);
esp_err_t append_effect_defaults_json(char *out,
                                      size_t out_size,
                                      size_t *io_len,
                                      const node_led_effect_descriptor_t *desc);
void apply_led_editor_fields(const char *body, node_led_strip_config_t *pins, size_t count);
const char *fallback_return_policy_text(uint8_t policy);
