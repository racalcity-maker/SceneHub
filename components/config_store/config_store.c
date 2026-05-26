#include "config_store.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mbedtls/sha256.h"
#include "mbedtls/version.h"
#include "sdkconfig.h"

#ifndef CONFIG_SCENEHUB_WEB_AUTH_DEFAULT_USER
#define CONFIG_SCENEHUB_WEB_AUTH_DEFAULT_USER "admin"
#endif

#ifndef CONFIG_SCENEHUB_WEB_AUTH_BOOTSTRAP_PASS
#define CONFIG_SCENEHUB_WEB_AUTH_BOOTSTRAP_PASS "admin"
#endif

static const char *TAG = "config_store";
static const char *NVS_NS = "cfg";
static const uint32_t CONFIG_VERSION = 2;
static const char *SCENEHUB_DEFAULT_HOSTNAME = "scenehub";
static const char *SCENEHUB_DEFAULT_MQTT_ID = "scenehub";
static const char *LEGACY_BROKER_NAME = "broker";
static app_config_t g_config;
static portMUX_TYPE g_config_lock = portMUX_INITIALIZER_UNLOCKED;
static EXT_RAM_BSS_ATTR app_config_t s_config_scratch;
static SemaphoreHandle_t s_config_scratch_mutex = NULL;
static StaticSemaphore_t s_config_scratch_mutex_storage;
static portMUX_TYPE s_config_scratch_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

static void config_lock(void)
{
    taskENTER_CRITICAL(&g_config_lock);
}

static void config_unlock(void)
{
    taskEXIT_CRITICAL(&g_config_lock);
}

