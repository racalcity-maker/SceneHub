#define WEB_UI_UTILS_NO_HTTP_MACROS
#include "web_ui_utils.h"

static const char *web_ui_scenario_validation_level_text(room_scenario_validation_level_t level)
{
    return level == ROOM_SCENARIO_VALIDATION_WARNING ? "warning" : "error";
}

esp_err_t web_ui_send_store_operation_json(httpd_req_t *req,
                                           const char *operation,
                                           const char *path,
                                           uint32_t generation)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "operation", operation ? operation : "");
    cJSON_AddStringToObject(root, "path", path ? path : "");
    cJSON_AddNumberToObject(root, "generation", generation);
    return web_ui_send_json(req, root);
}

esp_err_t web_ui_send_import_result_json(httpd_req_t *req,
                                         const char *operation,
                                         const char *count_key,
                                         int count,
                                         uint32_t generation)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "operation", operation ? operation : "");
    cJSON_AddNumberToObject(root, count_key ? count_key : "count", count);
    cJSON_AddNumberToObject(root, "generation", generation);
    return web_ui_send_json(req, root);
}

esp_err_t web_ui_send_selection_result_json(httpd_req_t *req,
                                            const char *scope_key,
                                            const char *scope_value,
                                            const char *selected_key,
                                            const char *selected_value)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, scope_key ? scope_key : "scope_id", scope_value ? scope_value : "");
    cJSON_AddStringToObject(root,
                            selected_key ? selected_key : "selected_id",
                            selected_value ? selected_value : "");
    return web_ui_send_json(req, root);
}

esp_err_t web_ui_send_deleted_result_json(httpd_req_t *req,
                                          const char *deleted_key,
                                          const char *deleted_value,
                                          uint32_t generation)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root,
                            deleted_key ? deleted_key : "deleted_id",
                            deleted_value ? deleted_value : "");
    cJSON_AddNumberToObject(root, "generation", generation);
    return web_ui_send_json(req, root);
}

esp_err_t web_ui_send_room_saved_json(httpd_req_t *req,
                                      const char *room_id,
                                      const char *name)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "room_id", room_id ? room_id : "");
    cJSON_AddStringToObject(root, "name", name ? name : "");
    return web_ui_send_json(req, root);
}

esp_err_t web_ui_send_room_deleted_json(httpd_req_t *req,
                                        const char *room_id,
                                        size_t removed_rooms,
                                        size_t removed_profiles,
                                        size_t removed_scenarios)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "room_id", room_id ? room_id : "");
    cJSON_AddNumberToObject(root, "removed_rooms", (double)removed_rooms);
    cJSON_AddNumberToObject(root, "removed_profiles", (double)removed_profiles);
    cJSON_AddNumberToObject(root, "removed_scenarios", (double)removed_scenarios);
    return web_ui_send_json(req, root);
}

esp_err_t web_ui_send_room_action_result_json(httpd_req_t *req,
                                              const char *room_id,
                                              const char *action_id)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "room_id", room_id ? room_id : "");
    cJSON_AddStringToObject(root, "action_id", action_id ? action_id : "");
    return web_ui_send_json(req, root);
}

esp_err_t web_ui_send_generation_item_json(httpd_req_t *req,
                                           uint32_t generation,
                                           const char *item_key,
                                           cJSON *item)
{
    cJSON *root = cJSON_CreateObject();
    if (!root || !item) {
        if (root) {
            cJSON_Delete(root);
        }
        if (item) {
            cJSON_Delete(item);
        }
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "generation", generation);
    cJSON_AddItemToObject(root, item_key ? item_key : "item", item);
    return web_ui_send_json(req, root);
}

esp_err_t web_ui_send_device_command_result_json(httpd_req_t *req,
                                                 const char *device_id,
                                                 const char *device_name,
                                                 const char *command_id,
                                                 const char *command_label)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "device_id", device_id ? device_id : "");
    cJSON_AddStringToObject(root, "device_name", device_name ? device_name : "");
    cJSON_AddStringToObject(root, "command_id", command_id ? command_id : "");
    cJSON_AddStringToObject(root, "command_label", command_label ? command_label : "");
    return web_ui_send_json(req, root);
}

esp_err_t web_ui_send_scenario_validation_result_json(httpd_req_t *req,
                                                      const char *scenario_id,
                                                      const room_scenario_validation_report_t *report)
{
    cJSON *root = cJSON_CreateObject();
    esp_err_t err = ESP_OK;
    if (!root) {
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "scenario_id", scenario_id ? scenario_id : "");
    err = web_ui_add_scenario_validation_report_json(root, report);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    return web_ui_send_json(req, root);
}

esp_err_t web_ui_add_scenario_validation_report_json(cJSON *root,
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
        cJSON_AddStringToObject(item, "level", web_ui_scenario_validation_level_text(issue->level));
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
