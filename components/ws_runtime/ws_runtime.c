#include "ws_runtime.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define WS_RUNTIME_URI "/api/ws"
#define WS_RUNTIME_MAX_CLIENTS 2
#define WS_RUNTIME_MAX_INCOMING_FRAME 512
#define WS_RUNTIME_ENVELOPE_JSON_MAX 512
#define WS_RUNTIME_PAYLOAD_JSON_MAX 320

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
static SemaphoreHandle_t s_state_mutex = NULL;
static StaticSemaphore_t s_state_mutex_storage;
static portMUX_TYPE s_state_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} ws_json_writer_t;

static esp_err_t ws_runtime_state_lock(void)
{
    if (!s_state_mutex) {
        portENTER_CRITICAL(&s_state_mutex_init_lock);
        if (!s_state_mutex) {
            s_state_mutex = xSemaphoreCreateMutexStatic(&s_state_mutex_storage);
        }
        portEXIT_CRITICAL(&s_state_mutex_init_lock);
        if (!s_state_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    return xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void ws_runtime_state_unlock(void)
{
    if (s_state_mutex) {
        xSemaphoreGive(s_state_mutex);
    }
}

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

static void ws_json_init(ws_json_writer_t *writer, char *buf, size_t cap)
{
    writer->buf = buf;
    writer->cap = cap;
    writer->len = 0;
    if (cap > 0) {
        buf[0] = '\0';
    }
}

static esp_err_t ws_json_putn(ws_json_writer_t *writer, const char *text, size_t len)
{
    if (!writer || !writer->buf || !text || writer->cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len >= writer->cap - writer->len) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(writer->buf + writer->len, text, len);
    writer->len += len;
    writer->buf[writer->len] = '\0';
    return ESP_OK;
}

static esp_err_t ws_json_puts(ws_json_writer_t *writer, const char *text)
{
    return ws_json_putn(writer, text, strlen(text));
}

static esp_err_t ws_json_putc(ws_json_writer_t *writer, char ch)
{
    return ws_json_putn(writer, &ch, 1);
}

static esp_err_t ws_json_put_u32(ws_json_writer_t *writer, uint32_t value)
{
    char tmp[16];
    int written = snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)value);
    if (written < 0 || written >= (int)sizeof(tmp)) {
        return ESP_ERR_NO_MEM;
    }
    return ws_json_putn(writer, tmp, (size_t)written);
}

static esp_err_t ws_json_put_string(ws_json_writer_t *writer, const char *value)
{
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    esp_err_t err = ws_json_putc(writer, '"');
    if (err != ESP_OK) {
        return err;
    }

    while (*p) {
        char escape[7];
        switch (*p) {
        case '"':
            err = ws_json_puts(writer, "\\\"");
            break;
        case '\\':
            err = ws_json_puts(writer, "\\\\");
            break;
        case '\b':
            err = ws_json_puts(writer, "\\b");
            break;
        case '\f':
            err = ws_json_puts(writer, "\\f");
            break;
        case '\n':
            err = ws_json_puts(writer, "\\n");
            break;
        case '\r':
            err = ws_json_puts(writer, "\\r");
            break;
        case '\t':
            err = ws_json_puts(writer, "\\t");
            break;
        default:
            if (*p < 0x20) {
                snprintf(escape, sizeof(escape), "\\u%04x", (unsigned int)*p);
                err = ws_json_puts(writer, escape);
            } else {
                err = ws_json_putc(writer, (char)*p);
            }
            break;
        }
        if (err != ESP_OK) {
            return err;
        }
        ++p;
    }

    return ws_json_putc(writer, '"');
}

static esp_err_t ws_json_field_name(ws_json_writer_t *writer, bool *first, const char *name)
{
    esp_err_t err = ESP_OK;
    if (!first || !name) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!*first) {
        err = ws_json_putc(writer, ',');
        if (err != ESP_OK) {
            return err;
        }
    }
    *first = false;

    err = ws_json_put_string(writer, name);
    if (err != ESP_OK) {
        return err;
    }
    return ws_json_putc(writer, ':');
}

static esp_err_t ws_json_put_string_field(ws_json_writer_t *writer,
                                          bool *first,
                                          const char *name,
                                          const char *value)
{
    esp_err_t err = ws_json_field_name(writer, first, name);
    if (err != ESP_OK) {
        return err;
    }
    return ws_json_put_string(writer, value);
}

static esp_err_t ws_json_put_u32_field(ws_json_writer_t *writer,
                                       bool *first,
                                       const char *name,
                                       uint32_t value)
{
    esp_err_t err = ws_json_field_name(writer, first, name);
    if (err != ESP_OK) {
        return err;
    }
    return ws_json_put_u32(writer, value);
}

