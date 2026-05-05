#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "unity.h"

#include "cJSON.h"
#include "device_control_ingest.h"
#include "event_bus.h"
#include "esp_attr.h"
#include "gm_api.h"
#include "gm_game_profile.h"
#include "orchestrator_registry.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "service_status.h"

EXT_RAM_BSS_ATTR static room_scenario_t s_flow_scenario;
EXT_RAM_BSS_ATTR static gm_room_state_view_t s_flow_state;
EXT_RAM_BSS_ATTR static gm_room_session_t s_flow_session;
EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_flow_snapshot;

static void flow_copy(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static void flow_bootstrap(void)
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
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_init());
    gm_room_session_reset_all();
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_registry_init());
    orchestrator_registry_invalidate();
    memset(&s_flow_scenario, 0, sizeof(s_flow_scenario));
    memset(&s_flow_state, 0, sizeof(s_flow_state));
    memset(&s_flow_session, 0, sizeof(s_flow_session));
    memset(&s_flow_snapshot, 0, sizeof(s_flow_snapshot));
}

static void flow_add_room(void)
{
    room_catalog_entry_t room = {0};
    flow_copy(room.room_id, sizeof(room.room_id), "room_flow");
    flow_copy(room.name, sizeof(room.name), "Flow Room");
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&room));
}

static void flow_add_relay_device(void)
{
    quest_device_t device = {0};

    flow_copy(device.id, sizeof(device.id), "relay");
    flow_copy(device.client_id, sizeof(device.client_id), "relay_client");
    flow_copy(device.name, sizeof(device.name), "Relay");
    device.enabled = true;

    device.event_count = 2;
    flow_copy(device.events[0].id, sizeof(device.events[0].id), "door_opened");
    flow_copy(device.events[0].label, sizeof(device.events[0].label), "Door opened");
    flow_copy(device.events[0].capability, sizeof(device.events[0].capability), "input");
    flow_copy(device.events[0].event, sizeof(device.events[0].event), "door_opened");

    flow_copy(device.events[1].id, sizeof(device.events[1].id), "drawer_opened");
    flow_copy(device.events[1].label, sizeof(device.events[1].label), "Drawer opened");
    flow_copy(device.events[1].capability, sizeof(device.events[1].capability), "input");
    flow_copy(device.events[1].event, sizeof(device.events[1].event), "drawer_opened");

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&device));
}

static void flow_add_scenario(void)
{
    room_scenario_step_t *step = NULL;

    memset(&s_flow_scenario, 0, sizeof(s_flow_scenario));
    flow_copy(s_flow_scenario.id, sizeof(s_flow_scenario.id), "scenario_flow");
    flow_copy(s_flow_scenario.name, sizeof(s_flow_scenario.name), "Door flow");
    flow_copy(s_flow_scenario.room_id, sizeof(s_flow_scenario.room_id), "room_flow");

    step = &s_flow_scenario.steps[s_flow_scenario.step_count++];
    flow_copy(step->id, sizeof(step->id), "wait_door");
    flow_copy(step->label, sizeof(step->label), "Wait door");
    step->enabled = true;
    step->type = ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT;
    flow_copy(step->data.wait_device_event.device_id,
              sizeof(step->data.wait_device_event.device_id),
              "relay");
    flow_copy(step->data.wait_device_event.event_id,
              sizeof(step->data.wait_device_event.event_id),
              "door_opened");

    step = &s_flow_scenario.steps[s_flow_scenario.step_count++];
    flow_copy(step->id, sizeof(step->id), "mark_door");
    flow_copy(step->label, sizeof(step->label), "Mark door");
    step->enabled = true;
    step->type = ROOM_SCENARIO_STEP_SET_FLAG;
    flow_copy(step->data.set_flag.name, sizeof(step->data.set_flag.name), "door_opened_seen");
    step->data.set_flag.value = true;

    step = &s_flow_scenario.steps[s_flow_scenario.step_count++];
    flow_copy(step->id, sizeof(step->id), "finish");
    flow_copy(step->label, sizeof(step->label), "Finish");
    step->enabled = true;
    step->type = ROOM_SCENARIO_STEP_END_GAME;

    flow_copy(s_flow_scenario.branches[0].id, sizeof(s_flow_scenario.branches[0].id), "main");
    flow_copy(s_flow_scenario.branches[0].name, sizeof(s_flow_scenario.branches[0].name), "Main");
    s_flow_scenario.branches[0].enabled = true;
    s_flow_scenario.branches[0].type = ROOM_SCENARIO_BRANCH_NORMAL;
    s_flow_scenario.branches[0].required_for_completion = true;
    s_flow_scenario.branches[0].step_start_index = 0;
    s_flow_scenario.branches[0].step_count = s_flow_scenario.step_count;
    s_flow_scenario.branch_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&s_flow_scenario));
}

