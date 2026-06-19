#include "node_provisioning_internal.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "node_admin_control.h"
static const char *TAG = "node_provisioning";
static const uint32_t PROVISIONING_AUTO_CLOSE_TIMEOUT_SEC = 300;

node_provisioning_state_t g_node_prov;

static esp_err_t start_web_server(void);
static esp_err_t stop_web_server(void);
static void make_ap_credentials(const node_config_t *config,
                                char *ssid,
                                size_t ssid_size,
                                char *password,
                                size_t password_size);
static esp_err_t ensure_auto_close_task(void);
static esp_err_t ensure_got_ip_task(void);
static void arm_auto_close_timer(void);
static StaticTask_t s_sta_retry_task_storage;
static StackType_t s_sta_retry_task_stack[2048];
static TaskHandle_t s_sta_retry_task;
static volatile bool s_sta_retry_enabled;
static uint32_t s_sta_retry_delay_ms = 2000;

static void got_ip_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t notify = 0;

        if (xTaskNotifyWait(0, UINT32_MAX, &notify, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        (void)notify;

        if (!g_node_prov.status.sta_got_ip) {
            continue;
        }

        esp_err_t err = start_web_server();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "STA web server start failed: %s", esp_err_to_name(err));
        }
        if (g_node_prov.callbacks.got_ip_cb) {
            g_node_prov.callbacks.got_ip_cb(&g_node_prov.config, g_node_prov.callbacks.got_ip_ctx);
        }
    }
}

static void provisioning_auto_close_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t notify = 0;
        if (!g_node_prov.status.auto_close_running || g_node_prov.status.auto_close_keep_open ||
            !g_node_prov.status.web_started) {
            xTaskNotifyWait(0, UINT32_MAX, &notify, portMAX_DELAY);
            continue;
        }
        if (xTaskNotifyWait(0, UINT32_MAX, &notify, pdMS_TO_TICKS(1000)) == pdTRUE) {
            continue;
        }
        if (!g_node_prov.status.auto_close_running || g_node_prov.status.auto_close_keep_open ||
            !g_node_prov.status.web_started) {
            continue;
        }
        if (g_node_prov.status.auto_close_remaining_sec > 0) {
            --g_node_prov.status.auto_close_remaining_sec;
        }
        if (g_node_prov.status.auto_close_remaining_sec > 0) {
            continue;
        }

        g_node_prov.status.auto_close_running = false;
        g_node_prov.auto_close_closed_for_boot = true;
        ESP_LOGW(TAG, "provisioning web server auto-close timeout reached; disabling setup for this boot");
        // httpd_stop() walks a fairly heavy shutdown path; keep this worker stack roomy.
        (void)stop_web_server();
    }
}

static esp_err_t ensure_auto_close_task(void)
{
    if (g_node_prov.auto_close_task) {
        return ESP_OK;
    }
    g_node_prov.auto_close_task = xTaskCreateStatic(provisioning_auto_close_task,
                                                    "node_prov_close",
                                                    sizeof(g_node_prov.auto_close_task_stack) /
                                                        sizeof(g_node_prov.auto_close_task_stack[0]),
                                                    NULL,
                                                    tskIDLE_PRIORITY + 1,
                                                    g_node_prov.auto_close_task_stack,
                                                    &g_node_prov.auto_close_task_storage);
    return g_node_prov.auto_close_task ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t ensure_got_ip_task(void)
{
    if (g_node_prov.got_ip_task) {
        return ESP_OK;
    }
    g_node_prov.got_ip_task = xTaskCreateStatic(got_ip_task,
                                                "node_prov_ip",
                                                sizeof(g_node_prov.got_ip_task_stack) /
                                                    sizeof(g_node_prov.got_ip_task_stack[0]),
                                                NULL,
                                                tskIDLE_PRIORITY + 2,
                                                g_node_prov.got_ip_task_stack,
                                                &g_node_prov.got_ip_task_storage);
    return g_node_prov.got_ip_task ? ESP_OK : ESP_ERR_NO_MEM;
}

static void arm_auto_close_timer(void)
{
    if (!g_node_prov.status.auto_close_supported || g_node_prov.status.auto_close_keep_open ||
        g_node_prov.auto_close_closed_for_boot) {
        g_node_prov.status.auto_close_running = false;
        g_node_prov.status.auto_close_remaining_sec = 0;
        return;
    }
    if (ensure_auto_close_task() != ESP_OK) {
        ESP_LOGE(TAG, "auto-close task init failed");
        g_node_prov.status.auto_close_running = false;
        g_node_prov.status.auto_close_remaining_sec = 0;
        return;
    }
    g_node_prov.status.auto_close_running = true;
    g_node_prov.status.auto_close_remaining_sec = g_node_prov.status.auto_close_timeout_sec;
    xTaskNotify(g_node_prov.auto_close_task, 1, eSetBits);
}

void node_provisioning_keep_open_for_boot(void)
{
    g_node_prov.status.auto_close_keep_open = true;
    g_node_prov.status.auto_close_running = false;
    g_node_prov.status.auto_close_remaining_sec = 0;
    if (g_node_prov.auto_close_task) {
        xTaskNotify(g_node_prov.auto_close_task, 1, eSetBits);
    }
}

static void sta_retry_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t notify = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &notify, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        (void)notify;
        uint32_t delay_ms = s_sta_retry_delay_ms;
        if (delay_ms < 2000) {
            delay_ms = 2000;
        } else if (delay_ms > 15000) {
            delay_ms = 15000;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        if (!s_sta_retry_enabled || g_node_prov.status.sta_got_ip) {
            continue;
        }
        ESP_LOGI(TAG, "STA reconnect after %u ms backoff", (unsigned)delay_ms);
        esp_wifi_connect();
        if (s_sta_retry_delay_ms < 15000) {
            s_sta_retry_delay_ms *= 2;
        }
    }
}

