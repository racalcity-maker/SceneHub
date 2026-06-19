#include "node_provisioning_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "node_admin_control.h"
#include "node_board.h"
#include "node_control.h"
#include "node_driver_nfc_reader.h"
#include "node_driver_nfc_reader_runtime.h"
#include "node_fallback_runtime.h"
#include "node_limits.h"
#include "node_rule_api.h"
#include "node_runtime_mode.h"
#include "sdkconfig.h"

static const char *TAG = "node_prov_api";
enum {
    NODE_PROVISIONING_CONFIG_JSON_CAPACITY = 24576,
    NODE_PROVISIONING_POST_BODY_CAPACITY = 10241,
};

typedef struct {
    node_config_t next_config;
    node_config_t get_config;
    node_nfc_reader_config_t nfc_reader_scratch;
    node_nfc_known_card_t nfc_cards_scratch[NODE_DRIVER_NFC_KNOWN_CARD_MAX];
    node_control_result_t preview_result;
} node_provisioning_scratch_t;

static char *s_config_json;
static char *s_post_body;
static node_provisioning_scratch_t *s_scratch;
static StaticSemaphore_t s_post_body_mutex_storage;
static SemaphoreHandle_t s_post_body_mutex;
static StaticSemaphore_t s_config_json_mutex_storage;
static SemaphoreHandle_t s_config_json_mutex;

#define s_next_config (s_scratch->next_config)
#define s_get_config (s_scratch->get_config)
#define s_nfc_reader_scratch (s_scratch->nfc_reader_scratch)
#define s_nfc_cards_scratch (s_scratch->nfc_cards_scratch)
#define s_preview_result (s_scratch->preview_result)

#define NODE_PROV_LED_EDITOR_MAX_TIMING_MS 60000U

static bool json_copy_string_field(const char *json, const char *key, char *out, size_t out_size);

static bool text_has_nonspace(const char *text)
{
    if (!text) {
        return false;
    }
    while (*text != '\0') {
        if (!isspace((unsigned char)*text)) {
            return true;
        }
        ++text;
    }
    return false;
}

static bool json_string_optional_copy(const cJSON *item, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    if (!item || cJSON_IsNull(item)) {
        return true;
    }
    if (!cJSON_IsString(item) || !item->valuestring) {
        return false;
    }
    if (strlen(item->valuestring) >= out_size) {
        return false;
    }
    snprintf(out, out_size, "%s", item->valuestring);
    return true;
}

static esp_err_t parse_nfc_cards_request(const char *json,
                                         node_nfc_known_card_t *cards,
                                         size_t card_capacity,
                                         size_t *out_count)
{
    cJSON *root = NULL;
    cJSON *known_cards = NULL;
    size_t count = 0;

    if (!json || !cards || !out_count || card_capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(cards, 0, sizeof(cards[0]) * card_capacity);
    *out_count = 0;

    root = cJSON_Parse(json);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }

    known_cards = cJSON_GetObjectItemCaseSensitive(root, "known_cards");
    if (!cJSON_IsArray(known_cards)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *card = NULL;
    cJSON_ArrayForEach(card, known_cards) {
        cJSON *name = NULL;
        cJSON *uid = NULL;
        cJSON *token_id = NULL;
        cJSON *event = NULL;

        if (!cJSON_IsObject(card) || count >= card_capacity) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }

        name = cJSON_GetObjectItemCaseSensitive(card, "name");
        uid = cJSON_GetObjectItemCaseSensitive(card, "uid");
        token_id = cJSON_GetObjectItemCaseSensitive(card, "token_id");
        event = cJSON_GetObjectItemCaseSensitive(card, "event");

        if (!json_string_optional_copy(name, cards[count].name, sizeof(cards[count].name)) ||
            !cJSON_IsString(uid) || !uid->valuestring ||
            strlen(uid->valuestring) >= sizeof(cards[count].uid) ||
            !text_has_nonspace(uid->valuestring) ||
            !json_string_optional_copy(event, cards[count].event_name, sizeof(cards[count].event_name))) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }

        snprintf(cards[count].uid, sizeof(cards[count].uid), "%s", uid->valuestring);
        cards[count].token_id = 0;
        if (token_id) {
            if (!cJSON_IsNumber(token_id)) {
                cJSON_Delete(root);
                return ESP_ERR_INVALID_ARG;
            }
            cards[count].token_id = (int32_t)token_id->valuedouble;
        }
        ++count;
    }

    *out_count = count;
    cJSON_Delete(root);
    return ESP_OK;
}

static void *alloc_provisioning_buffer(size_t size)
{
    void *ptr = NULL;

    if (size == 0) {
        return NULL;
    }
#if CONFIG_SPIRAM
    ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, size);
        return ptr;
    }
#endif
    ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

static bool ensure_provisioning_scratch(void)
{
    if (s_scratch) {
        return true;
    }

    s_scratch = (node_provisioning_scratch_t *)alloc_provisioning_buffer(sizeof(*s_scratch));
    if (!s_scratch) {
        ESP_LOGE(TAG, "provisioning scratch alloc failed (%u bytes)", (unsigned)sizeof(*s_scratch));
        return false;
    }
    return true;
}

static bool ensure_config_json_buffer(void)
{
    if (s_config_json) {
        return true;
    }

    s_config_json = (char *)alloc_provisioning_buffer(NODE_PROVISIONING_CONFIG_JSON_CAPACITY);
    if (!s_config_json) {
        ESP_LOGE(TAG, "config json buffer alloc failed (%u bytes)",
                 (unsigned)NODE_PROVISIONING_CONFIG_JSON_CAPACITY);
        return false;
    }
    return true;
}

static bool ensure_post_body_buffer(void)
{
    if (s_post_body) {
        return true;
    }

    s_post_body = (char *)alloc_provisioning_buffer(NODE_PROVISIONING_POST_BODY_CAPACITY);
    if (!s_post_body) {
        ESP_LOGE(TAG, "post body buffer alloc failed (%u bytes)",
                 (unsigned)NODE_PROVISIONING_POST_BODY_CAPACITY);
        return false;
    }
    return true;
}

static void drain_request_body(httpd_req_t *req)
{
    char discard[256];
    int remaining = 0;

    if (!req || req->content_len <= 0) {
        return;
    }

    remaining = req->content_len;
    while (remaining > 0) {
        int chunk = remaining > (int)sizeof(discard) ? (int)sizeof(discard) : remaining;
        int received = httpd_req_recv(req, discard, chunk);
        if (received <= 0) {
            break;
        }
        remaining -= received;
    }
}

static char *dup_request_json(const char *src, size_t len)
{
    char *copy = NULL;

    if (!src || len == 0 || len > NODE_RULE_BUNDLE_MAX_LEN) {
        return NULL;
    }

    copy = (char *)alloc_provisioning_buffer(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}

static esp_err_t send_preview_json(httpd_req_t *req,
                                   bool ok,
                                   const char *status,
                                   const char *error_code,
                                   const char *command)
{
    char response[192];
    int written = 0;

    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(response,
                       sizeof(response),
                       "{\"ok\":%s,\"status\":\"%s\",\"error_code\":\"%s\",\"command\":\"%s\"}",
                       ok ? "true" : "false",
                       status ? status : "",
                       error_code ? error_code : "",
                       command ? command : "");
    if (written < 0 || written >= (int)sizeof(response)) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, written);
}

static esp_err_t send_admin_result_json(httpd_req_t *req,
                                        int http_status,
                                        bool ok,
                                        const char *error_code,
                                        const node_admin_control_result_t *result)
{
    char response[224];
    int written = 0;

    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    if (http_status >= 400) {
        httpd_resp_set_status(req,
                              http_status == HTTPD_400_BAD_REQUEST ? "400 Bad Request"
                              : "500 Internal Server Error");
    }

    written = snprintf(response,
                       sizeof(response),
                       "{\"ok\":%s,\"applied\":%s,\"restart_required\":%s,"
                       "\"restarting\":%s,\"error_code\":\"%s\"}",
                       ok ? "true" : "false",
                       result && result->applied ? "true" : "false",
                       result && result->restart_required ? "true" : "false",
                       result && result->restarting ? "true" : "false",
                       error_code ? error_code : "");
    if (written < 0 || written >= (int)sizeof(response)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "admin response too large");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, written);
}

static esp_err_t send_rule_result_json(httpd_req_t *req,
                                       int http_status,
                                       bool ok,
                                       const char *error_code,
                                       const node_rule_bundle_metadata_t *metadata)
{
    char response[256];
    int written = 0;

    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    if (http_status >= 400) {
        httpd_resp_set_status(req,
                              http_status == HTTPD_400_BAD_REQUEST ? "400 Bad Request"
                              : "500 Internal Server Error");
    }

    written = snprintf(response,
                       sizeof(response),
                       "{\"ok\":%s,\"error_code\":\"%s\",\"metadata\":{"
                       "\"has_bundle\":%s,\"version\":%lu,\"generation\":%lu,"
                       "\"bundle_id\":\"%s\",\"mode\":\"%s\",\"raw_size\":%lu}}",
                       ok ? "true" : "false",
                       error_code ? error_code : "",
                       metadata && metadata->has_bundle ? "true" : "false",
                       metadata ? (unsigned long)metadata->version : 0UL,
                       metadata ? (unsigned long)metadata->generation : 0UL,
                       metadata ? metadata->bundle_id : "",
                       metadata ? metadata->mode : "",
                       metadata ? (unsigned long)metadata->raw_size : 0UL);
    if (written < 0 || written >= (int)sizeof(response)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules response too large");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, written);
}

static bool add_string_array(cJSON *parent,
                             const char *key,
                             const char *const *values,
                             size_t value_count)
{
    cJSON *array = NULL;

    if (!parent || !key) {
        return false;
    }
    array = cJSON_AddArrayToObject(parent, key);
    if (!array) {
        return false;
    }
    for (size_t i = 0; i < value_count; ++i) {
        cJSON *item = cJSON_CreateString(values[i]);
        if (!item) {
            return false;
        }
        cJSON_AddItemToArray(array, item);
    }
    return true;
}

static bool add_command_spec(cJSON *commands,
                             const char *command,
                             const char *target_type,
                             const char *args)
{
    cJSON *item = NULL;

    if (!commands || !command) {
        return false;
    }
    item = cJSON_CreateObject();
    if (!item) {
        return false;
    }
    if (!cJSON_AddStringToObject(item, "command", command)) {
        cJSON_Delete(item);
        return false;
    }
    if (target_type && target_type[0] != '\0' &&
        !cJSON_AddStringToObject(item, "target_type", target_type)) {
        cJSON_Delete(item);
        return false;
    }
    if (args && args[0] != '\0' && !cJSON_AddStringToObject(item, "args", args)) {
        cJSON_Delete(item);
        return false;
    }
    cJSON_AddItemToArray(commands, item);
    return true;
}

static bool add_basic_resource(cJSON *array,
                               const char *name,
                               const char *type,
                               uint8_t channel,
                               int gpio,
                               const char *label)
{
    cJSON *item = NULL;

    if (!array || !name || !type) {
        return false;
    }
    item = cJSON_CreateObject();
    if (!item) {
        return false;
    }
    if (!cJSON_AddStringToObject(item, "name", name) ||
        !cJSON_AddStringToObject(item, "type", type) ||
        !cJSON_AddNumberToObject(item, "channel", channel) ||
        !cJSON_AddNumberToObject(item, "gpio", gpio)) {
        cJSON_Delete(item);
        return false;
    }
    if (label && label[0] != '\0' && !cJSON_AddStringToObject(item, "label", label)) {
        cJSON_Delete(item);
        return false;
    }
    cJSON_AddItemToArray(array, item);
    return true;
}

static bool preview_status_ok(const char *status)
{
    return status &&
           (strcmp(status, "done") == 0 ||
            strcmp(status, "started") == 0 ||
            strcmp(status, "accepted") == 0 ||
            strcmp(status, "scheduled") == 0);
}

static bool led_preview_command_allowed(const char *command)
{
    return command &&
           (strcmp(command, "led.off") == 0 || strcmp(command, "led.preview.blink") == 0 ||
            strcmp(command, "led.preview.breathe") == 0 || strcmp(command, "led.preview.effect") == 0);
}

static bool read_request_body(httpd_req_t *req, char *out, size_t out_size)
{
    int remaining = 0;
    int offset = 0;

    if (!req || !out || out_size == 0 || req->content_len <= 0 || req->content_len >= (int)out_size) {
        return false;
    }

    remaining = req->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(req, out + offset, remaining);
        if (received <= 0) {
            return false;
        }
        offset += received;
        remaining -= received;
    }
    out[offset] = '\0';
    return true;
}

