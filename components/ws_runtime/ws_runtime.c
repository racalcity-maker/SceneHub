#include "ws_runtime.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "cJSON.h"

#define WS_RUNTIME_URI "/api/ws"
#define WS_RUNTIME_MAX_CLIENTS 2
#define WS_RUNTIME_MAX_INCOMING_FRAME 512

static const char *TAG = "ws_runtime";

#if CONFIG_HTTPD_WS_SUPPORT

bool ws_runtime_available(void)
{
    return true;
}

uint8_t ws_runtime_max_clients(void)
{
    return WS_RUNTIME_MAX_CLIENTS;
}

typedef struct {
    int fd;
    bool active;
    bool subscribed;
} ws_runtime_client_t;

static httpd_handle_t s_server = NULL;
static ws_runtime_client_t s_clients[WS_RUNTIME_MAX_CLIENTS];
static uint32_t s_ws_seq = 0;
static uint32_t s_snapshot_generation = 1;

static ws_runtime_client_t *find_client_by_fd(int fd)
{
    for (int i = 0; i < WS_RUNTIME_MAX_CLIENTS; ++i) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            return &s_clients[i];
        }
    }

    return NULL;
}

static ws_runtime_client_t *alloc_client_slot(int fd)
{
    ws_runtime_client_t *existing = find_client_by_fd(fd);
    if (existing != NULL) {
        return existing;
    }

    for (int i = 0; i < WS_RUNTIME_MAX_CLIENTS; ++i) {
        if (!s_clients[i].active) {
            s_clients[i].fd = fd;
            s_clients[i].active = true;
            s_clients[i].subscribed = false;
            return &s_clients[i];
        }
    }

    return NULL;
}

static void remove_client(int fd)
{
    for (int i = 0; i < WS_RUNTIME_MAX_CLIENTS; ++i) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            memset(&s_clients[i], 0, sizeof(s_clients[i]));
            ESP_LOGI(TAG, "client removed, fd=%d", fd);
            return;
        }
    }
}

static uint32_t ws_runtime_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static esp_err_t send_text_to_fd(int fd, const char *text)
{
    if (s_server == NULL || text == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = strlen(text),
    };

    return httpd_ws_send_frame_async(s_server, fd, &frame);
}

static esp_err_t send_envelope_to_fd(int fd, const char *type, const char *payload_json)
{
    if (type == NULL || payload_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char json[512];

    uint32_t seq = ++s_ws_seq;
    uint32_t now_ms = ws_runtime_now_ms();

    int written = snprintf(
        json,
        sizeof(json),
        "{\"type\":\"%s\","
        "\"seq\":%lu,"
        "\"schema_version\":1,"
        "\"snapshot_generation\":%lu,"
        "\"server_time_ms\":%lu,"
        "\"payload\":%s}",
        type,
        (unsigned long)seq,
        (unsigned long)s_snapshot_generation,
        (unsigned long)now_ms,
        payload_json
    );

    if (written < 0 || written >= (int)sizeof(json)) {
        ESP_LOGW(TAG, "ws envelope too large for type=%s", type);
        return ESP_ERR_NO_MEM;
    }

    return send_text_to_fd(fd, json);
}

static esp_err_t broadcast_envelope(const char *type, const char *payload_json)
{
    if (s_server == NULL || type == NULL || payload_json == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t last_err = ESP_OK;

    for (int i = 0; i < WS_RUNTIME_MAX_CLIENTS; ++i) {
        if (!s_clients[i].active || !s_clients[i].subscribed) {
            continue;
        }

        esp_err_t err = send_envelope_to_fd(s_clients[i].fd, type, payload_json);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "broadcast failed fd=%d: %s", s_clients[i].fd, esp_err_to_name(err));
            remove_client(s_clients[i].fd);
            last_err = err;
        }
    }

    return last_err;
}

static esp_err_t send_error_to_fd(int fd, const char *message)
{
    char json[192];

    snprintf(
        json,
        sizeof(json),
        "{\"type\":\"error\",\"schema_version\":1,\"payload\":{\"message\":\"%s\"}}",
        message ? message : "unknown error"
    );

    return send_text_to_fd(fd, json);
}

