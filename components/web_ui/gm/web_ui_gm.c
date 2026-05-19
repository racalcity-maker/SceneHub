#include "web_ui_handlers.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_attr.h"
#include "orch_room_view.h"
#include "scenehub_control.h"
#include "web_ui_page.h"
#include "web_ui_utils.h"

EXT_RAM_BSS_ATTR static orch_room_entry_t s_gm_rooms_list[ORCH_REGISTRY_MAX_ROOMS];

static esp_err_t gm_room_read_json(httpd_req_t *req, size_t max_len, cJSON **out_root)
{
    char *body = NULL;
    size_t received = 0;
    cJSON *root = NULL;

    if (!req || !out_root) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_root = NULL;
    if (req->content_len <= 0 || req->content_len > (int)max_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    body = web_ui_malloc((size_t)req->content_len + 1);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    while (received < (size_t)req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            web_ui_free(body);
            return ESP_FAIL;
        }
        received += (size_t)r;
    }
    body[received] = '\0';
    root = cJSON_ParseWithLength(body, received);
    web_ui_free(body);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_root = root;
    return ESP_OK;
}

esp_err_t gm_page_handler(httpd_req_t *req)
{
    return web_ui_send_ok(req, "text/html", web_ui_get_gm_html());
}

esp_err_t gm_rooms_handler(httpd_req_t *req)
{
    size_t room_count = 0;
    cJSON *rooms = NULL;
    memset(s_gm_rooms_list, 0, sizeof(s_gm_rooms_list));
    if (orchestrator_registry_list_rooms(s_gm_rooms_list,
                                         ORCH_REGISTRY_MAX_ROOMS,
                                         &room_count) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "gm rooms unavailable");
    }
    rooms = cJSON_CreateArray();
    if (!rooms) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    for (size_t i = 0; i < room_count; ++i) {
        const orch_room_entry_t *room = &s_gm_rooms_list[i];
        cJSON *obj = NULL;
        obj = cJSON_CreateObject();
        if (!obj) {
            cJSON_Delete(rooms);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        }
        cJSON_AddStringToObject(obj, "room_id", room->room_id);
        cJSON_AddStringToObject(obj, "name", room->title);
        cJSON_AddNumberToObject(obj, "device_count", room->device_count);
        cJSON_AddItemToArray(rooms, obj);
    }
    return web_ui_send_json(req, rooms);
}

esp_err_t gm_room_save_handler(httpd_req_t *req)
{
    cJSON *input = NULL;
    const cJSON *room_id_item = NULL;
    const cJSON *name_item = NULL;
    const char *room_id_src = NULL;
    const char *name_src = NULL;
    char room_id[ROOM_CATALOG_ROOM_ID_MAX_LEN] = {0};
    char name[ROOM_CATALOG_ROOM_NAME_MAX_LEN] = {0};
    scenehub_control_result_t result = {0};
    esp_err_t err = gm_room_read_json(req, 2048, &input);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid request");
    }
    room_id_item = cJSON_GetObjectItem(input, "room_id");
    name_item = cJSON_GetObjectItem(input, "name");
    room_id_src = cJSON_IsString(room_id_item) ? room_id_item->valuestring : NULL;
    name_src = cJSON_IsString(name_item) ? name_item->valuestring : NULL;
    if (!room_id_src || !room_id_src[0]) {
        cJSON_Delete(input);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    snprintf(room_id, sizeof(room_id), "%s", room_id_src);
    if (!name_src || !name_src[0]) {
        name_src = room_id_src;
    }
    snprintf(name, sizeof(name), "%s", name_src);
    cJSON_Delete(input);
    err = scenehub_control_save_room("http", room_id, name, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return web_ui_send_scenehub_control_error(req, err, &result, "room save failed");
    }
    return web_ui_send_room_saved_json(req, room_id, name);
}

static bool gm_room_json_bool(const cJSON *root, const char *key, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItem(root, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

esp_err_t gm_room_delete_handler(httpd_req_t *req)
{
    cJSON *input = NULL;
    const cJSON *room_id_item = NULL;
    const char *room_id_src = NULL;
    char room_id[ROOM_CATALOG_ROOM_ID_MAX_LEN] = {0};
    esp_err_t err = gm_room_read_json(req, 1024, &input);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid request");
    }
    room_id_item = cJSON_GetObjectItem(input, "room_id");
    room_id_src = cJSON_IsString(room_id_item) ? room_id_item->valuestring : NULL;
    if (!room_id_src || !room_id_src[0] || strcmp(room_id_src, "unassigned") == 0) {
        cJSON_Delete(input);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "valid room_id required");
    }
    snprintf(room_id, sizeof(room_id), "%s", room_id_src);
    bool delete_content = gm_room_json_bool(input, "delete_content", true);

    cJSON_Delete(input);
    bool existed = false;
    size_t removed_profiles = 0;
    size_t removed_scenarios = 0;
    scenehub_control_result_t result = {0};
    err = scenehub_control_delete_room("http",
                                       room_id,
                                       delete_content,
                                       &existed,
                                       &removed_profiles,
                                       &removed_scenarios,
                                       &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        if ((err != ESP_OK ? err : result.err) == ESP_ERR_INVALID_ARG) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "valid room_id required");
        }
        return web_ui_send_scenehub_control_error(req, err, &result, "room delete failed");
    }
    return web_ui_send_room_deleted_json(req,
                                         room_id,
                                         existed ? 1U : 0U,
                                         removed_profiles,
                                         removed_scenarios);
}
