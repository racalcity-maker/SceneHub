#include "quest_device.h"
#include "quest_device_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    bool in_use;
    quest_device_t device;
} quest_device_slot_t;

EXT_RAM_BSS_ATTR static quest_device_slot_t s_devices[QUEST_DEVICE_MAX_DEVICES];
static SemaphoreHandle_t s_lock = NULL;
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;
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
