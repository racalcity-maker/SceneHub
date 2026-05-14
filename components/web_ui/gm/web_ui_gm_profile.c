#include "web_ui_handlers.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cJSON.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"

#include "gm_game_profile.h"
#include "orchestrator_registry.h"
#include "scenehub_control.h"
#include "web_ui_utils.h"

#define GM_GAME_PROFILE_BODY_MAX_BYTES (128 * 1024)

EXT_RAM_BSS_ATTR static orch_room_entry_t s_gm_profile_room;

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
    return web_ui_send_scenehub_control_error(req, err, NULL, "game profile operation failed");
}

static esp_err_t gm_profile_send_control_error(httpd_req_t *req,
                                               esp_err_t call_err,
                                               const scenehub_control_result_t *result)
{
    return web_ui_send_scenehub_control_error(req, call_err, result, "game profile operation failed");
}

static esp_err_t gm_profile_send_store_ok(httpd_req_t *req, const char *operation)
{
    return web_ui_send_store_operation_json(req,
                                            operation,
                                            GM_GAME_PROFILE_STORAGE_PATH,
                                            gm_game_profile_generation());
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

static esp_err_t gm_profile_add_json(cJSON *out, const orch_room_profile_entry_t *profile)
{
    if (!cJSON_IsObject(out) || !profile) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(out, "id", profile->id);
    cJSON_AddStringToObject(out, "name", profile->name);
    cJSON_AddStringToObject(out, "room_id", profile->room_id);
    cJSON_AddStringToObject(out, "scenario_id", profile->scenario_id);
    cJSON_AddNumberToObject(out, "duration_ms", profile->duration_ms);
    cJSON_AddStringToObject(out, "hint_pack_id", profile->hint_pack_id);
    cJSON_AddStringToObject(out, "audio_pack_id", profile->audio_pack_id);
    cJSON_AddBoolToObject(out, "enabled", profile->enabled);
    cJSON_AddBoolToObject(out, "valid", profile->valid);
    return ESP_OK;
}

esp_err_t gm_room_profiles_handler(httpd_req_t *req)
{
    char room_id[GM_GAME_PROFILE_ROOM_ID_MAX_LEN] = {0};
    orch_room_profile_entry_t *profiles = NULL;
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
    err = orchestrator_registry_list_room_profiles(room_id,
                                                   profiles,
                                                   GM_GAME_PROFILE_MAX_PROFILES,
                                                   &count);
    if (err != ESP_OK) {
        heap_caps_free(profiles);
        return gm_profile_send_error(req, err);
    }

    root = cJSON_CreateObject();
    items = cJSON_CreateArray();
    if (!root || !items) {
        cJSON_Delete(root);
        cJSON_Delete(items);
        heap_caps_free(profiles);
        return gm_profile_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "room_id", room_id);
    cJSON_AddNumberToObject(root, "generation", gm_game_profile_generation());
    memset(&s_gm_profile_room, 0, sizeof(s_gm_profile_room));
    if (orchestrator_registry_get_room(room_id, &s_gm_profile_room) == ESP_OK) {
        cJSON_AddStringToObject(root, "selected_profile_id", s_gm_profile_room.selected_profile_id);
    } else {
        cJSON_AddStringToObject(root, "selected_profile_id", "");
    }
    for (size_t i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            heap_caps_free(profiles);
            return gm_profile_send_error(req, ESP_ERR_NO_MEM);
        }
        err = gm_profile_add_json(item, &profiles[i]);
        if (err != ESP_OK) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            cJSON_Delete(items);
            heap_caps_free(profiles);
            return gm_profile_send_error(req, err);
        }
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "profiles", items);
    heap_caps_free(profiles);
    return web_ui_send_json(req, root);
}

