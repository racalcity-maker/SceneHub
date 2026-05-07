#include "web_ui_handlers.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"

#include "quest_device.h"
#include "room_scenario.h"
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

static esp_err_t gm_scenario_add_field_schema(cJSON *fields,
                                              const char *key,
                                              const char *type,
                                              const char *label,
                                              const char *depends_on,
                                              bool required)
{
    cJSON *field = NULL;
    if (!fields || !key || !type || !label) {
        return ESP_ERR_INVALID_ARG;
    }
    field = cJSON_CreateObject();
    if (!field) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(field, "key", key);
    cJSON_AddStringToObject(field, "type", type);
    cJSON_AddStringToObject(field, "label", label);
    cJSON_AddBoolToObject(field, "required", required);
    if (depends_on && depends_on[0]) {
        cJSON_AddStringToObject(field, "depends_on", depends_on);
    }
    cJSON_AddItemToArray(fields, field);
    return ESP_OK;
}

static esp_err_t gm_scenario_add_wait_skip_schema(cJSON *fields)
{
    esp_err_t err = gm_scenario_add_field_schema(fields,
                                                 "allow_operator_skip",
                                                 "checkbox",
                                                 "Allow operator skip",
                                                 NULL,
                                                 false);
    if (err == ESP_OK) {
        err = gm_scenario_add_field_schema(fields,
                                           "operator_skip_label",
                                           "text",
                                           "Skip label",
                                           "allow_operator_skip",
                                           false);
    }
    return err;
}