static bool lock_post_body(void)
{
    if (!s_post_body_mutex) {
        s_post_body_mutex = xSemaphoreCreateMutexStatic(&s_post_body_mutex_storage);
    }
    return s_post_body_mutex && xSemaphoreTake(s_post_body_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void unlock_post_body(void)
{
    if (s_post_body_mutex) {
        xSemaphoreGive(s_post_body_mutex);
    }
}

static bool lock_config_json(void)
{
    if (!s_config_json_mutex) {
        s_config_json_mutex = xSemaphoreCreateMutexStatic(&s_config_json_mutex_storage);
    }
    return s_config_json_mutex && xSemaphoreTake(s_config_json_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void unlock_config_json(void)
{
    if (s_config_json_mutex) {
        xSemaphoreGive(s_config_json_mutex);
    }
}

static bool http_copy_header_value(httpd_req_t *req, const char *name, char *out, size_t out_size)
{
    size_t value_len = 0;

    if (!req || !name || !out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    value_len = httpd_req_get_hdr_value_len(req, name);
    if (value_len == 0 || value_len >= out_size) {
        return false;
    }
    return httpd_req_get_hdr_value_str(req, name, out, out_size) == ESP_OK;
}

static void log_admin_request_headers(const char *action, httpd_req_t *req)
{
    char user_agent[96] = {0};
    char origin[96] = {0};
    char referer[128] = {0};
    char host[64] = {0};
    char action_header[32] = {0};

    if (!req) {
        return;
    }
    (void)http_copy_header_value(req, "User-Agent", user_agent, sizeof(user_agent));
    (void)http_copy_header_value(req, "Origin", origin, sizeof(origin));
    (void)http_copy_header_value(req, "Referer", referer, sizeof(referer));
    (void)http_copy_header_value(req, "Host", host, sizeof(host));
    (void)http_copy_header_value(req, "X-Node-Action", action_header, sizeof(action_header));
    ESP_LOGW(TAG,
             "admin request action=%s method=%d content_len=%d host=%s x_node_action=%s origin=%s referer=%s ua=%s",
             action ? action : "",
             req->method,
             req->content_len,
             host,
             action_header,
             origin,
             referer,
             user_agent);
}

static bool json_escape_string(char *out, size_t out_size, const char *value)
{
    size_t len = 0;
    const unsigned char *p = (const unsigned char *)(value ? value : "");

    if (!out || out_size == 0) {
        return false;
    }

    while (*p) {
        char escape[7] = {0};
        const char *chunk = NULL;
        size_t chunk_len = 0;

        switch (*p) {
        case '\"':
            chunk = "\\\"";
            chunk_len = 2;
            break;
        case '\\':
            chunk = "\\\\";
            chunk_len = 2;
            break;
        case '\b':
            chunk = "\\b";
            chunk_len = 2;
            break;
        case '\f':
            chunk = "\\f";
            chunk_len = 2;
            break;
        case '\n':
            chunk = "\\n";
            chunk_len = 2;
            break;
        case '\r':
            chunk = "\\r";
            chunk_len = 2;
            break;
        case '\t':
            chunk = "\\t";
            chunk_len = 2;
            break;
        default:
            if (*p < 0x20) {
                snprintf(escape, sizeof(escape), "\\u%04x", (unsigned)*p);
                chunk = escape;
                chunk_len = 6;
            }
            break;
        }

        if (!chunk) {
            if (len + 1 >= out_size) {
                out[0] = '\0';
                return false;
            }
            out[len++] = (char)*p++;
            continue;
        }
        if (len + chunk_len >= out_size) {
            out[0] = '\0';
            return false;
        }
        memcpy(out + len, chunk, chunk_len);
        len += chunk_len;
        ++p;
    }

    out[len] = '\0';
    return true;
}

static uint32_t clamp_u32_or_default(int value, uint32_t fallback, uint32_t max_value)
{
    uint32_t parsed = 0;

    if (value < 0) {
        return fallback;
    }
    parsed = (uint32_t)value;
    return parsed > max_value ? max_value : parsed;
}

static uint16_t clamp_u16_or_default(int value, uint16_t fallback)
{
    if (value < 0) {
        return fallback;
    }
    if (value > 65535) {
        return 65535;
    }
    return (uint16_t)value;
}

static const char *fallback_return_policy_text(uint8_t policy)
{
    switch ((node_config_fallback_return_policy_t)policy) {
    case NODE_CONFIG_FALLBACK_RETURN_POLICY_MANUAL_STAY_ACTIVE:
        return "manual_stay_active";
    case NODE_CONFIG_FALLBACK_RETURN_POLICY_AUTO_ON_STABLE_MQTT:
    default:
        return "auto_on_stable_mqtt";
    }
}

static bool fallback_return_policy_from_text(const char *value, uint8_t *out_policy)
{
    if (!value || !out_policy) {
        return false;
    }
    if (strcmp(value, "auto_on_stable_mqtt") == 0) {
        *out_policy = NODE_CONFIG_FALLBACK_RETURN_POLICY_AUTO_ON_STABLE_MQTT;
        return true;
    }
    if (strcmp(value, "manual_stay_active") == 0) {
        *out_policy = NODE_CONFIG_FALLBACK_RETURN_POLICY_MANUAL_STAY_ACTIVE;
        return true;
    }
    return false;
}

static const char *led_chipset_text(node_led_chipset_t chipset)
{
    switch (chipset) {
    case NODE_LED_CHIPSET_WS2812:
        return "ws2812";
    case NODE_LED_CHIPSET_WS2815:
        return "ws2815";
    case NODE_LED_CHIPSET_SK6812:
        return "sk6812";
    default:
        return "ws2812";
    }
}

static const char *led_color_order_text(node_led_color_order_t color_order)
{
    switch (color_order) {
    case NODE_LED_COLOR_ORDER_RGB:
        return "rgb";
    case NODE_LED_COLOR_ORDER_RBG:
        return "rbg";
    case NODE_LED_COLOR_ORDER_GRB:
        return "grb";
    case NODE_LED_COLOR_ORDER_GBR:
        return "gbr";
    case NODE_LED_COLOR_ORDER_BRG:
        return "brg";
    case NODE_LED_COLOR_ORDER_BGR:
        return "bgr";
    default:
        return "grb";
    }
}

static void format_led_color_text(char *out,
                                  size_t out_size,
                                  uint8_t red,
                                  uint8_t green,
                                  uint8_t blue,
                                  uint8_t white)
{
    if (!out || out_size == 0) {
        return;
    }
    if (white > 0) {
        snprintf(out, out_size, "#%02x%02x%02x%02x", red, green, blue, white);
    } else {
        snprintf(out, out_size, "#%02x%02x%02x", red, green, blue);
    }
}

static bool parse_hex_byte(char high, char low, uint8_t *out)
{
    unsigned value = 0;
    if (!isxdigit((unsigned char)high) || !isxdigit((unsigned char)low) || !out) {
        return false;
    }
    if (sscanf((char[]){high, low, '\0'}, "%02x", &value) != 1) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

static bool parse_color_text(const char *text,
                             uint8_t *out_red,
                             uint8_t *out_green,
                             uint8_t *out_blue,
                             uint8_t *out_white)
{
    const char *p = text;
    size_t len = 0;

    if (!p || !*p || !out_red || !out_green || !out_blue || !out_white) {
        return false;
    }
    if (*p == '#') {
        ++p;
    }

    len = strlen(p);
    if (len != 6 && len != 8) {
        return false;
    }
    if (!parse_hex_byte(p[0], p[1], out_red) ||
        !parse_hex_byte(p[2], p[3], out_green) ||
        !parse_hex_byte(p[4], p[5], out_blue)) {
        return false;
    }
    *out_white = 0;
    if (len == 8 && !parse_hex_byte(p[6], p[7], out_white)) {
        return false;
    }
    return true;
}

static void json_copy_led_color_field(const char *json,
                                      const char *key,
                                      uint8_t *out_red,
                                      uint8_t *out_green,
                                      uint8_t *out_blue,
                                      uint8_t *out_white)
{
    char value[16] = {0};
    if (!json_copy_string_field(json, key, value, sizeof(value))) {
        return;
    }
    (void)parse_color_text(value, out_red, out_green, out_blue, out_white);
}

static void make_led_effect_key(char *out,
                                size_t out_size,
                                size_t index,
                                const char *effect_name,
                                const char *field)
{
    snprintf(out, out_size, "led%u_effect_%s_%s", (unsigned)(index + 1), effect_name, field);
}

static esp_err_t append_effect_controls_json(char *out,
                                             size_t out_size,
                                             size_t *len,
                                             uint32_t controls)
{
    bool first = true;

#define APPEND_SCHEMA_JSON(...) do { \
        int _n = snprintf((out) + *(len), (out_size) - *(len), __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= (out_size) - *(len)) { \
            return ESP_ERR_NO_MEM; \
        } \
        *(len) += (size_t)_n; \
    } while (0)
#define APPEND_CONTROL(name_) do { \
        APPEND_SCHEMA_JSON("%s\"%s\"", first ? "" : ",", (name_)); \
        first = false; \
    } while (0)

    if (controls & NODE_LED_CTRL_FLASH_ON) {
        APPEND_CONTROL("duration_ms");
    }
    if (controls & NODE_LED_CTRL_SPEED) {
        APPEND_CONTROL("step_ms");
    }
    if (controls & NODE_LED_CTRL_FLASH_OFF) {
        APPEND_CONTROL("step_ms");
    }
    if (controls & NODE_LED_CTRL_REPEAT_COUNT) {
        APPEND_CONTROL("repeat_count");
    }
    if (controls & NODE_LED_CTRL_SIZE) {
        APPEND_CONTROL("size");
    }
    if (controls & NODE_LED_CTRL_INTENSITY) {
        APPEND_CONTROL("intensity");
    }
    if (controls & NODE_LED_CTRL_DENSITY) {
        APPEND_CONTROL("density");
    }
    if (controls & NODE_LED_CTRL_FADE) {
        APPEND_CONTROL("fade");
    }
    if (controls & NODE_LED_CTRL_PALETTE_MODE) {
        APPEND_CONTROL("palette_mode");
    }
    if (controls & NODE_LED_CTRL_PRIMARY_COLOR) {
        APPEND_CONTROL("color");
    }
    if (controls & NODE_LED_CTRL_SECONDARY_COLOR) {
        APPEND_CONTROL("secondary_color");
    }
    if (controls & NODE_LED_CTRL_BACKGROUND_COLOR) {
        APPEND_CONTROL("background_color");
    }

#undef APPEND_CONTROL
#undef APPEND_SCHEMA_JSON
    return ESP_OK;
}

static esp_err_t append_effect_defaults_json(char *out,
                                             size_t out_size,
                                             size_t *len,
                                             const node_led_effect_descriptor_t *desc)
{
    char color[16];
    char secondary_color[16];
    char background_color[16];
    bool first = true;
    const node_led_effect_defaults_t *defaults = desc ? &desc->defaults : NULL;

    if (!desc || !defaults) {
        return ESP_ERR_INVALID_ARG;
    }

#define APPEND_SCHEMA_JSON(...) do { \
        int _n = snprintf((out) + *(len), (out_size) - *(len), __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= (out_size) - *(len)) { \
            return ESP_ERR_NO_MEM; \
        } \
        *(len) += (size_t)_n; \
    } while (0)
#define APPEND_DEFAULT_NUM(key_, value_) do { \
        APPEND_SCHEMA_JSON("%s\"%s\":%lu", first ? "" : ",", (key_), (unsigned long)(value_)); \
        first = false; \
    } while (0)
#define APPEND_DEFAULT_STR(key_, value_) do { \
        APPEND_SCHEMA_JSON("%s\"%s\":\"%s\"", first ? "" : ",", (key_), (value_)); \
        first = false; \
    } while (0)

    if (desc->controls & NODE_LED_CTRL_FLASH_ON) {
        APPEND_DEFAULT_NUM("duration_ms", defaults->duration_ms);
    }
    if (desc->controls & NODE_LED_CTRL_SPEED) {
        APPEND_DEFAULT_NUM("step_ms", defaults->step_ms);
    }
    if (desc->controls & NODE_LED_CTRL_FLASH_OFF) {
        APPEND_DEFAULT_NUM("step_ms", defaults->step_ms);
    }
    if (desc->controls & NODE_LED_CTRL_REPEAT_COUNT) {
        APPEND_DEFAULT_NUM("repeat_count", defaults->repeat_count);
    }
    if (desc->controls & NODE_LED_CTRL_SIZE) {
        APPEND_DEFAULT_NUM("size", defaults->size);
    }
    if (desc->controls & NODE_LED_CTRL_INTENSITY) {
        APPEND_DEFAULT_NUM("intensity", defaults->intensity);
    }
    if (desc->controls & NODE_LED_CTRL_DENSITY) {
        APPEND_DEFAULT_NUM("density", defaults->density);
    }
    if (desc->controls & NODE_LED_CTRL_FADE) {
        APPEND_DEFAULT_NUM("fade", defaults->fade);
    }
    if (desc->controls & NODE_LED_CTRL_PALETTE_MODE) {
        APPEND_DEFAULT_STR("palette_mode", node_led_palette_mode_name(defaults->palette_mode));
    }
    if (desc->controls & NODE_LED_CTRL_PRIMARY_COLOR) {
        format_led_color_text(color,
                              sizeof(color),
                              defaults->red,
                              defaults->green,
                              defaults->blue,
                              defaults->white);
        APPEND_DEFAULT_STR("color", color);
    }
    if (desc->controls & NODE_LED_CTRL_SECONDARY_COLOR) {
        format_led_color_text(secondary_color,
                              sizeof(secondary_color),
                              defaults->red2,
                              defaults->green2,
                              defaults->blue2,
                              defaults->white2);
        APPEND_DEFAULT_STR("secondary_color", secondary_color);
    }
    if (desc->controls & NODE_LED_CTRL_BACKGROUND_COLOR) {
        format_led_color_text(background_color,
                              sizeof(background_color),
                              defaults->bg_red,
                              defaults->bg_green,
                              defaults->bg_blue,
                              defaults->bg_white);
        APPEND_DEFAULT_STR("background_color", background_color);
    }

#undef APPEND_DEFAULT_STR
#undef APPEND_DEFAULT_NUM
#undef APPEND_SCHEMA_JSON
    return ESP_OK;
}

esp_err_t node_provisioning_status_get(httpd_req_t *req)
{
    char ap_ssid_json[sizeof(g_node_prov.status.ap_ssid) * 2 + 1];
    char ap_password_json[sizeof(g_node_prov.status.ap_password) * 2 + 1];
    char reader_id_json[(NODE_DRIVER_ID_MAX_LEN + 1) * 2 + 1];
    char uid_json[NODE_DRIVER_UID_TEXT_MAX_LEN * 2 + 1];
    char last_seen_uid_json[NODE_DRIVER_UID_TEXT_MAX_LEN * 2 + 1];
    char health_json[16 * 2 + 1];
    char state_json[16 * 2 + 1];
    char error_code_json[32 * 2 + 1];
    node_nfc_reader_runtime_status_t nfc_status = {0};
    node_fallback_runtime_status_t fallback_status = {0};
    int n = 0;

    if (!lock_config_json()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status busy");
    }
    if (!ensure_config_json_buffer()) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status no mem");
    }
    node_driver_nfc_reader_runtime_get_status(&nfc_status);
    node_fallback_runtime_get_status(&fallback_status);
    if (!json_escape_string(ap_ssid_json, sizeof(ap_ssid_json), g_node_prov.status.ap_ssid) ||
        !json_escape_string(ap_password_json, sizeof(ap_password_json), g_node_prov.status.ap_password) ||
        !json_escape_string(reader_id_json, sizeof(reader_id_json), nfc_status.reader_id) ||
        !json_escape_string(uid_json, sizeof(uid_json), nfc_status.uid) ||
        !json_escape_string(last_seen_uid_json, sizeof(last_seen_uid_json), nfc_status.last_seen_uid) ||
        !json_escape_string(health_json, sizeof(health_json), nfc_status.health) ||
        !json_escape_string(state_json, sizeof(state_json), nfc_status.state) ||
        !json_escape_string(error_code_json, sizeof(error_code_json), nfc_status.error_code)) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status too large");
    }

    n = snprintf(s_config_json,
                     NODE_PROVISIONING_CONFIG_JSON_CAPACITY,
                     "{\"ok\":true,\"mode\":\"%s\",\"ap_started\":%s,\"web_started\":%s,"
                     "\"ap_ssid\":\"%s\",\"ap_password\":\"%s\","
                     "\"sta_got_ip\":%s,\"sta_disconnected\":%s,\"sta_disconnect_reason\":%u,"
                     "\"auto_close_supported\":%s,\"auto_close_running\":%s,"
                     "\"auto_close_keep_open\":%s,\"auto_close_timeout_sec\":%u,"
                     "\"auto_close_remaining_sec\":%u,"
                     "\"fallback\":{\"initialized\":%s,\"enabled\":%s,\"state\":\"%s\","
                     "\"wifi_ready\":%s,\"mqtt_connected\":%s,\"fallback_rules_active\":%s,"
                     "\"fallback_timeout_ms\":%lu,\"fallback_return_delay_ms\":%lu,"
                     "\"return_policy\":\"%s\"},"
                     "\"nfc_reader\":{\"initialized\":%s,\"started\":%s,\"enabled\":%s,\"driver_ready\":%s,"
                     "\"health\":\"%s\",\"state\":\"%s\",\"error_code\":\"%s\","
                     "\"reader_id\":\"%s\",\"card_present\":%s,\"seen_count\":%lu,"
                     "\"token_id\":%ld,\"uid\":\"%s\",\"last_seen_uid\":\"%s\"}}",
                     g_node_prov.status.mode == NODE_PROVISIONING_MODE_AP ? "ap" : "sta",
                     g_node_prov.status.ap_started ? "true" : "false",
                     g_node_prov.status.web_started ? "true" : "false",
                     ap_ssid_json,
                     ap_password_json,
                     g_node_prov.status.sta_got_ip ? "true" : "false",
                     g_node_prov.status.sta_disconnected ? "true" : "false",
                     (unsigned)g_node_prov.status.sta_disconnect_reason,
                     g_node_prov.status.auto_close_supported ? "true" : "false",
                     g_node_prov.status.auto_close_running ? "true" : "false",
                     g_node_prov.status.auto_close_keep_open ? "true" : "false",
                     (unsigned)g_node_prov.status.auto_close_timeout_sec,
                     (unsigned)g_node_prov.status.auto_close_remaining_sec,
                     fallback_status.initialized ? "true" : "false",
                     fallback_status.enabled ? "true" : "false",
                     node_fallback_runtime_state_name(fallback_status.state),
                     fallback_status.wifi_ready ? "true" : "false",
                     fallback_status.mqtt_connected ? "true" : "false",
                     fallback_status.fallback_rules_active ? "true" : "false",
                     (unsigned long)fallback_status.fallback_timeout_ms,
                     (unsigned long)fallback_status.fallback_return_delay_ms,
                     node_fallback_runtime_return_policy_name(fallback_status.return_policy),
                     nfc_status.initialized ? "true" : "false",
                     nfc_status.started ? "true" : "false",
                     nfc_status.enabled ? "true" : "false",
                     nfc_status.driver_ready ? "true" : "false",
                     health_json,
                     state_json,
                     error_code_json,
                     reader_id_json,
                     nfc_status.card_present ? "true" : "false",
                     (unsigned long)nfc_status.seen_count,
                     (long)nfc_status.token_id,
                     uid_json,
                     last_seen_uid_json);
    if (n < 0 || n >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status too large");
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t resp = httpd_resp_send(req, s_config_json, (ssize_t)n);
    unlock_config_json();
    return resp;
}