static esp_err_t ensure_sta_retry_task(void)
{
    if (s_sta_retry_task) {
        return ESP_OK;
    }
    s_sta_retry_task = xTaskCreateStatic(sta_retry_task,
                                         "node_sta_retry",
                                         sizeof(s_sta_retry_task_stack) / sizeof(s_sta_retry_task_stack[0]),
                                         NULL,
                                         tskIDLE_PRIORITY + 1,
                                         s_sta_retry_task_stack,
                                         &s_sta_retry_task_storage);
    return s_sta_retry_task ? ESP_OK : ESP_ERR_NO_MEM;
}

static const char *wifi_reason_name(uint16_t reason)
{
    switch (reason) {
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4way_handshake_timeout";
    case WIFI_REASON_AUTH_FAIL:
        return "auth_fail";
    case WIFI_REASON_ASSOC_FAIL:
        return "assoc_fail";
    case WIFI_REASON_CONNECTION_FAIL:
        return "connection_fail";
    case WIFI_REASON_NO_AP_FOUND:
        return "no_ap_found";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "no_ap_found_compatible_security";
    default:
        return "unknown";
    }
}

static uint32_t fnv1a32(const char *text)
{
    uint32_t hash = 2166136261u;
    if (!text) {
        return hash;
    }
    while (*text) {
        hash ^= (uint8_t)*text++;
        hash *= 16777619u;
    }
    return hash;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)event_data;
        uint16_t reason = event ? event->reason : 0;
        g_node_prov.status.sta_disconnected = true;
        g_node_prov.status.sta_got_ip = false;
        g_node_prov.status.sta_disconnect_reason = reason;
        if (g_node_prov.callbacks.sta_disconnected_cb) {
            g_node_prov.callbacks.sta_disconnected_cb(&g_node_prov.config,
                                                      reason,
                                                      g_node_prov.callbacks.sta_disconnected_ctx);
        }
        ESP_LOGW(TAG,
                 "STA disconnected reason=%u (%s); scheduling reconnect",
                 (unsigned)reason,
                 wifi_reason_name(reason));
        if (s_sta_retry_task) {
            xTaskNotify(s_sta_retry_task, 1, eSetBits);
        }
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)arg;
    (void)base;
    (void)event_data;
    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    g_node_prov.status.sta_got_ip = true;
    s_sta_retry_delay_ms = 2000;
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    ESP_LOGI(TAG, "STA got IP");
    if (ensure_got_ip_task() != ESP_OK) {
        ESP_LOGE(TAG, "got-ip worker task init failed");
        return;
    }
    // esp_event/sys_evt must stay light; defer heavy startup work to a dedicated task.
    xTaskNotify(g_node_prov.got_ip_task, 1, eSetBits);
}

static esp_err_t start_web_server(void)
{
    if (g_node_prov.auto_close_closed_for_boot) {
        ESP_LOGW(TAG, "provisioning web server already auto-closed for this boot");
        return ESP_ERR_INVALID_STATE;
    }
    if (g_node_prov.httpd) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;
    config.stack_size = 4096;
    config.max_uri_handlers = 24;

    esp_err_t err = httpd_start(&g_node_prov.httpd, &config);
    if (err != ESP_OK) {
        return err;
    }
    err = node_provisioning_register_routes(g_node_prov.httpd);
    if (err != ESP_OK) {
        httpd_stop(g_node_prov.httpd);
        g_node_prov.httpd = NULL;
        return err;
    }

    g_node_prov.status.web_started = true;
    ESP_LOGI(TAG, "provisioning web server started");
    arm_auto_close_timer();
    return ESP_OK;
}

