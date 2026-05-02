#include <string.h>

#include "unity.h"

#include "device_control_ingest.h"
#include "event_bus.h"
#include "esp_attr.h"
#include "gm_game_profile.h"
#include "orchestrator_registry.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "service_status.h"

EXT_RAM_BSS_ATTR static room_scenario_t s_reg_scenario;
EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_reg_snapshot_a;
EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_reg_snapshot_b;
EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_reg_snapshot_c;
EXT_RAM_BSS_ATTR static orch_device_entry_t s_reg_device;
EXT_RAM_BSS_ATTR static orch_room_scenario_entry_t s_reg_scenarios[2];
EXT_RAM_BSS_ATTR static orch_room_scenario_detail_t s_reg_details[1];

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
    quest_device_t device = {0};
    reg_copy(device.id, sizeof(device.id), id);
    reg_copy(device.client_id, sizeof(device.client_id), id);
    reg_copy(device.name, sizeof(device.name), name);
    device.enabled = enabled;
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&device));
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
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&s_reg_scenario));
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
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_init());
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

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_registry_build_snapshot(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_registry_get_device(NULL, &s_reg_device));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_registry_get_device("", &s_reg_device));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, orchestrator_registry_get_device("device", NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_scenarios(NULL, s_reg_scenarios, 1, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_scenarios("room", NULL, 1, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_scenarios("room", s_reg_scenarios, 0, &count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      orchestrator_registry_list_room_scenarios("room", s_reg_scenarios, 1, NULL));
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

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                      orchestrator_registry_list_room_scenarios("missing", s_reg_scenarios, 2, &count));
}

void register_orchestrator_registry_tests(void)
{
    RUN_TEST(test_orchestrator_registry_rejects_invalid_args);
    RUN_TEST(test_orchestrator_registry_snapshot_collects_rooms_devices_services_and_scenarios);
    RUN_TEST(test_orchestrator_registry_cache_version_changes_after_invalidate);
    RUN_TEST(test_orchestrator_registry_lists_room_scenarios_and_details);
}
