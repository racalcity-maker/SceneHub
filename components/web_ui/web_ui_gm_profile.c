#include "web_ui_handlers.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"

#include "gm_api.h"
#include "gm_game_profile.h"
#include "orchestrator_registry.h"
#include "web_ui_utils.h"

#define GM_GAME_PROFILE_BODY_MAX_BYTES (128 * 1024)

static void *gm_profile_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static bool gm_profile_read_query_value(httpd_req_t *req,
                                        const char *key,
                                        char *out,
                                        size_t out_size)
{
    char query[256] = {0};
    if (!req || !key || !out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, out_size) == ESP_OK;
}

static esp_err_t gm_profile_read_body(httpd_req_t *req,
                                      size_t max_len,
                                      char **out_body,
                                      size_t *out_len)
{
    char *body = NULL;
    size_t received = 0;
    if (!req || !out_body || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_body = NULL;
    *out_len = 0;
    if (req->content_len <= 0 || req->content_len > (int)max_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    body = gm_profile_alloc((size_t)req->content_len + 1);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    while (received < (size_t)req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            heap_caps_free(body);
            return ESP_FAIL;
        }
        received += (size_t)r;
    }
    body[received] = '\0';
    *out_body = body;
    *out_len = received;
    return ESP_OK;
}

static esp_err_t gm_profile_send_error(httpd_req_t *req, esp_err_t err)
{
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE ||
        err == ESP_ERR_INVALID_VERSION) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid game profile request");
    }
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "game profile not found");
    }
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "game profile invalid state", HTTPD_RESP_USE_STRLEN);
    }
    if (err == ESP_ERR_NO_MEM) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "game profile operation failed");
}

static esp_err_t gm_profile_send_store_ok(httpd_req_t *req, const char *operation)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "operation", operation ? operation : "");
    cJSON_AddStringToObject(root, "path", GM_GAME_PROFILE_STORAGE_PATH);
    cJSON_AddNumberToObject(root, "generation", gm_game_profile_generation());
    return web_ui_send_json(req, root);
}

static const cJSON *gm_profile_payload_object(const cJSON *root)
{
    const cJSON *profile = cJSON_GetObjectItemCaseSensitive(root, "profile");
    if (cJSON_IsObject(profile)) {
        return profile;
    }
    return root;
}

static esp_err_t gm_profile_read_json(httpd_req_t *req,
                                      size_t max_len,
                                      cJSON **out_root,
                                      size_t *out_len)
{
    char *body = NULL;
    size_t body_len = 0;
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;
    if (!out_root) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_root = NULL;
    err = gm_profile_read_body(req, max_len, &body, &body_len);
    if (err != ESP_OK) {
        return err;
    }
    root = cJSON_ParseWithLength(body, body_len);
    heap_caps_free(body);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_root = root;
    if (out_len) {
        *out_len = body_len;
    }
    return ESP_OK;
}

esp_err_t gm_room_profiles_handler(httpd_req_t *req)
{
    char room_id[GM_GAME_PROFILE_ROOM_ID_MAX_LEN] = {0};
    gm_game_profile_t *profiles = NULL;
    gm_room_session_t *session = NULL;
    size_t count = 0;
    cJSON *root = NULL;
    cJSON *items = NULL;
    esp_err_t err = ESP_OK;

    if (!gm_profile_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return gm_profile_send_error(req, ESP_ERR_INVALID_ARG);
    }
    profiles = gm_profile_alloc(sizeof(*profiles) * GM_GAME_PROFILE_MAX_PROFILES);
    if (!profiles) {
        return gm_profile_send_error(req, ESP_ERR_NO_MEM);
    }
    session = gm_profile_alloc(sizeof(*session));
    if (!session) {
        heap_caps_free(profiles);
        return gm_profile_send_error(req, ESP_ERR_NO_MEM);
    }
    err = gm_game_profile_list_by_room(room_id, profiles, GM_GAME_PROFILE_MAX_PROFILES, &count);
    if (err != ESP_OK) {
        heap_caps_free(session);
        heap_caps_free(profiles);
        return gm_profile_send_error(req, err);
    }

    root = cJSON_CreateObject();
    items = cJSON_CreateArray();
    if (!root || !items) {
        cJSON_Delete(root);
        cJSON_Delete(items);
        heap_caps_free(session);
        heap_caps_free(profiles);
        return gm_profile_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "room_id", room_id);
    cJSON_AddNumberToObject(root, "generation", gm_game_profile_generation());
    if (gm_api_room_session_get(room_id, session) == ESP_OK) {
        cJSON_AddStringToObject(root, "selected_profile_id", session->selected_profile_id);
    } else {
        cJSON_AddStringToObject(root, "selected_profile_id", "");
    }
    for (size_t i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            heap_caps_free(session);
            heap_caps_free(profiles);
            return gm_profile_send_error(req, ESP_ERR_NO_MEM);
        }
        err = gm_game_profile_to_json(&profiles[i], item);
        if (err != ESP_OK) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            cJSON_Delete(items);
            heap_caps_free(session);
            heap_caps_free(profiles);
            return gm_profile_send_error(req, err);
        }
        cJSON_AddBoolToObject(item, "valid", gm_game_profile_validate_reference(&profiles[i]) == ESP_OK);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "profiles", items);
    heap_caps_free(session);
    heap_caps_free(profiles);
    return web_ui_send_json(req, root);
}