static esp_err_t config_scratch_lock(void)
{
    if (!s_config_scratch_mutex) {
        portENTER_CRITICAL(&s_config_scratch_mutex_init_lock);
        if (!s_config_scratch_mutex) {
            s_config_scratch_mutex = xSemaphoreCreateMutexStatic(&s_config_scratch_mutex_storage);
        }
        portEXIT_CRITICAL(&s_config_scratch_mutex_init_lock);
    }
    if (!s_config_scratch_mutex) {
        return ESP_ERR_NO_MEM;
    }
    return xSemaphoreTake(s_config_scratch_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void config_scratch_unlock(void)
{
    if (s_config_scratch_mutex) {
        xSemaphoreGive(s_config_scratch_mutex);
    }
}

static void fill_device_id_string(char *out, size_t out_len)
{
    uint8_t mac[6] = {0};
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        snprintf(out, out_len, "scenehub");
        return;
    }
    snprintf(out, out_len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void fill_random_salt(uint8_t salt[CONFIG_STORE_AUTH_SALT_LEN])
{
    if (!salt) {
        return;
    }
    for (size_t i = 0; i < CONFIG_STORE_AUTH_SALT_LEN; i += sizeof(uint32_t)) {
        uint32_t value = esp_random();
        size_t chunk = CONFIG_STORE_AUTH_SALT_LEN - i;
        if (chunk > sizeof(value)) {
            chunk = sizeof(value);
        }
        memcpy(salt + i, &value, chunk);
    }
}

static bool salt_is_nonzero(const uint8_t *salt)
{
    if (!salt) {
        return false;
    }
    for (size_t i = 0; i < CONFIG_STORE_AUTH_SALT_LEN; ++i) {
        if (salt[i] != 0) {
            return true;
        }
    }
    return false;
}

void config_store_hash_password(const uint8_t salt[CONFIG_STORE_AUTH_SALT_LEN],
                                const char *password,
                                uint8_t out_hash[CONFIG_STORE_AUTH_HASH_LEN])
{
    int rc = 0;
    char device_id[24] = {0};

    if (!out_hash || !salt) {
        return;
    }
    const unsigned char *input = (const unsigned char *)(password ? password : "");
    size_t len = strlen((const char *)input);
    fill_device_id_string(device_id, sizeof(device_id));
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);

#if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03000000
    rc = mbedtls_sha256_starts(&ctx, 0);
    if (rc == 0) {
        rc = mbedtls_sha256_update(&ctx, salt, CONFIG_STORE_AUTH_SALT_LEN);
    }
    if (rc == 0) {
        rc = mbedtls_sha256_update(&ctx, input, len);
    }
    if (rc == 0) {
        rc = mbedtls_sha256_update(&ctx, (const unsigned char *)device_id, strlen(device_id));
    }
    if (rc == 0) {
        rc = mbedtls_sha256_finish(&ctx, out_hash);
    }
#else
    rc = mbedtls_sha256_starts_ret(&ctx, 0);
    if (rc == 0) {
        rc = mbedtls_sha256_update_ret(&ctx, salt, CONFIG_STORE_AUTH_SALT_LEN);
    }
    if (rc == 0) {
        rc = mbedtls_sha256_update_ret(&ctx, input, len);
    }
    if (rc == 0) {
        rc = mbedtls_sha256_update_ret(&ctx, (const unsigned char *)device_id, strlen(device_id));
    }
    if (rc == 0) {
        rc = mbedtls_sha256_finish_ret(&ctx, out_hash);
    }
#endif
    if (rc != 0) {
        memset(out_hash, 0, CONFIG_STORE_AUTH_HASH_LEN);
    }
    mbedtls_sha256_free(&ctx);
}

static void apply_web_auth_credentials(app_web_auth_t *web,
                                       const char *username,
                                       const char *password,
                                       bool initialized)
{
    if (!web || !username || !password) {
        return;
    }
    memset(web, 0, sizeof(*web));
    strncpy(web->username, username, sizeof(web->username) - 1);
    fill_random_salt(web->salt);
    config_store_hash_password(web->salt, password, web->password_hash);
    web->password_initialized = initialized;
}

static void apply_default_web_auth(app_web_auth_t *web)
{
    if (!web) {
        return;
    }
    apply_web_auth_credentials(web,
                               CONFIG_SCENEHUB_WEB_AUTH_DEFAULT_USER,
                               CONFIG_SCENEHUB_WEB_AUTH_BOOTSTRAP_PASS,
                               false);
}

static void log_initial_admin_credentials(const char *reason)
{
    ESP_LOGW(TAG,
             "initial admin credentials (%s): username=%s password=%s",
             reason ? reason : "setup",
             CONFIG_SCENEHUB_WEB_AUTH_DEFAULT_USER,
             CONFIG_SCENEHUB_WEB_AUTH_BOOTSTRAP_PASS);
}

static void load_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    // Empty SSID starts setup AP mode.
    cfg->wifi.ssid[0] = '\0';
    cfg->wifi.password[0] = '\0';
    strncpy(cfg->wifi.hostname, SCENEHUB_DEFAULT_HOSTNAME, sizeof(cfg->wifi.hostname) - 1);
    strncpy(cfg->mqtt.broker_id, SCENEHUB_DEFAULT_MQTT_ID, sizeof(cfg->mqtt.broker_id) - 1);
    cfg->mqtt.port = 1883;
    cfg->mqtt.keepalive_seconds = 30;
    cfg->mqtt.user_count = 0;
    strncpy(cfg->time.ntp_server, "pool.ntp.org", sizeof(cfg->time.ntp_server) - 1);
    cfg->time.timezone_offset_min = 180;
    apply_default_web_auth(&cfg->web);
    memset(&cfg->web_user, 0, sizeof(cfg->web_user));
    cfg->web_user_enabled = false;
    cfg->verbose_logging = false;
}

static bool apply_legacy_scenehub_migration(app_config_t *cfg)
{
    bool changed = false;
    if (!cfg) {
        return false;
    }
    if (strcmp(cfg->wifi.hostname, LEGACY_BROKER_NAME) == 0) {
        memset(cfg->wifi.hostname, 0, sizeof(cfg->wifi.hostname));
        strncpy(cfg->wifi.hostname, SCENEHUB_DEFAULT_HOSTNAME, sizeof(cfg->wifi.hostname) - 1);
        changed = true;
    }
    if (strcmp(cfg->mqtt.broker_id, LEGACY_BROKER_NAME) == 0) {
        memset(cfg->mqtt.broker_id, 0, sizeof(cfg->mqtt.broker_id));
        strncpy(cfg->mqtt.broker_id, SCENEHUB_DEFAULT_MQTT_ID, sizeof(cfg->mqtt.broker_id) - 1);
        changed = true;
    }
    return changed;
}

static bool normalize_bootstrap_admin_policy(app_config_t *cfg)
{
    if (!cfg) {
        return false;
    }
    if (cfg->web.password_initialized) {
        return false;
    }
    apply_default_web_auth(&cfg->web);
    return true;
}

static bool validate_string(const char *s, size_t max_len)
{
    if (!s) {
        return false;
    }
    size_t len = strnlen(s, max_len);
    return len > 0 && len < max_len;
}

static bool hash_is_nonzero(const uint8_t *hash)
{
    if (!hash) {
        return false;
    }
    for (size_t i = 0; i < CONFIG_STORE_AUTH_HASH_LEN; ++i) {
        if (hash[i] != 0) {
            return true;
        }
    }
    return false;
}

static bool validate_web_auth(const app_web_auth_t *web)
{
    if (!web) {
        return false;
    }
    if (!validate_string(web->username, sizeof(web->username))) {
        return false;
    }
    return salt_is_nonzero(web->salt) && hash_is_nonzero(web->password_hash);
}

static bool web_auth_is_empty(const app_web_auth_t *web)
{
    if (!web) {
        return true;
    }
    if (web->username[0] != '\0') {
        return false;
    }
    return !hash_is_nonzero(web->password_hash);
}

static bool validate_mqtt_user(const app_mqtt_user_t *user)
{
    if (!user) {
        return false;
    }
    if (!validate_string(user->client_id, sizeof(user->client_id))) {
        return false;
    }
    if (!validate_string(user->username, sizeof(user->username))) {
        return false;
    }
    if (!validate_string(user->password, sizeof(user->password))) {
        return false;
    }
    return true;
}

static bool validate_config(const app_config_t *cfg)
{
    if (!cfg) {
        return false;
    }
    // Wi-Fi SSID may be empty; that starts setup AP mode.
    if (cfg->wifi.hostname[0] != '\0' && !validate_string(cfg->wifi.hostname, sizeof(cfg->wifi.hostname))) {
        return false;
    }
    if (!validate_string(cfg->mqtt.broker_id, sizeof(cfg->mqtt.broker_id))) {
        return false;
    }
    if (cfg->mqtt.port <= 0 || cfg->mqtt.port > 65535) {
        return false;
    }
    if (cfg->mqtt.keepalive_seconds <= 0 || cfg->mqtt.keepalive_seconds > 600) {
        return false;
    }
    if (cfg->mqtt.user_count > CONFIG_STORE_MAX_MQTT_USERS) {
        return false;
    }
    for (uint8_t i = 0; i < cfg->mqtt.user_count; ++i) {
        if (!validate_mqtt_user(&cfg->mqtt.users[i])) {
            return false;
        }
    }
    if (!validate_string(cfg->time.ntp_server, sizeof(cfg->time.ntp_server))) {
        return false;
    }
    if (!validate_web_auth(&cfg->web)) {
        return false;
    }
    if (!web_auth_is_empty(&cfg->web_user) && !validate_web_auth(&cfg->web_user)) {
        return false;
    }
    return true;
}

bool config_store_has_web_user(const app_config_t *cfg)
{
    return cfg && validate_web_auth(&cfg->web_user);
}

static esp_err_t save_to_nvs(const app_config_t *cfg)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &handle), TAG, "nvs_open failed");

    esp_err_t err = nvs_set_u32(handle, "ver", CONFIG_VERSION);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "cfg", cfg, sizeof(*cfg));
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t config_commit_locked(const app_config_t *next, bool log_result)
{
    if (!validate_config(next)) {
        ESP_LOGE(TAG, "config validation failed");
        return ESP_ERR_INVALID_ARG;
    }
    config_lock();
    g_config = *next;
    config_unlock();
    esp_err_t err = save_to_nvs(next);
    if (log_result) {
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "config saved to NVS");
        } else {
            ESP_LOGE(TAG, "failed to save config: %s", esp_err_to_name(err));
        }
    }
    return err;
}