static esp_err_t gm_scenario_add_step_schema(cJSON *schemas,
                                             const char *type,
                                             const char *label,
                                             const char *description)
{
    cJSON *schema = NULL;
    cJSON *fields = NULL;
    esp_err_t err = ESP_OK;
    if (!schemas || !type || !label) {
        return ESP_ERR_INVALID_ARG;
    }
    schema = cJSON_CreateObject();
    fields = cJSON_CreateArray();
    if (!schema || !fields) {
        cJSON_Delete(schema);
        cJSON_Delete(fields);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(schema, "type", type);
    cJSON_AddStringToObject(schema, "label", label);
    cJSON_AddStringToObject(schema, "description", description ? description : "");
    if (strcmp(type, "DEVICE_COMMAND") == 0) {
        err = gm_scenario_add_field_schema(fields, "device_id", "device_select", "Device", NULL, true);
        if (err == ESP_OK) {
            err = gm_scenario_add_field_schema(fields, "command_id", "device_command_select", "Command", "device_id", true);
        }
        if (err == ESP_OK) {
            err = gm_scenario_add_field_schema(fields, "params", "params_object", "Parameters", "command_id", false);
        }
    } else if (strcmp(type, "WAIT_DEVICE_EVENT") == 0) {
        err = gm_scenario_add_field_schema(fields, "device_id", "device_select", "Device", NULL, true);
        if (err == ESP_OK) {
            err = gm_scenario_add_field_schema(fields, "event_id", "device_event_select", "Event", "device_id", true);
        }
        if (err == ESP_OK) {
            err = gm_scenario_add_field_schema(fields, "timeout_ms", "optional_duration_ms", "Timeout", NULL, false);
        }
        if (err == ESP_OK) {
            err = gm_scenario_add_field_schema(fields, "timeout_message", "textarea", "Timeout message", NULL, false);
        }
        if (err == ESP_OK) {
            err = gm_scenario_add_wait_skip_schema(fields);
        }
    } else if (strcmp(type, "WAIT_ANY_DEVICE_EVENT") == 0) {
        err = gm_scenario_add_field_schema(fields, "events", "event_group", "Events", NULL, true);
        if (err == ESP_OK) {
            err = gm_scenario_add_wait_skip_schema(fields);
        }
    } else if (strcmp(type, "WAIT_ALL_DEVICE_EVENTS") == 0) {
        err = gm_scenario_add_field_schema(fields, "events", "event_group", "Events", NULL, true);
        if (err == ESP_OK) {
            err = gm_scenario_add_wait_skip_schema(fields);
        }
    } else if (strcmp(type, "DEVICE_COMMAND_GROUP") == 0) {
        err = gm_scenario_add_field_schema(fields, "commands", "command_group", "Commands", NULL, true);
    } else if (strcmp(type, "WAIT_TIME") == 0) {
        err = gm_scenario_add_field_schema(fields, "duration_ms", "duration_ms", "Duration", NULL, true);
        if (err == ESP_OK) {
            err = gm_scenario_add_wait_skip_schema(fields);
        }
    } else if (strcmp(type, "OPERATOR_APPROVAL") == 0) {
        err = gm_scenario_add_field_schema(fields, "prompt", "text", "Prompt", NULL, true);
        if (err == ESP_OK) {
            err = gm_scenario_add_field_schema(fields, "approve_label", "text", "Button label", NULL, false);
        }
    } else if (strcmp(type, "SHOW_OPERATOR_MESSAGE") == 0) {
        err = gm_scenario_add_field_schema(fields, "message", "textarea", "Message", NULL, true);
    } else if (strcmp(type, "SET_FLAG") == 0) {
        err = gm_scenario_add_field_schema(fields, "flag_name", "text", "Flag", NULL, true);
        if (err == ESP_OK) {
            err = gm_scenario_add_field_schema(fields, "value", "checkbox", "Value", NULL, true);
        }
    } else if (strcmp(type, "WAIT_FLAGS") == 0) {
        err = gm_scenario_add_field_schema(fields, "flags", "flag_list", "Flags", NULL, true);
        if (err == ESP_OK) {
            err = gm_scenario_add_field_schema(fields, "timeout_ms", "optional_duration_ms", "Timeout", NULL, false);
        }
        if (err == ESP_OK) {
            err = gm_scenario_add_field_schema(fields, "timeout_message", "textarea", "Timeout message", NULL, false);
        }
        if (err == ESP_OK) {
            err = gm_scenario_add_wait_skip_schema(fields);
        }
    } else if (strcmp(type, "END_GAME") == 0) {
        err = ESP_OK;
    } else {
        err = ESP_ERR_NOT_SUPPORTED;
    }
    if (err != ESP_OK) {
        cJSON_Delete(schema);
        cJSON_Delete(fields);
        return err;
    }
    cJSON_AddItemToObject(schema, "fields", fields);
    cJSON_AddItemToArray(schemas, schema);
    return ESP_OK;
}

static esp_err_t gm_scenario_add_step_schemas(cJSON *root)
{
    cJSON *schemas = cJSON_CreateArray();
    esp_err_t err = ESP_OK;
    if (!root || !schemas) {
        cJSON_Delete(schemas);
        return ESP_ERR_NO_MEM;
    }
    err = gm_scenario_add_step_schema(schemas,
                                      "DEVICE_COMMAND",
                                      "Device command",
                                      "Send a saved capability command to a quest or system device");
    if (err == ESP_OK) {
        err = gm_scenario_add_step_schema(schemas,
                                          "DEVICE_COMMAND_GROUP",
                                          "Command group",
                                          "Send several saved capability commands in order");
    }
    if (err == ESP_OK) {
        err = gm_scenario_add_step_schema(schemas,
                                          "WAIT_DEVICE_EVENT",
                                          "Wait device event",
                                          "Wait for a saved capability event from a quest or system device");
    }
    if (err == ESP_OK) {
        err = gm_scenario_add_step_schema(schemas,
                                          "WAIT_ANY_DEVICE_EVENT",
                                          "Wait any device event",
                                          "Wait until one event from a small list arrives");
    }
    if (err == ESP_OK) {
        err = gm_scenario_add_step_schema(schemas,
                                          "WAIT_ALL_DEVICE_EVENTS",
                                          "Wait all device events",
                                          "Wait until every event in a small list arrives");
    }
    if (err == ESP_OK) {
        err = gm_scenario_add_step_schema(schemas,
                                          "WAIT_TIME",
                                          "Wait time",
                                          "Pause the scenario for a fixed duration");
    }
    if (err == ESP_OK) {
        err = gm_scenario_add_step_schema(schemas,
                                          "OPERATOR_APPROVAL",
                                          "Operator approval",
                                          "Pause until the operator approves continuing");
    }
    if (err == ESP_OK) {
        err = gm_scenario_add_step_schema(schemas,
                                          "SHOW_OPERATOR_MESSAGE",
                                          "Show operator message",
                                          "Show a note to the operator and continue");
    }
    if (err == ESP_OK) {
        err = gm_scenario_add_step_schema(schemas,
                                          "SET_FLAG",
                                          "Set flag",
                                          "Set a runtime boolean flag for this scenario run");
    }
    if (err == ESP_OK) {
        err = gm_scenario_add_step_schema(schemas,
                                          "WAIT_FLAGS",
                                          "Wait flags",
                                          "Wait until selected scenario flags have expected values");
    }
    if (err == ESP_OK) {
        err = gm_scenario_add_step_schema(schemas,
                                          "END_GAME",
                                          "End game",
                                          "Finish the game timer without stopping audio automatically");
    }
    if (err != ESP_OK) {
        cJSON_Delete(schemas);
        return err;
    }
    cJSON_AddItemToObject(root, "step_schemas", schemas);
    return ESP_OK;
}

static quest_device_t *gm_scenario_alloc_devices(size_t count)
{
    quest_device_t *devices = heap_caps_calloc(count,
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
    quest_device_t *devices = NULL;
    size_t count = 0;
    cJSON *array = NULL;
    esp_err_t err = ESP_OK;
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    devices = gm_scenario_alloc_devices(QUEST_DEVICE_MAX_DEVICES);
    array = cJSON_CreateArray();
    if (!devices || !array) {
        heap_caps_free(devices);
        cJSON_Delete(array);
        return ESP_ERR_NO_MEM;
    }
    err = quest_device_list(devices, QUEST_DEVICE_MAX_DEVICES, &count, false);
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
        err = quest_device_to_json(&devices[i], obj);
        if (err != ESP_OK) {
            cJSON_Delete(obj);
            heap_caps_free(devices);
            cJSON_Delete(array);
            return err;
        }
        cJSON_AddItemToArray(array, obj);
    }
    heap_caps_free(devices);

    const char *system_ids[] = {
        QUEST_DEVICE_SYSTEM_AUDIO_ID,
        QUEST_DEVICE_SYSTEM_RELAY_ID,
        QUEST_DEVICE_SYSTEM_MOSFET_ID,
        QUEST_DEVICE_SYSTEM_INPUT_ID,
        QUEST_DEVICE_SYSTEM_GPIO_ID,
    };
    quest_device_t *system_device = gm_scenario_alloc_devices(1);
    if (!system_device) {
        cJSON_Delete(array);
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < sizeof(system_ids) / sizeof(system_ids[0]); ++i) {
        memset(system_device, 0, sizeof(*system_device));
        err = quest_device_get(system_ids[i], system_device);
        if (err != ESP_OK) {
            break;
        }
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            heap_caps_free(system_device);
            cJSON_Delete(array);
            return ESP_ERR_NO_MEM;
        }
        err = quest_device_to_json(system_device, obj);
        if (err == ESP_OK) {
            cJSON_AddItemToArray(array, obj);
        } else {
            cJSON_Delete(obj);
            break;
        }
    }
    heap_caps_free(system_device);
    if (err != ESP_OK) {
        cJSON_Delete(array);
        return err;
    }
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