static esp_err_t ws_json_put_raw_field(ws_json_writer_t *writer,
                                       bool *first,
                                       const char *name,
                                       const char *json)
{
    esp_err_t err = ws_json_field_name(writer, first, name);
    if (err != ESP_OK) {
        return err;
    }
    return ws_json_puts(writer, json ? json : "null");
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

static esp_err_t build_envelope_json(char *out,
                                     size_t out_size,
                                     const char *type,
                                     const char *payload_json,
                                     uint32_t seq,
                                     uint32_t snapshot_generation)
{
    if (type == NULL || payload_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ws_json_writer_t writer;
    bool first = true;
    esp_err_t err = ESP_OK;

    ws_json_init(&writer, out, out_size);
    err = ws_json_putc(&writer, '{');
    if (err == ESP_OK) {
        err = ws_json_put_string_field(&writer, &first, "type", type);
    }
    if (err == ESP_OK) {
        err = ws_json_put_u32_field(&writer, &first, "seq", seq);
    }
    if (err == ESP_OK) {
        err = ws_json_put_u32_field(&writer, &first, "schema_version", 1);
    }
    if (err == ESP_OK) {
        err = ws_json_put_u32_field(&writer, &first, "snapshot_generation", snapshot_generation);
    }
    if (err == ESP_OK) {
        err = ws_json_put_u32_field(&writer, &first, "server_time_ms", ws_runtime_now_ms());
    }
    if (err == ESP_OK) {
        err = ws_json_put_raw_field(&writer, &first, "payload", payload_json);
    }
    if (err == ESP_OK) {
        err = ws_json_putc(&writer, '}');
    }
    return err;
}

static esp_err_t ws_runtime_next_envelope_meta(uint32_t *seq, uint32_t *snapshot_generation)
{
    esp_err_t err = ws_runtime_state_lock();
    if (err != ESP_OK) {
        return err;
    }
    *seq = ++s_ws_seq;
    *snapshot_generation = s_snapshot_generation;
    ws_runtime_state_unlock();
    return ESP_OK;
}

static esp_err_t send_envelope_to_fd(int fd,
                                     const char *type,
                                     const char *payload_json,
                                     uint32_t seq,
                                     uint32_t snapshot_generation)
{
    char envelope_json[WS_RUNTIME_ENVELOPE_JSON_MAX];
    esp_err_t err = build_envelope_json(envelope_json,
                                        sizeof(envelope_json),
                                        type,
                                        payload_json,
                                        seq,
                                        snapshot_generation);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ws envelope too large for type=%s", type ? type : "(null)");
        return err;
    }

    return send_text_to_fd(fd, envelope_json);
}

static esp_err_t send_envelope_to_fd_current(int fd, const char *type, const char *payload_json)
{
    uint32_t seq = 0;
    uint32_t snapshot_generation = 0;
    esp_err_t err = ws_runtime_next_envelope_meta(&seq, &snapshot_generation);
    if (err != ESP_OK) {
        return err;
    }
    return send_envelope_to_fd(fd, type, payload_json, seq, snapshot_generation);
}

static esp_err_t snapshot_subscribed_clients(int *fds,
                                             size_t fds_count,
                                             size_t *out_count,
                                             uint32_t *out_seq,
                                             uint32_t *out_snapshot_generation,
                                             uint32_t snapshot_generation_update)
{
    if (!fds || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_server == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ws_runtime_state_lock();
    if (err != ESP_OK) {
        return err;
    }

    if (snapshot_generation_update != 0) {
        s_snapshot_generation = snapshot_generation_update;
    }
    *out_count = 0;
    if (out_seq != NULL) {
        *out_seq = ++s_ws_seq;
    }
    if (out_snapshot_generation != NULL) {
        *out_snapshot_generation = s_snapshot_generation;
    }
    for (int i = 0; i < WS_RUNTIME_MAX_CLIENTS; ++i) {
        if (!s_clients[i].active || !s_clients[i].subscribed) {
            continue;
        }
        if (*out_count < fds_count) {
            fds[*out_count] = s_clients[i].fd;
            ++(*out_count);
        }
    }
    ws_runtime_state_unlock();
    return ESP_OK;
}

static void remove_failed_client(int fd)
{
    if (ws_runtime_state_lock() == ESP_OK) {
        remove_client(fd);
        ws_runtime_state_unlock();
    }
}

static esp_err_t broadcast_envelope(const char *type,
                                    const char *payload_json,
                                    uint32_t snapshot_generation_update)
{
    int fds[WS_RUNTIME_MAX_CLIENTS];
    size_t fd_count = 0;
    uint32_t seq = 0;
    uint32_t snapshot_generation = 0;
    esp_err_t last_err = ESP_OK;
    esp_err_t err = ESP_OK;

    if (type == NULL || payload_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = snapshot_subscribed_clients(fds,
                                      WS_RUNTIME_MAX_CLIENTS,
                                      &fd_count,
                                      &seq,
                                      &snapshot_generation,
                                      snapshot_generation_update);
    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0; i < fd_count; ++i) {
        err = send_envelope_to_fd(fds[i], type, payload_json, seq, snapshot_generation);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "broadcast failed fd=%d: %s", fds[i], esp_err_to_name(err));
            remove_failed_client(fds[i]);
            last_err = err;
        }
    }
    return last_err;
}