static void flow_add_reactive_scenario(void)
{
    room_scenario_step_t *step = NULL;

    memset(&s_flow_scenario, 0, sizeof(s_flow_scenario));
    flow_copy(s_flow_scenario.id, sizeof(s_flow_scenario.id), "scenario_reactive_flow");
    flow_copy(s_flow_scenario.name, sizeof(s_flow_scenario.name), "Reactive door flow");
    flow_copy(s_flow_scenario.room_id, sizeof(s_flow_scenario.room_id), "room_flow");

    step = &s_flow_scenario.steps[s_flow_scenario.step_count++];
    flow_copy(step->id, sizeof(step->id), "main_wait");
    flow_copy(step->label, sizeof(step->label), "Main wait");
    step->enabled = true;
    step->type = ROOM_SCENARIO_STEP_WAIT_TIME;
    step->data.wait_time.duration_ms = 60000;

    step = &s_flow_scenario.steps[s_flow_scenario.step_count++];
    flow_copy(step->id, sizeof(step->id), "react_wait");
    flow_copy(step->label, sizeof(step->label), "React wait");
    step->enabled = true;
    step->type = ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT;
    flow_copy(step->data.wait_device_event.device_id,
              sizeof(step->data.wait_device_event.device_id),
              "relay");
    flow_copy(step->data.wait_device_event.event_id,
              sizeof(step->data.wait_device_event.event_id),
              "door_opened");

    step = &s_flow_scenario.steps[s_flow_scenario.step_count++];
    flow_copy(step->id, sizeof(step->id), "react_mark");
    flow_copy(step->label, sizeof(step->label), "React mark");
    step->enabled = true;
    step->type = ROOM_SCENARIO_STEP_SET_FLAG;
    flow_copy(step->data.set_flag.name, sizeof(step->data.set_flag.name), "reactive_door_seen");
    step->data.set_flag.value = true;

    flow_copy(s_flow_scenario.branches[0].id, sizeof(s_flow_scenario.branches[0].id), "main");
    flow_copy(s_flow_scenario.branches[0].name, sizeof(s_flow_scenario.branches[0].name), "Main");
    s_flow_scenario.branches[0].enabled = true;
    s_flow_scenario.branches[0].type = ROOM_SCENARIO_BRANCH_NORMAL;
    s_flow_scenario.branches[0].required_for_completion = true;
    s_flow_scenario.branches[0].step_start_index = 0;
    s_flow_scenario.branches[0].step_count = 1;

    flow_copy(s_flow_scenario.branches[1].id, sizeof(s_flow_scenario.branches[1].id), "react_door");
    flow_copy(s_flow_scenario.branches[1].name, sizeof(s_flow_scenario.branches[1].name), "React door");
    s_flow_scenario.branches[1].enabled = true;
    s_flow_scenario.branches[1].type = ROOM_SCENARIO_BRANCH_REACTIVE;
    s_flow_scenario.branches[1].required_for_completion = false;
    s_flow_scenario.branches[1].run_once = true;
    s_flow_scenario.branches[1].step_start_index = 1;
    s_flow_scenario.branches[1].step_count = 2;
    s_flow_scenario.branch_count = 2;

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&s_flow_scenario));
}

static void flow_add_profile(void)
{
    gm_game_profile_t profile = {0};
    flow_copy(profile.id, sizeof(profile.id), "profile_flow");
    flow_copy(profile.name, sizeof(profile.name), "Flow Profile");
    flow_copy(profile.room_id, sizeof(profile.room_id), "room_flow");
    flow_copy(profile.scenario_id, sizeof(profile.scenario_id), "scenario_flow");
    profile.duration_ms = 90000;
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_upsert(&profile));
}