esp_err_t node_provisioning_keep_open_post(httpd_req_t *req)
{
    if (!g_node_prov.status.auto_close_supported) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
                                  "{\"ok\":true,\"auto_close_supported\":false,"
                                  "\"auto_close_keep_open\":false}");
    }

    node_provisioning_keep_open_for_boot();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req,
                              "{\"ok\":true,\"auto_close_supported\":true,"
                              "\"auto_close_keep_open\":true}");
}

esp_err_t node_provisioning_config_get(httpd_req_t *req)
{
    char node_id_json[NODE_ID_MAX_LEN * 2 + 1];
    char node_name_json[NODE_NAME_MAX_LEN * 2 + 1];
    char wifi_ssid_json[NODE_WIFI_SSID_MAX_LEN * 2 + 1];
    char controller_host_json[NODE_HOST_MAX_LEN * 2 + 1];
    char mqtt_client_id_json[NODE_MQTT_CLIENT_ID_MAX_LEN * 2 + 1];
    const char *fallback_return_policy = "auto_on_stable_mqtt";
    bool has_nfc_reader = false;
    const char *operation_mode = "scenehub";
    int n = 0;
    size_t len = 0;

    if (!ensure_provisioning_scratch()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (!lock_config_json()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }
    if (!ensure_config_json_buffer()) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (node_admin_control_get_config(&s_get_config) != ESP_OK) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    }
    if (!json_escape_string(node_id_json, sizeof(node_id_json), s_get_config.node_id) ||
        !json_escape_string(node_name_json, sizeof(node_name_json), s_get_config.node_name) ||
        !json_escape_string(wifi_ssid_json, sizeof(wifi_ssid_json), s_get_config.wifi_ssid) ||
        !json_escape_string(controller_host_json, sizeof(controller_host_json), s_get_config.controller_host) ||
        !json_escape_string(mqtt_client_id_json, sizeof(mqtt_client_id_json), s_get_config.mqtt_client_id)) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
    }
    operation_mode = node_runtime_mode_name((node_operation_mode_t)s_get_config.operation_mode);
    fallback_return_policy = fallback_return_policy_text(s_get_config.fallback_return_policy);
    memset(&s_nfc_reader_scratch, 0, sizeof(s_nfc_reader_scratch));
    has_nfc_reader = node_driver_nfc_reader_load_factory_config(&s_get_config, &s_nfc_reader_scratch) == ESP_OK;

    n = snprintf(s_config_json,
                 NODE_PROVISIONING_CONFIG_JSON_CAPACITY,
                 "{\"ok\":true,\"node_id\":\"%s\",\"node_name\":\"%s\","
                 "\"wifi_ssid\":\"%s\",\"controller_host\":\"%s\","
                 "\"mqtt_port\":%u,\"mqtt_client_id\":\"%s\","
                 "\"reset_gpio\":%d,\"operation_mode\":\"%s\",\"standalone_mqtt_enabled\":%s,"
                 "\"fallback_timeout_ms\":%lu,\"fallback_return_delay_ms\":%lu,"
                 "\"fallback_return_policy\":\"%s\",\"pin_config_locked\":%s,"
                 "\"relays\":[",
                 node_id_json,
                 node_name_json,
                 wifi_ssid_json,
                 controller_host_json,
                 (unsigned)s_get_config.mqtt_port,
                 mqtt_client_id_json,
                 s_get_config.reset_gpio,
                 operation_mode,
                 s_get_config.standalone_mqtt_enabled ? "true" : "false",
                 (unsigned long)s_get_config.fallback_timeout_ms,
                 (unsigned long)s_get_config.fallback_return_delay_ms,
                 fallback_return_policy,
                 s_get_config.pin_config_locked ? "true" : "false");
    if (n < 0 || n >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
    }
    len = (size_t)n;
