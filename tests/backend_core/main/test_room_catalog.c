#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "cJSON.h"
#include "room_catalog.h"

static void rc_set_text(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static void rc_bootstrap(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_init());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_clear());
}

static room_catalog_entry_t rc_entry(const char *room_id, const char *name, uint16_t device_count)
{
    room_catalog_entry_t entry = {0};
    rc_set_text(entry.room_id, sizeof(entry.room_id), room_id);
    if (name) {
        rc_set_text(entry.name, sizeof(entry.name), name);
    }
    entry.device_count = device_count;
    return entry;
}

static void test_room_catalog_upsert_find_update_delete(void)
{
    room_catalog_entry_t room = {0};
    room_catalog_entry_t loaded = {0};
    uint32_t before_generation = 0;

    rc_bootstrap();
    before_generation = room_catalog_generation();

    room = rc_entry("room_a", "Room A", 3);
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&room));
    TEST_ASSERT_TRUE(room_catalog_generation() > before_generation);
    TEST_ASSERT_EQUAL_UINT(1, room_catalog_count());
    TEST_ASSERT_TRUE(room_catalog_exists("room_a"));
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_find("room_a", &loaded));
    TEST_ASSERT_EQUAL_STRING("Room A", loaded.name);
    TEST_ASSERT_EQUAL_UINT16(3, loaded.device_count);

    room = rc_entry("room_a", "Room A updated", 7);
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&room));
    TEST_ASSERT_EQUAL_UINT(1, room_catalog_count());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_get(0, &loaded));
    TEST_ASSERT_EQUAL_STRING("Room A updated", loaded.name);
    TEST_ASSERT_EQUAL_UINT16(7, loaded.device_count);

    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_delete("room_a"));
    TEST_ASSERT_EQUAL_UINT(0, room_catalog_count());
    TEST_ASSERT_FALSE(room_catalog_exists("room_a"));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, room_catalog_find("room_a", &loaded));
}

static void test_room_catalog_delete_middle_preserves_list_order(void)
{
    room_catalog_entry_t loaded = {0};

    rc_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&(room_catalog_entry_t){
        .room_id = "room_a",
        .name = "Room A",
    }));
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&(room_catalog_entry_t){
        .room_id = "room_b",
        .name = "Room B",
    }));
#if ROOM_CATALOG_MAX_ROOMS >= 3
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&(room_catalog_entry_t){
        .room_id = "room_c",
        .name = "Room C",
    }));

    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_delete("room_b"));
    TEST_ASSERT_EQUAL_UINT(2, room_catalog_count());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_get(0, &loaded));
    TEST_ASSERT_EQUAL_STRING("room_a", loaded.room_id);
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_get(1, &loaded));
    TEST_ASSERT_EQUAL_STRING("room_c", loaded.room_id);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, room_catalog_get(2, &loaded));
#else
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_delete("room_a"));
    TEST_ASSERT_EQUAL_UINT(1, room_catalog_count());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_get(0, &loaded));
    TEST_ASSERT_EQUAL_STRING("room_b", loaded.room_id);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, room_catalog_get(1, &loaded));
#endif
}

static void test_room_catalog_rejects_invalid_entries_and_reserved_delete(void)
{
    room_catalog_entry_t entry = {0};

    rc_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_catalog_upsert(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_catalog_upsert(&entry));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_catalog_delete(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_catalog_delete(""));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_catalog_delete("unassigned"));
}

static void test_room_catalog_save_load_path_errors_are_reported(void)
{
    rc_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_catalog_save_to_path(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_catalog_save_to_path(""));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_catalog_load_from_path(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_catalog_load_from_path(""));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, room_catalog_load_from_path("missing_rooms_test.json"));
}