static esp_err_t stop_web_server(void)
{
    httpd_handle_t httpd = g_node_prov.httpd;

    g_node_prov.status.web_started = false;
    g_node_prov.status.auto_close_running = false;
    g_node_prov.status.auto_close_remaining_sec = 0;

    if (!httpd) {
        return ESP_OK;
    }

    g_node_prov.httpd = NULL;
    esp_err_t err = httpd_stop(httpd);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "provisioning web server stopped");
    } else {
        ESP_LOGE(TAG, "provisioning web server stop failed: %s", esp_err_to_name(err));
    }
    return err;
}

static void make_ap_credentials(const node_config_t *config,
                                char *ssid,
                                size_t ssid_size,
                                char *password,
                                size_t password_size)
{
    uint8_t mac[6] = {0};
    (void)config;

    if (!ssid || ssid_size == 0 || !password || password_size == 0) {
        return;
    }

    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
        memset(mac, 0, sizeof(mac));
    }

    snprintf(ssid, ssid_size, "SceneNode-%02X%02X", mac[4], mac[5]);
    snprintf(password, password_size, "setup-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static esp_err_t start_ap_mode(const node_config_t *config)
{
    char ssid[33];
    char password[17];
    make_ap_credentials(config, ssid, sizeof(ssid), password, sizeof(password));

    g_node_prov.ap_netif = esp_netif_create_default_wifi_ap();
    if (!g_node_prov.ap_netif) {
        return ESP_ERR_NO_MEM;
    }

    wifi_config_t ap_config = {0};
    size_t ssid_len = strlen(ssid);
    if (ssid_len > sizeof(ap_config.ap.ssid)) {
        ssid_len = sizeof(ap_config.ap.ssid);
    }
    memcpy(ap_config.ap.ssid, ssid, ssid_len);
    ap_config.ap.ssid_len = ssid_len;
    snprintf((char *)ap_config.ap.password, sizeof(ap_config.ap.password), "%s", password);
    ap_config.ap.channel = 6;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    g_node_prov.status.mode = NODE_PROVISIONING_MODE_AP;
    g_node_prov.status.ap_started = true;
    snprintf(g_node_prov.status.ap_ssid, sizeof(g_node_prov.status.ap_ssid), "%s", ssid);
    snprintf(g_node_prov.status.ap_password, sizeof(g_node_prov.status.ap_password), "%s", password);
    ESP_LOGW(TAG, "provisioning AP started ssid=%s password=%s", ssid, password);
    return start_web_server();
}

static esp_err_t start_sta_mode(const node_config_t *config)
{
    esp_err_t err = ensure_sta_retry_task();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "retry task failed: %s", esp_err_to_name(err));
        return err;
    }
    g_node_prov.sta_netif = esp_netif_create_default_wifi_sta();
    if (!g_node_prov.sta_netif) {
        return ESP_ERR_NO_MEM;
    }

    wifi_config_t sta_config = {0};
    snprintf((char *)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), "%s", config->wifi_ssid);
    snprintf((char *)sta_config.sta.password, sizeof(sta_config.sta.password), "%s", config->wifi_password);
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    s_sta_retry_enabled = true;
    s_sta_retry_delay_ms = 2000;
    g_node_prov.status.ap_ssid[0] = '\0';
    g_node_prov.status.ap_password[0] = '\0';
    ESP_ERROR_CHECK(esp_wifi_connect());

    g_node_prov.status.mode = NODE_PROVISIONING_MODE_STA;
    ESP_LOGI(TAG,
             "STA connect started ssid=%s password_len=%u password_hash=%08lx",
             config->wifi_ssid,
             (unsigned)strlen(config->wifi_password),
             (unsigned long)fnv1a32(config->wifi_password));
    return ESP_OK;
}

esp_err_t node_provisioning_start(const node_config_t *config, const node_provisioning_callbacks_t *callbacks)
{
    bool provisioning_required = false;

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    g_node_prov.config = *config;
    g_node_prov.callbacks = callbacks ? *callbacks : (node_provisioning_callbacks_t){0};
    g_node_prov.auto_close_closed_for_boot = false;
    ESP_ERROR_CHECK(ensure_got_ip_task());
    ESP_ERROR_CHECK(node_admin_control_init(&g_node_prov.config));

    static bool initialized = false;
    if (!initialized) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_handler, NULL));
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        initialized = true;
    }

    provisioning_required = node_config_needs_provisioning(config);
    g_node_prov.status.auto_close_timeout_sec = PROVISIONING_AUTO_CLOSE_TIMEOUT_SEC;
    g_node_prov.status.auto_close_supported = !provisioning_required;
    g_node_prov.status.auto_close_running = false;
    g_node_prov.status.auto_close_keep_open = false;
    g_node_prov.status.auto_close_remaining_sec = 0;

    if (provisioning_required) {
        return start_ap_mode(config);
    }
    return start_sta_mode(config);
}

node_provisioning_status_t node_provisioning_get_status(void)
{
    return g_node_prov.status;
}
