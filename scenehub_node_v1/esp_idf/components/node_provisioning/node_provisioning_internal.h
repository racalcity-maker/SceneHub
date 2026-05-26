#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_provisioning.h"

typedef struct {
    node_provisioning_status_t status;
    node_config_t config;
    esp_netif_t *ap_netif;
    esp_netif_t *sta_netif;
    httpd_handle_t httpd;
    node_provisioning_callbacks_t callbacks;
    bool auto_close_closed_for_boot;
    StaticTask_t auto_close_task_storage;
    StackType_t auto_close_task_stack[4096];
    TaskHandle_t auto_close_task;
} node_provisioning_state_t;

extern node_provisioning_state_t g_node_prov;

esp_err_t node_provisioning_register_routes(httpd_handle_t httpd);
void node_provisioning_keep_open_for_boot(void);
esp_err_t node_provisioning_ui_root_get(httpd_req_t *req);
esp_err_t node_provisioning_config_get(httpd_req_t *req);
esp_err_t node_provisioning_led_config_get(httpd_req_t *req);
esp_err_t node_provisioning_led_effects_schema_get(httpd_req_t *req);
esp_err_t node_provisioning_config_post(httpd_req_t *req);
esp_err_t node_provisioning_led_config_post(httpd_req_t *req);
esp_err_t node_provisioning_led_preview_post(httpd_req_t *req);
esp_err_t node_provisioning_status_get(httpd_req_t *req);
esp_err_t node_provisioning_keep_open_post(httpd_req_t *req);
esp_err_t node_provisioning_restart_post(httpd_req_t *req);
esp_err_t node_provisioning_reset_wifi_post(httpd_req_t *req);
esp_err_t node_provisioning_factory_reset_post(httpd_req_t *req);