static void test_room_catalog_capacity_limit_preserves_existing_rooms(void)
{
    room_catalog_entry_t entry = {0};
    room_catalog_entry_t loaded = {0};
    char id[ROOM_CATALOG_ROOM_ID_MAX_LEN] = {0};
    char name[ROOM_CATALOG_ROOM_NAME_MAX_LEN] = {0};

    rc_bootstrap();

    for (uint16_t i = 0; i < ROOM_CATALOG_MAX_ROOMS; ++i) {
        snprintf(id, sizeof(id), "room_%02u", (unsigned)i);
        snprintf(name, sizeof(name), "Room %02u", (unsigned)i);
        entry = rc_entry(id, name, i);
        TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&entry));
    }

    TEST_ASSERT_EQUAL_UINT(ROOM_CATALOG_MAX_ROOMS, room_catalog_count());
    entry = rc_entry("room_extra", "Room Extra", 99);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, room_catalog_upsert(&entry));
    TEST_ASSERT_EQUAL_UINT(ROOM_CATALOG_MAX_ROOMS, room_catalog_count());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_find("room_00", &loaded));
    TEST_ASSERT_EQUAL_STRING("Room 00", loaded.name);
}

static void test_room_catalog_json_round_trip_and_alias_id(void)
{
    cJSON *root = NULL;
    cJSON *rooms = NULL;
    cJSON *room = NULL;
    room_catalog_entry_t loaded = {0};

    rc_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&(room_catalog_entry_t){
        .room_id = "room_a",
        .name = "Room A",
        .device_count = 4,
    }));
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&(room_catalog_entry_t){
        .room_id = "room_b",
        .name = "",
        .device_count = 2,
    }));

    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_export_json(&root));
    TEST_ASSERT_NOT_NULL(root);
    rooms = cJSON_GetObjectItemCaseSensitive(root, "rooms");
    TEST_ASSERT_TRUE(cJSON_IsArray(rooms));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(rooms));
    cJSON_Delete(root);

    root = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(root);
    cJSON_AddNumberToObject(root, "version", 1);
    rooms = cJSON_AddArrayToObject(root, "rooms");
    TEST_ASSERT_NOT_NULL(rooms);
    room = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(room);
    cJSON_AddStringToObject(room, "id", "imported_room");
    cJSON_AddItemToArray(rooms, room);

    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_import_json(root));
    TEST_ASSERT_EQUAL_UINT(1, room_catalog_count());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_find("imported_room", &loaded));
    TEST_ASSERT_EQUAL_STRING("imported_room", loaded.name);
    cJSON_Delete(root);
}

static void test_room_catalog_import_rejects_duplicates_and_bad_shape(void)
{
    cJSON *root = NULL;
    cJSON *rooms = NULL;
    cJSON *room = NULL;

    rc_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_catalog_import_json(NULL));

    root = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(root);
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddArrayToObject(root, "rooms");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_catalog_import_json(root));
    cJSON_Delete(root);

    root = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(root);
    cJSON_AddNumberToObject(root, "version", 1);
    rooms = cJSON_AddArrayToObject(root, "rooms");
    TEST_ASSERT_NOT_NULL(rooms);
    for (int i = 0; i < 2; ++i) {
        room = cJSON_CreateObject();
        TEST_ASSERT_NOT_NULL(room);
        cJSON_AddStringToObject(room, "room_id", "same");
        cJSON_AddStringToObject(room, "name", i == 0 ? "A" : "B");
        cJSON_AddItemToArray(rooms, room);
    }
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_catalog_import_json(root));
    TEST_ASSERT_EQUAL_UINT(0, room_catalog_count());
    cJSON_Delete(root);
}

void register_room_catalog_tests(void)
{
    RUN_TEST(test_room_catalog_upsert_find_update_delete);
    RUN_TEST(test_room_catalog_delete_middle_preserves_list_order);
    RUN_TEST(test_room_catalog_rejects_invalid_entries_and_reserved_delete);
    RUN_TEST(test_room_catalog_save_load_path_errors_are_reported);
    RUN_TEST(test_room_catalog_capacity_limit_preserves_existing_rooms);
    RUN_TEST(test_room_catalog_json_round_trip_and_alias_id);
    RUN_TEST(test_room_catalog_import_rejects_duplicates_and_bad_shape);
}
