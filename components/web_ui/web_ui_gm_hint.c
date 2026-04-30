#include "web_ui_handlers.h"

#include <string.h>
#include <stdlib.h>

#include "cJSON.h"
#include "esp_heap_caps.h"

#include "gm_api.h"
#include "orchestrator_registry.h"
#include "web_ui_utils.h"

static void *gm_hint_body_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static esp_err_t gm_send_hint_state(httpd_req_t *req, const char *room_id, const char *status)
{
    gm_room_session_t *session = NULL;
    cJSON *root = NULL;
    if (!req || !room_id || !status) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid state");
    }
    session = gm_hint_body_alloc(sizeof(*session));
    if (!session) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    if (gm_api_room_session_get(room_id, session) != ESP_OK) {
        heap_caps_free(session);
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "room session not found");
    }
    root = cJSON_CreateObject();
    if (!root) {
        heap_caps_free(session);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddStringToObject(root, "room_id", session->room_id);
    cJSON_AddBoolToObject(root, "hint_active", session->hint.active);
    cJSON_AddNumberToObject(root, "hint_sent_count", session->hint.sent_count);
    cJSON_AddStringToObject(root, "hint_message", session->hint.message);
    cJSON_AddNumberToObject(root, "hint_last_changed_ms", (double)session->hint.last_changed_ms);
    heap_caps_free(session);
    return web_ui_send_json(req, root);
}

static esp_err_t gm_send_hint_error(httpd_req_t *req, esp_err_t err, const char *internal_message)
{
    if (err == ESP_ERR_INVALID_ARG) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid arguments");
    }
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "room session not found");
    }
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "operation not allowed in current state", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, internal_message);
}

esp_err_t gm_hint_send_handler(httpd_req_t *req)
{
    char *body = NULL;
    cJSON *json = NULL;
    const cJSON *room_id_item = NULL;
    const cJSON *message_item = NULL;
    const char *room_id = NULL;
    const char *message = NULL;
    esp_err_t err;

    if (!req || req->content_len <= 0 || req->content_len > 512) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body required");
    }
    body = gm_hint_body_alloc(req->content_len + 1);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    size_t received = 0;
    while (received < (size_t)req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            heap_caps_free(body);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        }
        received += (size_t)r;
    }

    json = cJSON_Parse(body);
    heap_caps_free(body);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    room_id_item = cJSON_GetObjectItem(json, "room_id");
    message_item = cJSON_GetObjectItem(json, "message");
    room_id = cJSON_IsString(room_id_item) ? room_id_item->valuestring : NULL;
    message = cJSON_IsString(message_item) ? message_item->valuestring : NULL;
    if (!room_id || !room_id[0] || !message || !message[0]) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id/message required");
    }

    err = gm_api_hint_send(room_id, message);
    cJSON_Delete(json);
    if (err != ESP_OK) {
        return gm_send_hint_error(req, err, "hint send failed");
    }
    orchestrator_registry_invalidate();
    return gm_send_hint_state(req, room_id, "sent");
}

esp_err_t gm_hint_clear_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    esp_err_t err;
    char query[256] = {0};

    if (!req) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid request");
    }
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "room_id", room_id, sizeof(room_id));
    }
    if (!room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    err = gm_api_hint_clear(room_id);
    if (err != ESP_OK) {
        return gm_send_hint_error(req, err, "hint clear failed");
    }
    orchestrator_registry_invalidate();
    return gm_send_hint_state(req, room_id, "cleared");
}
