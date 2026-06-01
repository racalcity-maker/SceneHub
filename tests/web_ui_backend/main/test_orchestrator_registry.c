#include <string.h>

#include "unity.h"

#include "device_control_ingest.h"
#include "event_bus.h"
#include "esp_attr.h"
#include "gm_game_profile.h"
#include "gm_room_session.h"
#include "orchestrator_registry.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "scenehub_control.h"
#include "service_status.h"

EXT_RAM_BSS_ATTR static room_scenario_t s_reg_scenario;
EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_reg_snapshot_a;
EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_reg_snapshot_b;
EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_reg_snapshot_c;
EXT_RAM_BSS_ATTR static orch_device_entry_t s_reg_device;
EXT_RAM_BSS_ATTR static orch_room_scenario_entry_t s_reg_scenarios[2];
EXT_RAM_BSS_ATTR static orch_room_scenario_detail_t s_reg_details[1];
EXT_RAM_BSS_ATTR static orch_room_profile_entry_t s_reg_profiles[2];
EXT_RAM_BSS_ATTR static quest_device_t s_reg_device_scratch;
EXT_RAM_BSS_ATTR static quest_device_t s_reg_invalid_devices[1];
EXT_RAM_BSS_ATTR static quest_device_t s_reg_devices_with_system[QUEST_DEVICE_MAX_DEVICES + 4];

static void reg_copy(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static void reg_add_room(const char *room_id, const char *name)
{
    room_catalog_entry_t room = {0};
    reg_copy(room.room_id, sizeof(room.room_id), room_id);
    reg_copy(room.name, sizeof(room.name), name);
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&room));
}

static void reg_add_device(const char *id, const char *name, bool enabled)
{
    memset(&s_reg_device_scratch, 0, sizeof(s_reg_device_scratch));
    reg_copy(s_reg_device_scratch.id, sizeof(s_reg_device_scratch.id), id);
    reg_copy(s_reg_device_scratch.client_id, sizeof(s_reg_device_scratch.client_id), id);
    reg_copy(s_reg_device_scratch.name, sizeof(s_reg_device_scratch.name), name);
    s_reg_device_scratch.enabled = enabled;
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&s_reg_device_scratch));
}

static void reg_add_scenario(const char *scenario_id, const char *room_id)
{
    memset(&s_reg_scenario, 0, sizeof(s_reg_scenario));
    reg_copy(s_reg_scenario.id, sizeof(s_reg_scenario.id), scenario_id);
    reg_copy(s_reg_scenario.name, sizeof(s_reg_scenario.name), "Scenario");
    reg_copy(s_reg_scenario.room_id, sizeof(s_reg_scenario.room_id), room_id);
    reg_copy(s_reg_scenario.steps[0].id, sizeof(s_reg_scenario.steps[0].id), "delay");
    reg_copy(s_reg_scenario.steps[0].label, sizeof(s_reg_scenario.steps[0].label), "Delay");
    s_reg_scenario.steps[0].type = ROOM_SCENARIO_STEP_WAIT_TIME;
    s_reg_scenario.steps[0].enabled = true;
    s_reg_scenario.steps[0].data.wait_time.duration_ms = 1000;
    s_reg_scenario.step_count = 1;
    reg_copy(s_reg_scenario.branches[0].id, sizeof(s_reg_scenario.branches[0].id), "main");
    reg_copy(s_reg_scenario.branches[0].name, sizeof(s_reg_scenario.branches[0].name), "Main");
    s_reg_scenario.branches[0].type = ROOM_SCENARIO_BRANCH_NORMAL;
    s_reg_scenario.branches[0].enabled = true;
    s_reg_scenario.branches[0].required_for_completion = true;
    s_reg_scenario.branches[0].step_start_index = 0;
    s_reg_scenario.branches[0].step_count = 1;
    s_reg_scenario.branch_count = 1;
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&s_reg_scenario));
}

