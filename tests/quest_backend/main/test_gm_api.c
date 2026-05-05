#include <string.h>

#include "unity.h"

#include "event_bus.h"
#include "esp_attr.h"
#include "gm_api.h"
#include "gm_game_profile.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"

EXT_RAM_BSS_ATTR static room_scenario_t s_api_scenario;
EXT_RAM_BSS_ATTR static gm_room_session_t s_api_session;

static void api_copy(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static void api_add_room(const char *room_id, const char *name)
{
    room_catalog_entry_t room = {0};
    api_copy(room.room_id, sizeof(room.room_id), room_id);
    api_copy(room.name, sizeof(room.name), name);
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&room));
}

static void api_add_wait_scenario(const char *scenario_id, const char *room_id)
{
    memset(&s_api_scenario, 0, sizeof(s_api_scenario));
    api_copy(s_api_scenario.id, sizeof(s_api_scenario.id), scenario_id);
    api_copy(s_api_scenario.name, sizeof(s_api_scenario.name), "Wait scenario");
    api_copy(s_api_scenario.room_id, sizeof(s_api_scenario.room_id), room_id);
    api_copy(s_api_scenario.steps[0].id, sizeof(s_api_scenario.steps[0].id), "wait");
    api_copy(s_api_scenario.steps[0].label, sizeof(s_api_scenario.steps[0].label), "Wait");
    s_api_scenario.steps[0].type = ROOM_SCENARIO_STEP_WAIT_TIME;
    s_api_scenario.steps[0].enabled = true;
    s_api_scenario.steps[0].data.wait_time.duration_ms = 1000;
    s_api_scenario.step_count = 1;
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&s_api_scenario));
}

static void api_add_profile(const char *profile_id,
                            const char *room_id,
                            const char *scenario_id,
                            uint32_t duration_ms)
{
    gm_game_profile_t profile = {0};
    api_copy(profile.id, sizeof(profile.id), profile_id);
    api_copy(profile.name, sizeof(profile.name), "Profile");
    api_copy(profile.room_id, sizeof(profile.room_id), room_id);
    api_copy(profile.scenario_id, sizeof(profile.scenario_id), scenario_id);
    profile.duration_ms = duration_ms;
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_upsert(&profile));
}

static void api_bootstrap(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_init());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_clear());
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_init());
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_clear());
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_init());
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_clear());
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_init());
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_clear());
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_init());
    gm_room_session_reset_all();
}

static void test_gm_api_rejects_null_and_unknown_rooms(void)
{
    gm_room_state_view_t state = {0};

    api_bootstrap();
    memset(&s_api_session, 0, sizeof(s_api_session));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_api_get_room_state(NULL, &state));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_api_get_room_state("", &state));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_api_get_room_state("room_a", NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_api_get_room_state("missing", &state));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_api_room_session_get("missing", &s_api_session));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_api_timer_start("missing", 1000));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_api_hint_send("missing", "hint"));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_api_select_profile("missing", "profile"));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_api_select_scenario("missing", "scenario"));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_api_game_start("missing"));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_api_scenario_start("missing"));
}

static void test_gm_api_room_state_for_existing_room_without_session(void)
{
    gm_room_state_view_t state = {0};

    api_bootstrap();
    api_add_room("room_a", "Room A");

    TEST_ASSERT_EQUAL(ESP_OK, gm_api_get_room_state("room_a", &state));
    TEST_ASSERT_TRUE(state.exists);
    TEST_ASSERT_FALSE(state.session_present);
    TEST_ASSERT_FALSE(state.session_active);
    TEST_ASSERT_EQUAL_STRING("room_a", state.room_id);
    TEST_ASSERT_EQUAL(GM_SESSION_IDLE, state.session_state);
    TEST_ASSERT_EQUAL(GM_TIMER_IDLE, state.timer_state);
}