static esp_err_t send_connection_ready_to_fd(int fd)
{
    return send_envelope_to_fd(fd, "connection.ready",
        "{"
            "\"device_id\":\"scenehub-main\","
            "\"api_version\":1"
        "}"
    );
}

static esp_err_t send_subscribed_to_fd(int fd)
{
    return send_envelope_to_fd(fd, "subscription.ready",
        "{\"ok\":true}"
    );
}

static esp_err_t send_unsubscribed_to_fd(int fd)
{
    return send_envelope_to_fd(fd, "subscription.closed",
        "{\"ok\":true}"
    );
}

static esp_err_t send_pong_to_fd(int fd)
{
    return send_envelope_to_fd(fd, "pong",
        "{\"ok\":true}"
    );
}

static esp_err_t handle_text_message(int fd, const char *text)
{
    ws_runtime_client_t *client = find_client_by_fd(fd);
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "ws recv fd=%d: %s", fd, text);

    cJSON *root = cJSON_Parse(text);
    if (root == NULL) {
        ESP_LOGW(TAG, "invalid json from fd=%d", fd);
        return send_error_to_fd(fd, "invalid json");
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == NULL) {
        cJSON_Delete(root);
        return send_error_to_fd(fd, "missing type");
    }

    esp_err_t result = ESP_OK;

    if (strcmp(type->valuestring, "subscribe") == 0) {
        client->subscribed = true;

        result = send_connection_ready_to_fd(fd);
        if (result == ESP_OK) {
            result = send_subscribed_to_fd(fd);
        }

    } else if (strcmp(type->valuestring, "unsubscribe") == 0) {
        client->subscribed = false;

        result = send_unsubscribed_to_fd(fd);
    } else if (strcmp(type->valuestring, "ping") == 0) {
        result = send_pong_to_fd(fd);
    } else {
        ESP_LOGW(TAG, "unknown ws message type from fd=%d: %s", fd, type->valuestring);
        result = send_error_to_fd(fd, "unknown message type");
    }

    cJSON_Delete(root);
    return result;
}

static esp_err_t ws_runtime_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);

        ws_runtime_client_t *client = alloc_client_slot(fd);
        if (client == NULL) {
            ESP_LOGW(TAG, "too many ws clients, fd=%d", fd);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "client connected, fd=%d", fd);
        return ESP_OK;
    }

    int fd = httpd_req_to_sockfd(req);

    ws_runtime_client_t *client = alloc_client_slot(fd);
    if (client == NULL) {
        ESP_LOGW(TAG, "too many ws clients while receiving, fd=%d", fd);
        return ESP_FAIL;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
        .len = 0,
    };

    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to get ws frame length: %s", esp_err_to_name(err));
        remove_client(fd);
        return err;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        remove_client(fd);
        return ESP_OK;
    }

    if (frame.type != HTTPD_WS_TYPE_TEXT) {
        ESP_LOGW(TAG, "unsupported ws frame type=%d", frame.type);
        return ESP_OK;
    }

    if (frame.len > WS_RUNTIME_MAX_INCOMING_FRAME) {
        ESP_LOGW(TAG, "incoming ws frame too large: %d bytes", frame.len);
        return ESP_FAIL;
    }

    char buffer[WS_RUNTIME_MAX_INCOMING_FRAME + 1];
    memset(buffer, 0, sizeof(buffer));

    frame.payload = (uint8_t *)buffer;

    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to receive ws frame: %s", esp_err_to_name(err));
        remove_client(fd);
        return err;
    }

    buffer[frame.len] = '\0';

    return handle_text_message(fd, buffer);
}

esp_err_t ws_runtime_register_httpd(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_server = server;

    httpd_uri_t ws_uri = {
        .uri = WS_RUNTIME_URI,
        .method = HTTP_GET,
        .handler = ws_runtime_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };

    esp_err_t err = httpd_register_uri_handler(server, &ws_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to register %s: %s", WS_RUNTIME_URI, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "registered websocket endpoint: %s", WS_RUNTIME_URI);
    return ESP_OK;
}

esp_err_t ws_runtime_broadcast_json(const char *json)
{
    if (s_server == NULL || json == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t last_err = ESP_OK;

    for (int i = 0; i < WS_RUNTIME_MAX_CLIENTS; ++i) {
        if (!s_clients[i].active || !s_clients[i].subscribed) {
            continue;
        }

        esp_err_t err = send_text_to_fd(s_clients[i].fd, json);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "broadcast failed fd=%d: %s", s_clients[i].fd, esp_err_to_name(err));
            remove_client(s_clients[i].fd);
            last_err = err;
        }
    }

    return last_err;
}

