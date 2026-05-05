#include "web_ui_handlers.h"

#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "audio_player.h"
#include "gm_api.h"
#include "gm_timer.h"
#include "orchestrator_registry.h"
#include "quest_device.h"
#include "room_scenario.h"
#include "web_ui_utils.h"

static const char *TAG = "web_ui_gm_scenario";

static uint64_t gm_scenario_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static int64_t gm_scenario_perf_start(void)
{
    return esp_timer_get_time();
}

static void gm_scenario_perf_log(const char *label, int64_t start_us, const char *room_id)
{
    int64_t dt_ms = (esp_timer_get_time() - start_us) / 1000;
    ESP_LOGW(TAG, "PERF %s room=%s took %lld ms", label ? label : "scenario", room_id ? room_id : "", dt_ms);
}

static const char *gm_scenario_session_state_str(gm_session_state_t state)
{
    switch (state) {
    case GM_SESSION_RUNNING:
        return "running";
    case GM_SESSION_PAUSED:
        return "paused";
    case GM_SESSION_FINISHED:
        return "finished";
    case GM_SESSION_IDLE:
    default:
        return "idle";
    }
}

static const char *gm_scenario_timer_state_str(gm_timer_state_t state)
{
    switch (state) {
    case GM_TIMER_RUNNING:
        return "running";
    case GM_TIMER_PAUSED:
        return "paused";
    case GM_TIMER_FINISHED:
        return "finished";
    case GM_TIMER_IDLE:
    default:
        return "idle";
    }
}

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
    case GM_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT:
        return "any_events";
    case GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS:
        return "all_events";
    case GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT:
        return "command_result";
    case GM_ROOM_SCENARIO_WAIT_OPERATOR:
        return "operator";
    case GM_ROOM_SCENARIO_WAIT_FLAGS:
        return "flags";
    case GM_ROOM_SCENARIO_WAIT_NONE:
    default:
        return "none";
    }
}

typedef struct {
    uint16_t total;
    uint16_t ready;
    uint16_t missing;
    uint16_t bad;
    uint16_t unsupported;
    uint16_t io_error;
    uint16_t unknown;
} gm_scenario_asset_summary_t;

static void gm_scenario_count_audio_asset(gm_scenario_asset_summary_t *summary, const char *path)
{
    audio_player_asset_info_t info = {0};
    if (!summary || !path || !path[0]) {
        return;
    }
    summary->total++;
    if (audio_player_asset_get(path, &info) != ESP_OK) {
        summary->unknown++;
        return;
    }
    switch (info.status) {
    case AUDIO_PLAYER_ASSET_READY:
        summary->ready++;
        break;
    case AUDIO_PLAYER_ASSET_MISSING:
        summary->missing++;
        break;
    case AUDIO_PLAYER_ASSET_BAD_HEADER:
        summary->bad++;
        break;
    case AUDIO_PLAYER_ASSET_UNSUPPORTED_FORMAT:
        summary->unsupported++;
        break;
    case AUDIO_PLAYER_ASSET_IO_ERROR:
        summary->io_error++;
        break;
    case AUDIO_PLAYER_ASSET_UNKNOWN:
    default:
        summary->unknown++;
        break;
    }
}

static void gm_scenario_count_audio_args(gm_scenario_asset_summary_t *summary, const char *args_json)
{
    cJSON *root = NULL;
    cJSON *file = NULL;
    if (!summary || !args_json || !args_json[0]) {
        return;
    }
    root = cJSON_Parse(args_json);
    if (!root) {
        return;
    }
    file = cJSON_GetObjectItem(root, "file");
    if (cJSON_IsString(file)) {
        gm_scenario_count_audio_asset(summary, file->valuestring);
    }
    cJSON_Delete(root);
}

static void gm_scenario_count_audio_command(gm_scenario_asset_summary_t *summary,
                                            const room_scenario_device_command_t *step_command)
{
    quest_device_command_t command = {0};
    if (!summary || !step_command ||
        strcmp(step_command->device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) != 0) {
        return;
    }
    if (quest_device_get_command(step_command->device_id, step_command->command_id, &command) != ESP_OK) {
        return;
    }
    if (strcmp(command.command, "audio.play") != 0) {
        return;
    }
    gm_scenario_count_audio_args(summary, command.default_args_json);
    gm_scenario_count_audio_args(summary, step_command->params_json);
}