#define APPEND_JSON(...) do { \
        int _n = snprintf(s_config_json + len, NODE_PROVISIONING_CONFIG_JSON_CAPACITY - len, __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY - len) { \
            unlock_config_json(); \
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large"); \
        } \
        len += (size_t)_n; \
    } while (0)
    for (size_t i = 0; i < NODE_RELAY_MAX; ++i) {
        const node_output_pin_config_t *p = &s_get_config.relays[i];
        char label_json[sizeof(p->label) * 2 + 1];
        if (!json_escape_string(label_json, sizeof(label_json), p->label)) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
        }
        APPEND_JSON("%s{\"enabled\":%s,\"gpio\":%d,\"active_low\":%s,\"label\":\"%s\"}",
                    i ? "," : "",
                    p->enabled ? "true" : "false",
                    p->gpio,
                    p->active_low ? "true" : "false",
                    label_json);
    }
    APPEND_JSON("],\"mosfets\":[");
    for (size_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        const node_output_pin_config_t *p = &s_get_config.mosfets[i];
        char label_json[sizeof(p->label) * 2 + 1];
        if (!json_escape_string(label_json, sizeof(label_json), p->label)) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
        }
        APPEND_JSON("%s{\"enabled\":%s,\"gpio\":%d,\"active_low\":%s,\"pulse_duration_ms\":%u,\"fade_duration_ms\":%u,\"blink_on_ms\":%u,\"blink_off_ms\":%u,\"blink_repeat_count\":%u,\"breathe_fade_ms\":%u,\"breathe_hold_ms\":%u,\"breathe_repeat_count\":%u,\"default_value\":%u,\"default_target\":%u,\"default_min\":%u,\"default_max\":%u,\"default_final_value\":%u,\"label\":\"%s\"}",
                    i ? "," : "",
                    p->enabled ? "true" : "false",
                    p->gpio,
                    p->active_low ? "true" : "false",
                    (unsigned)p->pulse_duration_ms,
                    (unsigned)p->fade_duration_ms,
                    (unsigned)p->blink_on_ms,
                    (unsigned)p->blink_off_ms,
                    (unsigned)p->blink_repeat_count,
                    (unsigned)p->breathe_fade_ms,
                    (unsigned)p->breathe_hold_ms,
                    (unsigned)p->breathe_repeat_count,
                    (unsigned)p->default_value,
                    (unsigned)p->default_target,
                    (unsigned)p->default_min,
                    (unsigned)p->default_max,
                    (unsigned)p->default_final_value,
                    label_json);
    }
    APPEND_JSON("],\"universal_io\":[");
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_universal_pin_config_t *p = &s_get_config.universal_io[i];
        char label_json[sizeof(p->label) * 2 + 1];
        if (!json_escape_string(label_json, sizeof(label_json), p->label)) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
        }
        APPEND_JSON("%s{\"enabled\":%s,\"gpio\":%d,\"role\":%d,\"active_low\":%s,\"debounce_ms\":%u,\"label\":\"%s\"}",
                    i ? "," : "",
                    p->enabled ? "true" : "false",
                    p->gpio,
                    (int)p->role,
                    p->active_low ? "true" : "false",
                    (unsigned)p->debounce_ms,
                    label_json);
    }
    APPEND_JSON("],\"led_strips\":[");
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *p = &s_get_config.led_strips[i];
        char label_json[sizeof(p->label) * 2 + 1];
        if (!json_escape_string(label_json, sizeof(label_json), p->label)) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
        }
        APPEND_JSON("%s{\"enabled\":%s,\"gpio\":%d,\"pixel_count\":%u,\"chipset\":\"%s\",\"color_order\":\"%s\",\"rgbw\":%s,\"label\":\"%s\"}",
                    i ? "," : "",
                    p->enabled ? "true" : "false",
                    p->gpio,
                    (unsigned)p->pixel_count,
                    led_chipset_text(p->chipset),
                    led_color_order_text(p->color_order),
                    p->rgbw ? "true" : "false",
                    label_json);
    }
    if (has_nfc_reader) {
        char id_json[sizeof(s_nfc_reader_scratch.id) * 2 + 1];
        char driver_json[sizeof(s_nfc_reader_scratch.driver_impl) * 2 + 1];
        char bus_json[sizeof(s_nfc_reader_scratch.bus) * 2 + 1];

        if (!json_escape_string(id_json, sizeof(id_json), s_nfc_reader_scratch.id) ||
            !json_escape_string(driver_json, sizeof(driver_json), s_nfc_reader_scratch.driver_impl) ||
            !json_escape_string(bus_json, sizeof(bus_json), s_nfc_reader_scratch.bus)) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
        }
        APPEND_JSON("],\"nfc_reader\":{\"enabled\":%s,\"id\":\"%s\",\"driver\":\"%s\",\"bus\":\"%s\","
                    "\"i2c_sda_gpio\":%d,\"i2c_scl_gpio\":%d,\"reset_gpio\":%d,\"i2c_address\":%u,"
                    "\"poll_interval_ms\":%lu,\"debounce_ms\":%lu,\"known_cards_max\":%u,\"known_cards\":[",
                    s_nfc_reader_scratch.enabled ? "true" : "false",
                    id_json,
                    driver_json,
                    bus_json,
                    s_nfc_reader_scratch.i2c_sda_gpio,
                    s_nfc_reader_scratch.i2c_scl_gpio,
                    s_nfc_reader_scratch.reset_gpio,
                    (unsigned)s_nfc_reader_scratch.i2c_address,
                    (unsigned long)s_nfc_reader_scratch.poll_interval_ms,
                    (unsigned long)s_nfc_reader_scratch.debounce_ms,
                    (unsigned)NODE_DRIVER_NFC_KNOWN_CARD_MAX);
        for (size_t i = 0; i < s_nfc_reader_scratch.known_card_count; ++i) {
            char name_json[sizeof(s_nfc_reader_scratch.known_cards[i].name) * 2 + 1];
            char uid_json[sizeof(s_nfc_reader_scratch.known_cards[i].uid) * 2 + 1];
            char event_json[sizeof(s_nfc_reader_scratch.known_cards[i].event_name) * 2 + 1];

            if (!json_escape_string(name_json, sizeof(name_json), s_nfc_reader_scratch.known_cards[i].name) ||
                !json_escape_string(uid_json, sizeof(uid_json), s_nfc_reader_scratch.known_cards[i].uid) ||
                !json_escape_string(event_json, sizeof(event_json), s_nfc_reader_scratch.known_cards[i].event_name)) {
                unlock_config_json();
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
            }
            APPEND_JSON("%s{\"name\":\"%s\",\"uid\":\"%s\",\"token_id\":%ld,\"event\":\"%s\"}",
                        i ? "," : "",
                        name_json,
                        uid_json,
                        (long)s_nfc_reader_scratch.known_cards[i].token_id,
                        event_json);
        }
        APPEND_JSON("]}}");
    } else {
        APPEND_JSON("],\"nfc_reader\":null}");
    }
#undef APPEND_JSON
    httpd_resp_set_type(req, "application/json");
    esp_err_t resp = httpd_resp_send(req, s_config_json, (ssize_t)len);
    unlock_config_json();
    return resp;
}

esp_err_t node_provisioning_led_config_get(httpd_req_t *req)
{
    int n = 0;
    size_t len = 0;

    if (!ensure_provisioning_scratch()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (!lock_config_json()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }
    if (!ensure_config_json_buffer()) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (node_admin_control_get_config(&s_get_config) != ESP_OK) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    }

    n = snprintf(s_config_json, NODE_PROVISIONING_CONFIG_JSON_CAPACITY, "{\"ok\":true,\"led_strips\":[");
    if (n < 0 || n >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "led config too large");
    }
    len = (size_t)n;
#define APPEND_LED_JSON(...) do { \
        int _n = snprintf(s_config_json + len, NODE_PROVISIONING_CONFIG_JSON_CAPACITY - len, __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY - len) { \
            unlock_config_json(); \
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "led config too large"); \
        } \
        len += (size_t)_n; \
    } while (0)
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *p = &s_get_config.led_strips[i];
        char blink_color[16];
        char breathe_color[16];
        format_led_color_text(blink_color, sizeof(blink_color), p->blink.red, p->blink.green, p->blink.blue, p->blink.white);
        format_led_color_text(breathe_color, sizeof(breathe_color), p->breathe.red, p->breathe.green, p->breathe.blue, p->breathe.white);
        APPEND_LED_JSON("%s{\"blink\":{\"on_ms\":%u,\"off_ms\":%u,\"repeat_count\":%u,\"color\":\"%s\"},\"breathe\":{\"cycle_ms\":%u,\"step_ms\":%u,\"repeat_count\":%u,\"color\":\"%s\"},\"effects\":{",
                        i ? "," : "",
                        (unsigned)p->blink.on_ms,
                        (unsigned)p->blink.off_ms,
                        (unsigned)p->blink.repeat_count,
                        blink_color,
                        (unsigned)p->breathe.cycle_ms,
                        (unsigned)p->breathe.step_ms,
                        (unsigned)p->breathe.repeat_count,
                        breathe_color);
        bool first_effect = true;
        for (size_t effect_index = 0; effect_index < node_led_effect_descriptor_count(); ++effect_index) {
            const node_led_effect_descriptor_t *desc = &node_led_effect_descriptors()[effect_index];
            const node_led_effect_preset_t *effect = &p->effects.items[desc->id];
            char color[16];
            char secondary_color[16];
            char background_color[16];
            if (!node_led_effect_is_advanced(desc->id)) {
                continue;
            }
            format_led_color_text(color, sizeof(color), effect->red, effect->green, effect->blue, effect->white);
            format_led_color_text(secondary_color, sizeof(secondary_color), effect->red2, effect->green2, effect->blue2, effect->white2);
            format_led_color_text(background_color, sizeof(background_color), effect->bg_red, effect->bg_green, effect->bg_blue, effect->bg_white);
            APPEND_LED_JSON("%s\"%s\":{\"duration_ms\":%u,\"step_ms\":%u,\"repeat_count\":%u,\"size\":%u,\"intensity\":%u,\"density\":%u,\"fade\":%u,\"palette_mode\":\"%s\",\"color\":\"%s\",\"secondary_color\":\"%s\",\"background_color\":\"%s\"}",
                            first_effect ? "" : ",",
                            desc->name,
                            (unsigned)effect->duration_ms,
                            (unsigned)effect->step_ms,
                            (unsigned)effect->repeat_count,
                            (unsigned)effect->size,
                            (unsigned)effect->intensity,
                            (unsigned)effect->density,
                            (unsigned)effect->fade,
                            node_led_palette_mode_name(effect->palette_mode),
                            color,
                            secondary_color,
                            background_color);
            first_effect = false;
        }
        APPEND_LED_JSON("}}");
    }
    APPEND_LED_JSON("]}");
#undef APPEND_LED_JSON
    httpd_resp_set_type(req, "application/json");
    esp_err_t resp = httpd_resp_send(req, s_config_json, (ssize_t)len);
    unlock_config_json();
    return resp;
}

esp_err_t node_provisioning_led_effects_schema_get(httpd_req_t *req)
{
    const node_led_effect_defaults_t *blink_defaults = node_led_effect_defaults(NODE_LED_EFFECT_BLINK);
    const node_led_effect_defaults_t *breathe_defaults = node_led_effect_defaults(NODE_LED_EFFECT_BREATHE);
    bool first_effect = true;
    int n = 0;
    size_t len = 0;

    if (!lock_config_json()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema busy");
    }
    if (!ensure_config_json_buffer()) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema no mem");
    }

    n = snprintf(s_config_json,
                 NODE_PROVISIONING_CONFIG_JSON_CAPACITY,
                 "{\"ok\":true,\"blink_defaults\":{\"on_ms\":%lu,\"off_ms\":%lu,\"repeat_count\":%u,\"color\":\"#%02x%02x%02x\"},"
                 "\"breathe_defaults\":{\"cycle_ms\":%lu,\"step_ms\":%lu,\"repeat_count\":%u,\"color\":\"#%02x%02x%02x\"},"
                 "\"groups\":[",
                 blink_defaults ? (unsigned long)blink_defaults->duration_ms : 500UL,
                 blink_defaults ? (unsigned long)blink_defaults->step_ms : 500UL,
                 blink_defaults ? (unsigned)blink_defaults->repeat_count : 0U,
                 blink_defaults ? (unsigned)blink_defaults->red : 255U,
                 blink_defaults ? (unsigned)blink_defaults->green : 255U,
                 blink_defaults ? (unsigned)blink_defaults->blue : 255U,
                 breathe_defaults ? (unsigned long)breathe_defaults->duration_ms : 2000UL,
                 breathe_defaults ? (unsigned long)breathe_defaults->step_ms : 40UL,
                 breathe_defaults ? (unsigned)breathe_defaults->repeat_count : 0U,
                 breathe_defaults ? (unsigned)breathe_defaults->red : 255U,
                 breathe_defaults ? (unsigned)breathe_defaults->green : 255U,
                 breathe_defaults ? (unsigned)breathe_defaults->blue : 255U);
    if (n < 0 || n >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large");
    }
    len = (size_t)n;
#define APPEND_SCHEMA_JSON(...) do { \
        int _n = snprintf(s_config_json + len, NODE_PROVISIONING_CONFIG_JSON_CAPACITY - len, __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY - len) { \
            unlock_config_json(); \
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large"); \
        } \
        len += (size_t)_n; \
    } while (0)

    for (size_t i = 0; i < node_led_effect_group_count(); ++i) {
        char group_json[64];
        if (!json_escape_string(group_json, sizeof(group_json), node_led_effect_group_name(i))) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large");
        }
        APPEND_SCHEMA_JSON("%s\"%s\"", i ? "," : "", group_json);
    }

    APPEND_SCHEMA_JSON("],\"effects\":[");
    for (size_t i = 0; i < node_led_effect_descriptor_count(); ++i) {
        const node_led_effect_descriptor_t *desc = &node_led_effect_descriptors()[i];
        char name_json[64];
        char label_json[128];
        char group_json[64];
        char note_json[256];
        if (!node_led_effect_is_advanced(desc->id)) {
            continue;
        }
        if (!json_escape_string(name_json, sizeof(name_json), desc->name) ||
            !json_escape_string(label_json, sizeof(label_json), desc->label) ||
            !json_escape_string(group_json, sizeof(group_json), desc->group ? desc->group : "Other") ||
            !json_escape_string(note_json, sizeof(note_json), desc->note ? desc->note : "")) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large");
        }
        APPEND_SCHEMA_JSON("%s{\"name\":\"%s\",\"label\":\"%s\",\"group\":\"%s\",\"note\":\"%s\",\"controls\":[",
                           first_effect ? "" : ",",
                           name_json,
                           label_json,
                           group_json,
                           note_json);
        if (append_effect_controls_json(s_config_json, NODE_PROVISIONING_CONFIG_JSON_CAPACITY, &len, desc->controls) != ESP_OK) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large");
        }
        APPEND_SCHEMA_JSON("],\"defaults\":{");
        if (append_effect_defaults_json(s_config_json, NODE_PROVISIONING_CONFIG_JSON_CAPACITY, &len, desc) != ESP_OK) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large");
        }
        APPEND_SCHEMA_JSON("}}");
        first_effect = false;
    }
    APPEND_SCHEMA_JSON("]}");