static esp_err_t config_commit(const app_config_t *next, bool log_result)
{
    esp_err_t err = config_scratch_lock();
    if (err != ESP_OK) {
        return err;
    }
    s_config_scratch = *next;
    err = config_commit_locked(&s_config_scratch, log_result);
    config_scratch_unlock();
    return err;
}

static esp_err_t load_from_nvs(app_config_t *cfg)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t ver = 0;
    size_t size = sizeof(*cfg);
    err = nvs_get_u32(handle, "ver", &ver);
    if (err != ESP_OK || ver != CONFIG_VERSION) {
        nvs_close(handle);
        return ESP_ERR_INVALID_VERSION;
    }
    memset(cfg, 0, sizeof(*cfg));
    err = nvs_get_blob(handle, "cfg", cfg, &size);
    nvs_close(handle);
    if (err == ESP_OK && size > sizeof(*cfg)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

esp_err_t config_store_init(void)
{
    esp_err_t err = config_scratch_lock();
    if (err != ESP_OK) {
        return err;
    }
    load_defaults(&s_config_scratch);
    config_lock();
    g_config = s_config_scratch;
    config_unlock();

    memset(&s_config_scratch, 0, sizeof(s_config_scratch));
    if (load_from_nvs(&s_config_scratch) == ESP_OK && validate_config(&s_config_scratch)) {
        bool migrated = apply_legacy_scenehub_migration(&s_config_scratch);
        bool bootstrap_normalized = normalize_bootstrap_admin_policy(&s_config_scratch);
        config_lock();
        g_config = s_config_scratch;
        config_unlock();
        if (migrated || bootstrap_normalized) {
            err = save_to_nvs(&s_config_scratch);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "failed to persist config migration: %s", esp_err_to_name(err));
            } else {
                if (migrated) {
                    ESP_LOGI(TAG, "legacy broker naming migrated to scenehub");
                }
                if (bootstrap_normalized) {
                    ESP_LOGI(TAG, "bootstrap admin policy normalized to admin/admin");
                    log_initial_admin_credentials("bootstrap_normalized");
                }
            }
        }
        config_scratch_unlock();
        ESP_LOGI(TAG, "config loaded from NVS");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "using default config (failed to load or invalid)");
    config_lock();
    s_config_scratch = g_config;
    config_unlock();
    err = save_to_nvs(&s_config_scratch);
    log_initial_admin_credentials("defaults");
    config_scratch_unlock();
    return err;
}

