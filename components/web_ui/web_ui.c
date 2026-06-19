#include "web_ui.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

#include "cJSON.h"
#include "web_ui_utils.h"
#include "web_ui_handlers.h"
#include "web_ui_auth.h"
#include "web_ui_devices.h"
#include "orch_registry_snapshot.h"
#include "orchestrator_timeline.h"
#include "ws_runtime.h"

static const char *TAG = "web_ui";
static httpd_handle_t s_server = NULL;
static SemaphoreHandle_t s_httpd_restart_lock = NULL;
static bool s_httpd_restart_pending = false;
static char s_httpd_restart_reason[64];
static void web_ui_schedule_httpd_restart(const char *reason);
static void web_ui_httpd_restart_task(void *param);
static esp_err_t start_httpd(void);
static esp_err_t stop_httpd(void);
static esp_err_t register_httpd_routes(void);
static esp_err_t web_ui_restart_state_init(void);
static bool web_ui_try_begin_httpd_restart(const char *reason);
static void web_ui_cancel_httpd_restart(void);
static void web_ui_finish_httpd_restart(void);
void web_ui_report_httpd_error(esp_err_t err, const char *context);

static void *web_ui_json_malloc(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static void web_ui_json_free(void *ptr)
{
    heap_caps_free(ptr);
}

typedef struct {
    const char *uri;
    httpd_method_t method;
    bool guarded;
    web_user_role_t min_role;
    bool redirect_on_fail;
    esp_err_t (*fn)(httpd_req_t *req);
} web_route_desc_t;

#define WEB_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static esp_err_t web_http_check(esp_err_t err, const char *context)
{
    if (err != ESP_OK) {
        web_ui_report_httpd_error(err, context);
    }
    return err;
}

#define WEB_HTTP_CHECK(call) web_http_check((call), __func__)
#define WEB_HTTP_CHECK_CTX(call, ctx) web_http_check((call), (ctx))

static esp_err_t register_guarded_route(const char *uri, httpd_method_t method, const web_route_t *route);
static esp_err_t register_public_route(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *));