static void flow_add_profile_for_scenario(const char *profile_id, const char *scenario_id)
{
    gm_game_profile_t profile = {0};
    flow_copy(profile.id, sizeof(profile.id), profile_id);
    flow_copy(profile.name, sizeof(profile.name), "Flow Profile");
    flow_copy(profile.room_id, sizeof(profile.room_id), "room_flow");
    flow_copy(profile.scenario_id, sizeof(profile.scenario_id), scenario_id);
    profile.duration_ms = 90000;
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_upsert(&profile));
}

static void flow_add_pack(void)
{
    flow_add_room();
    flow_add_relay_device();
    flow_add_scenario();
    flow_add_profile();
}

static void flow_post_text_event(const char *payload)
{
    event_bus_message_t message = {0};
    message.type = EVENT_DEVICE_CONTROL;
    message.payload_type = EVENT_BUS_PAYLOAD_DEVICE_CONTROL;
    flow_copy(message.payload, sizeof(message.payload), payload);
    flow_copy(message.data.device_control.device_id,
              sizeof(message.data.device_control.device_id),
              "relay_client");
    flow_copy(message.data.device_control.action_id,
              sizeof(message.data.device_control.action_id),
              payload);
    flow_copy(message.data.device_control.source,
              sizeof(message.data.device_control.source),
              "event");
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_on_event(&message));
}

static esp_err_t flow_post_text_event_expect_err(const char *payload)
{
    event_bus_message_t message = {0};
    message.type = EVENT_DEVICE_CONTROL;
    message.payload_type = EVENT_BUS_PAYLOAD_DEVICE_CONTROL;
    flow_copy(message.payload, sizeof(message.payload), payload);
    flow_copy(message.data.device_control.device_id,
              sizeof(message.data.device_control.device_id),
              "relay_client");
    flow_copy(message.data.device_control.action_id,
              sizeof(message.data.device_control.action_id),
              payload);
    flow_copy(message.data.device_control.source,
              sizeof(message.data.device_control.source),
              "event");
    return gm_room_session_scenario_on_event(&message);
}

