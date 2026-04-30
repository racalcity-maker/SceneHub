#include "web_ui_handlers.h"

#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"

#include "gm_api.h"
#include "orchestrator_registry.h"
#include "quest_device.h"
#include "room_scenario.h"
#include "web_ui_utils.h"

#define GM_ROOM_SCENARIO_IMPORT_MAX_BYTES (256 * 1024)

static const char *gm_scenario_runtime_state_str(gm_room_scenario_state_t state)
{
    switch (state) {
    case GM_ROOM_SCENARIO_RUNNING:
        return "running";
    case GM_ROOM_SCENARIO_WAITING:
        return "waiting";
    case GM_ROOM_SCENARIO_PAUSED:
        return "paused";
    case GM_ROOM_SCENARIO_DONE:
        return "done";
    case GM_ROOM_SCENARIO_STOPPED:
        return "stopped";
    case GM_ROOM_SCENARIO_COOLDOWN:
        return "cooldown";
    case GM_ROOM_SCENARIO_ERROR:
        return "error";
    case GM_ROOM_SCENARIO_IDLE:
    default:
        return "idle";
    }
}

static const char *gm_scenario_wait_type_str(gm_room_scenario_wait_type_t wait_type)
{
    switch (wait_type) {
    case GM_ROOM_SCENARIO_WAIT_TIME:
        return "time";
    case GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT:
        return "event";
    case GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS:
        return "all_events";
    case GM_ROOM_SCENARIO_WAIT_OPERATOR:
        return "operator";
    case GM_ROOM_SCENARIO_WAIT_FLAGS:
        return "flags";
    case GM_ROOM_SCENARIO_WAIT_NONE:
    default:
        return "none";
    }
}

static void *gm_scenario_body_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static bool gm_scenario_read_query_value(httpd_req_t *req, const char *key, char *out, size_t out_size)
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

static esp_err_t gm_scenario_read_body(httpd_req_t *req, char **out_body)
{
    char *body = NULL;
    size_t received = 0;
    if (!req || !out_body) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_body = NULL;
    if (req->content_len <= 0 || req->content_len > 512) {
        return ESP_ERR_INVALID_SIZE;
    }
    body = gm_scenario_body_alloc((size_t)req->content_len + 1);
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
    return ESP_OK;
}

static esp_err_t gm_scenario_read_body_limit(httpd_req_t *req,
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
    body = gm_scenario_body_alloc((size_t)req->content_len + 1);
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

static esp_err_t gm_scenario_send_error(httpd_req_t *req, esp_err_t err)
{
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id/scenario_id required");
    }
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "room or scenario not found");
    }
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "scenario invalid state", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scenario select failed");
}

static esp_err_t gm_room_scenarios_send_store_error(httpd_req_t *req, esp_err_t err)
{
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE ||
        err == ESP_ERR_INVALID_VERSION) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid room scenarios json");
    }
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "room scenarios file not found");
    }
    if (err == ESP_ERR_NO_MEM) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "room scenarios operation failed");
}

static esp_err_t gm_room_scenarios_send_ok(httpd_req_t *req, const char *operation)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "operation", operation ? operation : "");
    cJSON_AddStringToObject(root, "path", ROOM_SCENARIO_STORAGE_PATH);
    cJSON_AddNumberToObject(root, "generation", room_scenario_generation());
    return web_ui_send_json(req, root);
}

