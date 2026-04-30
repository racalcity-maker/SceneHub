#include "quest_device.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sd_storage.h"

#define QUEST_DEVICE_JSON_VERSION 1
#define QUEST_DEVICE_FILE_MAX_BYTES (160 * 1024)

typedef struct {
    bool in_use;
    quest_device_t device;
} quest_device_slot_t;

EXT_RAM_BSS_ATTR static quest_device_slot_t s_devices[QUEST_DEVICE_MAX_DEVICES];
static SemaphoreHandle_t s_lock = NULL;
static SemaphoreHandle_t s_persist_lock = NULL;
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_generation = 0;

static esp_err_t quest_device_save_to_path_locked(const char *path);
static esp_err_t quest_device_load_from_path_locked(const char *path);

static esp_err_t qd_ensure_lock(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_init_lock);
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&s_init_lock);
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t qd_ensure_persist_lock(void)
{
    if (s_persist_lock) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_init_lock);
    if (!s_persist_lock) {
        s_persist_lock = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&s_init_lock);
    return s_persist_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t qd_lock(void)
{
    esp_err_t err = qd_ensure_lock();
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t qd_persist_lock(void)
{
    esp_err_t err = qd_ensure_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_persist_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void qd_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static void qd_persist_unlock(void)
{
    if (s_persist_lock) {
        xSemaphoreGive(s_persist_lock);
    }
}

static void qd_copy(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

static esp_err_t qd_json_copy_string(const cJSON *json,
                                     const char *name,
                                     char *dst,
                                     size_t dst_len,
                                     bool required)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
    if (!item || cJSON_IsNull(item)) {
        if (required) {
            return ESP_ERR_INVALID_ARG;
        }
        dst[0] = '\0';
        return ESP_OK;
    }
    if (!cJSON_IsString(item) || !item->valuestring ||
        (required && !item->valuestring[0]) ||
        strlen(item->valuestring) >= dst_len) {
        return ESP_ERR_INVALID_ARG;
    }
    qd_copy(dst, dst_len, item->valuestring);
    return ESP_OK;
}

static bool qd_json_bool(const cJSON *json, const char *name, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
    if (!item || cJSON_IsNull(item)) {
        return fallback;
    }
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    if (cJSON_IsNumber(item)) {
        return item->valueint != 0;
    }
    return fallback;
}

static bool qd_valid_id(const char *value)
{
    return value && value[0];
}

static bool qd_commands_have_duplicate_ids(const quest_device_t *device)
{
    if (!device) {
        return true;
    }
    for (uint8_t i = 0; i < device->command_count; ++i) {
        for (uint8_t j = i + 1; j < device->command_count; ++j) {
            if (strcmp(device->commands[i].id, device->commands[j].id) == 0) {
                return true;
            }
        }
    }
    return false;
}

static bool qd_events_have_duplicate_ids(const quest_device_t *device)
{
    if (!device) {
        return true;
    }
    for (uint8_t i = 0; i < device->event_count; ++i) {
        for (uint8_t j = i + 1; j < device->event_count; ++j) {
            if (strcmp(device->events[i].id, device->events[j].id) == 0) {
                return true;
            }
        }
    }
    return false;
}

static bool qd_device_valid(const quest_device_t *device)
{
    if (!device || !qd_valid_id(device->id) || !device->name[0] || !device->client_id[0]) {
        return false;
    }
    if (device->system_device || strcmp(device->id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0) {
        return false;
    }
    if (device->command_count > QUEST_DEVICE_MAX_COMMANDS ||
        device->event_count > QUEST_DEVICE_MAX_EVENTS) {
        return false;
    }
    if (qd_commands_have_duplicate_ids(device) || qd_events_have_duplicate_ids(device)) {
        return false;
    }
    for (uint8_t i = 0; i < device->command_count; ++i) {
        const quest_device_command_t *cmd = &device->commands[i];
        if (!cmd->id[0] || !cmd->label[0] || !cmd->topic[0] ||
            cmd->param_count > QUEST_DEVICE_MAX_COMMAND_PARAMS) {
            return false;
        }
        for (uint8_t p = 0; p < cmd->param_count; ++p) {
            if (!cmd->params[p].key[0] || !cmd->params[p].label[0]) {
                return false;
            }
        }
    }
    for (uint8_t i = 0; i < device->event_count; ++i) {
        const quest_device_event_t *event = &device->events[i];
        if (!event->id[0] || !event->label[0] || !event->topic[0] || !event->event_type[0]) {
            return false;
        }
    }
    return true;
}

static bool qd_client_id_in_use_locked(const char *client_id, const char *except_device_id)
{
    if (!client_id || !client_id[0]) {
        return false;
    }
    for (size_t i = 0; i < QUEST_DEVICE_MAX_DEVICES; ++i) {
        const quest_device_slot_t *slot = &s_devices[i];
        if (!slot->in_use) {
            continue;
        }
        if (except_device_id && except_device_id[0] &&
            strcmp(slot->device.id, except_device_id) == 0) {
            continue;
        }
        if (strcmp(slot->device.client_id, client_id) == 0) {
            return true;
        }
    }
    return false;
}

static quest_device_slot_t *qd_find_locked(const char *device_id)
{
    if (!qd_valid_id(device_id)) {
        return NULL;
    }
    for (size_t i = 0; i < QUEST_DEVICE_MAX_DEVICES; ++i) {
        quest_device_slot_t *slot = &s_devices[i];
        if (slot->in_use && strcmp(slot->device.id, device_id) == 0) {
            return slot;
        }
    }
    return NULL;
}

static quest_device_slot_t *qd_find_free_locked(void)
{
    for (size_t i = 0; i < QUEST_DEVICE_MAX_DEVICES; ++i) {
        if (!s_devices[i].in_use) {
            return &s_devices[i];
        }
    }
    return NULL;
}

static quest_device_t *qd_alloc_items(size_t count)
{
    quest_device_t *items = NULL;
    if (count == 0) {
        return NULL;
    }
    items = heap_caps_calloc(count, sizeof(*items), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!items) {
        items = heap_caps_calloc(count, sizeof(*items), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return items;
}

static char *qd_alloc_bytes(size_t size)
{
    char *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buf;
}

static esp_err_t qd_ensure_sd_for_path(const char *path)
{
    const char *root = sd_storage_root_path();
    size_t root_len = strlen(root);
    if (!path || strncmp(path, root, root_len) != 0 ||
        (path[root_len] != '\0' && path[root_len] != '/')) {
        return ESP_OK;
    }
    esp_err_t err = sd_storage_init();
    if (err != ESP_OK) {
        return err;
    }
    if (!sd_storage_available()) {
        err = sd_storage_mount();
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t qd_mkdir_parent(const char *path)
{
    char dir[160] = {0};
    const char *slash = NULL;
    size_t len = 0;
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    slash = strrchr(path, '/');
    if (!slash || slash == path) {
        return ESP_OK;
    }
    len = (size_t)(slash - path);
    if (len >= sizeof(dir)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
    for (char *p = dir + 1; *p; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
            *p = '/';
            return ESP_FAIL;
        }
        *p = '/';
    }
    if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t qd_make_tmp_path(const char *path, char *tmp, size_t tmp_len)
{
    int written = 0;
    if (!path || !path[0] || !tmp || tmp_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    written = snprintf(tmp, tmp_len, "%s.tmp", path);
    if (written < 0 || (size_t)written >= tmp_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static void qd_fill_system_audio(quest_device_t *out)
{
    quest_device_command_t *cmd = NULL;
    quest_device_event_t *event = NULL;
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    qd_copy(out->id, sizeof(out->id), QUEST_DEVICE_SYSTEM_AUDIO_ID);
    qd_copy(out->client_id, sizeof(out->client_id), "internal");
    qd_copy(out->name, sizeof(out->name), "System Audio");
    out->enabled = true;
    out->system_device = true;

    cmd = &out->commands[out->command_count++];
    qd_copy(cmd->id, sizeof(cmd->id), "play");
    qd_copy(cmd->label, sizeof(cmd->label), "Play audio");
    qd_copy(cmd->kind, sizeof(cmd->kind), "internal_audio_play");
    cmd->button_enabled = false;
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_AUDIO_FILE_SELECT,
        .optional = false,
    };
    qd_copy(cmd->params[0].key, sizeof(cmd->params[0].key), "file");
    qd_copy(cmd->params[0].label, sizeof(cmd->params[0].label), "File");
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_NUMBER,
        .optional = true,
    };
    qd_copy(cmd->params[1].key, sizeof(cmd->params[1].key), "volume");
    qd_copy(cmd->params[1].label, sizeof(cmd->params[1].label), "Volume");
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_TEXT,
        .optional = true,
    };
    qd_copy(cmd->params[2].key, sizeof(cmd->params[2].key), "channel");
    qd_copy(cmd->params[2].label, sizeof(cmd->params[2].label), "Channel");
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_CHECKBOX,
        .optional = true,
    };
    qd_copy(cmd->params[3].key, sizeof(cmd->params[3].key), "repeat");
    qd_copy(cmd->params[3].label, sizeof(cmd->params[3].label), "Repeat background");

    cmd = &out->commands[out->command_count++];
    qd_copy(cmd->id, sizeof(cmd->id), "stop");
    qd_copy(cmd->label, sizeof(cmd->label), "Stop audio");
    qd_copy(cmd->kind, sizeof(cmd->kind), "internal_audio_stop");
    cmd->button_enabled = true;

    cmd = &out->commands[out->command_count++];
    qd_copy(cmd->id, sizeof(cmd->id), "pause");
    qd_copy(cmd->label, sizeof(cmd->label), "Pause audio");
    qd_copy(cmd->kind, sizeof(cmd->kind), "internal_audio_pause");
    cmd->button_enabled = true;

    cmd = &out->commands[out->command_count++];
    qd_copy(cmd->id, sizeof(cmd->id), "resume");
    qd_copy(cmd->label, sizeof(cmd->label), "Resume audio");
    qd_copy(cmd->kind, sizeof(cmd->kind), "internal_audio_resume");
    cmd->button_enabled = true;

    cmd = &out->commands[out->command_count++];
    qd_copy(cmd->id, sizeof(cmd->id), "set_volume");
    qd_copy(cmd->label, sizeof(cmd->label), "Set volume");
    qd_copy(cmd->kind, sizeof(cmd->kind), "internal_audio_set_volume");
    cmd->button_enabled = false;
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_NUMBER,
        .optional = false,
    };
    qd_copy(cmd->params[0].key, sizeof(cmd->params[0].key), "volume");
    qd_copy(cmd->params[0].label, sizeof(cmd->params[0].label), "Volume");

    event = &out->events[out->event_count++];
    qd_copy(event->id, sizeof(event->id), "playback_finished");
    qd_copy(event->label, sizeof(event->label), "Playback finished");
    qd_copy(event->event_type, sizeof(event->event_type), "audio_finished");

    event = &out->events[out->event_count++];
    qd_copy(event->id, sizeof(event->id), "playback_failed");
    qd_copy(event->label, sizeof(event->label), "Playback failed");
    qd_copy(event->event_type, sizeof(event->event_type), "playback_failed");
}

static esp_err_t qd_system_audio_command(const char *command_id, quest_device_command_t *out)
{
    quest_device_command_t cmd = {0};
    if (!command_id || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(command_id, "play") == 0) {
        qd_copy(cmd.id, sizeof(cmd.id), "play");
        qd_copy(cmd.label, sizeof(cmd.label), "Play audio");
        qd_copy(cmd.kind, sizeof(cmd.kind), "internal_audio_play");
        cmd.button_enabled = false;
        cmd.params[cmd.param_count++] = (quest_device_command_param_t) {
            .type = QUEST_DEVICE_COMMAND_PARAM_AUDIO_FILE_SELECT,
            .optional = false,
        };
        qd_copy(cmd.params[0].key, sizeof(cmd.params[0].key), "file");
        qd_copy(cmd.params[0].label, sizeof(cmd.params[0].label), "File");
        cmd.params[cmd.param_count++] = (quest_device_command_param_t) {
            .type = QUEST_DEVICE_COMMAND_PARAM_NUMBER,
            .optional = true,
        };
        qd_copy(cmd.params[1].key, sizeof(cmd.params[1].key), "volume");
        qd_copy(cmd.params[1].label, sizeof(cmd.params[1].label), "Volume");
        cmd.params[cmd.param_count++] = (quest_device_command_param_t) {
            .type = QUEST_DEVICE_COMMAND_PARAM_TEXT,
            .optional = true,
        };
        qd_copy(cmd.params[2].key, sizeof(cmd.params[2].key), "channel");
        qd_copy(cmd.params[2].label, sizeof(cmd.params[2].label), "Channel");
        cmd.params[cmd.param_count++] = (quest_device_command_param_t) {
            .type = QUEST_DEVICE_COMMAND_PARAM_CHECKBOX,
            .optional = true,
        };
        qd_copy(cmd.params[3].key, sizeof(cmd.params[3].key), "repeat");
        qd_copy(cmd.params[3].label, sizeof(cmd.params[3].label), "Repeat background");
        *out = cmd;
        return ESP_OK;
    }
    if (strcmp(command_id, "stop") == 0) {
        qd_copy(cmd.id, sizeof(cmd.id), "stop");
        qd_copy(cmd.label, sizeof(cmd.label), "Stop audio");
        qd_copy(cmd.kind, sizeof(cmd.kind), "internal_audio_stop");
        cmd.button_enabled = true;
        *out = cmd;
        return ESP_OK;
    }
    if (strcmp(command_id, "pause") == 0) {
        qd_copy(cmd.id, sizeof(cmd.id), "pause");
        qd_copy(cmd.label, sizeof(cmd.label), "Pause audio");
        qd_copy(cmd.kind, sizeof(cmd.kind), "internal_audio_pause");
        cmd.button_enabled = true;
        *out = cmd;
        return ESP_OK;
    }
    if (strcmp(command_id, "resume") == 0) {
        qd_copy(cmd.id, sizeof(cmd.id), "resume");
        qd_copy(cmd.label, sizeof(cmd.label), "Resume audio");
        qd_copy(cmd.kind, sizeof(cmd.kind), "internal_audio_resume");
        cmd.button_enabled = true;
        *out = cmd;
        return ESP_OK;
    }
    if (strcmp(command_id, "set_volume") == 0) {
        qd_copy(cmd.id, sizeof(cmd.id), "set_volume");
        qd_copy(cmd.label, sizeof(cmd.label), "Set volume");
        qd_copy(cmd.kind, sizeof(cmd.kind), "internal_audio_set_volume");
        cmd.button_enabled = false;
        cmd.params[cmd.param_count++] = (quest_device_command_param_t) {
            .type = QUEST_DEVICE_COMMAND_PARAM_NUMBER,
            .optional = false,
        };
        qd_copy(cmd.params[0].key, sizeof(cmd.params[0].key), "volume");
        qd_copy(cmd.params[0].label, sizeof(cmd.params[0].label), "Volume");
        *out = cmd;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t qd_system_audio_event(const char *event_id, quest_device_event_t *out)
{
    quest_device_event_t event = {0};
    if (!event_id || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(event_id, "playback_finished") == 0) {
        qd_copy(event.id, sizeof(event.id), "playback_finished");
        qd_copy(event.label, sizeof(event.label), "Playback finished");
        qd_copy(event.event_type, sizeof(event.event_type), "audio_finished");
        *out = event;
        return ESP_OK;
    }
    if (strcmp(event_id, "playback_failed") == 0) {
        qd_copy(event.id, sizeof(event.id), "playback_failed");
        qd_copy(event.label, sizeof(event.label), "Playback failed");
        qd_copy(event.event_type, sizeof(event.event_type), "playback_failed");
        *out = event;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

const char *quest_device_command_param_type_to_str(quest_device_command_param_type_t type)
{
    switch (type) {
        case QUEST_DEVICE_COMMAND_PARAM_TEXT:
            return "text";
        case QUEST_DEVICE_COMMAND_PARAM_NUMBER:
            return "number";
        case QUEST_DEVICE_COMMAND_PARAM_CHECKBOX:
            return "checkbox";
        case QUEST_DEVICE_COMMAND_PARAM_AUDIO_FILE_SELECT:
            return "audio_file_select";
        default:
            return "text";
    }
}

esp_err_t quest_device_command_param_type_from_str(const char *s,
                                                   quest_device_command_param_type_t *out)
{
    if (!s || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(s, "text") == 0) {
        *out = QUEST_DEVICE_COMMAND_PARAM_TEXT;
    } else if (strcmp(s, "number") == 0) {
        *out = QUEST_DEVICE_COMMAND_PARAM_NUMBER;
    } else if (strcmp(s, "checkbox") == 0) {
        *out = QUEST_DEVICE_COMMAND_PARAM_CHECKBOX;
    } else if (strcmp(s, "audio_file_select") == 0) {
        *out = QUEST_DEVICE_COMMAND_PARAM_AUDIO_FILE_SELECT;
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

esp_err_t quest_device_init(void)
{
    esp_err_t err = qd_ensure_lock();
    if (err != ESP_OK) {
        return err;
    }
    return qd_ensure_persist_lock();
}

esp_err_t quest_device_upsert(const quest_device_t *device)
{
    quest_device_slot_t *slot = NULL;
    esp_err_t err = ESP_OK;
    if (!qd_device_valid(device)) {
        return ESP_ERR_INVALID_ARG;
    }
    err = qd_lock();
    if (err != ESP_OK) {
        return err;
    }
    if (qd_client_id_in_use_locked(device->client_id, device->id)) {
        qd_unlock();
        return ESP_ERR_INVALID_ARG;
    }
    slot = qd_find_locked(device->id);
    if (!slot) {
        slot = qd_find_free_locked();
    }
    if (!slot) {
        qd_unlock();
        return ESP_ERR_NO_MEM;
    }
    memset(slot, 0, sizeof(*slot));
    slot->in_use = true;
    slot->device = *device;
    s_generation++;
    qd_unlock();
    return ESP_OK;
}

esp_err_t quest_device_delete(const char *device_id)
{
    quest_device_slot_t *slot = NULL;
    esp_err_t err = ESP_OK;
    if (!qd_valid_id(device_id) || strcmp(device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    err = qd_lock();
    if (err != ESP_OK) {
        return err;
    }
    slot = qd_find_locked(device_id);
    if (!slot) {
        qd_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    memset(slot, 0, sizeof(*slot));
    s_generation++;
    qd_unlock();
    return ESP_OK;
}

esp_err_t quest_device_get(const char *device_id, quest_device_t *out)
{
    quest_device_slot_t *slot = NULL;
    esp_err_t err = ESP_OK;
    if (!qd_valid_id(device_id) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0) {
        qd_fill_system_audio(out);
        return ESP_OK;
    }
    err = qd_lock();
    if (err != ESP_OK) {
        return err;
    }
    slot = qd_find_locked(device_id);
    if (!slot) {
        qd_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    *out = slot->device;
    qd_unlock();
    return ESP_OK;
}

esp_err_t quest_device_get_command(const char *device_id,
                                   const char *command_id,
                                   quest_device_command_t *out)
{
    quest_device_slot_t *slot = NULL;
    esp_err_t err = ESP_OK;
    if (!qd_valid_id(command_id) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!qd_valid_id(device_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0) {
        return qd_system_audio_command(command_id, out);
    }
    err = qd_lock();
    if (err != ESP_OK) {
        return err;
    }
    slot = qd_find_locked(device_id);
    if (!slot) {
        qd_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    for (uint8_t i = 0; i < slot->device.command_count; ++i) {
        if (strcmp(slot->device.commands[i].id, command_id) == 0) {
            *out = slot->device.commands[i];
            qd_unlock();
            return ESP_OK;
        }
    }
    qd_unlock();
    return ESP_ERR_NOT_FOUND;
}

esp_err_t quest_device_get_event(const char *device_id,
                                 const char *event_id,
                                 quest_device_event_t *out)
{
    quest_device_slot_t *slot = NULL;
    esp_err_t err = ESP_OK;
    if (!qd_valid_id(event_id) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!qd_valid_id(device_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0) {
        return qd_system_audio_event(event_id, out);
    }
    err = qd_lock();
    if (err != ESP_OK) {
        return err;
    }
    slot = qd_find_locked(device_id);
    if (!slot) {
        qd_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    for (uint8_t i = 0; i < slot->device.event_count; ++i) {
        if (strcmp(slot->device.events[i].id, event_id) == 0) {
            *out = slot->device.events[i];
            qd_unlock();
            return ESP_OK;
        }
    }
    qd_unlock();
    return ESP_ERR_NOT_FOUND;
}

esp_err_t quest_device_list(quest_device_t *out,
                            size_t max_count,
                            size_t *out_count,
                            bool include_system)
{
    size_t count = 0;
    esp_err_t err = ESP_OK;
    if (!out_count || (max_count > 0 && !out)) {
        return ESP_ERR_INVALID_ARG;
    }
    err = qd_lock();
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < QUEST_DEVICE_MAX_DEVICES; ++i) {
        if (!s_devices[i].in_use) {
            continue;
        }
        if (count < max_count) {
            out[count] = s_devices[i].device;
        }
        count++;
    }
    qd_unlock();
    if (include_system) {
        if (count < max_count) {
            qd_fill_system_audio(&out[count]);
        }
        count++;
    }
    *out_count = count;
    return count <= max_count ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t quest_device_clear(void)
{
    esp_err_t err = qd_lock();
    if (err != ESP_OK) {
        return err;
    }
    memset(s_devices, 0, sizeof(s_devices));
    s_generation++;
    qd_unlock();
    return ESP_OK;
}

uint32_t quest_device_generation(void)
{
    return s_generation;
}

static esp_err_t qd_param_to_json(const quest_device_command_param_t *param, cJSON *array)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(obj, "key", param->key);
    cJSON_AddStringToObject(obj, "label", param->label);
    cJSON_AddStringToObject(obj, "type", quest_device_command_param_type_to_str(param->type));
    cJSON_AddBoolToObject(obj, "optional", param->optional);
    if (!cJSON_AddItemToArray(array, obj)) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t qd_command_to_json(const quest_device_command_t *cmd, cJSON *array)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON *params = NULL;
    if (!obj) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(obj, "id", cmd->id);
    cJSON_AddStringToObject(obj, "label", cmd->label);
    if (cmd->kind[0]) {
        cJSON_AddStringToObject(obj, "kind", cmd->kind);
    }
    cJSON_AddStringToObject(obj, "topic", cmd->topic);
    cJSON_AddStringToObject(obj, "payload", cmd->payload);
    cJSON_AddBoolToObject(obj, "button_enabled", cmd->button_enabled);
    cJSON_AddBoolToObject(obj, "dangerous", cmd->dangerous);
    params = cJSON_AddArrayToObject(obj, "params_schema");
    if (!params) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < cmd->param_count; ++i) {
        esp_err_t err = qd_param_to_json(&cmd->params[i], params);
        if (err != ESP_OK) {
            cJSON_Delete(obj);
            return err;
        }
    }
    if (!cJSON_AddItemToArray(array, obj)) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t qd_event_to_json(const quest_device_event_t *event, cJSON *array)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(obj, "id", event->id);
    cJSON_AddStringToObject(obj, "label", event->label);
    cJSON_AddStringToObject(obj, "topic", event->topic);
    cJSON_AddStringToObject(obj, "payload", event->payload);
    cJSON_AddStringToObject(obj, "event_type", event->event_type);
    if (!cJSON_AddItemToArray(array, obj)) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t quest_device_to_json(const quest_device_t *device, cJSON *out)
{
    cJSON *commands = NULL;
    cJSON *events = NULL;
    if (!device || !cJSON_IsObject(out)) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(out, "id", device->id);
    cJSON_AddStringToObject(out, "client_id", device->client_id);
    cJSON_AddStringToObject(out, "name", device->name);
    cJSON_AddBoolToObject(out, "enabled", device->enabled);
    cJSON_AddBoolToObject(out, "system_device", device->system_device);
    commands = cJSON_AddArrayToObject(out, "commands");
    events = cJSON_AddArrayToObject(out, "events");
    if (!commands || !events) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < device->command_count; ++i) {
        esp_err_t err = qd_command_to_json(&device->commands[i], commands);
        if (err != ESP_OK) {
            return err;
        }
    }
    for (uint8_t i = 0; i < device->event_count; ++i) {
        esp_err_t err = qd_event_to_json(&device->events[i], events);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t qd_param_from_json(const cJSON *json, quest_device_command_param_t *out)
{
    const cJSON *type = NULL;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(json) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    err = qd_json_copy_string(json, "key", out->key, sizeof(out->key), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "label", out->label, sizeof(out->label), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->label[0]) {
        qd_copy(out->label, sizeof(out->label), out->key);
    }
    type = cJSON_GetObjectItemCaseSensitive(json, "type");
    if (!cJSON_IsString(type) || !type->valuestring) {
        return ESP_ERR_INVALID_ARG;
    }
    err = quest_device_command_param_type_from_str(type->valuestring, &out->type);
    if (err != ESP_OK) {
        return err;
    }
    out->optional = qd_json_bool(json, "optional", false);
    return ESP_OK;
}

static esp_err_t qd_command_from_json(const cJSON *json, quest_device_command_t *out)
{
    const cJSON *params = NULL;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(json) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    err = qd_json_copy_string(json, "id", out->id, sizeof(out->id), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "label", out->label, sizeof(out->label), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->label[0]) {
        qd_copy(out->label, sizeof(out->label), out->id);
    }
    err = qd_json_copy_string(json, "kind", out->kind, sizeof(out->kind), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->kind[0]) {
        qd_copy(out->kind, sizeof(out->kind), "mqtt_publish");
    }
    err = qd_json_copy_string(json, "topic", out->topic, sizeof(out->topic), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "payload", out->payload, sizeof(out->payload), false);
    if (err != ESP_OK) {
        return err;
    }
    out->button_enabled = qd_json_bool(json, "button_enabled", true);
    out->dangerous = qd_json_bool(json, "dangerous", false);
    params = cJSON_GetObjectItemCaseSensitive(json, "params_schema");
    if (params && !cJSON_IsNull(params)) {
        int count = cJSON_GetArraySize(params);
        if (!cJSON_IsArray(params) || count < 0 || count > QUEST_DEVICE_MAX_COMMAND_PARAMS) {
            return ESP_ERR_INVALID_ARG;
        }
        for (int i = 0; i < count; ++i) {
            err = qd_param_from_json(cJSON_GetArrayItem(params, i),
                                     &out->params[out->param_count]);
            if (err != ESP_OK) {
                return err;
            }
            out->param_count++;
        }
    }
    return ESP_OK;
}

static esp_err_t qd_event_from_json(const cJSON *json, quest_device_event_t *out)
{
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(json) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    err = qd_json_copy_string(json, "id", out->id, sizeof(out->id), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "label", out->label, sizeof(out->label), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->label[0]) {
        qd_copy(out->label, sizeof(out->label), out->id);
    }
    err = qd_json_copy_string(json, "topic", out->topic, sizeof(out->topic), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "payload", out->payload, sizeof(out->payload), false);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "event_type", out->event_type, sizeof(out->event_type), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->event_type[0]) {
        qd_copy(out->event_type, sizeof(out->event_type), out->id);
    }
    return ESP_OK;
}

esp_err_t quest_device_from_json(const cJSON *json, quest_device_t *out)
{
    const cJSON *commands = NULL;
    const cJSON *events = NULL;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(json) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    err = qd_json_copy_string(json, "id", out->id, sizeof(out->id), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "client_id", out->client_id, sizeof(out->client_id), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "name", out->name, sizeof(out->name), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->name[0]) {
        qd_copy(out->name, sizeof(out->name), out->id);
    }
    out->enabled = qd_json_bool(json, "enabled", true);
    out->system_device = false;

    commands = cJSON_GetObjectItemCaseSensitive(json, "commands");
    if (!cJSON_IsArray(commands)) {
        return ESP_ERR_INVALID_ARG;
    }
    int command_count = cJSON_GetArraySize(commands);
    if (command_count < 0 || command_count > QUEST_DEVICE_MAX_COMMANDS) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < command_count; ++i) {
        err = qd_command_from_json(cJSON_GetArrayItem(commands, i),
                                   &out->commands[out->command_count]);
        if (err != ESP_OK) {
            return err;
        }
        out->command_count++;
    }

    events = cJSON_GetObjectItemCaseSensitive(json, "events");
    if (!cJSON_IsArray(events)) {
        return ESP_ERR_INVALID_ARG;
    }
    int event_count = cJSON_GetArraySize(events);
    if (event_count < 0 || event_count > QUEST_DEVICE_MAX_EVENTS) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < event_count; ++i) {
        err = qd_event_from_json(cJSON_GetArrayItem(events, i),
                                 &out->events[out->event_count]);
        if (err != ESP_OK) {
            return err;
        }
        out->event_count++;
    }
    return qd_device_valid(out) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t quest_device_export_json(cJSON **out)
{
    cJSON *root = NULL;
    cJSON *array = NULL;
    esp_err_t err = ESP_OK;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "version", QUEST_DEVICE_JSON_VERSION);
    array = cJSON_AddArrayToObject(root, "quest_devices");
    if (!array) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    err = qd_lock();
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return err;
    }
    for (size_t i = 0; i < QUEST_DEVICE_MAX_DEVICES; ++i) {
        cJSON *obj = NULL;
        if (!s_devices[i].in_use) {
            continue;
        }
        obj = cJSON_CreateObject();
        if (!obj) {
            qd_unlock();
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        err = quest_device_to_json(&s_devices[i].device, obj);
        if (err != ESP_OK || !cJSON_AddItemToArray(array, obj)) {
            cJSON_Delete(obj);
            qd_unlock();
            cJSON_Delete(root);
            return err != ESP_OK ? err : ESP_ERR_NO_MEM;
        }
    }
    qd_unlock();
    *out = root;
    return ESP_OK;
}

static bool qd_has_duplicate_ids(const quest_device_t *items, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (strcmp(items[i].id, items[j].id) == 0) {
                return true;
            }
        }
    }
    return false;
}

static bool qd_has_duplicate_client_ids(const quest_device_t *items, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (strcmp(items[i].client_id, items[j].client_id) == 0) {
                return true;
            }
        }
    }
    return false;
}

esp_err_t quest_device_import_json(const cJSON *root)
{
    const cJSON *version = NULL;
    const cJSON *array = NULL;
    quest_device_t *items = NULL;
    int count = 0;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(root)) {
        return ESP_ERR_INVALID_ARG;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valueint != QUEST_DEVICE_JSON_VERSION) {
        return ESP_ERR_INVALID_ARG;
    }
    array = cJSON_GetObjectItemCaseSensitive(root, "quest_devices");
    if (!cJSON_IsArray(array)) {
        return ESP_ERR_INVALID_ARG;
    }
    count = cJSON_GetArraySize(array);
    if (count < 0 || count > QUEST_DEVICE_MAX_DEVICES) {
        return ESP_ERR_INVALID_ARG;
    }
    items = qd_alloc_items((size_t)count);
    if (count > 0 && !items) {
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < count; ++i) {
        err = quest_device_from_json(cJSON_GetArrayItem(array, i), &items[i]);
        if (err != ESP_OK) {
            heap_caps_free(items);
            return err;
        }
    }
    if (count > 1 &&
        (qd_has_duplicate_ids(items, (size_t)count) ||
         qd_has_duplicate_client_ids(items, (size_t)count))) {
        heap_caps_free(items);
        return ESP_ERR_INVALID_ARG;
    }
    err = qd_lock();
    if (err != ESP_OK) {
        heap_caps_free(items);
        return err;
    }
    memset(s_devices, 0, sizeof(s_devices));
    for (int i = 0; i < count; ++i) {
        s_devices[i].in_use = true;
        s_devices[i].device = items[i];
    }
    s_generation++;
    qd_unlock();
    heap_caps_free(items);
    return ESP_OK;
}

esp_err_t quest_device_save(void)
{
    return quest_device_save_to_path(QUEST_DEVICE_STORAGE_PATH);
}

esp_err_t quest_device_load(void)
{
    return quest_device_load_from_path(QUEST_DEVICE_STORAGE_PATH);
}

esp_err_t quest_device_save_to_path(const char *path)
{
    esp_err_t err = qd_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = quest_device_save_to_path_locked(path);
    qd_persist_unlock();
    return err;
}

static esp_err_t quest_device_save_to_path_locked(const char *path)
{
    cJSON *root = NULL;
    char *printed = NULL;
    FILE *f = NULL;
    char tmp[192] = {0};
    esp_err_t err = ESP_OK;
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = qd_ensure_sd_for_path(path);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_mkdir_parent(path);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_make_tmp_path(path, tmp, sizeof(tmp));
    if (err != ESP_OK) {
        return err;
    }
    err = quest_device_export_json(&root);
    if (err != ESP_OK) {
        return err;
    }
    printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }
    f = fopen(tmp, "wb");
    if (!f) {
        cJSON_free(printed);
        return ESP_FAIL;
    }
    size_t len = strlen(printed);
    if (fwrite(printed, 1, len, f) != len) {
        fclose(f);
        unlink(tmp);
        cJSON_free(printed);
        return ESP_FAIL;
    }
    if (fclose(f) != 0) {
        unlink(tmp);
        cJSON_free(printed);
        return ESP_FAIL;
    }
    cJSON_free(printed);
    unlink(path);
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t quest_device_upsert_and_save(const quest_device_t *device)
{
    esp_err_t err = qd_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = quest_device_upsert(device);
    if (err == ESP_OK) {
        err = quest_device_save_to_path_locked(QUEST_DEVICE_STORAGE_PATH);
    }
    qd_persist_unlock();
    return err;
}

esp_err_t quest_device_delete_and_save(const char *device_id)
{
    esp_err_t err = qd_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = quest_device_delete(device_id);
    if (err == ESP_OK) {
        err = quest_device_save_to_path_locked(QUEST_DEVICE_STORAGE_PATH);
    }
    qd_persist_unlock();
    return err;
}

esp_err_t quest_device_import_json_and_save(const cJSON *root)
{
    esp_err_t err = qd_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = quest_device_import_json(root);
    if (err == ESP_OK) {
        err = quest_device_save_to_path_locked(QUEST_DEVICE_STORAGE_PATH);
    }
    qd_persist_unlock();
    return err;
}

esp_err_t quest_device_load_from_path(const char *path)
{
    esp_err_t err = qd_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = quest_device_load_from_path_locked(path);
    qd_persist_unlock();
    return err;
}

static esp_err_t quest_device_load_from_path_locked(const char *path)
{
    FILE *f = NULL;
    char *buf = NULL;
    long size = 0;
    size_t bytes_read = 0;
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = qd_ensure_sd_for_path(path);
    if (err != ESP_OK) {
        return err;
    }
    f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    size = ftell(f);
    if (size < 0 || size > QUEST_DEVICE_FILE_MAX_BYTES) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(f);
    buf = qd_alloc_bytes((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    bytes_read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (bytes_read != (size_t)size) {
        heap_caps_free(buf);
        return ESP_FAIL;
    }
    buf[bytes_read] = '\0';
    root = cJSON_ParseWithLength(buf, bytes_read);
    heap_caps_free(buf);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    err = quest_device_import_json(root);
    cJSON_Delete(root);
    return err;
}
