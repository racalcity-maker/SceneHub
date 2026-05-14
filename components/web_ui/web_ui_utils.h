#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "room_scenario.h"
#include "scenehub_control.h"

typedef struct {
    esp_err_t (*resp_send)(httpd_req_t *req, const char *body, ssize_t body_len);
    esp_err_t (*resp_send_chunk)(httpd_req_t *req, const char *body, ssize_t body_len);
    esp_err_t (*resp_send_err)(httpd_req_t *req, httpd_err_code_t error, const char *message);
    esp_err_t (*resp_set_status)(httpd_req_t *req, const char *status);
    esp_err_t (*resp_set_type)(httpd_req_t *req, const char *type);
    esp_err_t (*resp_set_hdr)(httpd_req_t *req, const char *field, const char *value);
    int (*req_recv)(httpd_req_t *req, char *buf, size_t buf_len);
    esp_err_t (*req_get_url_query_str)(httpd_req_t *req, char *buf, size_t buf_len);
    esp_err_t (*query_key_value)(const char *query, const char *key, char *val, size_t val_size);
    size_t (*req_get_hdr_value_len)(httpd_req_t *req, const char *field);
    esp_err_t (*req_get_hdr_value_str)(httpd_req_t *req, const char *field, char *val, size_t val_size);
} web_ui_http_adapter_t;

esp_err_t web_ui_http_resp_send(httpd_req_t *req, const char *body, ssize_t body_len);
esp_err_t web_ui_http_resp_send_chunk(httpd_req_t *req, const char *body, ssize_t body_len);
esp_err_t web_ui_http_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *message);
esp_err_t web_ui_http_resp_set_status(httpd_req_t *req, const char *status);
esp_err_t web_ui_http_resp_set_type(httpd_req_t *req, const char *type);
esp_err_t web_ui_http_resp_set_hdr(httpd_req_t *req, const char *field, const char *value);
int web_ui_http_req_recv(httpd_req_t *req, char *buf, size_t buf_len);
esp_err_t web_ui_http_req_get_url_query_str(httpd_req_t *req, char *buf, size_t buf_len);
esp_err_t web_ui_http_query_key_value(const char *query, const char *key, char *val, size_t val_size);
size_t web_ui_http_req_get_hdr_value_len(httpd_req_t *req, const char *field);
esp_err_t web_ui_http_req_get_hdr_value_str(httpd_req_t *req, const char *field, char *val, size_t val_size);
void web_ui_http_set_adapter_for_test(const web_ui_http_adapter_t *adapter);
void web_ui_http_reset_adapter_for_test(void);

esp_err_t web_ui_send_ok(httpd_req_t *req, const char *mime, const char *body);
esp_err_t web_ui_send_json(httpd_req_t *req, cJSON *root);
bool web_ui_scenehub_control_is_done(esp_err_t call_err, const scenehub_control_result_t *result);
esp_err_t web_ui_send_scenehub_control_ack(httpd_req_t *req);
esp_err_t web_ui_send_scenehub_control_error(httpd_req_t *req,
                                             esp_err_t call_err,
                                             const scenehub_control_result_t *result,
                                             const char *fallback_message);
esp_err_t web_ui_send_scenehub_control_error_json(httpd_req_t *req,
                                                  esp_err_t call_err,
                                                  const scenehub_control_result_t *result,
                                                  const char *fallback_error_code,
                                                  const char *room_id,
                                                  const char *action_id);
esp_err_t web_ui_send_store_operation_json(httpd_req_t *req,
                                           const char *operation,
                                           const char *path,
                                           uint32_t generation);
esp_err_t web_ui_send_import_result_json(httpd_req_t *req,
                                         const char *operation,
                                         const char *count_key,
                                         int count,
                                         uint32_t generation);
esp_err_t web_ui_send_selection_result_json(httpd_req_t *req,
                                            const char *scope_key,
                                            const char *scope_value,
                                            const char *selected_key,
                                            const char *selected_value);
esp_err_t web_ui_send_deleted_result_json(httpd_req_t *req,
                                          const char *deleted_key,
                                          const char *deleted_value,
                                          uint32_t generation);
esp_err_t web_ui_send_room_saved_json(httpd_req_t *req,
                                      const char *room_id,
                                      const char *name);
esp_err_t web_ui_send_room_deleted_json(httpd_req_t *req,
                                        const char *room_id,
                                        size_t removed_rooms,
                                        size_t removed_profiles,
                                        size_t removed_scenarios);
esp_err_t web_ui_send_room_action_result_json(httpd_req_t *req,
                                              const char *room_id,
                                              const char *action_id);
esp_err_t web_ui_send_generation_item_json(httpd_req_t *req,
                                           uint32_t generation,
                                           const char *item_key,
                                           cJSON *item);
esp_err_t web_ui_send_device_command_result_json(httpd_req_t *req,
                                                 const char *device_id,
                                                 const char *device_name,
                                                 const char *command_id,
                                                 const char *command_label);
esp_err_t web_ui_send_scenario_validation_result_json(httpd_req_t *req,
                                                      const char *scenario_id,
                                                      const room_scenario_validation_report_t *report);
esp_err_t web_ui_add_scenario_validation_report_json(cJSON *root,
                                                     const room_scenario_validation_report_t *report);
void *web_ui_malloc(size_t size);
void *web_ui_calloc(size_t count, size_t size);
void web_ui_free(void *ptr);
void web_ui_url_decode(char *out, size_t out_len, const char *in);
void web_ui_sanitize_filename_token(char *out, size_t out_len, const char *in, const char *fallback);
bool web_ui_is_same_origin_request(httpd_req_t *req);
void web_ui_report_httpd_error(esp_err_t err, const char *context);

#ifndef WEB_UI_UTILS_NO_HTTP_MACROS
#define httpd_resp_send web_ui_http_resp_send
#define httpd_resp_send_chunk web_ui_http_resp_send_chunk
#define httpd_resp_send_err web_ui_http_resp_send_err
#define httpd_resp_set_status web_ui_http_resp_set_status
#define httpd_resp_set_type web_ui_http_resp_set_type
#define httpd_resp_set_hdr web_ui_http_resp_set_hdr
#define httpd_req_recv web_ui_http_req_recv
#define httpd_req_get_url_query_str web_ui_http_req_get_url_query_str
#define httpd_query_key_value web_ui_http_query_key_value
#define httpd_req_get_hdr_value_len web_ui_http_req_get_hdr_value_len
#define httpd_req_get_hdr_value_str web_ui_http_req_get_hdr_value_str
#endif
