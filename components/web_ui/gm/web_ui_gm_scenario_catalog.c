#include "web_ui_handlers.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"

#include "orch_device_view.h"
#include "orch_scenario_view.h"
#include "room_scenario.h"
#include "gm/web_ui_gm_quest_device_json.h"
#include "web_ui_utils.h"

static bool gm_scenario_catalog_read_query_value(httpd_req_t *req,
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

static esp_err_t gm_scenario_add_step_schemas(cJSON *root)
{
    orch_room_scenario_step_schema_t *catalog = NULL;
    size_t count = 0;
    cJSON *schemas = cJSON_CreateArray();
    esp_err_t err = ESP_OK;
    if (!root || !schemas) {
        cJSON_Delete(schemas);
        return ESP_ERR_NO_MEM;
    }
    catalog = heap_caps_calloc(ORCH_ROOM_SCENARIO_MAX_STEP_SCHEMAS,
                               sizeof(*catalog),
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!catalog) {
        cJSON_Delete(schemas);
        return ESP_ERR_NO_MEM;
    }
    err = orchestrator_registry_list_scenario_step_schemas(catalog,
                                                           ORCH_ROOM_SCENARIO_MAX_STEP_SCHEMAS,
                                                           &count);
    if (err != ESP_OK) {
        heap_caps_free(catalog);
        cJSON_Delete(schemas);
        return err;
    }
    for (size_t i = 0; i < count; ++i) {
        cJSON *schema = cJSON_CreateObject();
        cJSON *fields = cJSON_CreateArray();
        if (!schema || !fields) {
            cJSON_Delete(schema);
            cJSON_Delete(fields);
            heap_caps_free(catalog);
            cJSON_Delete(schemas);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(schema, "type", catalog[i].type);
        cJSON_AddStringToObject(schema, "label", catalog[i].label);
        cJSON_AddStringToObject(schema, "description", catalog[i].description);
        for (uint8_t field_index = 0; field_index < catalog[i].field_count; ++field_index) {
            const orch_room_scenario_field_schema_t *field = &catalog[i].fields[field_index];
            cJSON *field_obj = cJSON_CreateObject();
            if (!field_obj) {
                cJSON_Delete(schema);
                cJSON_Delete(fields);
                heap_caps_free(catalog);
                cJSON_Delete(schemas);
                return ESP_ERR_NO_MEM;
            }
            cJSON_AddStringToObject(field_obj, "key", field->key);
            cJSON_AddStringToObject(field_obj, "type", field->type);
            cJSON_AddStringToObject(field_obj, "label", field->label);
            cJSON_AddBoolToObject(field_obj, "required", field->required);
            if (field->depends_on[0]) {
                cJSON_AddStringToObject(field_obj, "depends_on", field->depends_on);
            }
            cJSON_AddItemToArray(fields, field_obj);
        }
        cJSON_AddItemToObject(schema, "fields", fields);
        cJSON_AddItemToArray(schemas, schema);
    }
    heap_caps_free(catalog);
    cJSON_AddItemToObject(root, "step_schemas", schemas);
    return ESP_OK;
}

static orch_quest_device_catalog_entry_t *gm_scenario_alloc_devices(size_t count)
{
    orch_quest_device_catalog_entry_t *devices = heap_caps_calloc(count,
                                                                  sizeof(*devices),
                                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!devices) {
        devices = heap_caps_calloc(count,
                                   sizeof(*devices),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return devices;
}

static esp_err_t gm_scenario_add_quest_devices_catalog(cJSON *root)
{
    orch_quest_device_catalog_entry_t *devices = NULL;
    size_t count = 0;
    cJSON *array = NULL;
    esp_err_t err = ESP_OK;
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    devices = gm_scenario_alloc_devices(QUEST_DEVICE_MAX_DEVICES + 4);
    array = cJSON_CreateArray();
    if (!devices || !array) {
        heap_caps_free(devices);
        cJSON_Delete(array);
        return ESP_ERR_NO_MEM;
    }
    err = orchestrator_registry_list_quest_device_catalog(devices,
                                                          QUEST_DEVICE_MAX_DEVICES + 4,
                                                          &count,
                                                          true);
    if (err != ESP_OK) {
        heap_caps_free(devices);
        cJSON_Delete(array);
        return err;
    }
    for (size_t i = 0; i < count; ++i) {
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            heap_caps_free(devices);
            cJSON_Delete(array);
            return ESP_ERR_NO_MEM;
        }
        err = gm_quest_device_catalog_entry_to_json(&devices[i], obj, true);
        if (err != ESP_OK) {
            cJSON_Delete(obj);
            heap_caps_free(devices);
            cJSON_Delete(array);
            return err;
        }
        cJSON_AddItemToArray(array, obj);
    }
    heap_caps_free(devices);
    cJSON_AddItemToObject(root, "quest_devices", array);
    return ESP_OK;
}

esp_err_t gm_room_scenario_editor_catalog_handler(httpd_req_t *req)
{
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN] = {0};
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;

    if (!gm_scenario_catalog_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }

    root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    cJSON_AddStringToObject(root, "room_id", room_id);
    err = gm_scenario_add_quest_devices_catalog(root);
    if (err == ESP_OK) {
        err = gm_scenario_add_step_schemas(root);
    }
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    return web_ui_send_json(req, root);
}