static esp_err_t web_ui_restart_state_init(void)
{
    if (s_httpd_restart_lock) {
        return ESP_OK;
    }
    s_httpd_restart_lock = xSemaphoreCreateMutex();
    if (!s_httpd_restart_lock) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static bool web_ui_try_begin_httpd_restart(const char *reason)
{
    if (!s_httpd_restart_lock) {
        return false;
    }
    if (xSemaphoreTake(s_httpd_restart_lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    if (s_httpd_restart_pending) {
        xSemaphoreGive(s_httpd_restart_lock);
        return false;
    }
    s_httpd_restart_pending = true;
    if (reason && reason[0]) {
        strncpy(s_httpd_restart_reason, reason, sizeof(s_httpd_restart_reason) - 1);
        s_httpd_restart_reason[sizeof(s_httpd_restart_reason) - 1] = '\0';
    } else {
        strncpy(s_httpd_restart_reason, "unknown", sizeof(s_httpd_restart_reason) - 1);
        s_httpd_restart_reason[sizeof(s_httpd_restart_reason) - 1] = '\0';
    }
    xSemaphoreGive(s_httpd_restart_lock);
    return true;
}

static void web_ui_cancel_httpd_restart(void)
{
    if (!s_httpd_restart_lock) {
        return;
    }
    if (xSemaphoreTake(s_httpd_restart_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_httpd_restart_pending = false;
    s_httpd_restart_reason[0] = '\0';
    xSemaphoreGive(s_httpd_restart_lock);
}

static void web_ui_finish_httpd_restart(void)
{
    web_ui_cancel_httpd_restart();
}

static esp_err_t register_guarded_route(const char *uri, httpd_method_t method, const web_route_t *route)
{
    if (!s_server || !route || !route->fn) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_uri_t desc = {
        .uri = uri,
        .method = method,
        .handler = auth_gate_handler,
        .user_ctx = (void *)route,
    };
    return WEB_HTTP_CHECK_CTX(httpd_register_uri_handler(s_server, &desc), uri);
}

static esp_err_t register_public_route(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    if (!s_server || !handler) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_uri_t desc = {
        .uri = uri,
        .method = method,
        .handler = handler,
    };
    return WEB_HTTP_CHECK_CTX(httpd_register_uri_handler(s_server, &desc), uri);
}

static esp_err_t stop_httpd(void)
{
    if (!s_server) {
        return ESP_OK;
    }

    httpd_handle_t server = s_server;
    esp_err_t err = httpd_stop(server);
    if (err == ESP_OK) {
        s_server = NULL;
    } else {
        ESP_LOGE(TAG, "httpd stop failed: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t register_httpd_routes(void)
{
    static const web_route_desc_t k_routes[] = {
        {.uri = "/login", .method = HTTP_GET, .guarded = false, .fn = login_page_handler},
        {.uri = "/api/auth/login", .method = HTTP_POST, .guarded = false, .fn = auth_login_handler},
        {.uri = "/api/auth/logout", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = auth_logout_handler},
        {.uri = "/api/auth/password", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = auth_password_handler},
        {.uri = "/api/meta", .method = HTTP_GET, .guarded = false, .fn = meta_handler},
        {.uri = "/api/session/info", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = session_info_handler},
        {.uri = "/", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = true, .fn = root_get_handler},
        {.uri = "/gm", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = true, .fn = gm_page_handler},
        {.uri = "/api/gm/rooms", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_rooms_handler},
        {.uri = "/api/gm/room/save", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_room_save_handler},
        {.uri = "/api/gm/room/delete", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_room_delete_handler},
        {.uri = "/api/gm/room/profiles", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_profiles_handler},
        {.uri = "/api/gm/room/profile/select", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_profile_select_handler},
        {.uri = "/api/gm/room/profile/save", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_room_profile_save_handler},
        {.uri = "/api/gm/room/profile/delete", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_room_profile_delete_handler},
        {.uri = "/api/gm/room/scenarios", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_scenarios_handler},
        {.uri = "/api/gm/room/scenario/select", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_scenario_select_handler},
        {.uri = "/api/gm/room/scenario-editor/catalog", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_room_scenario_editor_catalog_handler},
        {.uri = "/api/gm/room/scenario/validate", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_room_scenario_validate_handler},
        {.uri = "/api/gm/room/scenario/save", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_room_scenario_save_handler},
        {.uri = "/api/gm/room/scenario/delete", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_room_scenario_delete_handler},
        {.uri = "/api/gm/room/runtime", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_runtime_state_handler},
        {.uri = "/api/gm/rooms/runtime", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_rooms_runtime_summary_handler},
        {.uri = "/api/gm/room/scenario/start", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_scenario_start_handler},
        {.uri = "/api/gm/room/scenario/stop", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_scenario_stop_handler},
        {.uri = "/api/gm/room/scenario/next", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_scenario_next_handler},
        {.uri = "/api/gm/room/scenario/approve", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_scenario_approve_handler},
        {.uri = "/api/gm/room/scenario/reset", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_scenario_reset_handler},
        {.uri = "/api/gm/room/game/start", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_game_start_handler},
        {.uri = "/api/gm/room/game/stop", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_game_stop_handler},
        {.uri = "/api/gm/room/game/reset", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_room_game_reset_handler},
        {.uri = "/api/gm/room/scenarios/export", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_room_scenarios_export_handler},
        {.uri = "/api/gm/room/scenarios/import", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_room_scenarios_import_handler},
        {.uri = "/api/gm/room/scenarios/save", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_room_scenarios_save_handler},
        {.uri = "/api/gm/room/scenarios/load", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_room_scenarios_load_handler},
        {.uri = "/api/gm/profiles/export", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_profiles_export_handler},
        {.uri = "/api/gm/profiles/import", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_profiles_import_handler},
        {.uri = "/api/gm/profiles/save", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_profiles_save_handler},
        {.uri = "/api/gm/profiles/load", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_profiles_load_handler},
        {.uri = "/api/gm/devices", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_quest_devices_handler},
        {.uri = "/api/gm/device/save", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_quest_device_save_handler},
        {.uri = "/api/gm/device/delete", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_quest_device_delete_handler},
        {.uri = "/api/gm/device/command/run", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_quest_device_command_run_handler},
        {.uri = "/api/gm/device/admin/run", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_quest_device_admin_command_run_handler},
        {.uri = "/api/hardware-io/status", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = hardware_io_status_handler},
        {.uri = "/api/hardware-io/io-mode", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = hardware_io_io_mode_handler},
        {.uri = "/api/gm/device/describe-interface", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = orchestrator_describe_interface_handler},
        {.uri = "/api/gm/devices/export", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_quest_devices_export_handler},
        {.uri = "/api/gm/devices/import", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_quest_devices_import_handler},
        {.uri = "/api/gm/devices/save", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_quest_devices_save_handler},
        {.uri = "/api/gm/devices/load", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_quest_devices_load_handler},
        {.uri = "/api/gm/sidebar-presets", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_sidebar_presets_handler},
        {.uri = "/api/gm/sidebar-presets/save", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_sidebar_presets_save_handler},
        {.uri = "/api/gm/sidebar-presets/load", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_sidebar_presets_load_handler},
        {.uri = "/api/gm/sidebar-presets/export", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_sidebar_presets_export_handler},
        {.uri = "/api/gm/sidebar-presets/import", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = gm_sidebar_presets_import_handler},
        {.uri = "/api/gm/state", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_state_handler},
        {.uri = "/api/gm/system/summary", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_system_summary_handler},
        {.uri = "/api/gm/versions", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_versions_handler},
        {.uri = "/api/orchestrator/audit/recent", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = orchestrator_audit_recent_handler},
        {.uri = "/api/orchestrator/timeline/recent", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = orchestrator_timeline_recent_handler},
        {.uri = "/api/orchestrator/control/devices", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = orchestrator_control_devices_handler},
        {.uri = "/api/gm/room/timer/start", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_timer_start_handler},
        {.uri = "/api/gm/room/timer/pause", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_timer_pause_handler},
        {.uri = "/api/gm/room/timer/resume", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_timer_resume_handler},
        {.uri = "/api/gm/room/timer/reset", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_timer_reset_handler},
        {.uri = "/api/gm/room/timer/add", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_timer_add_handler},
        {.uri = "/api/gm/room/session/finish", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_session_finish_handler},
        {.uri = "/api/gm/room/hint/send", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_hint_send_handler},
        {.uri = "/api/gm/room/hint/clear", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_USER, .redirect_on_fail = false, .fn = gm_hint_clear_handler},
        {.uri = "/api/status", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = status_handler},
        {.uri = "/api/ota/status", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = ota_status_handler},
        {.uri = "/api/ota/upload", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = ota_upload_handler},
        {.uri = "/api/ota/reboot", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = ota_reboot_handler},
        {.uri = "/api/ping", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = ping_handler},
        {.uri = "/api/config/wifi", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = wifi_config_handler},
        {.uri = "/api/config/mqtt", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = mqtt_config_handler},
        {.uri = "/api/config/mqtt_users", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = mqtt_users_handler},
        {.uri = "/api/config/logging", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = logging_config_handler},
        {.uri = "/api/wifi/scan", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = wifi_scan_handler},
        {.uri = "/api/ap/stop", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = ap_stop_handler},
        {.uri = "/api/audio/play", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = audio_play_handler},
        {.uri = "/api/audio/stop", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = audio_stop_handler},
        {.uri = "/api/audio/pause", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = audio_pause_handler},
        {.uri = "/api/audio/resume", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = audio_resume_handler},
        {.uri = "/api/audio/volume", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = audio_volume_handler},
        {.uri = "/api/audio/seek", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = audio_seek_handler},
        {.uri = "/api/publish", .method = HTTP_POST, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = publish_handler},
        {.uri = "/api/files", .method = HTTP_GET, .guarded = true, .min_role = WEB_USER_ROLE_ADMIN, .redirect_on_fail = false, .fn = files_handler},
    };
    static web_route_t s_guarded_routes[WEB_ARRAY_SIZE(k_routes)];

    for (size_t i = 0; i < WEB_ARRAY_SIZE(k_routes); ++i) {
        const web_route_desc_t *desc = &k_routes[i];
        esp_err_t err;

        if (desc->guarded) {
            s_guarded_routes[i].fn = desc->fn;
            s_guarded_routes[i].redirect_on_fail = desc->redirect_on_fail;
            s_guarded_routes[i].min_role = desc->min_role;
            err = register_guarded_route(desc->uri, desc->method, &s_guarded_routes[i]);
        } else {
            err = register_public_route(desc->uri, desc->method, desc->fn);
        }

        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}


static esp_err_t start_httpd(void)
{
    if (s_server) {
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 96; // API routes + embedded UI assets, keep headroom for GM panel growth
    config.max_open_sockets = 20;  // target max clients (clamped by LWIP budget below)
    // Keep max_open_sockets within LWIP_MAX_SOCKETS budget (httpd uses ~3 internally).
#ifdef CONFIG_LWIP_MAX_SOCKETS
    int max_httpd_sockets = CONFIG_LWIP_MAX_SOCKETS - 3;
    if (max_httpd_sockets < 1) {
        max_httpd_sockets = 1;
    }
    if (config.max_open_sockets > max_httpd_sockets) {
        config.max_open_sockets = max_httpd_sockets;
    }
#endif
    config.backlog_conn = config.max_open_sockets;
    config.lru_purge_enable = true; // drop oldest sockets instead of refusing new ones
    config.keep_alive_enable = false; // close connections immediately
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.stack_size = 16384; // avoid stack overflow with larger handlers/pages
    #if WEB_UI_DEBUG
    ESP_LOGI(TAG, "starting httpd: uri=%d sockets=%d backlog=%d keepalive=%d free_int=%u",
             config.max_uri_handlers, config.max_open_sockets, config.backlog_conn, config.keep_alive_enable,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed: %s", esp_err_to_name(err));
        return err;
    }
    err = web_ui_devices_register_assets(s_server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "devices assets register failed: %s", esp_err_to_name(err));
        (void)stop_httpd();
        return err;
    }

    err = register_httpd_routes();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd route registration failed: %s", esp_err_to_name(err));
        (void)stop_httpd();
        return err;
    }

    err = ws_runtime_register_httpd(s_server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "runtime websocket registration failed: %s", esp_err_to_name(err));
        (void)stop_httpd();
        return err;
    }

    return ESP_OK;
}

esp_err_t web_ui_init(void)
{
    cJSON_Hooks hooks = {
        .malloc_fn = web_ui_json_malloc,
        .free_fn = web_ui_json_free,
    };

    cJSON_InitHooks(&hooks);

    ESP_RETURN_ON_ERROR(web_ui_restart_state_init(), TAG, "restart state init failed");
    ESP_RETURN_ON_ERROR(orchestrator_registry_init(), TAG, "orchestrator registry init failed");
    ESP_RETURN_ON_ERROR(orchestrator_timeline_init(), TAG, "orchestrator timeline init failed");
    ESP_RETURN_ON_ERROR(web_ui_system_init(), TAG, "system handlers init failed");
    ESP_RETURN_ON_ERROR(web_sessions_init(), TAG, "sessions init failed");

    return ESP_OK;
}

esp_err_t web_ui_start(void)
{
    ESP_RETURN_ON_ERROR(start_httpd(), TAG, "start httpd failed");
    #if WEB_UI_DEBUG
    ESP_LOGI(TAG, "web ui started on port 80");
#endif
    return ESP_OK;
}


#if defined(ESP_ERR_HTTPD_INVALID_RESP)
#define HAS_HTTPD_INVALID_RESP 1
#endif

void web_ui_report_httpd_error(esp_err_t err, const char *context)
{
    if (err == ESP_OK) {
        return;
    }
    const char *ctx = (context && context[0]) ? context : "httpd";
    ESP_LOGE(TAG, "httpd error %s (0x%x) at %s", esp_err_to_name(err), err, ctx);
    bool fatal = (err == ESP_ERR_HTTPD_HANDLERS_FULL ||
                  err == ESP_ERR_HTTPD_ALLOC_MEM ||
                  err == ESP_ERR_NO_MEM);
#ifdef ESP_ERR_HTTPD_INVALID_STATE
    fatal = fatal || (err == ESP_ERR_HTTPD_INVALID_STATE);
#endif
#ifdef HAS_HTTPD_INVALID_RESP
    fatal = fatal || (err == ESP_ERR_HTTPD_INVALID_RESP);
#endif
    if (fatal) {
        web_ui_schedule_httpd_restart(ctx);
    }
}

static void web_ui_schedule_httpd_restart(const char *reason)
{
    if (!web_ui_try_begin_httpd_restart(reason)) {
        return;
    }
    if (xTaskCreate(web_ui_httpd_restart_task, "web_ui_httpd_rst", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to schedule httpd restart");
        web_ui_cancel_httpd_restart();
        (void)stop_httpd();
    }
}

static void web_ui_httpd_restart_task(void *param)
{
    char reason[sizeof(s_httpd_restart_reason)] = {0};
    if (s_httpd_restart_lock && xSemaphoreTake(s_httpd_restart_lock, portMAX_DELAY) == pdTRUE) {
        strncpy(reason, s_httpd_restart_reason, sizeof(reason) - 1);
        reason[sizeof(reason) - 1] = '\0';
        xSemaphoreGive(s_httpd_restart_lock);
    }
    ESP_LOGW(TAG, "restarting httpd due to %s", reason[0] ? reason : "unknown");
    (void)stop_httpd();
    esp_err_t err = start_httpd();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd restart failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "httpd restarted");
    }
    web_ui_finish_httpd_restart();
    vTaskDelete(NULL);
}

esp_err_t web_ui_send_ok(httpd_req_t *req, const char *mime, const char *body)
{
    httpd_resp_set_type(req, mime);
    return WEB_HTTP_CHECK_CTX(httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN), "web_ui_send_ok");
}
