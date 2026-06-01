#include "web_ui_handlers.h"

#include <string.h>
#include <stdlib.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "scenehub_control.h"
#include "web_ui_utils.h"

static const char *TAG = "web_ui_gm_hint";

static int64_t gm_hint_perf_start(void)
{
    return esp_timer_get_time();
}

static void gm_hint_perf_log(const char *label, int64_t start_us, const char *room_id)
{
    int64_t dt_ms = (esp_timer_get_time() - start_us) / 1000;
    ESP_LOGW(TAG, "PERF %s room=%s took %lld ms", label ? label : "hint", room_id ? room_id : "", dt_ms);
}

static void *gm_hint_body_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

esp_err_t gm_hint_send_handler(httpd_req_t *req)
{
    char *body = NULL;
    cJSON *json = NULL;
    const cJSON *room_id_item = NULL;
    const cJSON *message_item = NULL;
    const char *room_id = NULL;
    const char *message = NULL;
    int64_t t0 = gm_hint_perf_start();
    scenehub_control_result_t result = {0};

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

    cJSON_Delete(json);
    esp_err_t err = scenehub_control_hint_send("http", room_id, message, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return web_ui_send_scenehub_control_error(req, err, &result, "hint send failed");
    }
    gm_hint_perf_log("POST hint send", t0, room_id);
    return web_ui_send_scenehub_control_ack(req);
}

esp_err_t gm_hint_clear_handler(httpd_req_t *req)
{
    char room_id[QUEST_ROOM_ID_MAX_LEN] = {0};
    int64_t t0 = gm_hint_perf_start();
    scenehub_control_result_t result = {0};
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
    esp_err_t err = scenehub_control_hint_clear("http", room_id, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return web_ui_send_scenehub_control_error(req, err, &result, "hint clear failed");
    }
    gm_hint_perf_log("POST hint clear", t0, room_id);
    return web_ui_send_scenehub_control_ack(req);
}