static esp_err_t send_error_to_fd(int fd, const char *message)
{
    char payload_json[192];
    ws_json_writer_t writer;
    bool first = true;
    esp_err_t err = ESP_OK;

    ws_json_init(&writer, payload_json, sizeof(payload_json));
    err = ws_json_putc(&writer, '{');
    if (err == ESP_OK) {
        err = ws_json_put_string_field(&writer, &first, "message", message ? message : "unknown error");
    }
    if (err != ESP_OK) {
        return err;
    }
    if (err == ESP_OK) {
        err = ws_json_putc(&writer, '}');
    }
    if (err != ESP_OK) {
        return err;
    }
    return send_envelope_to_fd_current(fd, "error", payload_json);
}

static esp_err_t send_connection_ready_to_fd(int fd)
{
    return send_envelope_to_fd_current(fd, "connection.ready",
        "{"
            "\"device_id\":\"scenehub-main\","
            "\"api_version\":1"
        "}"
    );
}

static esp_err_t send_subscribed_to_fd(int fd)
{
    return send_envelope_to_fd_current(fd, "subscription.ready",
        "{\"ok\":true}"
    );
}

static esp_err_t send_unsubscribed_to_fd(int fd)
{
    return send_envelope_to_fd_current(fd, "subscription.closed",
        "{\"ok\":true}"
    );
}

static esp_err_t send_pong_to_fd(int fd)
{
    return send_envelope_to_fd_current(fd, "pong",
        "{\"ok\":true}"
    );
}

static esp_err_t handle_text_message(int fd, const char *text)
{
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

    esp_err_t result = ws_runtime_state_lock();
    if (result != ESP_OK) {
        cJSON_Delete(root);
        return result;
    }
    ws_runtime_client_t *client = find_client_by_fd(fd);
    if (client == NULL) {
        ws_runtime_state_unlock();
        cJSON_Delete(root);
        return ESP_ERR_INVALID_STATE;
    }

    if (strcmp(type->valuestring, "subscribe") == 0) {
        client->subscribed = true;
        ws_runtime_state_unlock();
        cJSON_Delete(root);

        result = send_connection_ready_to_fd(fd);
        if (result == ESP_OK) {
            result = send_subscribed_to_fd(fd);
        }
        return result;

    } else if (strcmp(type->valuestring, "unsubscribe") == 0) {
        client->subscribed = false;
        ws_runtime_state_unlock();
        cJSON_Delete(root);

        return send_unsubscribed_to_fd(fd);
    } else if (strcmp(type->valuestring, "ping") == 0) {
        ws_runtime_state_unlock();
        cJSON_Delete(root);
        return send_pong_to_fd(fd);
    } else {
        ESP_LOGW(TAG, "unknown ws message type from fd=%d: %s", fd, type->valuestring);
        ws_runtime_state_unlock();
        cJSON_Delete(root);
        return send_error_to_fd(fd, "unknown message type");
    }
}

static esp_err_t ws_runtime_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        esp_err_t err = ws_runtime_state_lock();
        if (err != ESP_OK) {
            return err;
        }
        ws_runtime_client_t *client = alloc_client_slot(fd);
        ws_runtime_state_unlock();
        if (client == NULL) {
            ESP_LOGW(TAG, "too many ws clients, fd=%d", fd);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "client connected, fd=%d", fd);
        return ESP_OK;
    }

    int fd = httpd_req_to_sockfd(req);
    esp_err_t lock_err = ws_runtime_state_lock();
    if (lock_err != ESP_OK) {
        return lock_err;
    }
    ws_runtime_client_t *client = alloc_client_slot(fd);
    ws_runtime_state_unlock();
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
        if (ws_runtime_state_lock() == ESP_OK) {
            remove_client(fd);
            ws_runtime_state_unlock();
        }
        return err;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        if (ws_runtime_state_lock() == ESP_OK) {
            remove_client(fd);
            ws_runtime_state_unlock();
        }
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
        if (ws_runtime_state_lock() == ESP_OK) {
            remove_client(fd);
            ws_runtime_state_unlock();
        }
        return err;
    }

    buffer[frame.len] = '\0';

    return handle_text_message(fd, buffer);
}