static int flow_find_flag(const gm_room_session_t *session, const char *name)
{
    for (uint8_t i = 0; session && i < session->scenario_flag_count; ++i) {
        if (strcmp(session->scenario_flags[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void test_integration_quest_flow_runs_profile_scenario_from_device_event(void)
{
    int flag_index = -1;

    flow_bootstrap();
    flow_add_pack();

    TEST_ASSERT_EQUAL(ESP_OK, gm_api_select_profile("room_flow", "profile_flow"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_game_start("room_flow"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_get_room_state("room_flow", &s_flow_state));
    TEST_ASSERT_TRUE(s_flow_state.session_present);
    TEST_ASSERT_EQUAL(GM_SESSION_RUNNING, s_flow_state.session_state);
    TEST_ASSERT_EQUAL(GM_TIMER_RUNNING, s_flow_state.timer_state);
    TEST_ASSERT_EQUAL_STRING("profile_flow", s_flow_state.selected_profile_id);
    TEST_ASSERT_EQUAL_STRING("scenario_flow", s_flow_state.running_scenario_id);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_flow_state.scenario_runtime_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT, s_flow_state.scenario_wait_type);
    TEST_ASSERT_EQUAL_STRING("door_opened", s_flow_state.scenario_wait_event_type);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, flow_post_text_event_expect_err("drawer_opened"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_get_room_state("room_flow", &s_flow_state));
    TEST_ASSERT_EQUAL(GM_SESSION_RUNNING, s_flow_state.session_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_flow_state.scenario_runtime_state);

    flow_post_text_event("door_opened");
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_get_room_state("room_flow", &s_flow_state));
    TEST_ASSERT_EQUAL(GM_SESSION_FINISHED, s_flow_state.session_state);
    TEST_ASSERT_EQUAL(GM_TIMER_FINISHED, s_flow_state.timer_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_flow_state.scenario_runtime_state);

    TEST_ASSERT_EQUAL(ESP_OK, gm_api_room_session_get("room_flow", &s_flow_session));
    flag_index = flow_find_flag(&s_flow_session, "door_opened_seen");
    TEST_ASSERT_TRUE(flag_index >= 0);
    TEST_ASSERT_TRUE(s_flow_session.scenario_flags[flag_index].value);
}

static void test_integration_persistence_round_trip_builds_orchestrator_snapshot(void)
{
    cJSON *rooms = NULL;
    cJSON *devices = NULL;
    cJSON *scenarios = NULL;
    cJSON *profiles = NULL;

    flow_bootstrap();
    flow_add_pack();

    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_export_json(&rooms));
    TEST_ASSERT_NOT_NULL(rooms);
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_export_json(&devices));
    TEST_ASSERT_NOT_NULL(devices);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_export_json(&scenarios));
    TEST_ASSERT_NOT_NULL(scenarios);
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_export_json(&profiles));
    TEST_ASSERT_NOT_NULL(profiles);

    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_clear());
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_clear());
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_clear());
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_clear());
    gm_room_session_reset_all();

    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_import_json(rooms));
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_import_json(devices));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_import_json(scenarios));
    TEST_ASSERT_EQUAL(ESP_OK, gm_game_profile_import_json(profiles));
    cJSON_Delete(rooms);
    cJSON_Delete(devices);
    cJSON_Delete(scenarios);
    cJSON_Delete(profiles);

    orchestrator_registry_invalidate();
    TEST_ASSERT_EQUAL(ESP_OK, orchestrator_registry_build_snapshot(&s_flow_snapshot));
    TEST_ASSERT_EQUAL_UINT(1, s_flow_snapshot.room_count);
    TEST_ASSERT_EQUAL_STRING("room_flow", s_flow_snapshot.rooms[0].room_id);
    TEST_ASSERT_EQUAL_STRING("Flow Room", s_flow_snapshot.rooms[0].title);
    TEST_ASSERT_EQUAL_UINT(1, s_flow_snapshot.device_count);
    TEST_ASSERT_EQUAL_STRING("relay", s_flow_snapshot.devices[0].device_id);
    TEST_ASSERT_EQUAL_UINT(1, s_flow_snapshot.room_scenario_count);
    TEST_ASSERT_EQUAL_STRING("scenario_flow", s_flow_snapshot.room_scenarios[0].id);

    TEST_ASSERT_EQUAL(ESP_OK, gm_api_select_profile("room_flow", "profile_flow"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_game_start("room_flow"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_get_room_state("room_flow", &s_flow_state));
    TEST_ASSERT_EQUAL_STRING("profile_flow", s_flow_state.selected_profile_id);
    TEST_ASSERT_EQUAL_STRING("scenario_flow", s_flow_state.running_scenario_id);
}

static void test_integration_reactive_branch_fires_from_device_event_without_finishing_main(void)
{
    int flag_index = -1;

    flow_bootstrap();
    flow_add_room();
    flow_add_relay_device();
    flow_add_reactive_scenario();
    flow_add_profile_for_scenario("profile_reactive", "scenario_reactive_flow");

    TEST_ASSERT_EQUAL(ESP_OK, gm_api_select_profile("room_flow", "profile_reactive"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_game_start("room_flow"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_room_session_get("room_flow", &s_flow_session));
    TEST_ASSERT_EQUAL_UINT(2, s_flow_session.branch_runtime_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_flow_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_flow_session.branch_runtimes[1].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_TIME, s_flow_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT, s_flow_session.branch_runtimes[1].wait_type);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, flow_post_text_event_expect_err("drawer_opened"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_room_session_get("room_flow", &s_flow_session));
    TEST_ASSERT_EQUAL(-1, flow_find_flag(&s_flow_session, "reactive_door_seen"));

    flow_post_text_event("door_opened");
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_room_session_get("room_flow", &s_flow_session));
    TEST_ASSERT_EQUAL(GM_SESSION_RUNNING, s_flow_session.state);
    TEST_ASSERT_EQUAL(GM_TIMER_RUNNING, s_flow_session.timer.state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_flow_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_flow_session.branch_runtimes[1].scenario_state);
    TEST_ASSERT_TRUE(s_flow_session.branch_runtimes[1].fired_once);
    flag_index = flow_find_flag(&s_flow_session, "reactive_door_seen");
    TEST_ASSERT_TRUE(flag_index >= 0);
    TEST_ASSERT_TRUE(s_flow_session.scenario_flags[flag_index].value);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, flow_post_text_event_expect_err("door_opened"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_api_room_session_get("room_flow", &s_flow_session));
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_flow_session.branch_runtimes[1].scenario_state);
}

void register_integration_quest_flow_tests(void)
{
    RUN_TEST(test_integration_quest_flow_runs_profile_scenario_from_device_event);
    RUN_TEST(test_integration_persistence_round_trip_builds_orchestrator_snapshot);
    RUN_TEST(test_integration_reactive_branch_fires_from_device_event_without_finishing_main);
}