#undef APPEND_SCHEMA_JSON

    httpd_resp_set_type(req, "application/json");
    esp_err_t resp = httpd_resp_send(req, s_config_json, (ssize_t)len);
    unlock_config_json();
    return resp;
}

static bool json_copy_string_field(const char *json, const char *key, char *out, size_t out_size)
{
    if (!json || !key || !out || out_size == 0) {
        return false;
    }
    char pattern[48];
    int pn = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pn <= 0 || pn >= (int)sizeof(pattern)) {
        return false;
    }
    const char *p = strstr(json, pattern);
    if (!p || !(p = strchr(p + pn, ':'))) {
        return false;
    }
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    if (*p++ != '"') {
        return false;
    }
    size_t len = 0;
    while (p[len] && p[len] != '"' && len + 1 < out_size) {
        if (p[len] == '\\') {
            return false;
        }
        out[len] = p[len];
        ++len;
    }
    if (p[len] != '"') {
        out[0] = '\0';
        return false;
    }
    out[len] = '\0';
    return true;
}

static bool json_copy_nonempty_string_field(const char *json, const char *key, char *out, size_t out_size)
{
    char scratch[NODE_WIFI_PASSWORD_MAX_LEN];
    if (!json_copy_string_field(json, key, scratch, sizeof(scratch))) {
        return false;
    }
    if (scratch[0] == '\0') {
        return false;
    }
    snprintf(out, out_size, "%s", scratch);
    return true;
}

static bool json_copy_int_field(const char *json, const char *key, int *out)
{
    char pattern[48];
    int pn = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pn <= 0 || pn >= (int)sizeof(pattern)) {
        return false;
    }
    const char *p = strstr(json, pattern);
    return p && (p = strchr(p + pn, ':')) && sscanf(p + 1, "%d", out) == 1;
}