static void reg_add_scenario_with_group_and_wait_devices(const char *scenario_id,
                                                         const char *room_id)
{
    memset(&s_reg_scenario, 0, sizeof(s_reg_scenario));
    reg_copy(s_reg_scenario.id, sizeof(s_reg_scenario.id), scenario_id);
    reg_copy(s_reg_scenario.name, sizeof(s_reg_scenario.name), "Scenario");
    reg_copy(s_reg_scenario.room_id, sizeof(s_reg_scenario.room_id), room_id);

    reg_copy(s_reg_scenario.steps[0].id, sizeof(s_reg_scenario.steps[0].id), "group");
    reg_copy(s_reg_scenario.steps[0].label, sizeof(s_reg_scenario.steps[0].label), "Command group");
    s_reg_scenario.steps[0].type = ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP;
    s_reg_scenario.steps[0].enabled = true;
    s_reg_scenario.steps[0].data.device_command_group.command_count = 2;
    reg_copy(s_reg_scenario.steps[0].data.device_command_group.commands[0].device_id,
             sizeof(s_reg_scenario.steps[0].data.device_command_group.commands[0].device_id),
             "relay");
    reg_copy(s_reg_scenario.steps[0].data.device_command_group.commands[0].command_id,
             sizeof(s_reg_scenario.steps[0].data.device_command_group.commands[0].command_id),
             "pulse");
    reg_copy(s_reg_scenario.steps[0].data.device_command_group.commands[1].device_id,
             sizeof(s_reg_scenario.steps[0].data.device_command_group.commands[1].device_id),
             "uid_gate");
    reg_copy(s_reg_scenario.steps[0].data.device_command_group.commands[1].command_id,
             sizeof(s_reg_scenario.steps[0].data.device_command_group.commands[1].command_id),
             "reset");

    reg_copy(s_reg_scenario.steps[1].id, sizeof(s_reg_scenario.steps[1].id), "wait_any");
    reg_copy(s_reg_scenario.steps[1].label, sizeof(s_reg_scenario.steps[1].label), "Wait any");
    s_reg_scenario.steps[1].type = ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT;
    s_reg_scenario.steps[1].enabled = true;
    s_reg_scenario.steps[1].data.wait_any_device_event.event_count = 1;
    reg_copy(s_reg_scenario.steps[1].data.wait_any_device_event.events[0].device_id,
             sizeof(s_reg_scenario.steps[1].data.wait_any_device_event.events[0].device_id),
             "relay");
    reg_copy(s_reg_scenario.steps[1].data.wait_any_device_event.events[0].event_id,
             sizeof(s_reg_scenario.steps[1].data.wait_any_device_event.events[0].event_id),
             "opened");

    s_reg_scenario.step_count = 2;
    reg_copy(s_reg_scenario.branches[0].id, sizeof(s_reg_scenario.branches[0].id), "main");
    reg_copy(s_reg_scenario.branches[0].name, sizeof(s_reg_scenario.branches[0].name), "Main");
    s_reg_scenario.branches[0].type = ROOM_SCENARIO_BRANCH_NORMAL;
    s_reg_scenario.branches[0].enabled = true;
    s_reg_scenario.branches[0].required_for_completion = true;
    s_reg_scenario.branches[0].step_start_index = 0;
    s_reg_scenario.branches[0].step_count = 2;
    s_reg_scenario.branch_count = 1;
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&s_reg_scenario));
}

static void reg_add_profile(const char *profile_id,
                            const char *room_id,
                            const char *scenario_id,
                            uint32_t duration_ms)
{
    gm_game_profile_t profile = {0};
    reg_copy(profile.id, sizeof(profile.id), profile_id);
    reg_copy(profile.name, sizeof(profile.name), "Profile");
    reg_copy(profile.room_id, sizeof(profile.room_id), room_id);
    reg_copy(profile.scenario_id, sizeof(profile.scenario_id), scenario_id);
    reg_copy(profile.hint_pack_id, sizeof(profile.hint_pack_id), "hint");
    reg_copy(profile.audio_pack_id, sizeof(profile.audio_pack_id), "audio");
    profile.duration_ms = duration_ms;
    profile.enabled = true;
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_upsert(&profile));
}

static void reg_bootstrap(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, service_status_init());
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
    TEST_ASSERT_EQUAL(ESP_OK, device_control_ingest_init());
    TEST_ASSERT_EQUAL(ESP_OK, device_control_ingest_reset());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_init());
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_clear());
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_init());
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_clear());
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_init());
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_clear());
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_init());
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_clear());
    TEST_ASSERT_EQUAL(ESP_OK, scenehub_control_init());
    gm_room_session_reset_all();
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_registry_init());
    orchestrator_registry_invalidate();
}

