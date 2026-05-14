#include "orchestrator_registry_internal.h"

#include <string.h>

#include "audio_player.h"

typedef struct {
    bool valid;
    char scenario_id[ROOM_SCENARIO_ID_MAX_LEN];
    uint32_t scenario_generation;
    uint32_t device_generation;
    uint32_t asset_generation;
    orch_room_asset_summary_t summary;
} orch_room_asset_cache_entry_t;

typedef struct {
    const char *value;
    size_t value_len;
} orch_json_string_view_t;

static orch_room_asset_cache_entry_t s_room_asset_cache[ORCH_REGISTRY_MAX_ROOMS];
static size_t s_room_asset_cache_next = 0;

static const char *orch_json_skip_ws(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        ++p;
    }
    return p;
}

static bool orch_json_key_equals(const char *key, size_t key_len, const char *expected)
{
    return key && expected && strlen(expected) == key_len && strncmp(key, expected, key_len) == 0;
}

static esp_err_t orch_json_read_string_token(const char **cursor,
                                             const char **out_value,
                                             size_t *out_len)
{
    const char *p = orch_json_skip_ws(cursor ? *cursor : NULL);
    const char *start = NULL;

    if (!p || *p != '"' || !out_value || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    ++p;
    start = p;
    while (*p) {
        if (*p == '\\') {
            ++p;
            if (!*p) {
                return ESP_ERR_INVALID_ARG;
            }
            ++p;
            continue;
        }
        if (*p == '"') {
            *out_value = start;
            *out_len = (size_t)(p - start);
            *cursor = p + 1;
            return ESP_OK;
        }
        ++p;
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t orch_json_skip_string(const char **cursor)
{
    const char *value = NULL;
    size_t value_len = 0;
    return orch_json_read_string_token(cursor, &value, &value_len);
}

static esp_err_t orch_json_skip_value(const char **cursor)
{
    const char *p = orch_json_skip_ws(cursor ? *cursor : NULL);
    int depth = 0;

    if (!p) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*p == '"') {
        esp_err_t err = orch_json_skip_string(&p);
        if (err != ESP_OK) {
            return err;
        }
        *cursor = p;
        return ESP_OK;
    }
    if (*p == '{' || *p == '[') {
        char open = *p++;
        char close = open == '{' ? '}' : ']';
        depth = 1;
        while (*p && depth > 0) {
            if (*p == '"') {
                esp_err_t err = orch_json_skip_string(&p);
                if (err != ESP_OK) {
                    return err;
                }
                continue;
            }
            if (*p == open) {
                ++depth;
            } else if (*p == close) {
                --depth;
            }
            ++p;
        }
        if (depth != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        *cursor = p;
        return ESP_OK;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') {
        ++p;
    }
    *cursor = p;
    return ESP_OK;
}

static esp_err_t orch_json_find_string_field(const char *json,
                                             const char *key,
                                             orch_json_string_view_t *out)
{
    const char *p = orch_json_skip_ws(json);

    if (!key || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!p || !*p) {
        return ESP_ERR_NOT_FOUND;
    }
    if (*p != '{') {
        return ESP_ERR_INVALID_ARG;
    }
    ++p;
    for (;;) {
        const char *json_key = NULL;
        size_t json_key_len = 0;
        esp_err_t err = ESP_OK;

        p = orch_json_skip_ws(p);
        if (*p == '}') {
            return ESP_ERR_NOT_FOUND;
        }
        err = orch_json_read_string_token(&p, &json_key, &json_key_len);
        if (err != ESP_OK) {
            return err;
        }
        p = orch_json_skip_ws(p);
        if (*p != ':') {
            return ESP_ERR_INVALID_ARG;
        }
        ++p;
        p = orch_json_skip_ws(p);
        if (orch_json_key_equals(json_key, json_key_len, key)) {
            if (*p != '"') {
                return ESP_ERR_INVALID_ARG;
            }
            err = orch_json_read_string_token(&p, &out->value, &out->value_len);
            return err;
        }
        err = orch_json_skip_value(&p);
        if (err != ESP_OK) {
            return err;
        }
        p = orch_json_skip_ws(p);
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == '}') {
            return ESP_ERR_NOT_FOUND;
        }
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t orch_json_copy_string_view(const orch_json_string_view_t *value,
                                            char *out,
                                            size_t out_size)
{
    const char *p = value ? value->value : NULL;
    const char *end = p ? p + value->value_len : NULL;
    size_t written = 0;

    if (!value || !out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    while (p < end) {
        char ch = *p++;
        if (ch == '\\') {
            if (p >= end) {
                return ESP_ERR_INVALID_ARG;
            }
            ch = *p++;
            switch (ch) {
            case '"':
            case '\\':
            case '/':
                break;
            case 'b':
                ch = '\b';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            default:
                return ESP_ERR_NOT_SUPPORTED;
            }
        }
        if (written + 1 >= out_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        out[written++] = ch;
    }
    out[written] = '\0';
    return written > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static orch_room_asset_cache_entry_t *orch_room_asset_cache_find(const char *scenario_id,
                                                                 uint32_t scenario_generation,
                                                                 uint32_t device_generation,
                                                                 uint32_t asset_generation)
{
    if (!scenario_id || !scenario_id[0]) {
        return NULL;
    }
    for (size_t i = 0; i < ORCH_REGISTRY_MAX_ROOMS; ++i) {
        orch_room_asset_cache_entry_t *entry = &s_room_asset_cache[i];
        if (!entry->valid) {
            continue;
        }
        if (entry->scenario_generation == scenario_generation &&
            entry->device_generation == device_generation &&
            entry->asset_generation == asset_generation &&
            strcmp(entry->scenario_id, scenario_id) == 0) {
            return entry;
        }
    }
    return NULL;
}

static orch_room_asset_cache_entry_t *orch_room_asset_cache_alloc(void)
{
    for (size_t i = 0; i < ORCH_REGISTRY_MAX_ROOMS; ++i) {
        if (!s_room_asset_cache[i].valid) {
            return &s_room_asset_cache[i];
        }
    }
    return &s_room_asset_cache[s_room_asset_cache_next++ % ORCH_REGISTRY_MAX_ROOMS];
}

static void orch_room_asset_cache_store(const char *scenario_id,
                                        uint32_t scenario_generation,
                                        uint32_t device_generation,
                                        uint32_t asset_generation,
                                        const orch_room_asset_summary_t *summary)
{
    orch_room_asset_cache_entry_t *entry = NULL;
    if (!scenario_id || !scenario_id[0] || !summary) {
        return;
    }
    entry = orch_room_asset_cache_alloc();
    if (!entry) {
        return;
    }
    memset(entry, 0, sizeof(*entry));
    entry->valid = true;
    quest_str_copy(entry->scenario_id, sizeof(entry->scenario_id), scenario_id);
    entry->scenario_generation = scenario_generation;
    entry->device_generation = device_generation;
    entry->asset_generation = asset_generation;
    entry->summary = *summary;
}

static void orch_room_asset_count_file(orch_room_asset_summary_t *summary, const char *path)
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

static void orch_room_asset_count_args(orch_room_asset_summary_t *summary, const char *args_json)
{
    orch_json_string_view_t file = {0};
    char path[QUEST_DEVICE_DEFAULT_ARGS_JSON_MAX_LEN] = {0};
    if (!summary || !args_json || !args_json[0]) {
        return;
    }
    if (orch_json_find_string_field(args_json, "file", &file) != ESP_OK) {
        return;
    }
    if (orch_json_copy_string_view(&file, path, sizeof(path)) == ESP_OK && path[0]) {
        orch_room_asset_count_file(summary, path);
    }
}

static void orch_room_asset_count_command(orch_room_asset_summary_t *summary,
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
    orch_room_asset_count_args(summary, command.default_args_json);
    orch_room_asset_count_args(summary, step_command->params_json);
}

static void orch_room_asset_count_scenario(orch_room_asset_summary_t *summary,
                                           const room_scenario_t *scenario)
{
    if (!summary || !scenario) {
        return;
    }
    for (size_t i = 0; i < scenario->step_count; ++i) {
        const room_scenario_step_t *step = &scenario->steps[i];
        if (!step->enabled || step->type != ROOM_SCENARIO_STEP_DEVICE_COMMAND) {
            continue;
        }
        orch_room_asset_count_command(summary, &step->data.device_command);
    }
}

bool orch_room_runtime_assets_collect(const char *scenario_id,
                                      const gm_room_session_t *session,
                                      room_scenario_t *scratch_scenario,
                                      orch_room_asset_summary_t *out_summary)
{
    if (!scenario_id || !scenario_id[0] || !out_summary) {
        return false;
    }

    memset(out_summary, 0, sizeof(*out_summary));
    if (session && session->running_scenario_valid) {
        orch_room_asset_count_scenario(out_summary, &session->running_scenario);
        return true;
    }
    if (scratch_scenario && room_scenario_get(scenario_id, scratch_scenario) == ESP_OK) {
        orch_room_asset_count_scenario(out_summary, scratch_scenario);
        return true;
    }
    return false;
}

uint32_t orch_room_runtime_assets_generation(void)
{
    return audio_player_asset_generation();
}

void orch_room_runtime_assets_apply_summary(orch_room_runtime_view_t *out,
                                            const orch_room_asset_summary_t *summary)
{
    const char *state = "none";
    if (!out || !summary) {
        return;
    }
    if (summary->total > 0) {
        state = (summary->missing || summary->bad || summary->unsupported || summary->io_error)
                    ? "error"
                    : (summary->unknown ? "pending" : "ready");
    }
    quest_str_copy(out->asset_prepare_state, sizeof(out->asset_prepare_state), state);
    out->asset_audio_total = summary->total;
    out->asset_audio_ready = summary->ready;
    out->asset_audio_missing = summary->missing;
    out->asset_audio_bad = summary->bad;
    out->asset_audio_unsupported = summary->unsupported;
    out->asset_audio_io_error = summary->io_error;
    out->asset_audio_unknown = summary->unknown;
}

bool orch_room_runtime_assets_load_cached(orch_room_runtime_view_t *out,
                                          uint32_t scenario_generation,
                                          uint32_t device_generation,
                                          uint32_t asset_generation)
{
    orch_room_asset_cache_entry_t *cached = NULL;
    esp_err_t err = ESP_OK;
    orch_room_asset_summary_t summary = {0};

    if (!out || !out->room.running_scenario_id[0]) {
        return false;
    }

    err = orch_cache_lock();
    if (err == ESP_OK) {
        cached = orch_room_asset_cache_find(out->room.running_scenario_id,
                                            scenario_generation,
                                            device_generation,
                                            asset_generation);
        if (cached) {
            summary = cached->summary;
        }
        orch_cache_unlock();
    }
    if (cached) {
        orch_room_runtime_assets_apply_summary(out, &summary);
        return true;
    }
    return false;
}

void orch_room_runtime_assets_store_cached(const orch_room_runtime_view_t *out,
                                           uint32_t scenario_generation,
                                           uint32_t device_generation,
                                           uint32_t asset_generation,
                                           const orch_room_asset_summary_t *summary)
{
    esp_err_t err = ESP_OK;

    if (!out || !summary || !out->room.running_scenario_id[0]) {
        return;
    }

    err = orch_cache_lock();
    if (err != ESP_OK) {
        return;
    }
    orch_room_asset_cache_store(out->room.running_scenario_id,
                                scenario_generation,
                                device_generation,
                                asset_generation,
                                summary);
    orch_cache_unlock();
}