static void test_gm_api_timer_and_hint_commands_update_room_state(void)
{
    gm_room_state_view_t state = {0};

    api_bootstrap();
    api_add_room("room_a", "Room A");

    TEST_ASSERT_EQUAL(ESP_OK, gm_api_timer_start("room_a", 60000));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_hint_send("room_a", "Check the mirror"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_get_room_state("room_a", &state));
    TEST_ASSERT_TRUE(state.session_present);
    TEST_ASSERT_TRUE(state.session_active);
    TEST_ASSERT_EQUAL(GM_SESSION_RUNNING, state.session_state);
    TEST_ASSERT_EQUAL(GM_TIMER_RUNNING, state.timer_state);
    TEST_ASSERT_EQUAL_UINT32(60000, state.duration_ms);
    TEST_ASSERT_TRUE(state.remaining_ms <= 60000);
    TEST_ASSERT_TRUE(state.hint_active);
    TEST_ASSERT_EQUAL_UINT32(1, state.hint_count);
    TEST_ASSERT_EQUAL_STRING("Check the mirror", state.hint_text);

    TEST_ASSERT_EQUAL(ESP_OK, gm_api_timer_pause("room_a"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_hint_clear("room_a"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_get_room_state("room_a", &state));
    TEST_ASSERT_EQUAL(GM_SESSION_PAUSED, state.session_state);
    TEST_ASSERT_EQUAL(GM_TIMER_PAUSED, state.timer_state);
    TEST_ASSERT_FALSE(state.hint_active);
    TEST_ASSERT_EQUAL_STRING("", state.hint_text);
}

static void test_gm_api_profile_scenario_and_game_flow(void)
{
    gm_room_state_view_t state = {0};

    api_bootstrap();
    api_add_room("room_a", "Room A");
    api_add_wait_scenario("scenario_a", "room_a");
    api_add_profile("profile_a", "room_a", "scenario_a", 45000);

    TEST_ASSERT_EQUAL(ESP_OK, gm_api_select_profile("room_a", "profile_a"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_get_room_state("room_a", &state));
    TEST_ASSERT_EQUAL_STRING("profile_a", state.selected_profile_id);
    TEST_ASSERT_EQUAL_STRING("scenario_a", state.selected_profile_scenario_id);
    TEST_ASSERT_EQUAL_STRING("scenario_a", state.selected_scenario_id);
    TEST_ASSERT_EQUAL_UINT32(45000, state.selected_profile_duration_ms);

    TEST_ASSERT_EQUAL(ESP_OK, gm_api_game_start("room_a"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_get_room_state("room_a", &state));
    TEST_ASSERT_EQUAL(GM_SESSION_RUNNING, state.session_state);
    TEST_ASSERT_EQUAL(GM_TIMER_RUNNING, state.timer_state);

    TEST_ASSERT_EQUAL(ESP_OK, gm_api_game_stop("room_a"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_get_room_state("room_a", &state));
    TEST_ASSERT_EQUAL(GM_SESSION_FINISHED, state.session_state);

    TEST_ASSERT_EQUAL(ESP_OK, gm_api_game_reset("room_a"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_get_room_state("room_a", &state));
    TEST_ASSERT_EQUAL(GM_SESSION_IDLE, state.session_state);
}

static void test_gm_api_scenario_commands_report_invalid_state_when_not_running(void)
{
    api_bootstrap();
    api_add_room("room_a", "Room A");
    api_add_wait_scenario("scenario_a", "room_a");

    TEST_ASSERT_EQUAL(ESP_OK, gm_api_select_scenario("room_a", "scenario_a"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_scenario_start("room_a"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_scenario_stop("room_a"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_scenario_reset("room_a"));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, gm_api_scenario_next("room_a"));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, gm_api_scenario_next_branch("room_a", "main"));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, gm_api_scenario_approve("room_a"));
}

static void test_gm_api_device_command_run_validates_device_command(void)
{
    quest_device_t device = {0};

    api_bootstrap();
    api_copy(device.id, sizeof(device.id), "relay");
    api_copy(device.client_id, sizeof(device.client_id), "relay");
    api_copy(device.name, sizeof(device.name), "Relay");
    device.enabled = true;
    device.command_count = 1;
    api_copy(device.commands[0].id, sizeof(device.commands[0].id), "open");
    api_copy(device.commands[0].label, sizeof(device.commands[0].label), "Open");
    api_copy(device.commands[0].capability, sizeof(device.commands[0].capability), "relay");
    api_copy(device.commands[0].command, sizeof(device.commands[0].command), "relay.pulse");
    device.commands[0].manual_allowed = true;
    device.commands[0].scenario_allowed = true;
    device.commands[0].requires_confirmation = false;
    device.commands[0].result_required = true;
    device.commands[0].timeout_ms = QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS;
    api_copy(device.commands[0].danger_level, sizeof(device.commands[0].danger_level), "normal");
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&device));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_api_device_command_run(NULL, "open", NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gm_api_device_command_run("relay", NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_api_device_command_run("missing", "open", NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gm_api_device_command_run("relay", "missing", NULL));
}

void register_gm_api_tests(void)
{
    RUN_TEST(test_gm_api_rejects_null_and_unknown_rooms);
    RUN_TEST(test_gm_api_room_state_for_existing_room_without_session);
    RUN_TEST(test_gm_api_timer_and_hint_commands_update_room_state);
    RUN_TEST(test_gm_api_profile_scenario_and_game_flow);
    RUN_TEST(test_gm_api_scenario_commands_report_invalid_state_when_not_running);
    RUN_TEST(test_gm_api_device_command_run_validates_device_command);
}