esp_err_t gm_room_profile_select_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    cJSON *out = NULL;
    const cJSON *room_id_item = NULL;
    const cJSON *profile_id_item = NULL;
    const char *room_id = NULL;
    const char *profile_id = NULL;
    esp_err_t err = gm_profile_read_json(req, 512, &root, NULL);
    if (err != ESP_OK) {
        return gm_profile_send_error(req, err);
    }
    room_id_item = cJSON_GetObjectItemCaseSensitive(root, "room_id");
    profile_id_item = cJSON_GetObjectItemCaseSensitive(root, "profile_id");
    room_id = cJSON_IsString(room_id_item) ? room_id_item->valuestring : NULL;
    profile_id = cJSON_IsString(profile_id_item) ? profile_id_item->valuestring : NULL;
    if (!room_id || !room_id[0] || !profile_id || !profile_id[0]) {
        cJSON_Delete(root);
        return gm_profile_send_error(req, ESP_ERR_INVALID_ARG);
    }
    err = gm_api_select_profile(room_id, profile_id);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return gm_profile_send_error(req, err);
    }
    orchestrator_registry_invalidate();
    out = cJSON_CreateObject();
    if (!out) {
        cJSON_Delete(root);
        return gm_profile_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "room_id", room_id);
    cJSON_AddStringToObject(out, "selected_profile_id", profile_id);
    cJSON_Delete(root);
    return web_ui_send_json(req, out);
}

esp_err_t gm_room_profile_save_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    cJSON *out = NULL;
    cJSON *profile_json = NULL;
    gm_game_profile_t profile = {0};
    esp_err_t err = gm_profile_read_json(req, 2048, &root, NULL);
    if (err != ESP_OK) {
        return gm_profile_send_error(req, err);
    }
    err = gm_game_profile_from_json(gm_profile_payload_object(root), &profile);
    if (err == ESP_OK) {
        err = gm_game_profile_validate_reference(&profile);
    }
    if (err == ESP_OK) {
        err = gm_game_profile_upsert_and_save(&profile);
    }
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return gm_profile_send_error(req, err);
    }
    orchestrator_registry_invalidate();
    out = cJSON_CreateObject();
    profile_json = cJSON_CreateObject();
    if (!out || !profile_json) {
        cJSON_Delete(root);
        cJSON_Delete(out);
        cJSON_Delete(profile_json);
        return gm_profile_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddNumberToObject(out, "generation", gm_game_profile_generation());
    err = gm_game_profile_to_json(&profile, profile_json);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        cJSON_Delete(out);
        cJSON_Delete(profile_json);
        return gm_profile_send_error(req, err);
    }
    cJSON_AddItemToObject(out, "profile", profile_json);
    cJSON_Delete(root);
    return web_ui_send_json(req, out);
}

esp_err_t gm_room_profile_delete_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    cJSON *out = NULL;
    const cJSON *profile_id_item = NULL;
    const char *profile_id = NULL;
    esp_err_t err = gm_profile_read_json(req, 512, &root, NULL);
    if (err != ESP_OK) {
        return gm_profile_send_error(req, err);
    }
    profile_id_item = cJSON_GetObjectItemCaseSensitive(root, "profile_id");
    profile_id = cJSON_IsString(profile_id_item) ? profile_id_item->valuestring : NULL;
    if (!profile_id || !profile_id[0]) {
        cJSON_Delete(root);
        return gm_profile_send_error(req, ESP_ERR_INVALID_ARG);
    }
    err = gm_game_profile_delete_and_save(profile_id);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return gm_profile_send_error(req, err);
    }
    orchestrator_registry_invalidate();
    out = cJSON_CreateObject();
    if (!out) {
        cJSON_Delete(root);
        return gm_profile_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "deleted_profile_id", profile_id);
    cJSON_AddNumberToObject(out, "generation", gm_game_profile_generation());
    cJSON_Delete(root);
    return web_ui_send_json(req, out);
}

esp_err_t gm_profiles_export_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    esp_err_t err = gm_game_profile_export_json(&root);
    if (err != ESP_OK) {
        return gm_profile_send_error(req, err);
    }
    httpd_resp_set_hdr(req,
                       "Content-Disposition",
                       "attachment; filename=\"game_profiles.json\"");
    return web_ui_send_json(req, root);
}

esp_err_t gm_profiles_import_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    const cJSON *items = NULL;
    int profile_count = 0;
    esp_err_t err = gm_profile_read_json(req,
                                         GM_GAME_PROFILE_BODY_MAX_BYTES,
                                         &root,
                                         NULL);
    if (err != ESP_OK) {
        return gm_profile_send_error(req, err);
    }
    items = cJSON_GetObjectItemCaseSensitive(root, "game_profiles");
    if (cJSON_IsArray(items)) {
        profile_count = cJSON_GetArraySize(items);
    }
    err = gm_game_profile_import_json_and_save(root);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return gm_profile_send_error(req, err);
    }
    orchestrator_registry_invalidate();
    root = cJSON_CreateObject();
    if (!root) {
        return gm_profile_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "operation", "import");
    cJSON_AddNumberToObject(root, "profile_count", profile_count);
    cJSON_AddNumberToObject(root, "generation", gm_game_profile_generation());
    return web_ui_send_json(req, root);
}

esp_err_t gm_profiles_save_handler(httpd_req_t *req)
{
    esp_err_t err = gm_game_profile_save();
    if (err != ESP_OK) {
        return gm_profile_send_error(req, err);
    }
    return gm_profile_send_store_ok(req, "save");
}

esp_err_t gm_profiles_load_handler(httpd_req_t *req)
{
    esp_err_t err = gm_game_profile_load();
    if (err != ESP_OK) {
        return gm_profile_send_error(req, err);
    }
    orchestrator_registry_invalidate();
    return gm_profile_send_store_ok(req, "load");
}
