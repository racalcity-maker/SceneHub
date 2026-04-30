#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "cJSON.h"
#include "gm_game_profile.h"
#include "room_catalog.h"
#include "room_scenario.h"

static gm_game_profile_t s_profiles[4];

static void set_text(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static void game_profile_test_bootstrap(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_init());
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_clear());
}

static void game_profile_runtime_bootstrap(void)
{
    room_catalog_entry_t room = {0};
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_init());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_clear());
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_init());
    room_scenario_reset();
    set_text(room.room_id, sizeof(room.room_id), "room_1");
    set_text(room.name, sizeof(room.name), "Room 1");
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&room));
}

static void init_profile(gm_game_profile_t *profile,
                         const char *id,
                         const char *name,
                         const char *room_id,
                         const char *scenario_id,
                         uint32_t duration_ms)
{
    memset(profile, 0, sizeof(*profile));
    set_text(profile->id, sizeof(profile->id), id);
    set_text(profile->name, sizeof(profile->name), name);
    set_text(profile->room_id, sizeof(profile->room_id), room_id);
    set_text(profile->scenario_id, sizeof(profile->scenario_id), scenario_id);
    set_text(profile->hint_pack_id, sizeof(profile->hint_pack_id), "easy");
    set_text(profile->audio_pack_id, sizeof(profile->audio_pack_id), "classic");
    profile->duration_ms = duration_ms;
    profile->enabled = true;
}

static void add_room_scenario(const char *scenario_id, const char *room_id)
{
    room_scenario_t scenario = {0};
    set_text(scenario.id, sizeof(scenario.id), scenario_id);
    set_text(scenario.name, sizeof(scenario.name), scenario_id);
    set_text(scenario.room_id, sizeof(scenario.room_id), room_id);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&scenario));
}

static void test_gm_game_profile_add_and_get(void)
{
    gm_game_profile_t *profile = &s_profiles[0];
    gm_game_profile_t *loaded = &s_profiles[1];

    game_profile_test_bootstrap();
    memset(s_profiles, 0, sizeof(s_profiles));

    init_profile(profile, "easy", "Easy", "room_1", "easy_flow", 3600000);
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_add(profile));
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_get("easy", loaded));

    TEST_ASSERT_EQUAL_STRING("easy", loaded->id);
    TEST_ASSERT_EQUAL_STRING("Easy", loaded->name);
    TEST_ASSERT_EQUAL_STRING("room_1", loaded->room_id);
    TEST_ASSERT_EQUAL_STRING("easy_flow", loaded->scenario_id);
    TEST_ASSERT_EQUAL_UINT32(3600000, loaded->duration_ms);
    TEST_ASSERT_EQUAL_STRING("easy", loaded->hint_pack_id);
    TEST_ASSERT_EQUAL_STRING("classic", loaded->audio_pack_id);
    TEST_ASSERT_TRUE(loaded->enabled);
}

static void test_gm_game_profile_list_by_room(void)
{
    gm_game_profile_t *one = &s_profiles[0];
    gm_game_profile_t *two = &s_profiles[1];
    gm_game_profile_t *other = &s_profiles[2];
    gm_game_profile_t items[2] = {0};
    size_t count = 0;

    game_profile_test_bootstrap();
    memset(s_profiles, 0, sizeof(s_profiles));

    init_profile(one, "easy", "Easy", "room_1", "easy_flow", 3600000);
    init_profile(two, "hard", "Hard", "room_1", "hard_flow", 2700000);
    init_profile(other, "beta", "Beta", "room_2", "beta_flow", 1800000);
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_add(one));
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_add(other));
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_add(two));

    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_list_by_room("room_1", items, 2, &count));
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_EQUAL_STRING("easy", items[0].id);
    TEST_ASSERT_EQUAL_STRING("hard", items[1].id);
}

static void test_gm_game_profile_replace_existing_id(void)
{
    gm_game_profile_t *first = &s_profiles[0];
    gm_game_profile_t *second = &s_profiles[1];
    gm_game_profile_t *loaded = &s_profiles[2];
    uint32_t generation = 0;

    game_profile_test_bootstrap();
    memset(s_profiles, 0, sizeof(s_profiles));

    init_profile(first, "easy", "Easy", "room_1", "easy_flow", 3600000);
    init_profile(second, "easy", "Easy 2", "room_1", "easy_flow_v2", 4200000);
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_add(first));
    generation = gm_game_profile_generation();
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_add(second));
    TEST_ASSERT_EQUAL_UINT32(generation + 1, gm_game_profile_generation());

    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_get("easy", loaded));
    TEST_ASSERT_EQUAL_STRING("Easy 2", loaded->name);
    TEST_ASSERT_EQUAL_STRING("easy_flow_v2", loaded->scenario_id);
    TEST_ASSERT_EQUAL_UINT32(4200000, loaded->duration_ms);
}

