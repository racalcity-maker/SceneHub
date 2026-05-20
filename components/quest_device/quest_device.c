#include "quest_device.h"
#include "quest_device_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    bool in_use;
    quest_device_t device;
} quest_device_slot_t;

EXT_RAM_BSS_ATTR static quest_device_slot_t s_devices[QUEST_DEVICE_MAX_DEVICES];
EXT_RAM_BSS_ATTR static char s_compact_lookup_description_json[QUEST_DEVICE_DESCRIPTION_JSON_MAX_LEN];
static SemaphoreHandle_t s_lock = NULL;
static SemaphoreHandle_t s_compact_lookup_lock = NULL;
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_compact_lookup_lock_storage;
static portMUX_TYPE s_compact_lookup_init_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_generation = 0;

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

static esp_err_t qd_lock(void)
{
    esp_err_t err = qd_ensure_lock();
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void qd_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static esp_err_t qd_compact_lookup_lock(void)
{
    if (!s_compact_lookup_lock) {
        portENTER_CRITICAL(&s_compact_lookup_init_lock);
        if (!s_compact_lookup_lock) {
            s_compact_lookup_lock =
                xSemaphoreCreateMutexStatic(&s_compact_lookup_lock_storage);
        }
        portEXIT_CRITICAL(&s_compact_lookup_init_lock);
        if (!s_compact_lookup_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    return xSemaphoreTake(s_compact_lookup_lock, portMAX_DELAY) == pdTRUE ? ESP_OK
                                                                          : ESP_ERR_TIMEOUT;
}

static void qd_compact_lookup_unlock(void)
{
    if (s_compact_lookup_lock) {
        xSemaphoreGive(s_compact_lookup_lock);
    }
}

static bool qd_valid_id(const char *value)
{
    return value && value[0];
}

static void qd_copy(char *dst, size_t dst_len, const char *src)
{
    size_t len = 0;
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    len = strlen(src);
    if (len >= dst_len) {
        len = dst_len - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static const char *qd_json_string(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static void qd_default_capability_from_name(char *dst, size_t dst_len, const char *name)
{
    const char *dot = NULL;
    size_t len = 0;
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (!name || !name[0]) {
        return;
    }
    dot = strchr(name, '.');
    len = dot ? (size_t)(dot - name) : strlen(name);
    if (len == 0) {
        return;
    }
    if (len >= dst_len) {
        len = dst_len - 1;
    }
    memcpy(dst, name, len);
    dst[len] = '\0';
}

static bool qd_compact_manifest_root_valid(const cJSON *root)
{
    const cJSON *manifest_version = cJSON_GetObjectItemCaseSensitive(root, "manifest_version");
    return cJSON_IsObject(root) &&
           cJSON_IsNumber(manifest_version) &&
           manifest_version->valueint == 2 &&
           strcmp(qd_json_string(root, "format"), "compact_resources") == 0 &&
           qd_json_string(root, "node_kind")[0] &&
           strcmp(qd_json_string(root, "capability_contract"), "scenehub.node.compact.v1") == 0;
}

static const char *qd_compact_resource_key(const char *source)
{
    if (!source || !source[0]) {
        return NULL;
    }
    return strcmp(source, "led_strips") == 0 ? "strip" : "channel";
}

static const cJSON *qd_compact_resources_array(const cJSON *root, const char *source)
{
    const cJSON *resources = NULL;
    if (!root || !source || !source[0]) {
        return NULL;
    }
    resources = cJSON_GetObjectItemCaseSensitive(root, "resources");
    if (!cJSON_IsObject(resources)) {
        return NULL;
    }
    return cJSON_GetObjectItemCaseSensitive(resources, source);
}

static bool qd_compact_resource_item_matches(const cJSON *item,
                                             const char *resource_key,
                                             const char *resource_id)
{
    const cJSON *value = NULL;
    char buffer[16];
    if (!cJSON_IsObject(item) || !resource_key || !resource_id || !resource_id[0]) {
        return false;
    }
    value = cJSON_GetObjectItemCaseSensitive(item, resource_key);
    if (cJSON_IsString(value) && value->valuestring) {
        return strcmp(value->valuestring, resource_id) == 0;
    }
    if (cJSON_IsNumber(value)) {
        snprintf(buffer, sizeof(buffer), "%d", value->valueint);
        return strcmp(buffer, resource_id) == 0;
    }
    return false;
}

static const cJSON *qd_compact_find_resource_item(const cJSON *root,
                                                  const char *source,
                                                  const char *event_name,
                                                  const char *resource_id)
{
    const char *resource_key = qd_compact_resource_key(source);
    const cJSON *resources = qd_compact_resources_array(root, source);
    int count = 0;
    if (!resource_key || !cJSON_IsArray(resources) || !event_name || !event_name[0] ||
        !resource_id || !resource_id[0]) {
        return NULL;
    }
    count = cJSON_GetArraySize(resources);
    for (int i = 0; i < count; ++i) {
        const cJSON *item = cJSON_GetArrayItem(resources, i);
        if (strcmp(qd_json_string(item, "event"), event_name) == 0 &&
            qd_compact_resource_item_matches(item, resource_key, resource_id)) {
            return item;
        }
    }
    return NULL;
}

static esp_err_t qd_compact_build_match_json(const cJSON *resource_item,
                                             const char *source,
                                             char *dst,
                                             size_t dst_len)
{
    const char *resource_key = qd_compact_resource_key(source);
    const cJSON *value = NULL;
    cJSON *match = NULL;
    char *json = NULL;

    if (!resource_item || !resource_key || !dst || dst_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    value = cJSON_GetObjectItemCaseSensitive(resource_item, resource_key);
    if (!cJSON_IsString(value) && !cJSON_IsNumber(value)) {
        return ESP_ERR_NOT_FOUND;
    }
    match = cJSON_CreateObject();
    if (!match) {
        return ESP_ERR_NO_MEM;
    }
    if (cJSON_IsNumber(value)) {
        cJSON_AddNumberToObject(match, resource_key, value->valuedouble);
    } else {
        cJSON_AddStringToObject(match, resource_key, value->valuestring ? value->valuestring : "");
    }
    json = cJSON_PrintUnformatted(match);
    cJSON_Delete(match);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    qd_copy(dst, dst_len, json);
    free(json);
    return dst[0] ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t qd_compact_copy_match_object(const cJSON *item,
                                              char *dst,
                                              size_t dst_len)
{
    const cJSON *match = NULL;
    char *json = NULL;
    if (!item || !dst || dst_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    dst[0] = '\0';
    match = cJSON_GetObjectItemCaseSensitive(item, "match");
    if (!cJSON_IsObject(match)) {
        return ESP_ERR_NOT_FOUND;
    }
    json = cJSON_PrintUnformatted(match);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    qd_copy(dst, dst_len, json);
    free(json);
    return dst[0] ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t qd_compact_manifest_get_event(const char *device_description_json,
                                               const char *event_id,
                                               quest_device_event_t *out)
{
    cJSON *root = NULL;
    const cJSON *events = NULL;
    int event_count = 0;
    esp_err_t err = ESP_ERR_NOT_FOUND;
    const char *resource_sep = NULL;
    char base_event_id[QUEST_DEVICE_EVENT_ID_MAX_LEN];
    char resource_id[16];

    if (!device_description_json || !device_description_json[0] || !event_id || !out) {
        return ESP_ERR_NOT_FOUND;
    }
    root = cJSON_Parse(device_description_json);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!qd_compact_manifest_root_valid(root)) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    events = cJSON_GetObjectItemCaseSensitive(root, "event_templates");
    if (!cJSON_IsArray(events)) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    resource_sep = strchr(event_id, '@');
    base_event_id[0] = '\0';
    resource_id[0] = '\0';
    if (resource_sep && resource_sep != event_id && resource_sep[1]) {
        size_t base_len = (size_t)(resource_sep - event_id);
        if (base_len >= sizeof(base_event_id)) {
            base_len = sizeof(base_event_id) - 1;
        }
        memcpy(base_event_id, event_id, base_len);
        base_event_id[base_len] = '\0';
        qd_copy(resource_id, sizeof(resource_id), resource_sep + 1);
    }
    event_count = cJSON_GetArraySize(events);
    for (int i = 0; i < event_count; ++i) {
        const cJSON *item = cJSON_GetArrayItem(events, i);
        const cJSON *resource_item = NULL;
        const char *id = qd_json_string(item, "id");
        const char *event_name = qd_json_string(item, "event");
        const char *label = qd_json_string(item, "label");
        const char *capability = qd_json_string(item, "capability");
        const char *source = qd_json_string(item, "source");
        bool exact_match = false;
        bool resource_match = false;

        if (!cJSON_IsObject(item) || !event_name[0]) {
            continue;
        }
        exact_match = strcmp(id, event_id) == 0;
        resource_match = base_event_id[0] &&
                         strcmp(id, base_event_id) == 0 &&
                         source[0] &&
                         (resource_item = qd_compact_find_resource_item(root,
                                                                        source,
                                                                        event_name,
                                                                        resource_id)) != NULL;
        if (!exact_match && !resource_match) {
            continue;
        }
        memset(out, 0, sizeof(*out));
        qd_copy(out->id, sizeof(out->id), resource_match ? event_id : id);
        if (resource_match) {
            const char *resource_label = qd_json_string(resource_item, "label");
            char combined_label[QUEST_DEVICE_NAME_MAX_LEN];
            combined_label[0] = '\0';
            if (resource_label[0] && label[0]) {
                snprintf(combined_label,
                         sizeof(combined_label),
                         "%s - %s",
                         resource_label,
                         label);
            } else {
                qd_copy(combined_label,
                        sizeof(combined_label),
                        resource_label[0] ? resource_label : (label[0] ? label : event_id));
            }
            qd_copy(out->label, sizeof(out->label), combined_label);
            (void)qd_compact_build_match_json(resource_item,
                                              source,
                                              out->match_json,
                                              sizeof(out->match_json));
        } else {
            qd_copy(out->label, sizeof(out->label), label[0] ? label : id);
            (void)qd_compact_copy_match_object(item,
                                               out->match_json,
                                               sizeof(out->match_json));
        }
        if (capability[0]) {
            qd_copy(out->capability, sizeof(out->capability), capability);
        } else if (source[0]) {
            qd_copy(out->capability, sizeof(out->capability), source);
        } else {
            qd_default_capability_from_name(out->capability, sizeof(out->capability), event_name);
        }
        qd_copy(out->event, sizeof(out->event), event_name);
        err = ESP_OK;
        break;
    }
    cJSON_Delete(root);
    return err;
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
    if (device->system_device ||
        strcmp(device->id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0 ||
        strcmp(device->id, QUEST_DEVICE_SYSTEM_RELAY_ID) == 0 ||
        strcmp(device->id, QUEST_DEVICE_SYSTEM_MOSFET_ID) == 0 ||
        strcmp(device->id, QUEST_DEVICE_SYSTEM_IO_ID) == 0) {
        return false;
    }
    if (device->command_count > QUEST_DEVICE_MAX_COMMANDS ||
        device->event_count > QUEST_DEVICE_MAX_EVENTS) {
        return false;
    }
    if (qd_commands_have_duplicate_ids(device) || qd_events_have_duplicate_ids(device)) {
        return false;
    }
    if (device->device_description_json[0] &&
        (device->command_count != 0 || device->event_count != 0)) {
        return false;
    }
    for (uint8_t i = 0; i < device->command_count; ++i) {
        const quest_device_command_t *cmd = &device->commands[i];
        if (!cmd->id[0] || !cmd->label[0] || !cmd->capability[0] || !cmd->command[0] ||
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
        if (!event->id[0] || !event->label[0] || !event->capability[0] || !event->event[0]) {
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

esp_err_t quest_device_init(void)
{
    esp_err_t err = qd_ensure_lock();
    if (err != ESP_OK) {
        return err;
    }
    return quest_device_storage_init();
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
    if (!qd_valid_id(device_id) ||
        strcmp(device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0 ||
        strcmp(device_id, QUEST_DEVICE_SYSTEM_RELAY_ID) == 0 ||
        strcmp(device_id, QUEST_DEVICE_SYSTEM_MOSFET_ID) == 0 ||
        strcmp(device_id, QUEST_DEVICE_SYSTEM_IO_ID) == 0) {
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
        quest_device_fill_system_audio(out);
        return ESP_OK;
    }
    if (strcmp(device_id, QUEST_DEVICE_SYSTEM_RELAY_ID) == 0) {
        quest_device_fill_system_relay(out);
        return ESP_OK;
    }
    if (strcmp(device_id, QUEST_DEVICE_SYSTEM_MOSFET_ID) == 0) {
        quest_device_fill_system_mosfet(out);
        return ESP_OK;
    }
    if (strcmp(device_id, QUEST_DEVICE_SYSTEM_IO_ID) == 0) {
        quest_device_fill_system_io(out);
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
        return quest_device_system_audio_command(command_id, out);
    }
    if (strcmp(device_id, QUEST_DEVICE_SYSTEM_RELAY_ID) == 0) {
        return quest_device_system_relay_command(command_id, out);
    }
    if (strcmp(device_id, QUEST_DEVICE_SYSTEM_MOSFET_ID) == 0) {
        return quest_device_system_mosfet_command(command_id, out);
    }
    if (strcmp(device_id, QUEST_DEVICE_SYSTEM_IO_ID) == 0) {
        return quest_device_system_io_command(command_id, out);
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
        return quest_device_system_audio_event(event_id, out);
    }
    if (strcmp(device_id, QUEST_DEVICE_SYSTEM_RELAY_ID) == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (strcmp(device_id, QUEST_DEVICE_SYSTEM_MOSFET_ID) == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (strcmp(device_id, QUEST_DEVICE_SYSTEM_IO_ID) == 0) {
        return quest_device_system_io_event(event_id, out);
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
    if (slot->device.device_description_json[0]) {
        err = qd_compact_lookup_lock();
        if (err != ESP_OK) {
            qd_unlock();
            return err;
        }
        qd_copy(s_compact_lookup_description_json,
                sizeof(s_compact_lookup_description_json),
                slot->device.device_description_json);
        qd_unlock();
        err = qd_compact_manifest_get_event(s_compact_lookup_description_json, event_id, out);
        qd_compact_lookup_unlock();
        return err;
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
            quest_device_fill_system_audio(&out[count]);
        }
        count++;
        if (count < max_count) {
            quest_device_fill_system_relay(&out[count]);
        }
        count++;
        if (count < max_count) {
            quest_device_fill_system_mosfet(&out[count]);
        }
        count++;
        if (count < max_count) {
            quest_device_fill_system_io(&out[count]);
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

esp_err_t quest_device_replace_all(const quest_device_t *items, size_t count)
{
    esp_err_t err = ESP_OK;
    if (count > QUEST_DEVICE_MAX_DEVICES || (count > 0 && !items)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!qd_device_valid(&items[i])) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    err = qd_lock();
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (strcmp(items[i].id, items[j].id) == 0 ||
                strcmp(items[i].client_id, items[j].client_id) == 0) {
                qd_unlock();
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    memset(s_devices, 0, sizeof(s_devices));
    for (size_t i = 0; i < count; ++i) {
        s_devices[i].in_use = true;
        s_devices[i].device = items[i];
    }
    s_generation++;
    qd_unlock();
    return ESP_OK;
}