static bool json_copy_u16_field(const char *json, const char *key, uint16_t *out)
{
    int value = 0;
    if (!json_copy_int_field(json, key, &value) || value < 0 || value > 65535) {
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

static bool json_copy_u32_field(const char *json, const char *key, uint32_t *out)
{
    int value = 0;
    if (!out || !json_copy_int_field(json, key, &value) || value < 0) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool json_copy_bool_field(const char *json, const char *key, bool *out)
{
    char pattern[48];
    int pn = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pn <= 0 || pn >= (int)sizeof(pattern)) {
        return false;
    }
    const char *p = strstr(json, pattern);
    if (!p || !(p = strchr(p + pn, ':'))) {
        return false;
    }
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static void json_copy_led_chipset_field(const char *json, const char *key, node_led_chipset_t *out)
{
    char value[16];
    if (!out || !json_copy_string_field(json, key, value, sizeof(value))) {
        return;
    }
    if (strcmp(value, "ws2812") == 0) {
        *out = NODE_LED_CHIPSET_WS2812;
    } else if (strcmp(value, "ws2815") == 0) {
        *out = NODE_LED_CHIPSET_WS2815;
    } else if (strcmp(value, "sk6812") == 0) {
        *out = NODE_LED_CHIPSET_SK6812;
    }
}

static void json_copy_led_color_order_field(const char *json, const char *key, node_led_color_order_t *out)
{
    char value[16];
    if (!out || !json_copy_string_field(json, key, value, sizeof(value))) {
        return;
    }
    if (strcmp(value, "rgb") == 0) {
        *out = NODE_LED_COLOR_ORDER_RGB;
    } else if (strcmp(value, "rbg") == 0) {
        *out = NODE_LED_COLOR_ORDER_RBG;
    } else if (strcmp(value, "grb") == 0) {
        *out = NODE_LED_COLOR_ORDER_GRB;
    } else if (strcmp(value, "gbr") == 0) {
        *out = NODE_LED_COLOR_ORDER_GBR;
    } else if (strcmp(value, "brg") == 0) {
        *out = NODE_LED_COLOR_ORDER_BRG;
    } else if (strcmp(value, "bgr") == 0) {
        *out = NODE_LED_COLOR_ORDER_BGR;
    }
}

static void make_indexed_key(char *out, size_t out_size, const char *prefix, size_t index, const char *field)
{
    snprintf(out, out_size, "%s%u_%s", prefix, (unsigned)(index + 1), field);
}

static void apply_output_pin_fields(const char *body, const char *prefix, node_output_pin_config_t *pins, size_t count)
{
    char key[40];
    for (size_t i = 0; i < count; ++i) {
        node_output_pin_config_t *pin = &pins[i];
        make_indexed_key(key, sizeof(key), prefix, i, "enabled");
        json_copy_bool_field(body, key, &pin->enabled);
        make_indexed_key(key, sizeof(key), prefix, i, "gpio");
        json_copy_int_field(body, key, &pin->gpio);
        make_indexed_key(key, sizeof(key), prefix, i, "active_low");
        json_copy_bool_field(body, key, &pin->active_low);
        make_indexed_key(key, sizeof(key), prefix, i, "label");
        json_copy_string_field(body, key, pin->label, sizeof(pin->label));
        if (!node_board_gpio_is_allowed(pin->gpio)) {
            pin->enabled = false;
            pin->gpio = -1;
        }
    }
}

static void apply_mosfet_fields(const char *body, node_output_pin_config_t *pins, size_t count)
{
    char key[48];
    for (size_t i = 0; i < count; ++i) {
        node_output_pin_config_t *pin = &pins[i];
        int value = 0;
        make_indexed_key(key, sizeof(key), "mosfet", i, "pulse_duration_ms");
        value = (int)pin->pulse_duration_ms;
        json_copy_int_field(body, key, &value);
        pin->pulse_duration_ms = value > 0 ? (uint32_t)value : pin->pulse_duration_ms;
        make_indexed_key(key, sizeof(key), "mosfet", i, "fade_duration_ms");
        value = (int)pin->fade_duration_ms;
        json_copy_int_field(body, key, &value);
        pin->fade_duration_ms = value > 0 ? (uint32_t)value : pin->fade_duration_ms;
        make_indexed_key(key, sizeof(key), "mosfet", i, "blink_on_ms");
        value = (int)pin->blink_on_ms;
        json_copy_int_field(body, key, &value);
        pin->blink_on_ms = value > 0 ? (uint32_t)value : pin->blink_on_ms;
        make_indexed_key(key, sizeof(key), "mosfet", i, "blink_off_ms");
        value = (int)pin->blink_off_ms;
        json_copy_int_field(body, key, &value);
        pin->blink_off_ms = value > 0 ? (uint32_t)value : pin->blink_off_ms;
        make_indexed_key(key, sizeof(key), "mosfet", i, "blink_repeat_count");
        value = (int)pin->blink_repeat_count;
        json_copy_int_field(body, key, &value);
        pin->blink_repeat_count = value > 0 ? (uint16_t)value : pin->blink_repeat_count;
        make_indexed_key(key, sizeof(key), "mosfet", i, "breathe_fade_ms");
        value = (int)pin->breathe_fade_ms;
        json_copy_int_field(body, key, &value);
        pin->breathe_fade_ms = value > 0 ? (uint32_t)value : pin->breathe_fade_ms;
        make_indexed_key(key, sizeof(key), "mosfet", i, "breathe_hold_ms");
        value = (int)pin->breathe_hold_ms;
        json_copy_int_field(body, key, &value);
        pin->breathe_hold_ms = value >= 0 ? (uint32_t)value : pin->breathe_hold_ms;
        make_indexed_key(key, sizeof(key), "mosfet", i, "breathe_repeat_count");
        value = (int)pin->breathe_repeat_count;
        json_copy_int_field(body, key, &value);
        pin->breathe_repeat_count = value > 0 ? (uint16_t)value : pin->breathe_repeat_count;
        make_indexed_key(key, sizeof(key), "mosfet", i, "default_value");
        value = (int)pin->default_value;
        json_copy_int_field(body, key, &value);
        if (value >= 0 && value <= 255) pin->default_value = (uint8_t)value;
        make_indexed_key(key, sizeof(key), "mosfet", i, "default_target");
        value = (int)pin->default_target;
        json_copy_int_field(body, key, &value);
        if (value >= 0 && value <= 255) pin->default_target = (uint8_t)value;
        make_indexed_key(key, sizeof(key), "mosfet", i, "default_min");
        value = (int)pin->default_min;
        json_copy_int_field(body, key, &value);
        if (value >= 0 && value <= 255) pin->default_min = (uint8_t)value;
        make_indexed_key(key, sizeof(key), "mosfet", i, "default_max");
        value = (int)pin->default_max;
        json_copy_int_field(body, key, &value);
        if (value >= 0 && value <= 255) pin->default_max = (uint8_t)value;
        make_indexed_key(key, sizeof(key), "mosfet", i, "default_final_value");
        value = (int)pin->default_final_value;
        json_copy_int_field(body, key, &value);
        if (value >= 0 && value <= 255) pin->default_final_value = (uint8_t)value;
    }
}

static void apply_universal_io_fields(const char *body, node_universal_pin_config_t *pins, size_t count)
{
    char key[40];
    for (size_t i = 0; i < count; ++i) {
        node_universal_pin_config_t *pin = &pins[i];
        int role = (int)pin->role;
        make_indexed_key(key, sizeof(key), "io", i, "enabled");
        json_copy_bool_field(body, key, &pin->enabled);
        make_indexed_key(key, sizeof(key), "io", i, "gpio");
        json_copy_int_field(body, key, &pin->gpio);
        make_indexed_key(key, sizeof(key), "io", i, "role");
        json_copy_int_field(body, key, &role);
        make_indexed_key(key, sizeof(key), "io", i, "active_low");
        json_copy_bool_field(body, key, &pin->active_low);
        make_indexed_key(key, sizeof(key), "io", i, "debounce_ms");
        {
            int debounce_ms = (int)pin->debounce_ms;
            json_copy_int_field(body, key, &debounce_ms);
            pin->debounce_ms = (uint16_t)clamp_u32_or_default(debounce_ms, pin->debounce_ms, NODE_INPUT_DEBOUNCE_MAX_MS);
        }
        make_indexed_key(key, sizeof(key), "io", i, "label");
        json_copy_string_field(body, key, pin->label, sizeof(pin->label));
        pin->role = role == (int)NODE_PIN_UNIVERSAL_INPUT
                        ? NODE_PIN_UNIVERSAL_INPUT
                        : (role == (int)NODE_PIN_UNIVERSAL_OUTPUT ? NODE_PIN_UNIVERSAL_OUTPUT : NODE_PIN_DISABLED);
        ESP_LOGI(TAG,
                 "config io%u requested enabled=%d gpio=%d role=%d debounce_ms=%u label=%s",
                 (unsigned)pin->channel,
                 pin->enabled,
                 pin->gpio,
                 (int)pin->role,
                 (unsigned)pin->debounce_ms,
                 pin->label);
        if (pin->role != NODE_PIN_UNIVERSAL_INPUT) {
            pin->debounce_ms = 0;
        }
        if (!node_board_gpio_is_allowed(pin->gpio) || pin->role == NODE_PIN_DISABLED) {
            pin->enabled = false;
            pin->gpio = -1;
            pin->debounce_ms = 0;
        }
    }
}

static void apply_led_editor_fields(const char *body, node_led_strip_config_t *pins, size_t count)
{
    char key[64];
    for (size_t i = 0; i < count; ++i) {
        node_led_strip_config_t *pin = &pins[i];
        int blink_on_ms = (int)pin->blink.on_ms;
        int blink_off_ms = (int)pin->blink.off_ms;
        int blink_repeat_count = (int)pin->blink.repeat_count;
        int breathe_cycle_ms = (int)pin->breathe.cycle_ms;
        int breathe_step_ms = (int)pin->breathe.step_ms;
        int breathe_repeat_count = (int)pin->breathe.repeat_count;
        make_indexed_key(key, sizeof(key), "led", i, "blink_on_ms");
        json_copy_int_field(body, key, &blink_on_ms);
        make_indexed_key(key, sizeof(key), "led", i, "blink_off_ms");
        json_copy_int_field(body, key, &blink_off_ms);
        make_indexed_key(key, sizeof(key), "led", i, "blink_repeat_count");
        json_copy_int_field(body, key, &blink_repeat_count);
        make_indexed_key(key, sizeof(key), "led", i, "blink_color");
        json_copy_led_color_field(body, key, &pin->blink.red, &pin->blink.green, &pin->blink.blue, &pin->blink.white);
        make_indexed_key(key, sizeof(key), "led", i, "breathe_cycle_ms");
        json_copy_int_field(body, key, &breathe_cycle_ms);
        make_indexed_key(key, sizeof(key), "led", i, "breathe_step_ms");
        json_copy_int_field(body, key, &breathe_step_ms);
        make_indexed_key(key, sizeof(key), "led", i, "breathe_repeat_count");
        json_copy_int_field(body, key, &breathe_repeat_count);
        make_indexed_key(key, sizeof(key), "led", i, "breathe_color");
        json_copy_led_color_field(body, key, &pin->breathe.red, &pin->breathe.green, &pin->breathe.blue, &pin->breathe.white);
        pin->blink.on_ms = blink_on_ms > 0
                               ? clamp_u32_or_default(blink_on_ms, 500U, NODE_PROV_LED_EDITOR_MAX_TIMING_MS)
                               : 500U;
        pin->blink.off_ms = blink_off_ms > 0
                                ? clamp_u32_or_default(blink_off_ms, 500U, NODE_PROV_LED_EDITOR_MAX_TIMING_MS)
                                : 500U;
        pin->blink.repeat_count = clamp_u16_or_default(blink_repeat_count, pin->blink.repeat_count);
        pin->breathe.cycle_ms = breathe_cycle_ms > 0
                                    ? clamp_u32_or_default(breathe_cycle_ms, 2000U, NODE_PROV_LED_EDITOR_MAX_TIMING_MS)
                                    : 2000U;
        pin->breathe.step_ms = breathe_step_ms > 0
                                   ? clamp_u32_or_default(breathe_step_ms, 40U, NODE_PROV_LED_EDITOR_MAX_TIMING_MS)
                                   : 40U;
        pin->breathe.repeat_count = clamp_u16_or_default(breathe_repeat_count, pin->breathe.repeat_count);
        for (size_t effect_index = 0; effect_index < node_led_effect_descriptor_count(); ++effect_index) {
            const node_led_effect_descriptor_t *desc = &node_led_effect_descriptors()[effect_index];
            node_led_effect_preset_t *effect = &pin->effects.items[desc->id];
            int duration_ms = (int)effect->duration_ms;
            int step_ms = (int)effect->step_ms;
            int repeat_count = (int)effect->repeat_count;
            int size = (int)effect->size;
            int intensity = (int)effect->intensity;
            int density = (int)effect->density;
            int fade = (int)effect->fade;
            char palette_mode[16] = {0};
            char color_value[16] = {0};
            uint8_t legacy_red = 0;
            uint8_t legacy_green = 0;
            uint8_t legacy_blue = 0;
            uint8_t legacy_white = 0;
            bool has_secondary = false;
            bool has_background = false;
            if (!node_led_effect_is_advanced(desc->id)) {
                continue;
            }
            snprintf(palette_mode, sizeof(palette_mode), "%s", node_led_palette_mode_name(effect->palette_mode));
            make_led_effect_key(key, sizeof(key), i, desc->name, "duration_ms");
            json_copy_int_field(body, key, &duration_ms);
            make_led_effect_key(key, sizeof(key), i, desc->name, "step_ms");
            json_copy_int_field(body, key, &step_ms);
            make_led_effect_key(key, sizeof(key), i, desc->name, "repeat_count");
            json_copy_int_field(body, key, &repeat_count);
            make_led_effect_key(key, sizeof(key), i, desc->name, "size");
            json_copy_int_field(body, key, &size);
            make_led_effect_key(key, sizeof(key), i, desc->name, "intensity");
            json_copy_int_field(body, key, &intensity);
            make_led_effect_key(key, sizeof(key), i, desc->name, "density");
            json_copy_int_field(body, key, &density);
            make_led_effect_key(key, sizeof(key), i, desc->name, "fade");
            json_copy_int_field(body, key, &fade);
            make_led_effect_key(key, sizeof(key), i, desc->name, "palette_mode");
            json_copy_string_field(body, key, palette_mode, sizeof(palette_mode));
            make_led_effect_key(key, sizeof(key), i, desc->name, "color");
            json_copy_led_color_field(body, key, &effect->red, &effect->green, &effect->blue, &effect->white);
            make_led_effect_key(key, sizeof(key), i, desc->name, "secondary_color");
            has_secondary = json_copy_string_field(body, key, color_value, sizeof(color_value));
            if (has_secondary) {
                (void)parse_color_text(color_value, &effect->red2, &effect->green2, &effect->blue2, &effect->white2);
            }
            make_led_effect_key(key, sizeof(key), i, desc->name, "background_color");
            has_background = json_copy_string_field(body, key, color_value, sizeof(color_value));
            if (has_background) {
                (void)parse_color_text(color_value, &effect->bg_red, &effect->bg_green, &effect->bg_blue, &effect->bg_white);
            }
            make_led_effect_key(key, sizeof(key), i, desc->name, "color2");
            if (!has_secondary &&
                !has_background &&
                json_copy_string_field(body, key, color_value, sizeof(color_value)) &&
                parse_color_text(color_value, &legacy_red, &legacy_green, &legacy_blue, &legacy_white)) {
                if (desc->controls & NODE_LED_CTRL_SECONDARY_COLOR) {
                    effect->red2 = legacy_red;
                    effect->green2 = legacy_green;
                    effect->blue2 = legacy_blue;
                    effect->white2 = legacy_white;
                }
                if ((desc->controls & NODE_LED_CTRL_BACKGROUND_COLOR) &&
                    !(desc->controls & NODE_LED_CTRL_SECONDARY_COLOR)) {
                    effect->bg_red = legacy_red;
                    effect->bg_green = legacy_green;
                    effect->bg_blue = legacy_blue;
                    effect->bg_white = legacy_white;
                }
            }
            effect->duration_ms = clamp_u32_or_default(duration_ms,
                                                       effect->duration_ms,
                                                       NODE_PROV_LED_EDITOR_MAX_TIMING_MS);
            effect->step_ms = clamp_u32_or_default(step_ms,
                                                   effect->step_ms,
                                                   NODE_PROV_LED_EDITOR_MAX_TIMING_MS);
            effect->repeat_count = clamp_u16_or_default(repeat_count, effect->repeat_count);
            effect->size = size > 0 ? clamp_u16_or_default(size, effect->size) : effect->size;
            effect->intensity = clamp_u16_or_default(intensity, effect->intensity);
            effect->density = clamp_u16_or_default(density, effect->density);
            effect->fade = clamp_u16_or_default(fade, effect->fade);
            effect->palette_mode = node_led_palette_mode_from_name(palette_mode);
        }
    }
}

static void apply_led_fields(const char *body, node_led_strip_config_t *pins, size_t count)
{
    char key[64];
    for (size_t i = 0; i < count; ++i) {
        node_led_strip_config_t *pin = &pins[i];
        int pixels = pin->pixel_count;
        make_indexed_key(key, sizeof(key), "led", i, "enabled");
        json_copy_bool_field(body, key, &pin->enabled);
        make_indexed_key(key, sizeof(key), "led", i, "gpio");
        json_copy_int_field(body, key, &pin->gpio);
        make_indexed_key(key, sizeof(key), "led", i, "pixel_count");
        json_copy_int_field(body, key, &pixels);
        make_indexed_key(key, sizeof(key), "led", i, "chipset");
        json_copy_led_chipset_field(body, key, &pin->chipset);
        make_indexed_key(key, sizeof(key), "led", i, "color_order");
        json_copy_led_color_order_field(body, key, &pin->color_order);
        make_indexed_key(key, sizeof(key), "led", i, "rgbw");
        json_copy_bool_field(body, key, &pin->rgbw);
        make_indexed_key(key, sizeof(key), "led", i, "label");
        json_copy_string_field(body, key, pin->label, sizeof(pin->label));
        pin->pixel_count = pixels > 0 ? (uint16_t)pixels : 30;
        ESP_LOGI(TAG,
                 "config led%u requested enabled=%d gpio=%d pixels=%u chipset=%s color_order=%s rgbw=%d label=%s",
                 (unsigned)pin->channel,
                 pin->enabled,
                 pin->gpio,
                 (unsigned)pin->pixel_count,
                 led_chipset_text(pin->chipset),
                 led_color_order_text(pin->color_order),
                 pin->rgbw,
                 pin->label);
        if (!node_board_gpio_is_allowed(pin->gpio)) {
            pin->enabled = false;
            pin->gpio = -1;
        }
    }
    apply_led_editor_fields(body, pins, count);
}

esp_err_t node_provisioning_config_post(httpd_req_t *req)
{
    enum { MAX_BODY = 10240 };
    if (req->content_len <= 0 || req->content_len > MAX_BODY) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body size");
    }
    if (!ensure_provisioning_scratch()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (!lock_post_body()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }
    if (!ensure_post_body_buffer()) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }

    if (!read_request_body(req, s_post_body, NODE_PROVISIONING_POST_BODY_CAPACITY)) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
    }

    if (node_admin_control_get_config(&s_next_config) != ESP_OK) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    }
    json_copy_string_field(s_post_body, "node_id", s_next_config.node_id, sizeof(s_next_config.node_id));
    json_copy_string_field(s_post_body, "node_name", s_next_config.node_name, sizeof(s_next_config.node_name));
    json_copy_string_field(s_post_body, "wifi_ssid", s_next_config.wifi_ssid, sizeof(s_next_config.wifi_ssid));
    json_copy_nonempty_string_field(s_post_body, "wifi_password", s_next_config.wifi_password, sizeof(s_next_config.wifi_password));
    json_copy_string_field(s_post_body, "controller_host", s_next_config.controller_host, sizeof(s_next_config.controller_host));
    json_copy_string_field(s_post_body, "mqtt_client_id", s_next_config.mqtt_client_id, sizeof(s_next_config.mqtt_client_id));
    json_copy_u16_field(s_post_body, "mqtt_port", &s_next_config.mqtt_port);
    json_copy_int_field(s_post_body, "reset_gpio", &s_next_config.reset_gpio);
    json_copy_bool_field(s_post_body, "standalone_mqtt_enabled", &s_next_config.standalone_mqtt_enabled);
    json_copy_u32_field(s_post_body, "fallback_timeout_ms", &s_next_config.fallback_timeout_ms);
    json_copy_u32_field(s_post_body, "fallback_return_delay_ms", &s_next_config.fallback_return_delay_ms);
    char operation_mode_name[16] = {0};
    char fallback_return_policy_name[32] = {0};
    if (json_copy_string_field(s_post_body, "operation_mode", operation_mode_name, sizeof(operation_mode_name))) {
        node_operation_mode_t operation_mode = NODE_OPERATION_MODE_SCENEHUB;
        if (!node_runtime_mode_from_name(operation_mode_name, &operation_mode)) {
            unlock_post_body();
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid operation_mode");
        }
        s_next_config.operation_mode = (uint8_t)operation_mode;
    }
    if (json_copy_string_field(s_post_body,
                               "fallback_return_policy",
                               fallback_return_policy_name,
                               sizeof(fallback_return_policy_name))) {
        if (!fallback_return_policy_from_text(fallback_return_policy_name,
                                              &s_next_config.fallback_return_policy)) {
            unlock_post_body();
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid fallback_return_policy");
        }
    }
    if (!s_next_config.pin_config_locked) {
        apply_output_pin_fields(s_post_body, "relay", s_next_config.relays, NODE_RELAY_MAX);
        apply_output_pin_fields(s_post_body, "mosfet", s_next_config.mosfets, NODE_MOSFET_MAX);
        apply_mosfet_fields(s_post_body, s_next_config.mosfets, NODE_MOSFET_MAX);
        apply_universal_io_fields(s_post_body, s_next_config.universal_io, NODE_UNIVERSAL_IO_MAX);
        apply_led_fields(s_post_body, s_next_config.led_strips, NODE_LED_STRIP_MAX);
        if (node_board_sanitize_pin_config(&s_next_config)) {
            ESP_LOGW(TAG, "base config sanitized before save");
        }
    }
    if (s_next_config.mqtt_port == 0) {
        s_next_config.mqtt_port = 1883;
    }
    if (s_next_config.fallback_return_delay_ms == 0) {
        s_next_config.fallback_return_delay_ms = 3000;
    }
    if (s_next_config.node_id[0] == '\0' || s_next_config.mqtt_client_id[0] == '\0') {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "node_id and mqtt_client_id required");
    }

    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_save_base(&s_next_config, &admin_result);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config save failed: %s", esp_err_to_name(err));
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }
    unlock_post_body();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restart_required\":true}");
}