static void test_orchestrator_registry_rejects_invalid_args(void)
{
    size_t count = 0;

    reg_bootstrap();
    memset(&s_reg_snapshot_a, 0, sizeof(s_reg_snapshot_a));
    memset(&s_reg_device, 0, sizeof(s_reg_device));
    memset(s_reg_scenarios, 0, sizeof(s_reg_scenarios));
    memset(s_reg_details, 0, sizeof(s_reg_details));
    memset(s_reg_profiles, 0, sizeof(s_reg_profiles));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_registry_build_snapshot(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_registry_list_rooms(NULL, 1, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_registry_list_rooms(s_reg_snapshot_a.rooms, 0, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_registry_list_rooms(s_reg_snapshot_a.rooms, 1, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_registry_get_device(NULL, &s_reg_device));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_registry_get_device("", &s_reg_device));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_registry_get_device("device", NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_quest_devices(NULL, 1, &count, false));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_quest_devices(s_reg_invalid_devices, 1, NULL, false));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_scenario_step_schemas(NULL,
                                                                       ORCH_ROOM_SCENARIO_MAX_STEP_SCHEMAS,
                                                                       &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_scenario_step_schemas(
                          (orch_room_scenario_step_schema_t *)s_reg_snapshot_a.rooms,
                          ORCH_ROOM_SCENARIO_MAX_STEP_SCHEMAS,
                          NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_scenario_step_schemas(
                          (orch_room_scenario_step_schema_t *)s_reg_snapshot_a.rooms,
                          ORCH_ROOM_SCENARIO_MAX_STEP_SCHEMAS - 1,
                          &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_device_issues(NULL, s_reg_snapshot_a.issues, 1, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_device_issues("device", NULL, 1, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_device_issues("device", s_reg_snapshot_a.issues, 0, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_device_issues("device", s_reg_snapshot_a.issues, 1, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_scenarios(NULL, s_reg_scenarios, 1, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_scenarios("room", NULL, 1, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_scenarios("room", s_reg_scenarios, 0, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_scenarios("room", s_reg_scenarios, 1, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_profiles(NULL, s_reg_profiles, 1, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_profiles("room", NULL, 1, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_profiles("room", s_reg_profiles, 0, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_profiles("room", s_reg_profiles, 1, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_scenario_details(NULL, s_reg_details, 1, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_scenario_details("room", NULL, 1, &count));
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_registry_build_snapshot(&s_reg_snapshot_a));
}

static void test_orchestrator_registry_snapshot_collects_rooms_devices_services_and_scenarios(void)
{
    reg_bootstrap();
    memset(&s_reg_snapshot_a, 0, sizeof(s_reg_snapshot_a));
    memset(&s_reg_device, 0, sizeof(s_reg_device));
    service_status_mark_init(SERVICE_STATUS_MQTT, ESP_OK);
    service_status_mark_start(SERVICE_STATUS_MQTT, ESP_FAIL);
    reg_add_room("room_a", "Room A");
    reg_add_device("relay", "Relay", true);
    reg_add_device("disabled", "Disabled", false);
    reg_add_scenario("scenario_a", "room_a");

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_registry_build_snapshot(&s_reg_snapshot_a));
    TEST_ASSERT_TRUE(s_reg_snapshot_a.service_count >= SERVICE_STATUS_COUNT);
    TEST_ASSERT_EQUAL_UINT(1, s_reg_snapshot_a.room_count);
    TEST_ASSERT_EQUAL_STRING("room_a", s_reg_snapshot_a.rooms[0].room_id);
    TEST_ASSERT_EQUAL_STRING("Room A", s_reg_snapshot_a.rooms[0].title);
    TEST_ASSERT_EQUAL_UINT(2, s_reg_snapshot_a.device_count);
    TEST_ASSERT_EQUAL_UINT(1, s_reg_snapshot_a.room_scenario_count);
    TEST_ASSERT_TRUE(s_reg_snapshot_a.has_degraded);

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_registry_get_device("disabled", &s_reg_device));
    TEST_ASSERT_EQUAL_STRING("disabled", s_reg_device.device_id);
    TEST_ASSERT_EQUAL(ORCH_HEALTH_DEGRADED, s_reg_device.health);
    TEST_ASSERT_EQUAL_STRING("disabled", s_reg_device.state);
    TEST_ASSERT_EQUAL_UINT8(1, s_reg_device.badge_count);
    TEST_ASSERT_EQUAL_STRING("degraded", s_reg_device.badges[0]);
}

static void test_orchestrator_registry_service_fault_creates_system_issue(void)
{
    bool found = false;

    reg_bootstrap();
    memset(&s_reg_snapshot_a, 0, sizeof(s_reg_snapshot_a));
    service_status_mark_init(SERVICE_STATUS_HARDWARE_IO, ESP_OK);
    service_status_mark_fault(SERVICE_STATUS_HARDWARE_IO, ESP_ERR_INVALID_STATE);

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_registry_build_snapshot(&s_reg_snapshot_a));
    TEST_ASSERT_TRUE(s_reg_snapshot_a.has_fault);
    for (uint8_t i = 0; i < s_reg_snapshot_a.issue_count; ++i) {
        const orch_issue_entry_t *issue = &s_reg_snapshot_a.issues[i];
        if (strcmp(issue->code, "service_fault") == 0 &&
            strcmp(issue->title, "hardware_io fault") == 0) {
            found = true;
            TEST_ASSERT_EQUAL(ORCH_ISSUE_SCOPE_SYSTEM, issue->scope);
            TEST_ASSERT_EQUAL(ORCH_ISSUE_SEVERITY_ERROR, issue->severity);
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

static void test_orchestrator_registry_cache_version_changes_after_invalidate(void)
{
    reg_bootstrap();
    memset(&s_reg_snapshot_a, 0, sizeof(s_reg_snapshot_a));
    memset(&s_reg_snapshot_b, 0, sizeof(s_reg_snapshot_b));
    memset(&s_reg_snapshot_c, 0, sizeof(s_reg_snapshot_c));
    reg_add_room("room_a", "Room A");

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_registry_build_snapshot(&s_reg_snapshot_a));
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_registry_build_snapshot(&s_reg_snapshot_b));
    TEST_ASSERT_EQUAL_UINT32(s_reg_snapshot_a.cache_version, s_reg_snapshot_b.cache_version);

    orchestrator_registry_invalidate();
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_registry_build_snapshot(&s_reg_snapshot_c));
    TEST_ASSERT_TRUE(s_reg_snapshot_c.cache_version > s_reg_snapshot_b.cache_version);
}

static void test_orchestrator_registry_lists_room_scenarios_and_details(void)
{
    size_t count = 0;

    reg_bootstrap();
    memset(s_reg_scenarios, 0, sizeof(s_reg_scenarios));
    memset(s_reg_details, 0, sizeof(s_reg_details));
    reg_add_room("room_a", "Room A");
    reg_add_scenario("scenario_a", "room_a");
    reg_add_scenario("scenario_b", "room_a");

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_registry_list_room_scenarios("room_a", s_reg_scenarios, 2, &count));
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_EQUAL_STRING("scenario_a", s_reg_scenarios[0].id);
    TEST_ASSERT_EQUAL_STRING("scenario_b", s_reg_scenarios[1].id);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      orchestrator_registry_list_room_scenario_details("room_a", s_reg_details, 1, &count));
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_EQUAL_STRING("scenario_a", s_reg_details[0].summary.id);
    TEST_ASSERT_TRUE(s_reg_details[0].summary.valid);
    TEST_ASSERT_EQUAL_UINT(1, s_reg_details[0].summary.step_count);
    TEST_ASSERT_EQUAL_STRING("delay", s_reg_details[0].steps[0].id);
    TEST_ASSERT_EQUAL_UINT(1, s_reg_details[0].branch_count);
    TEST_ASSERT_EQUAL_STRING("main", s_reg_details[0].branches[0].id);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                      orchestrator_registry_list_room_scenarios("missing", s_reg_scenarios, 2, &count));
}

static void test_orchestrator_registry_lists_rooms_without_full_snapshot(void)
{
    size_t count = 0;

    reg_bootstrap();
    memset(&s_reg_snapshot_a, 0, sizeof(s_reg_snapshot_a));
    reg_add_room("room_a", "Room A");
    reg_add_room("room_b", "Room B");

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_registry_list_rooms(s_reg_snapshot_a.rooms,
                                                       ORCH_REGISTRY_MAX_ROOMS,
                                                       &count));
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_EQUAL_STRING("room_a", s_reg_snapshot_a.rooms[0].room_id);
    TEST_ASSERT_EQUAL_STRING("Room A", s_reg_snapshot_a.rooms[0].title);
    TEST_ASSERT_EQUAL_STRING("room_b", s_reg_snapshot_a.rooms[1].room_id);
    TEST_ASSERT_EQUAL_STRING("Room B", s_reg_snapshot_a.rooms[1].title);
}

static void test_orchestrator_registry_lists_room_profiles_from_read_model(void)
{
    size_t count = 0;

    reg_bootstrap();
    memset(s_reg_profiles, 0, sizeof(s_reg_profiles));
    reg_add_room("room_a", "Room A");
    reg_add_scenario("scenario_ok", "room_a");
    reg_add_profile("profile_ok", "room_a", "scenario_ok", 60000);
    reg_add_profile("profile_bad", "room_a", "scenario_missing", 30000);

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_registry_list_room_profiles("room_a",
                                                               s_reg_profiles,
                                                               2,
                                                               &count));
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_EQUAL_STRING("profile_ok", s_reg_profiles[0].id);
    TEST_ASSERT_EQUAL_STRING("scenario_ok", s_reg_profiles[0].scenario_id);
    TEST_ASSERT_TRUE(s_reg_profiles[0].valid);
    TEST_ASSERT_EQUAL_STRING("profile_bad", s_reg_profiles[1].id);
    TEST_ASSERT_EQUAL_STRING("scenario_missing", s_reg_profiles[1].scenario_id);
    TEST_ASSERT_FALSE(s_reg_profiles[1].valid);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                      orchestrator_registry_list_room_profiles("missing",
                                                               s_reg_profiles,
                                                               2,
                                                               &count));
}

static void test_orchestrator_registry_lists_quest_devices_with_optional_system_entries(void)
{
    size_t count = 0;

    reg_bootstrap();
    memset(s_reg_devices_with_system, 0, sizeof(s_reg_devices_with_system));
    reg_add_device("relay", "Relay", true);

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_registry_list_quest_devices(s_reg_devices_with_system,
                                                               QUEST_DEVICE_MAX_DEVICES + 4,
                                                               &count,
                                                               false));
    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_EQUAL_STRING("relay", s_reg_devices_with_system[0].id);

    memset(s_reg_devices_with_system, 0, sizeof(s_reg_devices_with_system));
    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_registry_list_quest_devices(s_reg_devices_with_system,
                                                               QUEST_DEVICE_MAX_DEVICES + 4,
                                                               &count,
                                                               true));
    TEST_ASSERT_EQUAL_UINT(5, count);
    TEST_ASSERT_EQUAL_STRING("relay", s_reg_devices_with_system[0].id);
    TEST_ASSERT_EQUAL_STRING(QUEST_DEVICE_SYSTEM_AUDIO_ID, s_reg_devices_with_system[1].id);
    TEST_ASSERT_EQUAL_STRING(QUEST_DEVICE_SYSTEM_RELAY_ID, s_reg_devices_with_system[2].id);
    TEST_ASSERT_EQUAL_STRING(QUEST_DEVICE_SYSTEM_MOSFET_ID, s_reg_devices_with_system[3].id);
    TEST_ASSERT_EQUAL_STRING(QUEST_DEVICE_SYSTEM_IO_ID, s_reg_devices_with_system[4].id);
}

static void test_orchestrator_registry_lists_device_issues_for_saved_device(void)
{
    orch_issue_entry_t issues[ORCH_REGISTRY_MAX_ISSUES] = {0};
    size_t count = 0;

    reg_bootstrap();
    reg_add_device("relay", "Relay", true);

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_registry_list_device_issues("relay",
                                                               issues,
                                                               ORCH_REGISTRY_MAX_ISSUES,
                                                               &count));
    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_EQUAL_STRING("device_offline", issues[0].code);
    TEST_ASSERT_EQUAL(ORCH_ISSUE_SCOPE_DEVICE, issues[0].scope);
    TEST_ASSERT_EQUAL(ORCH_ISSUE_SEVERITY_ERROR, issues[0].severity);
    TEST_ASSERT_EQUAL_STRING("relay", issues[0].device_id);
}

static void test_orchestrator_registry_room_faults_on_selected_scenario_device_issues(void)
{
    const orch_room_entry_t *room = NULL;

    reg_bootstrap();
    memset(&s_reg_snapshot_a, 0, sizeof(s_reg_snapshot_a));
    reg_add_room("room_a", "Room A");
    reg_add_device("relay", "Relay", true);
    reg_add_device("uid_gate", "UID Gate", true);
    reg_add_scenario_with_group_and_wait_devices("scenario_devices", "room_a");
    reg_add_profile("profile_devices", "room_a", "scenario_devices", 60000);
    {
        scenehub_control_result_t result = {0};
        TEST_ASSERT_EQUAL(ESP_OK,
                          scenehub_control_select_profile("test",
                                                          "room_a",
                                                          "profile_devices",
                                                          &result));
        TEST_ASSERT_EQUAL(SCENEHUB_CONTROL_STATUS_DONE, result.status);
        TEST_ASSERT_EQUAL(ESP_OK, result.err);
    }

    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_registry_build_snapshot(&s_reg_snapshot_a));
    TEST_ASSERT_EQUAL_UINT(1, s_reg_snapshot_a.room_count);
    room = &s_reg_snapshot_a.rooms[0];
    TEST_ASSERT_EQUAL_STRING("room_a", room->room_id);
    TEST_ASSERT_EQUAL(ORCH_HEALTH_FAULT, room->health);
    TEST_ASSERT_TRUE(room->scenario_device_count >= 2);
    TEST_ASSERT_TRUE(room->issue_count >= 2);
    TEST_ASSERT_TRUE(room->related_issue_count >= 2);
}

static void test_orchestrator_registry_lists_backend_scenario_step_schemas(void)
{
    orch_room_scenario_step_schema_t schemas[ORCH_ROOM_SCENARIO_MAX_STEP_SCHEMAS] = {0};
    size_t count = 0;

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_registry_list_scenario_step_schemas(schemas,
                                                                       ORCH_ROOM_SCENARIO_MAX_STEP_SCHEMAS,
                                                                       &count));
    TEST_ASSERT_EQUAL_UINT(ORCH_ROOM_SCENARIO_MAX_STEP_SCHEMAS, count);
    TEST_ASSERT_EQUAL_STRING("DEVICE_COMMAND", schemas[0].type);
    TEST_ASSERT_EQUAL_STRING("Device command", schemas[0].label);
    TEST_ASSERT_EQUAL_UINT(3, schemas[0].field_count);
    TEST_ASSERT_EQUAL_STRING("device_id", schemas[0].fields[0].key);
    TEST_ASSERT_TRUE(schemas[0].fields[0].required);
    TEST_ASSERT_EQUAL_STRING("WAIT_DEVICE_EVENT", schemas[2].type);
    TEST_ASSERT_EQUAL_UINT(6, schemas[2].field_count);
    TEST_ASSERT_EQUAL_STRING("allow_operator_skip", schemas[2].fields[4].key);
    TEST_ASSERT_EQUAL_STRING("END_GAME", schemas[count - 1].type);
    TEST_ASSERT_EQUAL_UINT(0, schemas[count - 1].field_count);
}

void register_orchestrator_registry_tests(void)
{
    RUN_TEST(test_orchestrator_registry_rejects_invalid_args);
    RUN_TEST(test_orchestrator_registry_snapshot_collects_rooms_devices_services_and_scenarios);
    RUN_TEST(test_orchestrator_registry_service_fault_creates_system_issue);
    RUN_TEST(test_orchestrator_registry_cache_version_changes_after_invalidate);
    RUN_TEST(test_orchestrator_registry_lists_room_scenarios_and_details);
    RUN_TEST(test_orchestrator_registry_lists_rooms_without_full_snapshot);
    RUN_TEST(test_orchestrator_registry_lists_room_profiles_from_read_model);
    RUN_TEST(test_orchestrator_registry_lists_quest_devices_with_optional_system_entries);
    RUN_TEST(test_orchestrator_registry_lists_backend_scenario_step_schemas);
    RUN_TEST(test_orchestrator_registry_lists_device_issues_for_saved_device);
    RUN_TEST(test_orchestrator_registry_room_faults_on_selected_scenario_device_issues);
}