static esp_err_t gm_scenario_send_runtime_state(httpd_req_t *req, const char *room_id)
{
    gm_room_session_t *session = NULL;
    cJSON *root = NULL;
    cJSON *flags = NULL;
    cJSON *wait_events = NULL;
    cJSON *wait_flags = NULL;
    cJSON *branches = NULL;
    esp_err_t err = ESP_OK;
    session = gm_scenario_body_alloc(sizeof(*session));
    if (!session) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    err = gm_api_room_session_get(room_id, session);
    if (err != ESP_OK) {
        heap_caps_free(session);
        return gm_scenario_send_error(req, err);
    }
    root = cJSON_CreateObject();
    if (!root) {
        heap_caps_free(session);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "room_id", session->room_id);
    cJSON_AddStringToObject(root, "selected_profile_id", session->selected_profile_id);
    cJSON_AddStringToObject(root, "selected_profile_name", session->selected_profile_name);
    cJSON_AddStringToObject(root, "selected_profile_scenario_id", session->selected_profile_scenario_id);
    cJSON_AddNumberToObject(root, "selected_profile_duration_ms", session->selected_profile_duration_ms);
    cJSON_AddStringToObject(root, "selected_scenario_id", session->selected_scenario_id);
    cJSON_AddStringToObject(root, "selected_scenario_name", session->selected_scenario_name);
    cJSON_AddStringToObject(root,
                            "running_scenario_id",
                            session->running_scenario_valid ? session->running_scenario.id : "");
    cJSON_AddStringToObject(root,
                            "running_scenario_name",
                            session->running_scenario_valid ? session->running_scenario.name : "");
    cJSON_AddNumberToObject(root, "running_scenario_generation", session->running_scenario_generation);
    cJSON_AddStringToObject(root, "scenario_runtime_state",
                            gm_scenario_runtime_state_str(session->scenario_state));
    cJSON_AddNumberToObject(root, "scenario_current_step_index", session->current_step_index);
    cJSON_AddStringToObject(root, "scenario_wait_type", gm_scenario_wait_type_str(session->wait_type));
    cJSON_AddNumberToObject(root, "scenario_wait_until_ms", session->wait_until_ms);
    cJSON_AddNumberToObject(root, "scenario_wait_started_at_ms", session->wait_started_at_ms);
    cJSON_AddStringToObject(root, "scenario_wait_event_type", session->wait_event_type);
    cJSON_AddStringToObject(root, "scenario_wait_source_id", session->wait_source_id);
    wait_events = cJSON_CreateArray();
    wait_flags = cJSON_CreateArray();
    if (!wait_events || !wait_flags) {
        heap_caps_free(session);
        cJSON_Delete(root);
        cJSON_Delete(wait_events);
        cJSON_Delete(wait_flags);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    for (uint8_t i = 0; i < session->wait_event_count && i < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS; ++i) {
        cJSON *event = cJSON_CreateObject();
        if (!event) {
            heap_caps_free(session);
            cJSON_Delete(root);
            cJSON_Delete(wait_events);
            cJSON_Delete(wait_flags);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        }
        cJSON_AddStringToObject(event, "event_type", session->wait_events[i].event_type);
        cJSON_AddStringToObject(event, "source_id", session->wait_events[i].source_id);
        cJSON_AddItemToArray(wait_events, event);
    }
    for (uint8_t i = 0; i < session->wait_flag_count && i < ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS; ++i) {
        cJSON *flag = cJSON_CreateObject();
        if (!flag) {
            heap_caps_free(session);
            cJSON_Delete(root);
            cJSON_Delete(wait_events);
            cJSON_Delete(wait_flags);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        }
        cJSON_AddStringToObject(flag, "name", session->wait_flags[i].name);
        cJSON_AddBoolToObject(flag, "value", session->wait_flags[i].value);
        cJSON_AddItemToArray(wait_flags, flag);
    }
    cJSON_AddItemToObject(root, "scenario_wait_events", wait_events);
    cJSON_AddNumberToObject(root, "scenario_wait_event_count", session->wait_event_count);
    cJSON_AddItemToObject(root, "scenario_wait_flags", wait_flags);
    cJSON_AddNumberToObject(root, "scenario_wait_flag_count", session->wait_flag_count);
    cJSON_AddStringToObject(root, "scenario_wait_operator_prompt", session->wait_operator_prompt);
    cJSON_AddStringToObject(root, "scenario_wait_operator_label", session->wait_operator_label);
    cJSON_AddBoolToObject(root,
                          "scenario_wait_operator_skip_allowed",
                          session->wait_operator_skip_allowed);
    cJSON_AddStringToObject(root,
                            "scenario_wait_operator_skip_label",
                            session->wait_operator_skip_label);
    cJSON_AddStringToObject(root, "scenario_operator_message", session->scenario_operator_message);
    flags = cJSON_CreateArray();
    if (!flags) {
        heap_caps_free(session);
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    for (uint8_t i = 0; i < session->scenario_flag_count && i < GM_ROOM_SCENARIO_MAX_FLAGS; ++i) {
        cJSON *flag = cJSON_CreateObject();
        if (!flag) {
            heap_caps_free(session);
            cJSON_Delete(root);
            cJSON_Delete(flags);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        }
        cJSON_AddStringToObject(flag, "name", session->scenario_flags[i].name);
        cJSON_AddBoolToObject(flag, "value", session->scenario_flags[i].value);
        cJSON_AddItemToArray(flags, flag);
    }
    cJSON_AddItemToObject(root, "scenario_flags", flags);
    cJSON_AddNumberToObject(root, "scenario_flag_count", session->scenario_flag_count);
    branches = cJSON_CreateArray();
    if (!branches) {
        heap_caps_free(session);
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    for (uint8_t i = 0; i < session->branch_runtime_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        const gm_room_scenario_branch_runtime_t *runtime = &session->branch_runtimes[i];
        const room_scenario_branch_t *branch =
            (session->running_scenario.branch_count > i) ? &session->running_scenario.branches[i] : NULL;
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            heap_caps_free(session);
            cJSON_Delete(root);
            cJSON_Delete(branches);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        }
        cJSON_AddNumberToObject(item, "index", i);
        cJSON_AddStringToObject(item, "id", branch && branch->id[0] ? branch->id : "main");
        cJSON_AddStringToObject(item, "name", branch && branch->name[0] ? branch->name : "Main");
        cJSON_AddBoolToObject(item, "active", runtime->active);
        cJSON_AddStringToObject(item, "type", room_scenario_branch_type_to_str(runtime->type));
        cJSON_AddBoolToObject(item, "required_for_completion", runtime->required_for_completion);
        cJSON_AddNumberToObject(item, "cooldown_ms", runtime->cooldown_ms);
        cJSON_AddNumberToObject(item, "cooldown_until_ms", runtime->cooldown_until_ms);
        cJSON_AddBoolToObject(item, "run_once", runtime->run_once);
        cJSON_AddBoolToObject(item, "fired_once", runtime->fired_once);
        cJSON_AddNumberToObject(item, "step_start_index", runtime->step_start_index);
        cJSON_AddNumberToObject(item, "step_count", runtime->step_count);
        cJSON_AddNumberToObject(item, "current_step_index", runtime->current_step_index);
        cJSON_AddStringToObject(item,
                                "state",
                                gm_scenario_runtime_state_str(runtime->scenario_state));
        cJSON_AddStringToObject(item,
                                "wait_type",
                                gm_scenario_wait_type_str(runtime->wait_type));
        cJSON_AddBoolToObject(item,
                              "wait_operator_skip_allowed",
                              runtime->wait_operator_skip_allowed);
        cJSON_AddStringToObject(item,
                                "wait_operator_skip_label",
                                runtime->wait_operator_skip_label);
        cJSON_AddItemToArray(branches, item);
    }
    cJSON_AddItemToObject(root, "scenario_branches", branches);
    cJSON_AddNumberToObject(root, "scenario_branch_count", session->branch_runtime_count);
    cJSON_AddStringToObject(root, "scenario_last_error", session->scenario_last_error);
    heap_caps_free(session);
    return web_ui_send_json(req, root);
}

static esp_err_t gm_scenario_runtime_handler(httpd_req_t *req, esp_err_t (*fn)(const char *room_id))
{
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN] = {0};
    esp_err_t err = ESP_OK;
    if (!gm_scenario_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    err = fn(room_id);
    if (err != ESP_OK) {
        return gm_scenario_send_error(req, err);
    }
    orchestrator_registry_invalidate();
    return gm_scenario_send_runtime_state(req, room_id);
}

esp_err_t gm_room_scenario_select_handler(httpd_req_t *req)
{
    char *body = NULL;
    cJSON *json = NULL;
    cJSON *root = NULL;
    const cJSON *room_id_item = NULL;
    const cJSON *scenario_id_item = NULL;
    const char *room_id = NULL;
    const char *scenario_id = NULL;
    esp_err_t err = gm_scenario_read_body(req, &body);

    if (err != ESP_OK) {
        return gm_scenario_send_error(req, err);
    }
    json = cJSON_Parse(body);
    heap_caps_free(body);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    room_id_item = cJSON_GetObjectItem(json, "room_id");
    scenario_id_item = cJSON_GetObjectItem(json, "scenario_id");
    room_id = cJSON_IsString(room_id_item) ? room_id_item->valuestring : NULL;
    scenario_id = cJSON_IsString(scenario_id_item) ? scenario_id_item->valuestring : NULL;
    if (!room_id || !room_id[0] || !scenario_id || !scenario_id[0]) {
        cJSON_Delete(json);
        return gm_scenario_send_error(req, ESP_ERR_INVALID_ARG);
    }

    err = gm_api_select_scenario(room_id, scenario_id);
    if (err != ESP_OK) {
        cJSON_Delete(json);
        return gm_scenario_send_error(req, err);
    }
    orchestrator_registry_invalidate();

    root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "room_id", room_id);
    cJSON_AddStringToObject(root, "selected_scenario_id", scenario_id);
    cJSON_Delete(json);
    return web_ui_send_json(req, root);
}

static const cJSON *gm_scenario_payload_object(const cJSON *root)
{
    const cJSON *scenario = cJSON_GetObjectItemCaseSensitive(root, "scenario");
    if (cJSON_IsObject(scenario)) {
        return scenario;
    }
    return root;
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

static esp_err_t gm_scenario_add_quest_devices_catalog(cJSON *root)
{
    quest_device_t *devices = NULL;
    size_t count = 0;
    cJSON *array = NULL;
    esp_err_t err = ESP_OK;
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    devices = heap_caps_calloc(QUEST_DEVICE_MAX_DEVICES,
                               sizeof(*devices),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!devices) {
        devices = heap_caps_calloc(QUEST_DEVICE_MAX_DEVICES,
                                   sizeof(*devices),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
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

    quest_device_t *system_audio = heap_caps_calloc(1,
                                                    sizeof(*system_audio),
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!system_audio) {
        system_audio = heap_caps_calloc(1,
                                        sizeof(*system_audio),
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!system_audio) {
        cJSON_Delete(array);
        return ESP_ERR_NO_MEM;
    }
    err = quest_device_get(QUEST_DEVICE_SYSTEM_AUDIO_ID, system_audio);
    if (err == ESP_OK) {
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            heap_caps_free(system_audio);
            cJSON_Delete(array);
            return ESP_ERR_NO_MEM;
        }
        err = quest_device_to_json(system_audio, obj);
        if (err == ESP_OK) {
            cJSON_AddItemToArray(array, obj);
        } else {
            cJSON_Delete(obj);
        }
    }
    heap_caps_free(system_audio);
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

    if (!gm_scenario_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
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

static const char *gm_scenario_validation_level_str(room_scenario_validation_level_t level)
{
    return level == ROOM_SCENARIO_VALIDATION_WARNING ? "warning" : "error";
}

static esp_err_t gm_scenario_add_validation_report(cJSON *root,
                                                   const room_scenario_validation_report_t *report)
{
    cJSON *issues = NULL;
    size_t error_count = 0;
    size_t warning_count = 0;
    if (!root || !report) {
        return ESP_ERR_INVALID_ARG;
    }
    issues = cJSON_CreateArray();
    if (!issues) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < report->issue_count; ++i) {
        const room_scenario_validation_issue_t *issue = &report->issues[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(issues);
            return ESP_ERR_NO_MEM;
        }
        if (issue->level == ROOM_SCENARIO_VALIDATION_WARNING) {
            ++warning_count;
        } else {
            ++error_count;
        }
        cJSON_AddStringToObject(item, "level", gm_scenario_validation_level_str(issue->level));
        cJSON_AddNumberToObject(item, "step_index", issue->step_index);
        cJSON_AddStringToObject(item, "code", issue->code);
        cJSON_AddStringToObject(item, "message", issue->message);
        cJSON_AddItemToArray(issues, item);
    }
    cJSON_AddBoolToObject(root, "valid", report->valid);
    cJSON_AddNumberToObject(root, "issue_count", report->issue_count);
    cJSON_AddNumberToObject(root, "error_count", error_count);
    cJSON_AddNumberToObject(root, "warning_count", warning_count);
    cJSON_AddItemToObject(root, "issues", issues);
    return ESP_OK;
}

esp_err_t gm_room_scenario_validate_handler(httpd_req_t *req)
{
    char *body = NULL;
    size_t body_len = 0;
    cJSON *root = NULL;
    cJSON *out = NULL;
    room_scenario_t *scenario = NULL;
    room_scenario_validation_report_t *report = NULL;
    esp_err_t err = gm_scenario_read_body_limit(req, 32768, &body, &body_len);
    if (err != ESP_OK) {
        return gm_room_scenarios_send_store_error(req, err);
    }
    scenario = gm_scenario_body_alloc(sizeof(*scenario));
    report = gm_scenario_body_alloc(sizeof(*report));
    if (!scenario || !report) {
        heap_caps_free(scenario);
        heap_caps_free(report);
        heap_caps_free(body);
        return gm_room_scenarios_send_store_error(req, ESP_ERR_NO_MEM);
    }
    root = cJSON_ParseWithLength(body, body_len);
    heap_caps_free(body);
    if (!root) {
        heap_caps_free(scenario);
        heap_caps_free(report);
        return gm_room_scenarios_send_store_error(req, ESP_ERR_INVALID_ARG);
    }
    err = room_scenario_from_json(gm_scenario_payload_object(root), scenario);
    if (err == ESP_OK) {
        err = room_scenario_validate(scenario, report);
    }
    cJSON_Delete(root);
    if (err != ESP_OK) {
        heap_caps_free(scenario);
        heap_caps_free(report);
        return gm_room_scenarios_send_store_error(req, err);
    }
    out = cJSON_CreateObject();
    if (!out) {
        heap_caps_free(scenario);
        heap_caps_free(report);
        return gm_room_scenarios_send_store_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "scenario_id", scenario->id);
    err = gm_scenario_add_validation_report(out, report);
    if (err != ESP_OK) {
        cJSON_Delete(out);
        heap_caps_free(scenario);
        heap_caps_free(report);
        return gm_room_scenarios_send_store_error(req, err);
    }
    heap_caps_free(scenario);
    heap_caps_free(report);
    return web_ui_send_json(req, out);
}

esp_err_t gm_room_scenario_save_handler(httpd_req_t *req)
{
    char *body = NULL;
    size_t body_len = 0;
    cJSON *root = NULL;
    cJSON *scenario_json = NULL;
    cJSON *out = NULL;
    room_scenario_t *scenario = NULL;
    esp_err_t err = gm_scenario_read_body_limit(req, 32768, &body, &body_len);
    if (err != ESP_OK) {
        return gm_room_scenarios_send_store_error(req, err);
    }
    scenario = gm_scenario_body_alloc(sizeof(*scenario));
    if (!scenario) {
        heap_caps_free(body);
        return gm_room_scenarios_send_store_error(req, ESP_ERR_NO_MEM);
    }
    root = cJSON_ParseWithLength(body, body_len);
    heap_caps_free(body);
    if (!root) {
        heap_caps_free(scenario);
        return gm_room_scenarios_send_store_error(req, ESP_ERR_INVALID_ARG);
    }
    err = room_scenario_from_json(gm_scenario_payload_object(root), scenario);
    if (err == ESP_OK) {
        err = room_scenario_add_and_save(scenario);
    }
    if (err != ESP_OK) {
        cJSON_Delete(root);
        heap_caps_free(scenario);
        return gm_room_scenarios_send_store_error(req, err);
    }
    orchestrator_registry_invalidate();
    out = cJSON_CreateObject();
    scenario_json = cJSON_CreateObject();
    if (!out || !scenario_json) {
        cJSON_Delete(root);
        cJSON_Delete(out);
        cJSON_Delete(scenario_json);
        heap_caps_free(scenario);
        return gm_room_scenarios_send_store_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddNumberToObject(out, "generation", room_scenario_generation());
    err = room_scenario_to_json(scenario, scenario_json);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        cJSON_Delete(out);
        cJSON_Delete(scenario_json);
        heap_caps_free(scenario);
        return gm_room_scenarios_send_store_error(req, err);
    }
    cJSON_AddItemToObject(out, "scenario", scenario_json);
    cJSON_Delete(root);
    heap_caps_free(scenario);
    return web_ui_send_json(req, out);
}

esp_err_t gm_room_scenario_delete_handler(httpd_req_t *req)
{
    char *body = NULL;
    cJSON *root = NULL;
    cJSON *out = NULL;
    const cJSON *scenario_id_item = NULL;
    const char *scenario_id = NULL;
    esp_err_t err = gm_scenario_read_body(req, &body);
    if (err != ESP_OK) {
        return gm_scenario_send_error(req, err);
    }
    root = cJSON_Parse(body);
    heap_caps_free(body);
    if (!root) {
        return gm_scenario_send_error(req, ESP_ERR_INVALID_ARG);
    }
    scenario_id_item = cJSON_GetObjectItemCaseSensitive(root, "scenario_id");
    scenario_id = cJSON_IsString(scenario_id_item) ? scenario_id_item->valuestring : NULL;
    if (!scenario_id || !scenario_id[0]) {
        cJSON_Delete(root);
        return gm_scenario_send_error(req, ESP_ERR_INVALID_ARG);
    }
    err = room_scenario_delete_and_save(scenario_id);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return gm_scenario_send_error(req, err);
    }
    orchestrator_registry_invalidate();
    out = cJSON_CreateObject();
    if (!out) {
        cJSON_Delete(root);
        return gm_scenario_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "deleted_scenario_id", scenario_id);
    cJSON_AddNumberToObject(out, "generation", room_scenario_generation());
    cJSON_Delete(root);
    return web_ui_send_json(req, out);
}

esp_err_t gm_room_scenario_start_handler(httpd_req_t *req)
{
    return gm_scenario_runtime_handler(req, gm_api_scenario_start);
}

esp_err_t gm_room_scenario_stop_handler(httpd_req_t *req)
{
    return gm_scenario_runtime_handler(req, gm_api_scenario_stop);
}

esp_err_t gm_room_scenario_next_handler(httpd_req_t *req)
{
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN] = {0};
    char branch_id[ROOM_SCENARIO_BRANCH_ID_MAX_LEN] = {0};
    esp_err_t err = ESP_OK;
    if (!gm_scenario_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    (void)gm_scenario_read_query_value(req, "branch_id", branch_id, sizeof(branch_id));
    err = branch_id[0] ? gm_api_scenario_next_branch(room_id, branch_id) : gm_api_scenario_next(room_id);
    if (err != ESP_OK) {
        return gm_scenario_send_error(req, err);
    }
    orchestrator_registry_invalidate();
    return gm_scenario_send_runtime_state(req, room_id);
}

esp_err_t gm_room_scenario_approve_handler(httpd_req_t *req)
{
    return gm_scenario_runtime_handler(req, gm_api_scenario_approve);
}

esp_err_t gm_room_scenario_reset_handler(httpd_req_t *req)
{
    return gm_scenario_runtime_handler(req, gm_api_scenario_reset);
}

esp_err_t gm_room_scenarios_export_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    esp_err_t err = room_scenario_store_export_json(&root);
    if (err != ESP_OK) {
        return gm_room_scenarios_send_store_error(req, err);
    }
    httpd_resp_set_hdr(req,
                       "Content-Disposition",
                       "attachment; filename=\"room_scenarios.json\"");
    return web_ui_send_json(req, root);
}

esp_err_t gm_room_scenarios_import_handler(httpd_req_t *req)
{
    char *body = NULL;
    size_t body_len = 0;
    cJSON *root = NULL;
    const cJSON *items = NULL;
    int scenario_count = 0;
    esp_err_t err = gm_scenario_read_body_limit(req,
                                                GM_ROOM_SCENARIO_IMPORT_MAX_BYTES,
                                                &body,
                                                &body_len);
    if (err != ESP_OK) {
        return gm_room_scenarios_send_store_error(req, err);
    }
    root = cJSON_ParseWithLength(body, body_len);
    heap_caps_free(body);
    if (!root) {
        return gm_room_scenarios_send_store_error(req, ESP_ERR_INVALID_ARG);
    }
    items = cJSON_GetObjectItemCaseSensitive(root, "room_scenarios");
    if (cJSON_IsArray(items)) {
        scenario_count = cJSON_GetArraySize(items);
    }
    err = room_scenario_store_import_json_and_save(root);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return gm_room_scenarios_send_store_error(req, err);
    }
    orchestrator_registry_invalidate();

    root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "operation", "import");
    cJSON_AddNumberToObject(root, "scenario_count", scenario_count);
    cJSON_AddNumberToObject(root, "generation", room_scenario_generation());
    return web_ui_send_json(req, root);
}

esp_err_t gm_room_scenarios_save_handler(httpd_req_t *req)
{
    esp_err_t err = room_scenario_store_save();
    if (err != ESP_OK) {
        return gm_room_scenarios_send_store_error(req, err);
    }
    return gm_room_scenarios_send_ok(req, "save");
}

esp_err_t gm_room_scenarios_load_handler(httpd_req_t *req)
{
    esp_err_t err = room_scenario_store_load();
    if (err != ESP_OK) {
        return gm_room_scenarios_send_store_error(req, err);
    }
    orchestrator_registry_invalidate();
    return gm_room_scenarios_send_ok(req, "load");
}
