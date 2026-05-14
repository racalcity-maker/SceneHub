#include "web_ui_handlers.h"

#include "config_store.h"
#include "esp_app_desc.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "web_ui_utils.h"
#include "ws_runtime.h"

#define WEB_META_API_VERSION 1
#define WEB_META_PRODUCT_ID "scenehub-controller"
#define WEB_META_DEFAULT_DEVICE_ID "scenehub"
#define WEB_META_DEFAULT_DEVICE_NAME "SceneHub"

static esp_err_t web_http_check(esp_err_t err, const char *context)
{
    if (err != ESP_OK) {
        web_ui_report_httpd_error(err, context);
    }
    return err;
}

#define WEB_HTTP_CHECK(call) web_http_check((call), __func__)

static const char *web_meta_hostname_or_default(const app_config_t *cfg)
{
    if (cfg && cfg->wifi.hostname[0] != '\0') {
        return cfg->wifi.hostname;
    }
    return WEB_META_DEFAULT_DEVICE_ID;
}

static const char *web_meta_firmware_version_or_default(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc && app_desc->version[0] != '\0') {
        return app_desc->version;
    }
    return "unknown";
}

cJSON *web_ui_build_meta_json(void)
{
    const app_config_t *cfg = config_store_get();
    const esp_app_desc_t *app_desc = esp_app_get_description();
    const char *hostname = web_meta_hostname_or_default(cfg);
    const char *device_name = WEB_META_DEFAULT_DEVICE_NAME;

    cJSON *root = cJSON_CreateObject();
    cJSON *build = root ? cJSON_AddObjectToObject(root, "build") : NULL;
    cJSON *capabilities = root ? cJSON_AddObjectToObject(root, "capabilities") : NULL;
    cJSON *limits = root ? cJSON_AddObjectToObject(root, "limits") : NULL;
    if (!root || !build || !capabilities || !limits) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddStringToObject(root, "product_id", WEB_META_PRODUCT_ID);
    cJSON_AddStringToObject(root, "device_id", hostname);
    cJSON_AddStringToObject(root, "device_name", device_name);
    cJSON_AddStringToObject(root, "hostname", hostname);
    cJSON_AddStringToObject(root, "firmware_version", web_meta_firmware_version_or_default());
    cJSON_AddNumberToObject(root, "api_version", WEB_META_API_VERSION);

    if (app_desc && app_desc->date[0] != '\0') {
        cJSON_AddStringToObject(build, "build_date", app_desc->date);
    }

    cJSON_AddBoolToObject(capabilities, "gm", true);
    cJSON_AddBoolToObject(capabilities, "ota", true);
    cJSON_AddBoolToObject(capabilities, "audio", true);
    cJSON_AddBoolToObject(capabilities, "hardware_io", true);
    cJSON_AddBoolToObject(capabilities, "ws", ws_runtime_available());

    cJSON_AddNumberToObject(limits, "max_rooms", ROOM_CATALOG_MAX_ROOMS);
    cJSON_AddNumberToObject(limits, "max_devices", QUEST_DEVICE_MAX_DEVICES);
    cJSON_AddNumberToObject(limits, "max_ws_clients", ws_runtime_max_clients());

    return root;
}

esp_err_t meta_handler(httpd_req_t *req)
{
    cJSON *root = web_ui_build_meta_json();
    if (!root) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }
    return WEB_HTTP_CHECK(web_ui_send_json(req, root));
}