esp_err_t ws_runtime_broadcast_versions_changed(const ws_runtime_versions_changed_t *versions)
{
    char payload[256];

    if (!versions) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(
        payload,
        sizeof(payload),
        "{\"generation\":%lu,"
        "\"rooms\":%lu,"
        "\"devices\":%lu,"
        "\"scenarios\":%lu,"
        "\"profiles\":%lu,"
        "\"ingest\":%lu,"
        "\"session\":%lu,"
        "\"static\":%lu,"
        "\"runtime\":%lu}",
        (unsigned long)versions->generation,
        (unsigned long)versions->rooms,
        (unsigned long)versions->devices,
        (unsigned long)versions->scenarios,
        (unsigned long)versions->profiles,
        (unsigned long)versions->ingest,
        (unsigned long)versions->session,
        (unsigned long)versions->static_generation,
        (unsigned long)versions->runtime_generation
    );

    return broadcast_envelope("gm.versions.changed", payload);
}

esp_err_t ws_runtime_broadcast_invalidation(const ws_runtime_invalidation_t *invalidation)
{
    char payload[320];
    int written = 0;
    const char *slice = NULL;
    const char *target_id = NULL;
    const char *scope = NULL;
    const char *reason = NULL;

    if (!invalidation || !invalidation->slice || !invalidation->scope) {
        return ESP_ERR_INVALID_ARG;
    }

    slice = invalidation->slice;
    target_id = invalidation->target_id ? invalidation->target_id : "";
    scope = invalidation->scope;
    reason = invalidation->reason ? invalidation->reason : "";

    written = snprintf(payload,
                       sizeof(payload),
                       "{\"slice\":\"%s\","
                       "\"target_id\":\"%s\","
                       "\"scope\":\"%s\","
                       "\"generation\":%lu,"
                       "\"reason\":\"%s\"}",
                       slice,
                       target_id,
                       scope,
                       (unsigned long)invalidation->generation,
                       reason);
    if (written < 0 || written >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "ws invalidation payload too large for slice=%s", slice);
        return ESP_ERR_NO_MEM;
    }

    return broadcast_envelope("gm.invalidate", payload);
}

esp_err_t ws_runtime_broadcast_resync_required(const ws_runtime_resync_required_t *resync)
{
    char payload[256];
    int written = 0;
    const char *reason = NULL;
    const char *target_id = NULL;

    if (!resync) {
        return ESP_ERR_INVALID_ARG;
    }

    reason = resync->reason ? resync->reason : "";
    target_id = resync->target_id ? resync->target_id : "";

    written = snprintf(payload,
                       sizeof(payload),
                       "{\"target_id\":\"%s\","
                       "\"generation\":%lu,"
                       "\"reason\":\"%s\"}",
                       target_id,
                       (unsigned long)resync->generation,
                       reason);
    if (written < 0 || written >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "ws resync payload too large");
        return ESP_ERR_NO_MEM;
    }

    return broadcast_envelope("gm.resync.required", payload);
}

#else

bool ws_runtime_available(void)
{
    return false;
}

uint8_t ws_runtime_max_clients(void)
{
    return 0;
}

esp_err_t ws_runtime_register_httpd(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "websocket support disabled in sdkconfig; skipping %s", WS_RUNTIME_URI);
    return ESP_OK;
}

esp_err_t ws_runtime_broadcast_json(const char *json)
{
    return json ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t ws_runtime_broadcast_versions_changed(const ws_runtime_versions_changed_t *versions)
{
    return versions ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t ws_runtime_broadcast_invalidation(const ws_runtime_invalidation_t *invalidation)
{
    return invalidation ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t ws_runtime_broadcast_resync_required(const ws_runtime_resync_required_t *resync)
{
    return resync ? ESP_OK : ESP_ERR_INVALID_ARG;
}

#endif
