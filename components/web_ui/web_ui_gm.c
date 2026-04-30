#include "web_ui_handlers.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "gm_game_profile.h"
#include "orchestrator_registry.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "web_ui_page.h"
#include "web_ui_utils.h"

esp_err_t gm_page_handler(httpd_req_t *req)
{
    return web_ui_send_ok(req, "text/html", web_ui_get_gm_html());
}

esp_err_t gm_rooms_handler(httpd_req_t *req)
{
    cJSON *rooms = NULL;
    if (room_catalog_init() != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "room catalog init failed");
    }
    if (room_catalog_refresh() != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "room catalog refresh failed");
    }
    rooms = cJSON_CreateArray();
    if (!rooms) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    size_t count = room_catalog_count();
    for (size_t i = 0; i < count; ++i) {
        room_catalog_entry_t room = {0};
        cJSON *obj = NULL;
        if (room_catalog_get(i, &room) != ESP_OK) {
            continue;
        }
        obj = cJSON_CreateObject();
        if (!obj) {
            cJSON_Delete(rooms);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        }
        cJSON_AddStringToObject(obj, "room_id", room.room_id);
        cJSON_AddStringToObject(obj, "name", room.name);
        cJSON_AddNumberToObject(obj, "device_count", room.device_count);
        cJSON_AddItemToArray(rooms, obj);
    }
    return web_ui_send_json(req, rooms);
}

esp_err_t gm_room_save_handler(httpd_req_t *req)
{
    if (!req || req->content_len == 0 || req->content_len > 2048) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    }
    char *body = heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        body = heap_caps_malloc(req->content_len + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    size_t received = 0;
    while (received < req->content_len) {
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
    body[req->content_len] = '\0';
    cJSON *input = cJSON_ParseWithLength(body, req->content_len);
    heap_caps_free(body);
    if (!input) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }
    const cJSON *room_id_item = cJSON_GetObjectItem(input, "room_id");
    const cJSON *name_item = cJSON_GetObjectItem(input, "name");
    const char *room_id_src = cJSON_IsString(room_id_item) ? room_id_item->valuestring : NULL;
    const char *name_src = cJSON_IsString(name_item) ? name_item->valuestring : NULL;
    if (!room_id_src || !room_id_src[0]) {
        cJSON_Delete(input);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    char room_id[ROOM_CATALOG_ROOM_ID_MAX_LEN] = {0};
    char name[ROOM_CATALOG_ROOM_NAME_MAX_LEN] = {0};
    snprintf(room_id, sizeof(room_id), "%s", room_id_src);
    if (!name_src || !name_src[0]) {
        name_src = room_id_src;
    }
    snprintf(name, sizeof(name), "%s", name_src);

    room_catalog_entry_t room = {0};
    snprintf(room.room_id, sizeof(room.room_id), "%s", room_id);
    snprintf(room.name, sizeof(room.name), "%s", name);
    cJSON_Delete(input);
    esp_err_t err = room_catalog_upsert_and_save(&room);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
    }
    orchestrator_registry_invalidate();
    cJSON *out = cJSON_CreateObject();
    if (!out) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddStringToObject(out, "status", "ok");
    cJSON_AddStringToObject(out, "room_id", room_id);
    cJSON_AddStringToObject(out, "name", name);
    return web_ui_send_json(req, out);
}