esp_err_t node_provisioning_led_config_post(httpd_req_t *req)
{
    enum { MAX_BODY = 10240 };
    if (req->content_len <= 0 || req->content_len > MAX_BODY) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body size");
    }
    if (!ensure_provisioning_scratch()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (!lock_post_body()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }
    if (!ensure_post_body_buffer()) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }

    if (!read_request_body(req, s_post_body, NODE_PROVISIONING_POST_BODY_CAPACITY)) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
    }

    if (node_admin_control_get_config(&s_next_config) != ESP_OK) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    }
    apply_led_editor_fields(s_post_body, s_next_config.led_strips, NODE_LED_STRIP_MAX);

    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_save_led(s_next_config.led_strips, NODE_LED_STRIP_MAX, &admin_result);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led config save failed: %s", esp_err_to_name(err));
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }
    unlock_post_body();
    if (!admin_result.applied) {
        ESP_LOGE(TAG, "led config apply failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "apply failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"applied\":true,\"restart_required\":false}");
}

esp_err_t node_provisioning_nfc_reader_post(httpd_req_t *req)
{
    enum { MAX_BODY = 8192 };
    char action[24] = {0};
    size_t known_card_count = 0;
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = ESP_OK;

    if (req->content_len <= 0 || req->content_len > MAX_BODY) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body size");
    }
    if (!ensure_provisioning_scratch()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (!lock_post_body()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }
    if (!ensure_post_body_buffer()) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (!read_request_body(req, s_post_body, NODE_PROVISIONING_POST_BODY_CAPACITY)) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
    }
    if (json_copy_string_field(s_post_body, "action", action, sizeof(action))) {
        if (strcmp(action, "reinit") == 0) {
            err = node_admin_control_reinit_nfc(&admin_result);
            unlock_post_body();
            if (err != ESP_OK || !admin_result.applied) {
                ESP_LOGW(TAG, "nfc reader reinit failed: %s", esp_err_to_name(err));
                return send_admin_result_json(req,
                                              HTTPD_500_INTERNAL_SERVER_ERROR,
                                              false,
                                              "reinit_failed",
                                              &admin_result);
            }
            return send_admin_result_json(req, 200, true, "", &admin_result);
        }
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid action");
    }
    err = parse_nfc_cards_request(s_post_body,
                                  s_nfc_cards_scratch,
                                  NODE_DRIVER_NFC_KNOWN_CARD_MAX,
                                  &known_card_count);
    if (err != ESP_OK) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid known_cards");
    }

    err = node_admin_control_save_nfc_cards(s_nfc_cards_scratch, known_card_count, &admin_result);
    unlock_post_body();
    if (err != ESP_OK || !admin_result.applied) {
        ESP_LOGE(TAG, "nfc cards save failed: %s", esp_err_to_name(err));
        return send_admin_result_json(req,
                                      HTTPD_500_INTERNAL_SERVER_ERROR,
                                      false,
                                      "save_failed",
                                      &admin_result);
    }
    return send_admin_result_json(req, 200, true, "", &admin_result);
}

esp_err_t node_provisioning_led_preview_post(httpd_req_t *req)
{
    enum { MAX_BODY = 10240 };
    char command[32] = {0};
    node_control_command_t control_command = {0};
    esp_err_t err = ESP_OK;

    if (req->content_len <= 0 || req->content_len > MAX_BODY) {
        return send_preview_json(req, false, "rejected", "invalid_body_size", "");
    }
    if (!ensure_provisioning_scratch()) {
        return send_preview_json(req, false, "failed", "config_no_mem", "");
    }
    if (!lock_post_body()) {
        return send_preview_json(req, false, "failed", "config_busy", "");
    }
    if (!ensure_post_body_buffer()) {
        unlock_post_body();
        return send_preview_json(req, false, "failed", "config_no_mem", "");
    }
    if (!read_request_body(req, s_post_body, NODE_PROVISIONING_POST_BODY_CAPACITY)) {
        unlock_post_body();
        return send_preview_json(req, false, "failed", "body_read_failed", "");
    }
    if (!json_copy_string_field(s_post_body, "command", command, sizeof(command)) ||
        !led_preview_command_allowed(command)) {
        unlock_post_body();
        return send_preview_json(req, false, "rejected", "invalid_command", command);
    }

    ESP_LOGI(TAG, "led preview command=%s", command);
    control_command.request_id = "led_preview";
    control_command.command = command;
    control_command.args_json = s_post_body;
    control_command.source = NODE_CONTROL_SOURCE_LOCAL_PREVIEW;
    memset(&s_preview_result, 0, sizeof(s_preview_result));
    err = node_control_execute(&control_command, &s_preview_result);
    unlock_post_body();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led preview execute failed: %s", esp_err_to_name(err));
        if (s_preview_result.status[0] == '\0') {
            snprintf(s_preview_result.status, sizeof(s_preview_result.status), "%s", "failed");
        }
        if (s_preview_result.error_code[0] == '\0') {
            snprintf(s_preview_result.error_code, sizeof(s_preview_result.error_code), "%s", "preview_failed");
        }
        return send_preview_json(req,
                                 false,
                                 s_preview_result.status,
                                 s_preview_result.error_code,
                                 command);
    }

    return send_preview_json(req,
                             preview_status_ok(s_preview_result.status),
                             s_preview_result.status[0] ? s_preview_result.status : "failed",
                             s_preview_result.error_code,
                             command);
}

esp_err_t node_provisioning_restart_post(httpd_req_t *req)
{
    char action_header[32] = {0};
    node_admin_control_result_t admin_result = {0};

    log_admin_request_headers("restart", req);
    if (!http_copy_header_value(req, "X-Node-Action", action_header, sizeof(action_header)) ||
        strcmp(action_header, "restart") != 0) {
        ESP_LOGW(TAG, "restart rejected: missing or invalid X-Node-Action header");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "restart confirmation required");
    }
    if (node_admin_control_restart(&admin_result) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "restart failed");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restarting\":true}");
}

esp_err_t node_provisioning_reset_wifi_post(httpd_req_t *req)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_reset_wifi(&admin_result);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi reset failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi reset failed");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restart_required\":true}");
}

esp_err_t node_provisioning_factory_reset_post(httpd_req_t *req)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_factory_reset(&admin_result);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "factory reset failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "factory reset failed");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restart_required\":true}");
}

esp_err_t node_provisioning_rules_get(httpd_req_t *req)
{
    int written = 0;
    esp_err_t err = ESP_OK;
    node_rule_store_entry_t *rule_entry = NULL;

    rule_entry = (node_rule_store_entry_t *)alloc_provisioning_buffer(sizeof(*rule_entry));
    if (!rule_entry) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules no mem");
    }

    err = node_rule_api_get_bundle(rule_entry);
    if (err != ESP_OK) {
        free(rule_entry);
        ESP_LOGE(TAG, "rules get failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules unavailable");
    }

    if (!lock_config_json()) {
        free(rule_entry);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules response busy");
    }
    if (!ensure_config_json_buffer()) {
        unlock_config_json();
        free(rule_entry);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules response no mem");
    }

    written = snprintf(s_config_json,
                       NODE_PROVISIONING_CONFIG_JSON_CAPACITY,
                       "{\"ok\":true,\"has_bundle\":%s,\"metadata\":{"
                       "\"version\":%lu,\"generation\":%lu,\"bundle_id\":\"%s\","
                       "\"mode\":\"%s\",\"raw_size\":%lu},\"bundle\":%s}",
                       rule_entry->metadata.has_bundle ? "true" : "false",
                       (unsigned long)rule_entry->metadata.version,
                       (unsigned long)rule_entry->metadata.generation,
                       rule_entry->metadata.bundle_id,
                       rule_entry->metadata.mode,
                       (unsigned long)rule_entry->metadata.raw_size,
                       rule_entry->metadata.has_bundle ? rule_entry->raw_json : "null");
    free(rule_entry);
    if (written < 0 || written >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules response too large");
    }

    httpd_resp_set_type(req, "application/json");
    err = httpd_resp_send(req, s_config_json, written);
    unlock_config_json();
    return err;
}