esp_err_t ws_runtime_register_httpd(httpd_handle_t server)
{
    esp_err_t err = ESP_OK;

    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = ws_runtime_state_lock();
    if (err != ESP_OK) {
        return err;
    }
    s_server = server;
    ws_runtime_state_unlock();

    httpd_uri_t ws_uri = {
        .uri = WS_RUNTIME_URI,
        .method = HTTP_GET,
        .handler = ws_runtime_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };

    err = httpd_register_uri_handler(server, &ws_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to register %s: %s", WS_RUNTIME_URI, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "registered websocket endpoint: %s", WS_RUNTIME_URI);
    return ESP_OK;
}

esp_err_t ws_runtime_broadcast_json(const char *json)
{
    int fds[WS_RUNTIME_MAX_CLIENTS];
    size_t fd_count = 0;
    esp_err_t last_err = ESP_OK;
    esp_err_t err = ESP_OK;

    if (s_server == NULL || json == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    err = snapshot_subscribed_clients(fds,
                                      WS_RUNTIME_MAX_CLIENTS,
                                      &fd_count,
                                      NULL,
                                      NULL,
                                      0);
    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0; i < fd_count; ++i) {
        err = send_text_to_fd(fds[i], json);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "broadcast failed fd=%d: %s", fds[i], esp_err_to_name(err));
            remove_failed_client(fds[i]);
            last_err = err;
        }
    }

    return last_err;
}

esp_err_t ws_runtime_broadcast_versions_changed(const ws_runtime_versions_changed_t *versions)
{
    char payload_json[WS_RUNTIME_PAYLOAD_JSON_MAX];
    int written = 0;

    if (!versions) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(
        payload_json,
        sizeof(payload_json),
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
    if (written < 0 || written >= (int)sizeof(payload_json)) {
        ESP_LOGW(TAG, "ws versions payload too large");
        return ESP_ERR_NO_MEM;
    }

    return broadcast_envelope("gm.versions.changed",
                              payload_json,
                              versions->snapshot_generation ? versions->snapshot_generation : versions->generation);
}

esp_err_t ws_runtime_broadcast_invalidation(const ws_runtime_invalidation_t *invalidation)
{
    const char *slice = NULL;
    const char *target_id = NULL;
    const char *scope = NULL;
    const char *reason = NULL;
    char payload_json[WS_RUNTIME_PAYLOAD_JSON_MAX];
    ws_json_writer_t writer;
    bool first = true;
    esp_err_t err = ESP_OK;

    if (!invalidation || !invalidation->slice || !invalidation->scope) {
        return ESP_ERR_INVALID_ARG;
    }

    slice = invalidation->slice;
    target_id = invalidation->target_id ? invalidation->target_id : "";
    scope = invalidation->scope;
    reason = invalidation->reason ? invalidation->reason : "";

    ws_json_init(&writer, payload_json, sizeof(payload_json));
    err = ws_json_putc(&writer, '{');
    if (err == ESP_OK) {
        err = ws_json_put_string_field(&writer, &first, "slice", slice);
    }
    if (err == ESP_OK) {
        err = ws_json_put_string_field(&writer, &first, "target_id", target_id);
    }
    if (err == ESP_OK) {
        err = ws_json_put_string_field(&writer, &first, "scope", scope);
    }
    if (err == ESP_OK) {
        err = ws_json_put_u32_field(&writer, &first, "generation", invalidation->generation);
    }
    if (err == ESP_OK) {
        err = ws_json_put_string_field(&writer, &first, "reason", reason);
    }
    if (err == ESP_OK) {
        err = ws_json_putc(&writer, '}');
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ws invalidation payload too large for slice=%s", slice);
        return ESP_ERR_NO_MEM;
    }

    return broadcast_envelope("gm.invalidate", payload_json, 0);
}

esp_err_t ws_runtime_broadcast_resync_required(const ws_runtime_resync_required_t *resync)
{
    const char *reason = NULL;
    const char *target_id = NULL;
    char payload_json[WS_RUNTIME_PAYLOAD_JSON_MAX];
    ws_json_writer_t writer;
    bool first = true;
    esp_err_t err = ESP_OK;

    if (!resync) {
        return ESP_ERR_INVALID_ARG;
    }

    reason = resync->reason ? resync->reason : "";
    target_id = resync->target_id ? resync->target_id : "";

    ws_json_init(&writer, payload_json, sizeof(payload_json));
    err = ws_json_putc(&writer, '{');
    if (err == ESP_OK) {
        err = ws_json_put_string_field(&writer, &first, "target_id", target_id);
    }
    if (err == ESP_OK) {
        err = ws_json_put_u32_field(&writer, &first, "generation", resync->generation);
    }
    if (err == ESP_OK) {
        err = ws_json_put_string_field(&writer, &first, "reason", reason);
    }
    if (err == ESP_OK) {
        err = ws_json_putc(&writer, '}');
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ws resync payload too large");
        return ESP_ERR_NO_MEM;
    }

    return broadcast_envelope("gm.resync.required", payload_json, 0);
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