static void test_gm_game_profile_delete_removes_profile(void)
{
    gm_game_profile_t *profile = &s_profiles[0];

    game_profile_test_bootstrap();
    memset(s_profiles, 0, sizeof(s_profiles));

    init_profile(profile, "easy", "Easy", "room_1", "easy_flow", 3600000);
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_add(profile));
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_delete("easy"));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_game_profile_get("easy", &s_profiles[1]));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_game_profile_delete("easy"));
}

static void test_gm_game_profile_validate_checks_room_and_scenario(void)
{
    gm_game_profile_t *profile = &s_profiles[0];
    gm_game_profile_t *other_room = &s_profiles[1];

    game_profile_test_bootstrap();
    game_profile_runtime_bootstrap();
    memset(s_profiles, 0, sizeof(s_profiles));
    add_room_scenario("easy_flow", "room_1");
    add_room_scenario("wrong_room_flow", "room_2");

    init_profile(profile, "easy", "Easy", "room_1", "easy_flow", 3600000);
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_validate(profile));

    init_profile(profile, "missing_scenario", "Missing", "room_1", "missing_flow", 3600000);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_game_profile_validate(profile));

    init_profile(profile, "missing_room", "Missing room", "room_missing", "easy_flow", 3600000);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_game_profile_validate(profile));

    init_profile(other_room, "wrong_room", "Wrong room", "room_1", "wrong_room_flow", 3600000);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, gm_game_profile_validate(other_room));
}

static void test_gm_game_profile_json_round_trip(void)
{
    gm_game_profile_t *profile = &s_profiles[0];
    gm_game_profile_t *loaded = &s_profiles[1];
    cJSON *json = NULL;

    game_profile_test_bootstrap();
    memset(s_profiles, 0, sizeof(s_profiles));
    init_profile(profile, "easy", "Easy", "room_1", "easy_flow", 3600000);

    json = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_to_json(profile, json));
    TEST_ASSERT_EQUAL_STRING("easy", cJSON_GetObjectItemCaseSensitive(json, "id")->valuestring);
    TEST_ASSERT_EQUAL_STRING("easy_flow", cJSON_GetObjectItemCaseSensitive(json, "scenario_id")->valuestring);
    TEST_ASSERT_EQUAL_INT(3600000, cJSON_GetObjectItemCaseSensitive(json, "duration_ms")->valueint);

    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_from_json(json, loaded));
    TEST_ASSERT_EQUAL_STRING("easy", loaded->id);
    TEST_ASSERT_EQUAL_STRING("Easy", loaded->name);
    TEST_ASSERT_EQUAL_STRING("room_1", loaded->room_id);
    TEST_ASSERT_EQUAL_STRING("classic", loaded->audio_pack_id);
    TEST_ASSERT_TRUE(loaded->enabled);
    cJSON_Delete(json);
}

static void test_gm_game_profile_collection_export_import(void)
{
    gm_game_profile_t *easy = &s_profiles[0];
    gm_game_profile_t *hard = &s_profiles[1];
    gm_game_profile_t *loaded = &s_profiles[2];
    cJSON *root = NULL;
    cJSON *array = NULL;

    game_profile_test_bootstrap();
    memset(s_profiles, 0, sizeof(s_profiles));

    init_profile(easy, "easy", "Easy", "room_1", "easy_flow", 3600000);
    init_profile(hard, "hard", "Hard", "room_1", "hard_flow", 2700000);
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_add(easy));
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_add(hard));

    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_export_json(&root));
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItemCaseSensitive(root, "version")->valueint);
    array = cJSON_GetObjectItemCaseSensitive(root, "game_profiles");
    TEST_ASSERT_TRUE(cJSON_IsArray(array));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(array));

    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_clear());
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_import_json(root));
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_get("easy", loaded));
    TEST_ASSERT_EQUAL_STRING("Easy", loaded->name);
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_get("hard", loaded));
    TEST_ASSERT_EQUAL_STRING("hard_flow", loaded->scenario_id);
}