esp_err_t node_provisioning_rules_context_get(httpd_req_t *req)
{
    static const char *const supported_triggers[] = {
        "boot",
        "input_edge",
        "input_hold",
        "timer",
        "local_event",
    };
    static const char *const supported_conditions[] = {
        "state_equals",
        "phase_is",
        "input_equals",
        "event_field_equals",
        "all_inputs_equal",
        "not",
        "all",
        "any",
    };
    static const char *const supported_actions[] = {
        "command",
        "set_state",
        "set_phase",
        "emit_event",
        "start_timer",
        "cancel_timer",
        "sequence",
        "choose",
    };
    static const char *const condition_event_fields[] = {
        "token_id",
    };
    static const char *const local_event_payload_fields[] = {
        "source_id",
        "token_id",
        "uid",
    };
    cJSON *root = NULL;
    cJSON *resources = NULL;
    cJSON *inputs = NULL;
    cJSON *outputs = NULL;
    cJSON *led_strips = NULL;
    cJSON *drivers = NULL;
    cJSON *commands = NULL;
    cJSON *engine = NULL;
    cJSON *limits = NULL;
    cJSON *notes = NULL;
    esp_err_t err = ESP_OK;
    char name[48];

    if (!ensure_provisioning_scratch()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context no mem");
    }
    if (node_admin_control_get_config(&s_get_config) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    }

    root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context no mem");
    }

    if (!cJSON_AddBoolToObject(root, "ok", true) ||
        !cJSON_AddStringToObject(root,
                                 "operation_mode",
                                 node_runtime_mode_name((node_operation_mode_t)s_get_config.operation_mode)) ||
        !cJSON_AddBoolToObject(root, "standalone_mqtt_enabled", s_get_config.standalone_mqtt_enabled) ||
        !cJSON_AddNumberToObject(root, "fallback_timeout_ms", s_get_config.fallback_timeout_ms) ||
        !cJSON_AddNumberToObject(root, "fallback_return_delay_ms", s_get_config.fallback_return_delay_ms) ||
        !cJSON_AddStringToObject(root,
                                 "fallback_return_policy",
                                 fallback_return_policy_text(s_get_config.fallback_return_policy))) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
    }

    resources = cJSON_AddObjectToObject(root, "resources");
    inputs = resources ? cJSON_AddArrayToObject(resources, "inputs") : NULL;
    outputs = resources ? cJSON_AddArrayToObject(resources, "outputs") : NULL;
    led_strips = resources ? cJSON_AddArrayToObject(resources, "led_strips") : NULL;
    drivers = resources ? cJSON_AddArrayToObject(resources, "drivers") : NULL;
    commands = cJSON_AddArrayToObject(root, "commands");
    engine = cJSON_AddObjectToObject(root, "engine");
    limits = cJSON_AddObjectToObject(root, "limits");
    notes = cJSON_AddArrayToObject(root, "authoring_notes");
    if (!resources || !inputs || !outputs || !led_strips || !drivers || !commands || !engine || !limits || !notes) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
    }

    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_universal_pin_config_t *pin = &s_get_config.universal_io[i];
        cJSON *item = NULL;

        if (!pin->enabled || pin->role != NODE_PIN_UNIVERSAL_INPUT) {
            continue;
        }
        snprintf(name, sizeof(name), "input_%u", (unsigned)pin->channel);
        item = cJSON_CreateObject();
        if (!item ||
            !cJSON_AddStringToObject(item, "name", name) ||
            !cJSON_AddStringToObject(item, "type", "input") ||
            !cJSON_AddNumberToObject(item, "channel", pin->channel) ||
            !cJSON_AddNumberToObject(item, "gpio", pin->gpio) ||
            !cJSON_AddNumberToObject(item, "debounce_ms", pin->debounce_ms)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
        if (pin->label[0] != '\0' && !cJSON_AddStringToObject(item, "label", pin->label)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
        cJSON_AddItemToArray(inputs, item);
    }
    for (size_t i = 0; i < NODE_RELAY_MAX; ++i) {
        const node_output_pin_config_t *pin = &s_get_config.relays[i];

        if (!pin->enabled) {
            continue;
        }
        snprintf(name, sizeof(name), "relay_%u", (unsigned)pin->channel);
        if (!add_basic_resource(outputs, name, "relay", pin->channel, pin->gpio, pin->label)) {
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
    }
    for (size_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        const node_output_pin_config_t *pin = &s_get_config.mosfets[i];

        if (!pin->enabled) {
            continue;
        }
        snprintf(name, sizeof(name), "mosfet_%u", (unsigned)pin->channel);
        if (!add_basic_resource(outputs, name, "mosfet", pin->channel, pin->gpio, pin->label)) {
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
    }
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_universal_pin_config_t *pin = &s_get_config.universal_io[i];

        if (!pin->enabled || pin->role != NODE_PIN_UNIVERSAL_OUTPUT) {
            continue;
        }
        snprintf(name, sizeof(name), "output_%u", (unsigned)pin->channel);
        if (!add_basic_resource(outputs, name, "io_output", pin->channel, pin->gpio, pin->label)) {
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
    }
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *strip = &s_get_config.led_strips[i];
        cJSON *item = NULL;

        if (!strip->enabled) {
            continue;
        }
        snprintf(name, sizeof(name), "strip_%u", (unsigned)strip->channel);
        item = cJSON_CreateObject();
        if (!item ||
            !cJSON_AddStringToObject(item, "name", name) ||
            !cJSON_AddStringToObject(item, "type", "led_strip") ||
            !cJSON_AddNumberToObject(item, "channel", strip->channel) ||
            !cJSON_AddNumberToObject(item, "gpio", strip->gpio) ||
            !cJSON_AddNumberToObject(item, "pixel_count", strip->pixel_count) ||
            !cJSON_AddBoolToObject(item, "rgbw", strip->rgbw)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
        if (strip->label[0] != '\0' && !cJSON_AddStringToObject(item, "label", strip->label)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
        cJSON_AddItemToArray(led_strips, item);
    }
    {
        memset(&s_nfc_reader_scratch, 0, sizeof(s_nfc_reader_scratch));
        if (node_driver_nfc_reader_load_factory_config(&s_get_config, &s_nfc_reader_scratch) == ESP_OK) {
            cJSON *item = cJSON_CreateObject();
            cJSON *known_cards = NULL;

            if (!item ||
                !cJSON_AddStringToObject(item, "id", s_nfc_reader_scratch.id) ||
                !cJSON_AddStringToObject(item, "type", "nfc_reader") ||
                !cJSON_AddStringToObject(item, "driver", s_nfc_reader_scratch.driver_impl) ||
                !cJSON_AddStringToObject(item, "bus", s_nfc_reader_scratch.bus) ||
                !cJSON_AddBoolToObject(item, "enabled", s_nfc_reader_scratch.enabled) ||
                !cJSON_AddNumberToObject(item, "poll_interval_ms", s_nfc_reader_scratch.poll_interval_ms) ||
                !cJSON_AddNumberToObject(item, "debounce_ms", s_nfc_reader_scratch.debounce_ms)) {
                cJSON_Delete(item);
                cJSON_Delete(root);
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
            }
            known_cards = cJSON_AddArrayToObject(item, "known_cards");
            if (!known_cards) {
                cJSON_Delete(item);
                cJSON_Delete(root);
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
            }
            for (size_t i = 0; i < s_nfc_reader_scratch.known_card_count; ++i) {
                cJSON *card = cJSON_CreateObject();

                if (!card ||
                    !cJSON_AddStringToObject(card, "name", s_nfc_reader_scratch.known_cards[i].name) ||
                    !cJSON_AddStringToObject(card, "uid", s_nfc_reader_scratch.known_cards[i].uid) ||
                    !cJSON_AddNumberToObject(card, "token_id", s_nfc_reader_scratch.known_cards[i].token_id)) {
                    cJSON_Delete(card);
                    cJSON_Delete(item);
                    cJSON_Delete(root);
                    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
                }
                if (s_nfc_reader_scratch.known_cards[i].event_name[0] != '\0' &&
                    !cJSON_AddStringToObject(card, "event", s_nfc_reader_scratch.known_cards[i].event_name)) {
                    cJSON_Delete(card);
                    cJSON_Delete(item);
                    cJSON_Delete(root);
                    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
                }
                cJSON_AddItemToArray(known_cards, card);
            }
            cJSON_AddItemToArray(drivers, item);
        }
    }

    if (!add_command_spec(commands, "relay.set", "relay", "{output,on}") ||
        !add_command_spec(commands, "relay.pulse", "relay", "{output,duration_ms}") ||
        !add_command_spec(commands, "relay.all_off", "", "{}") ||
        !add_command_spec(commands, "mosfet.set", "mosfet", "{output,value}") ||
        !add_command_spec(commands, "mosfet.fade", "mosfet", "{output,target[,duration_ms]}") ||
        !add_command_spec(commands, "mosfet.pulse", "mosfet", "{output,value[,duration_ms]}") ||
        !add_command_spec(commands, "mosfet.blink", "mosfet", "{output[,value,final_value,on_ms,off_ms,count]}") ||
        !add_command_spec(commands, "mosfet.breathe", "mosfet", "{output[,min,max,final_value,fade_ms,hold_ms,count]}") ||
        !add_command_spec(commands, "mosfet.effect", "mosfet", "{output,effect[,value,final_value,on_ms,off_ms,fade_ms,hold_ms,count,min,max]}") ||
        !add_command_spec(commands, "mosfet.all_off", "", "{}") ||
        !add_command_spec(commands, "io.set", "io_output", "{output,on}") ||
        !add_command_spec(commands, "io.all_off", "", "{}") ||
        !add_command_spec(commands, "led.off", "led_strip", "{output}") ||
        !add_command_spec(commands, "led.solid", "led_strip", "{output,color}") ||
        !add_command_spec(commands, "led.blink", "led_strip", "{output,color[,on_ms,off_ms,count]}") ||
        !add_command_spec(commands, "led.breathe", "led_strip", "{output,color[,duration_ms,step_ms,count]}") ||
        !add_command_spec(commands, "led.effect", "led_strip", "{output,effect[,duration_ms,step_ms,count,size,intensity,density,fade,palette_mode,color,secondary_color,background_color]}") ||
        !add_command_spec(commands, "node.all_off", "", "{}")) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
    }

    if (!add_string_array(engine, "supported_triggers", supported_triggers, sizeof(supported_triggers) / sizeof(supported_triggers[0])) ||
        !add_string_array(engine, "supported_conditions", supported_conditions, sizeof(supported_conditions) / sizeof(supported_conditions[0])) ||
        !add_string_array(engine, "supported_actions", supported_actions, sizeof(supported_actions) / sizeof(supported_actions[0])) ||
        !add_string_array(engine, "condition_event_fields", condition_event_fields, sizeof(condition_event_fields) / sizeof(condition_event_fields[0])) ||
        !add_string_array(engine, "local_event_payload_fields", local_event_payload_fields, sizeof(local_event_payload_fields) / sizeof(local_event_payload_fields[0])) ||
        !cJSON_AddStringToObject(engine, "wait_model", "Use phase changes plus timers or later events; do not model blocking waits.") ||
        !cJSON_AddStringToObject(engine, "branch_model", "Use all/any/not and choose; loops and recursion are unsupported.")) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
    }

    if (!cJSON_AddNumberToObject(limits, "max_bundle_bytes", NODE_RULE_BUNDLE_MAX_LEN) ||
        !cJSON_AddNumberToObject(limits, "max_rules", NODE_RULE_MAX_RULES) ||
        !cJSON_AddNumberToObject(limits, "max_total_actions", NODE_RULE_MAX_ACTIONS_TOTAL) ||
        !cJSON_AddNumberToObject(limits, "max_actions_per_rule", NODE_RULE_MAX_ACTIONS_PER_RULE) ||
        !cJSON_AddNumberToObject(limits, "max_action_nesting", NODE_RULE_MAX_ACTION_NESTING) ||
        !cJSON_AddNumberToObject(limits, "max_emit_events", NODE_RULE_MAX_EMIT_EVENTS) ||
        !cJSON_AddNumberToObject(limits, "max_state_keys", NODE_RULE_MAX_STATE_KEYS) ||
        !cJSON_AddNumberToObject(limits, "max_timers", NODE_RULE_MAX_TIMERS) ||
        !cJSON_AddNumberToObject(limits, "max_phases", NODE_RULE_MAX_PHASES) ||
        !cJSON_AddNumberToObject(limits, "max_group_inputs", NODE_RULE_MAX_GROUP_INPUTS)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
    }

    {
        cJSON *note1 = cJSON_CreateString("Use only logical resource names listed under resources.");
        cJSON *note2 = cJSON_CreateString("In standalone mode, apply stores the bundle and reboot activates it.");
        cJSON *note3 = cJSON_CreateString("For RFID/NFC flows, map driver card events into local_event rules and branch with event_field_equals on token_id.");
        cJSON *note4 = cJSON_CreateString("Unsupported trigger kinds or conditions must be rejected at compile time instead of emulated in script.");

        if (!note1 || !note2 || !note3 || !note4) {
            cJSON_Delete(note1);
            cJSON_Delete(note2);
            cJSON_Delete(note3);
            cJSON_Delete(note4);
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
        }
        cJSON_AddItemToArray(notes, note1);
        cJSON_AddItemToArray(notes, note2);
        cJSON_AddItemToArray(notes, note3);
        cJSON_AddItemToArray(notes, note4);
    }
    if (
        !cJSON_AddStringToObject(root,
                                 "prompt_template",
                                 "Author a standalone_bundle JSON for this node. Use only listed resources, commands, triggers, conditions and actions. Respect limits. Model waits with set_phase plus start_timer and later timer or local_event rules. Do not generate loops, recursion or arbitrary expressions.")) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context build failed");
    }

    if (!lock_config_json()) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context busy");
    }
    if (!ensure_config_json_buffer()) {
        unlock_config_json();
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context no mem");
    }
    if (!cJSON_PrintPreallocated(root,
                                 s_config_json,
                                 NODE_PROVISIONING_CONFIG_JSON_CAPACITY,
                                 true)) {
        cJSON_Delete(root);
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "context response too large");
    }
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    err = httpd_resp_send(req, s_config_json, HTTPD_RESP_USE_STRLEN);
    unlock_config_json();
    return err;
}

esp_err_t node_provisioning_rules_validate_post(httpd_req_t *req)
{
    node_rule_bundle_metadata_t metadata = {0};
    node_admin_control_result_t admin_result = {0};
    char *raw_json = NULL;
    char error_code[NODE_RULE_API_ERROR_MAX_LEN] = {0};
    esp_err_t err = ESP_OK;

    if (req->content_len <= 0 || req->content_len > NODE_RULE_BUNDLE_MAX_LEN) {
        drain_request_body(req);
        return send_rule_result_json(req, HTTPD_400_BAD_REQUEST, false, "bundle_too_large", NULL);
    }
    if (!lock_post_body()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules busy");
    }
    if (!ensure_post_body_buffer()) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules no mem");
    }
    if (!read_request_body(req, s_post_body, NODE_PROVISIONING_POST_BODY_CAPACITY)) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
    }
    raw_json = dup_request_json(s_post_body, (size_t)req->content_len);
    unlock_post_body();
    if (!raw_json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules no mem");
    }

    err = node_admin_control_validate_rules(raw_json,
                                            &metadata,
                                            error_code,
                                            sizeof(error_code),
                                            &admin_result);
    free(raw_json);
    if (err != ESP_OK) {
        return send_rule_result_json(req, HTTPD_400_BAD_REQUEST, false, error_code, &metadata);
    }
    return send_rule_result_json(req, 200, true, "", &metadata);
}

esp_err_t node_provisioning_rules_apply_post(httpd_req_t *req)
{
    node_rule_bundle_metadata_t metadata = {0};
    node_admin_control_result_t admin_result = {0};
    char *raw_json = NULL;
    char error_code[NODE_RULE_API_ERROR_MAX_LEN] = {0};
    esp_err_t err = ESP_OK;

    if (req->content_len <= 0 || req->content_len > NODE_RULE_BUNDLE_MAX_LEN) {
        drain_request_body(req);
        return send_rule_result_json(req, HTTPD_400_BAD_REQUEST, false, "bundle_too_large", NULL);
    }
    if (!lock_post_body()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules busy");
    }
    if (!ensure_post_body_buffer()) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules no mem");
    }
    if (!read_request_body(req, s_post_body, NODE_PROVISIONING_POST_BODY_CAPACITY)) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
    }
    raw_json = dup_request_json(s_post_body, (size_t)req->content_len);
    unlock_post_body();
    if (!raw_json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules no mem");
    }

    err = node_admin_control_apply_rules(raw_json,
                                         &metadata,
                                         error_code,
                                         sizeof(error_code),
                                         &admin_result);
    free(raw_json);
    if (err != ESP_OK) {
        int http_status = (strcmp(error_code, "store_failed") == 0) ? HTTPD_500_INTERNAL_SERVER_ERROR
                                                                     : HTTPD_400_BAD_REQUEST;
        ESP_LOGW(TAG, "rules apply failed: %s code=%s", esp_err_to_name(err), error_code);
        return send_rule_result_json(req, http_status, false, error_code, &metadata);
    }

    return send_rule_result_json(req, 200, true, "", &metadata);
}

esp_err_t node_provisioning_rules_clear_post(httpd_req_t *req)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_clear_rules(&admin_result);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rules clear failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules clear failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"cleared\":true}");
}

esp_err_t node_provisioning_rules_pause_post(httpd_req_t *req)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_pause_rules(&admin_result);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rules pause failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules pause failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"paused\":true}");
}

esp_err_t node_provisioning_rules_resume_post(httpd_req_t *req)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_resume_rules(&admin_result);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rules resume failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rules resume failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"resumed\":true}");
}