static bool gm_room_json_bool(const cJSON *root, const char *key, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItem(root, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

static esp_err_t gm_room_delete_profiles_for_room(const char *room_id, size_t *out_deleted)
{
    gm_game_profile_t *profiles = NULL;
    size_t count = 0;
    esp_err_t err = ESP_OK;
    if (out_deleted) {
        *out_deleted = 0;
    }
    profiles = heap_caps_calloc(GM_GAME_PROFILE_MAX_PROFILES,
                                sizeof(*profiles),
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!profiles) {
        profiles = heap_caps_calloc(GM_GAME_PROFILE_MAX_PROFILES,
                                    sizeof(*profiles),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!profiles) {
        return ESP_ERR_NO_MEM;
    }
    err = gm_game_profile_list_by_room(room_id, profiles, GM_GAME_PROFILE_MAX_PROFILES, &count);
    if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        heap_caps_free(profiles);
        return err;
    }
    for (size_t i = 0; i < count; ++i) {
        if (gm_game_profile_delete_and_save(profiles[i].id) == ESP_OK && out_deleted) {
            ++(*out_deleted);
        }
    }
    heap_caps_free(profiles);
    return ESP_OK;
}

static esp_err_t gm_room_delete_scenarios_for_room(const char *room_id, size_t *out_deleted)
{
    room_scenario_t *scenarios = NULL;
    size_t count = 0;
    esp_err_t err = ESP_OK;
    if (out_deleted) {
        *out_deleted = 0;
    }
    scenarios = heap_caps_calloc(ROOM_SCENARIO_MAX_SCENARIOS,
                                 sizeof(*scenarios),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!scenarios) {
        scenarios = heap_caps_calloc(ROOM_SCENARIO_MAX_SCENARIOS,
                                     sizeof(*scenarios),
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!scenarios) {
        return ESP_ERR_NO_MEM;
    }
    err = room_scenario_list_by_room(room_id, scenarios, ROOM_SCENARIO_MAX_SCENARIOS, &count);
    if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        heap_caps_free(scenarios);
        return err;
    }
    for (size_t i = 0; i < count; ++i) {
        if (room_scenario_delete_and_save(scenarios[i].id) == ESP_OK && out_deleted) {
            ++(*out_deleted);
        }
    }
    heap_caps_free(scenarios);
    return ESP_OK;
}

esp_err_t gm_room_delete_handler(httpd_req_t *req)
{
    if (!req || req->content_len == 0 || req->content_len > 1024) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    }
    char *body = heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        body = heap_caps_malloc(req->content_len + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    size_t received = 0;
    while (received < req->content_len) {
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
    body[req->content_len] = '\0';
    cJSON *input = cJSON_ParseWithLength(body, req->content_len);
    heap_caps_free(body);
    if (!input) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }
    const cJSON *room_id_item = cJSON_GetObjectItem(input, "room_id");
    const char *room_id_src = cJSON_IsString(room_id_item) ? room_id_item->valuestring : NULL;
    if (!room_id_src || !room_id_src[0] || strcmp(room_id_src, "unassigned") == 0) {
        cJSON_Delete(input);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "valid room_id required");
    }
    char room_id[ROOM_CATALOG_ROOM_ID_MAX_LEN] = {0};
    snprintf(room_id, sizeof(room_id), "%s", room_id_src);
    bool delete_content = gm_room_json_bool(input, "delete_content", true);

    cJSON_Delete(input);
    bool existed = room_catalog_exists(room_id);
    esp_err_t err = room_catalog_delete_and_save(room_id);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
    }

    size_t removed_profiles = 0;
    size_t removed_scenarios = 0;
    if (delete_content) {
        err = gm_room_delete_profiles_for_room(room_id, &removed_profiles);
        if (err != ESP_OK) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "profile cleanup failed");
        }
        err = gm_room_delete_scenarios_for_room(room_id, &removed_scenarios);
        if (err != ESP_OK) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scenario cleanup failed");
        }
    }

    orchestrator_registry_invalidate();
    cJSON *out = cJSON_CreateObject();
    if (!out) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddStringToObject(out, "status", "ok");
    cJSON_AddStringToObject(out, "room_id", room_id);
    cJSON_AddNumberToObject(out, "removed_rooms", existed ? 1 : 0);
    cJSON_AddNumberToObject(out, "removed_profiles", removed_profiles);
    cJSON_AddNumberToObject(out, "removed_scenarios", removed_scenarios);
    return web_ui_send_json(req, out);
}