static void test_gm_game_profile_import_invalid_json_keeps_existing_store(void)
{
    const char *json =
        "{\"version\":1,\"game_profiles\":[{\"id\":\"broken\",\"name\":\"Broken\","
        "\"room_id\":\"room_1\",\"scenario_id\":\"flow\",\"duration_ms\":0}]}";
    gm_game_profile_t *existing = &s_profiles[0];
    gm_game_profile_t *loaded = &s_profiles[1];
    cJSON *root = NULL;

    game_profile_test_bootstrap();
    memset(s_profiles, 0, sizeof(s_profiles));
    init_profile(existing, "existing", "Existing", "room_1", "flow", 1000);
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_add(existing));

    root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, gm_game_profile_import_json(root));
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_get("existing", loaded));
    TEST_ASSERT_EQUAL_STRING("Existing", loaded->name);
}

static void test_gm_game_profile_validation_rejects_invalid_profile(void)
{
    gm_game_profile_t *profile = &s_profiles[0];
    cJSON *json = NULL;

    game_profile_test_bootstrap();
    memset(s_profiles, 0, sizeof(s_profiles));

    init_profile(profile, "bad", "Bad", "room_1", "flow", 0);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_game_profile_add(profile));

    init_profile(profile, "bad", "Bad", "room_1", "", 1000);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_game_profile_add(profile));

    json = cJSON_Parse("{\"id\":\"easy\",\"name\":\"Easy\",\"room_id\":\"room_1\","
                       "\"scenario_id\":\"easy_flow\",\"duration_ms\":0}");
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_game_profile_from_json(json, profile));
    cJSON_Delete(json);
}

static void test_gm_game_profile_load_missing_file_keeps_existing_store(void)
{
    gm_game_profile_t *existing = &s_profiles[0];
    gm_game_profile_t *loaded = &s_profiles[1];

    game_profile_test_bootstrap();
    memset(s_profiles, 0, sizeof(s_profiles));
    init_profile(existing, "existing", "Existing", "room_1", "flow", 1000);
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_add(existing));

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                      gm_game_profile_load_from_path("missing_game_profiles_test.json"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_get("existing", loaded));
    TEST_ASSERT_EQUAL_STRING("Existing", loaded->name);
    TEST_ASSERT_EQUAL_STRING("/sdcard/quest/game_profiles.json", GM_GAME_PROFILE_STORAGE_PATH);
}

static void test_gm_game_profile_capacity_and_small_list_buffer(void)
{
    gm_game_profile_t items[2] = {0};
    gm_game_profile_t overflow = {0};
    size_t count = 0;

    game_profile_test_bootstrap();

    for (size_t i = 0; i < GM_GAME_PROFILE_MAX_PROFILES; ++i) {
        char id[GM_GAME_PROFILE_ID_MAX_LEN] = {0};
        gm_game_profile_t profile = {0};
        snprintf(id, sizeof(id), "profile_%02u", (unsigned)i);
        init_profile(&profile, id, "Profile", "room_1", "flow", 1000);
        TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_add(&profile));
    }

    init_profile(&overflow, "overflow", "Overflow", "room_1", "flow", 1000);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, gm_game_profile_add(&overflow));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, gm_game_profile_list_by_room("room_1", items, 2, &count));
    TEST_ASSERT_EQUAL_UINT(GM_GAME_PROFILE_MAX_PROFILES, count);
    TEST_ASSERT_EQUAL_STRING("profile_00", items[0].id);
    TEST_ASSERT_EQUAL_STRING("profile_01", items[1].id);
}

void register_gm_game_profile_tests(void)
{
    RUN_TEST(test_gm_game_profile_add_and_get);
    RUN_TEST(test_gm_game_profile_list_by_room);
    RUN_TEST(test_gm_game_profile_replace_existing_id);
    RUN_TEST(test_gm_game_profile_delete_removes_profile);
    RUN_TEST(test_gm_game_profile_validate_checks_room_and_scenario);
    RUN_TEST(test_gm_game_profile_json_round_trip);
    RUN_TEST(test_gm_game_profile_collection_export_import);
    RUN_TEST(test_gm_game_profile_import_invalid_json_keeps_existing_store);
    RUN_TEST(test_gm_game_profile_validation_rejects_invalid_profile);
    RUN_TEST(test_gm_game_profile_load_missing_file_keeps_existing_store);
    RUN_TEST(test_gm_game_profile_capacity_and_small_list_buffer);
}