esp_err_t gm_room_profile_select_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    char room_id[GM_GAME_PROFILE_ROOM_ID_MAX_LEN] = {0};
    char profile_id[GM_GAME_PROFILE_ID_MAX_LEN] = {0};
    const cJSON *room_id_item = NULL;
    const cJSON *profile_id_item = NULL;
    esp_err_t err = gm_profile_read_json(req, 512, &root, NULL);
    if (err != ESP_OK) {
        return gm_profile_send_error(req, err);
    }
    room_id_item = cJSON_GetObjectItemCaseSensitive(root, "room_id");
    profile_id_item = cJSON_GetObjectItemCaseSensitive(root, "profile_id");
    if (!cJSON_IsString(room_id_item) || !room_id_item->valuestring || !room_id_item->valuestring[0] ||
        !cJSON_IsString(profile_id_item) || !profile_id_item->valuestring ||
        !profile_id_item->valuestring[0]) {
        cJSON_Delete(root);
        return gm_profile_send_error(req, ESP_ERR_INVALID_ARG);
    }
    snprintf(room_id, sizeof(room_id), "%s", room_id_item->valuestring);
    snprintf(profile_id, sizeof(profile_id), "%s", profile_id_item->valuestring);
    scenehub_control_result_t result = {0};
    err = scenehub_control_select_profile("http", room_id, profile_id, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        cJSON_Delete(root);
        return gm_profile_send_control_error(req, err, &result);
    }
    cJSON_Delete(root);
    return web_ui_send_selection_result_json(req,
                                             "room_id",
                                             room_id,
                                             "selected_profile_id",
                                             profile_id);
}

esp_err_t gm_room_profile_save_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    cJSON *profile_json = NULL;
    gm_game_profile_t profile = {0};
    esp_err_t err = gm_profile_read_json(req, 2048, &root, NULL);
    if (err != ESP_OK) {
        return gm_profile_send_error(req, err);
    }
    scenehub_control_result_t result = {0};
    err = gm_game_profile_from_json(gm_profile_payload_object(root), &profile);
    if (err == ESP_OK) {
        err = scenehub_control_save_profile("http", &profile, &result);
    }
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        cJSON_Delete(root);
        return gm_profile_send_control_error(req, err, &result);
    }
    profile_json = cJSON_CreateObject();
    if (!profile_json) {
        cJSON_Delete(root);
        cJSON_Delete(profile_json);
        return gm_profile_send_error(req, ESP_ERR_NO_MEM);
    }
    err = gm_game_profile_to_json(&profile, profile_json);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        cJSON_Delete(profile_json);
        return gm_profile_send_error(req, err);
    }
    cJSON_Delete(root);
    return web_ui_send_generation_item_json(req,
                                            gm_game_profile_generation(),
                                            "profile",
                                            profile_json);
}

esp_err_t gm_room_profile_delete_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    char profile_id[GM_GAME_PROFILE_ID_MAX_LEN] = {0};
    const cJSON *profile_id_item = NULL;
    esp_err_t err = gm_profile_read_json(req, 512, &root, NULL);
    if (err != ESP_OK) {
        return gm_profile_send_error(req, err);
    }
    profile_id_item = cJSON_GetObjectItemCaseSensitive(root, "profile_id");
    if (!cJSON_IsString(profile_id_item) || !profile_id_item->valuestring ||
        !profile_id_item->valuestring[0]) {
        cJSON_Delete(root);
        return gm_profile_send_error(req, ESP_ERR_INVALID_ARG);
    }
    snprintf(profile_id, sizeof(profile_id), "%s", profile_id_item->valuestring);
    scenehub_control_result_t result = {0};
    err = scenehub_control_delete_profile("http", profile_id, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        cJSON_Delete(root);
        return gm_profile_send_control_error(req, err, &result);
    }
    cJSON_Delete(root);
    return web_ui_send_deleted_result_json(req,
                                           "deleted_profile_id",
                                           profile_id,
                                           gm_game_profile_generation());
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
    scenehub_control_result_t result = {0};
    err = scenehub_control_import_profiles("http", root, &result);
    cJSON_Delete(root);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_profile_send_control_error(req, err, &result);
    }
    return web_ui_send_import_result_json(req,
                                          "import",
                                          "profile_count",
                                          profile_count,
                                          gm_game_profile_generation());
}

esp_err_t gm_profiles_save_handler(httpd_req_t *req)
{
    scenehub_control_result_t result = {0};
    esp_err_t err = scenehub_control_save_profiles_store("http", &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_profile_send_control_error(req, err, &result);
    }
    return gm_profile_send_store_ok(req, "save");
}

esp_err_t gm_profiles_load_handler(httpd_req_t *req)
{
    scenehub_control_result_t result = {0};
    esp_err_t err = scenehub_control_load_profiles("http", &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_profile_send_control_error(req, err, &result);
    }
    return gm_profile_send_store_ok(req, "load");
}