static void gm_scenario_add_asset_summary(cJSON *root, const room_scenario_t *scenario)
{
    gm_scenario_asset_summary_t summary = {0};
    const char *state = "none";
    if (!root) {
        return;
    }
    if (scenario) {
        for (size_t i = 0; i < scenario->step_count; ++i) {
            const room_scenario_step_t *step = &scenario->steps[i];
            if (!step->enabled || step->type != ROOM_SCENARIO_STEP_DEVICE_COMMAND) {
                continue;
            }
            gm_scenario_count_audio_command(&summary, &step->data.device_command);
        }
    }
    if (summary.total > 0) {
        state = (summary.missing || summary.bad || summary.unsupported || summary.io_error) ? "error" :
            (summary.unknown ? "pending" : "ready");
    }
    cJSON_AddStringToObject(root, "asset_prepare_state", state);
    cJSON_AddNumberToObject(root, "asset_audio_total", summary.total);
    cJSON_AddNumberToObject(root, "asset_audio_ready", summary.ready);
    cJSON_AddNumberToObject(root, "asset_audio_missing", summary.missing);
    cJSON_AddNumberToObject(root, "asset_audio_bad", summary.bad);
    cJSON_AddNumberToObject(root, "asset_audio_unsupported", summary.unsupported);
    cJSON_AddNumberToObject(root, "asset_audio_io_error", summary.io_error);
    cJSON_AddNumberToObject(root, "asset_audio_unknown", summary.unknown);
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

static esp_err_t gm_scenario_send_runtime_state(httpd_req_t *req, const char *room_id)
{
    gm_room_session_t *session = NULL;
    cJSON *root = NULL;
    cJSON *flags = NULL;
    cJSON *wait_events = NULL;
    cJSON *wait_flags = NULL;
    cJSON *branches = NULL;
    room_scenario_t *asset_scenario_alloc = NULL;
    const room_scenario_t *asset_scenario = NULL;
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
    cJSON_AddStringToObject(root, "session_state", gm_scenario_session_state_str(session->state));
    cJSON_AddStringToObject(root, "timer_state", gm_scenario_timer_state_str(session->timer.state));
    cJSON_AddNumberToObject(root, "timer_duration_ms", session->timer.duration_ms);
    cJSON_AddNumberToObject(root,
                            "timer_remaining_ms",
                            gm_timer_get_remaining(&session->timer, gm_scenario_now_ms()));
    cJSON_AddBoolToObject(root, "hint_active", session->hint.active);
    cJSON_AddNumberToObject(root, "hint_sent_count", session->hint.sent_count);
    cJSON_AddStringToObject(root, "hint_message", session->hint.message);
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
    if (session->running_scenario_valid) {
        asset_scenario = &session->running_scenario;
    } else if (session->selected_scenario_id[0]) {
        asset_scenario_alloc = gm_scenario_body_alloc(sizeof(*asset_scenario_alloc));
        if (asset_scenario_alloc &&
            room_scenario_get(session->selected_scenario_id, asset_scenario_alloc) == ESP_OK) {
            asset_scenario = asset_scenario_alloc;
        }
    }
    gm_scenario_add_asset_summary(root, asset_scenario);
    heap_caps_free(asset_scenario_alloc);
    heap_caps_free(session);
    return web_ui_send_json(req, root);
}

static esp_err_t gm_scenario_runtime_handler(httpd_req_t *req, esp_err_t (*fn)(const char *room_id))
{
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN] = {0};
    int64_t t0 = gm_scenario_perf_start();
    esp_err_t err = ESP_OK;
    if (!gm_scenario_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
    err = fn(room_id);
    if (err != ESP_OK) {
        return gm_scenario_send_error(req, err);
    }
    orchestrator_registry_invalidate();
    gm_scenario_perf_log("POST scenario runtime", t0, room_id);
    return web_ui_send_ok(req, "application/json", "{\"ok\":true,\"accepted\":true}");
}

esp_err_t gm_room_runtime_state_handler(httpd_req_t *req)
{
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN] = {0};
    if (!gm_scenario_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }
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
    int64_t t0 = gm_scenario_perf_start();
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
    gm_scenario_perf_log("POST scenario next", t0, room_id);
    return web_ui_send_ok(req, "application/json", "{\"ok\":true,\"accepted\":true}");
}

esp_err_t gm_room_scenario_approve_handler(httpd_req_t *req)
{
    return gm_scenario_runtime_handler(req, gm_api_scenario_approve);
}

esp_err_t gm_room_scenario_reset_handler(httpd_req_t *req)
{
    return gm_scenario_runtime_handler(req, gm_api_scenario_reset);
}