const app_config_t *config_store_get(void)
{
    return &g_config;
}

esp_err_t config_store_set(const app_config_t *next)
{
    return config_commit(next, true);
}

esp_err_t config_store_reset_defaults(void)
{
    esp_err_t err = config_scratch_lock();
    if (err != ESP_OK) {
        return err;
    }
    load_defaults(&s_config_scratch);
    err = config_commit_locked(&s_config_scratch, false);
    if (err == ESP_OK) {
        log_initial_admin_credentials("config_reset");
    }
    config_scratch_unlock();
    return err;
}

esp_err_t config_store_set_web_auth(const char *username, const char *password, bool initialized)
{
    if (!username || !password) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!validate_string(username, CONFIG_STORE_USERNAME_MAX) || !password[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = config_scratch_lock();
    if (err != ESP_OK) {
        return err;
    }
    config_lock();
    s_config_scratch = g_config;
    config_unlock();
    apply_web_auth_credentials(&s_config_scratch.web, username, password, initialized);
    err = config_commit_locked(&s_config_scratch, true);
    config_scratch_unlock();
    return err;
}

esp_err_t config_store_reset_web_auth_defaults(void)
{
    esp_err_t err = config_scratch_lock();
    if (err != ESP_OK) {
        return err;
    }
    config_lock();
    s_config_scratch = g_config;
    config_unlock();
    apply_default_web_auth(&s_config_scratch.web);
    memset(&s_config_scratch.web_user, 0, sizeof(s_config_scratch.web_user));
    s_config_scratch.web_user_enabled = false;
    err = config_commit_locked(&s_config_scratch, true);
    if (err == ESP_OK) {
        log_initial_admin_credentials("reset");
    }
    config_scratch_unlock();
    return err;
}

esp_err_t config_store_set_web_user(const char *username, const char *password, bool enabled)
{
    if (enabled) {
        if (!username || !password) {
            return ESP_ERR_INVALID_ARG;
        }
        if (!validate_string(username, CONFIG_STORE_USERNAME_MAX) || !password[0]) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    esp_err_t err = config_scratch_lock();
    if (err != ESP_OK) {
        return err;
    }
    config_lock();
    s_config_scratch = g_config;
    config_unlock();
    s_config_scratch.web_user_enabled = enabled;
    memset(&s_config_scratch.web_user, 0, sizeof(s_config_scratch.web_user));
    if (enabled) {
        apply_web_auth_credentials(&s_config_scratch.web_user, username, password, true);
    }
    err = config_commit_locked(&s_config_scratch, true);
    config_scratch_unlock();
    return err;
}
