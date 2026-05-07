#include "room_scenario.h"
#include "room_scenario_internal.h"

#include <string.h>

#include "cJSON.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define ROOM_SCENARIO_JSON_VERSION 1

typedef struct {
    bool in_use;
    room_scenario_t scenario;
} room_scenario_slot_t;

EXT_RAM_BSS_ATTR static room_scenario_slot_t s_scenarios[ROOM_SCENARIO_MAX_SCENARIOS];
static SemaphoreHandle_t s_lock = NULL;
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_generation = 0;

static esp_err_t room_scenario_ensure_lock(void)
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

static esp_err_t room_scenario_lock(void)
{
    esp_err_t err = room_scenario_ensure_lock();
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void room_scenario_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static room_scenario_slot_t *room_scenario_find_locked(const char *scenario_id)
{
    if (!scenario_id || !scenario_id[0]) {
        return NULL;
    }
    for (size_t i = 0; i < ROOM_SCENARIO_MAX_SCENARIOS; ++i) {
        room_scenario_slot_t *slot = &s_scenarios[i];
        if (slot->in_use && strcmp(slot->scenario.id, scenario_id) == 0) {
            return slot;
        }
    }
    return NULL;
}

static room_scenario_slot_t *room_scenario_find_free_locked(void)
{
    for (size_t i = 0; i < ROOM_SCENARIO_MAX_SCENARIOS; ++i) {
        if (!s_scenarios[i].in_use) {
            return &s_scenarios[i];
        }
    }
    return NULL;
}

static room_scenario_t *room_scenario_alloc_import_items(size_t count)
{
    room_scenario_t *items = NULL;
    if (count == 0) {
        return NULL;
    }
    items = heap_caps_calloc(count,
                             sizeof(*items),
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!items) {
        items = heap_caps_calloc(count,
                                 sizeof(*items),
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return items;
}

esp_err_t room_scenario_init(void)
{
    return room_scenario_ensure_lock();
}

esp_err_t room_scenario_add(const room_scenario_t *scenario)
{
    room_scenario_slot_t *slot = NULL;
    esp_err_t err = room_scenario_validate_structural(scenario);
    if (err != ESP_OK) {
        return err;
    }
    err = room_scenario_lock();
    if (err != ESP_OK) {
        return err;
    }
    slot = room_scenario_find_locked(scenario->id);
    if (!slot) {
        slot = room_scenario_find_free_locked();
    }
    if (!slot) {
        room_scenario_unlock();
        return ESP_ERR_NO_MEM;
    }
    memset(slot, 0, sizeof(*slot));
    slot->in_use = true;
    slot->scenario = *scenario;
    s_generation++;
    room_scenario_unlock();
    return ESP_OK;
}

esp_err_t room_scenario_delete(const char *scenario_id)
{
    room_scenario_slot_t *slot = NULL;
    if (!scenario_id || !scenario_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = room_scenario_lock();
    if (err != ESP_OK) {
        return err;
    }
    slot = room_scenario_find_locked(scenario_id);
    if (!slot) {
        room_scenario_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    memset(slot, 0, sizeof(*slot));
    s_generation++;
    room_scenario_unlock();
    return ESP_OK;
}

esp_err_t room_scenario_get(const char *scenario_id, room_scenario_t *out)
{
    room_scenario_slot_t *slot = NULL;
    if (!scenario_id || !scenario_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = room_scenario_lock();
    if (err != ESP_OK) {
        return err;
    }
    slot = room_scenario_find_locked(scenario_id);
    if (!slot) {
        room_scenario_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    *out = slot->scenario;
    room_scenario_unlock();
    return ESP_OK;
}

esp_err_t room_scenario_exists_in_room(const char *scenario_id, const char *room_id)
{
    room_scenario_slot_t *slot = NULL;
    esp_err_t err = ESP_OK;

    if (!scenario_id || !scenario_id[0] || !room_id || !room_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_scenario_lock();
    if (err != ESP_OK) {
        return err;
    }
    slot = room_scenario_find_locked(scenario_id);
    if (!slot) {
        room_scenario_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    err = strcmp(slot->scenario.room_id, room_id) == 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
    room_scenario_unlock();
    return err;
}

esp_err_t room_scenario_get_name_in_room(const char *scenario_id,
                                         const char *room_id,
                                         char *out_name,
                                         size_t out_name_size)
{
    room_scenario_slot_t *slot = NULL;
    esp_err_t err = ESP_OK;

    if (!scenario_id || !scenario_id[0] ||
        !room_id || !room_id[0] ||
        !out_name || out_name_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out_name[0] = '\0';
    err = room_scenario_lock();
    if (err != ESP_OK) {
        return err;
    }
    slot = room_scenario_find_locked(scenario_id);
    if (!slot) {
        room_scenario_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    if (strcmp(slot->scenario.room_id, room_id) != 0) {
        room_scenario_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    size_t len = strlen(slot->scenario.name);
    if (len >= out_name_size) {
        len = out_name_size - 1;
    }
    memcpy(out_name, slot->scenario.name, len);
    out_name[len] = '\0';
    room_scenario_unlock();
    return ESP_OK;
}

esp_err_t room_scenario_list_by_room(const char *room_id,
                                     room_scenario_t *out,
                                     size_t max_count,
                                     size_t *out_count)
{
    size_t count = 0;
    if (!room_id || !room_id[0] || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (max_count > 0 && !out) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = room_scenario_lock();
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < ROOM_SCENARIO_MAX_SCENARIOS; ++i) {
        const room_scenario_slot_t *slot = &s_scenarios[i];
        if (!slot->in_use || strcmp(slot->scenario.room_id, room_id) != 0) {
            continue;
        }
        if (count < max_count) {
            out[count] = slot->scenario;
        }
        count++;
    }
    *out_count = count;
    room_scenario_unlock();
    return count > max_count ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

esp_err_t room_scenario_get_by_room_index(const char *room_id,
                                          size_t index,
                                          room_scenario_t *out,
                                          size_t *out_count)
{
    size_t count = 0;
    bool found = false;
    if (!room_id || !room_id[0] || !out || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    esp_err_t err = room_scenario_lock();
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < ROOM_SCENARIO_MAX_SCENARIOS; ++i) {
        const room_scenario_slot_t *slot = &s_scenarios[i];
        if (!slot->in_use || strcmp(slot->scenario.room_id, room_id) != 0) {
            continue;
        }
        if (count == index) {
            *out = slot->scenario;
            found = true;
        }
        count++;
    }
    *out_count = count;
    room_scenario_unlock();
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t room_scenario_store_export_json(cJSON **out)
{
    cJSON *root = NULL;
    cJSON *array = NULL;
    esp_err_t err = ESP_OK;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "version", ROOM_SCENARIO_JSON_VERSION);
    array = cJSON_AddArrayToObject(root, "room_scenarios");
    if (!array) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    err = room_scenario_lock();
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return err;
    }
    for (size_t i = 0; i < ROOM_SCENARIO_MAX_SCENARIOS; ++i) {
        const room_scenario_slot_t *slot = &s_scenarios[i];
        cJSON *obj = NULL;
        if (!slot->in_use) {
            continue;
        }
        obj = cJSON_CreateObject();
        if (!obj) {
            room_scenario_unlock();
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        err = room_scenario_to_json(&slot->scenario, obj);
        if (err != ESP_OK) {
            room_scenario_unlock();
            cJSON_Delete(obj);
            cJSON_Delete(root);
            return err;
        }
        if (!cJSON_AddItemToArray(array, obj)) {
            room_scenario_unlock();
            cJSON_Delete(obj);
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
    }
    room_scenario_unlock();
    *out = root;
    return ESP_OK;
}

esp_err_t room_scenario_store_import_json(const cJSON *root)
{
    const cJSON *version = NULL;
    const cJSON *array = NULL;
    room_scenario_t *items = NULL;
    int count = 0;
    esp_err_t err = ESP_OK;

    if (!cJSON_IsObject(root)) {
        return ESP_ERR_INVALID_ARG;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valueint != ROOM_SCENARIO_JSON_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    array = cJSON_GetObjectItemCaseSensitive(root, "room_scenarios");
    if (!cJSON_IsArray(array)) {
        return ESP_ERR_INVALID_ARG;
    }
    count = cJSON_GetArraySize(array);
    if (count < 0 || count > ROOM_SCENARIO_MAX_SCENARIOS) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (count > 0) {
        items = room_scenario_alloc_import_items((size_t)count);
        if (!items) {
            return ESP_ERR_NO_MEM;
        }
    }
    for (int i = 0; i < count; ++i) {
        const cJSON *scenario_obj = cJSON_GetArrayItem(array, i);
        err = room_scenario_from_json(scenario_obj, &items[i]);
        if (err != ESP_OK) {
            heap_caps_free(items);
            return err;
        }
        for (int j = 0; j < i; ++j) {
            if (strcmp(items[j].id, items[i].id) == 0) {
                heap_caps_free(items);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    err = room_scenario_lock();
    if (err != ESP_OK) {
        heap_caps_free(items);
        return err;
    }
    memset(s_scenarios, 0, sizeof(s_scenarios));
    for (int i = 0; i < count; ++i) {
        s_scenarios[i].in_use = true;
        s_scenarios[i].scenario = items[i];
    }
    s_generation++;
    room_scenario_unlock();
    heap_caps_free(items);
    return ESP_OK;
}

esp_err_t room_scenario_export_json(cJSON **out_root)
{
    return room_scenario_store_export_json(out_root);
}

esp_err_t room_scenario_import_json(const cJSON *root)
{
    return room_scenario_store_import_json(root);
}

esp_err_t room_scenario_clear(void)
{
    esp_err_t err = room_scenario_lock();
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < ROOM_SCENARIO_MAX_SCENARIOS; ++i) {
        memset(&s_scenarios[i], 0, sizeof(s_scenarios[i]));
        if ((i % 4U) == 3U) {
            vTaskDelay(1);
        }
    }
    s_generation++;
    room_scenario_unlock();
    return ESP_OK;
}

uint32_t room_scenario_generation(void)
{
    uint32_t generation = 0;
    if (room_scenario_lock() != ESP_OK) {
        return 0;
    }
    generation = s_generation;
    room_scenario_unlock();
    return generation;
}

void room_scenario_reset(void)
{
    (void)room_scenario_clear();
}
