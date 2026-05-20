#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "esp_attr.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gm_room_session.h"
#include "mqtt_core.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "scenehub_events.h"

EXT_RAM_BSS_ATTR static room_scenario_t s_scenario;
EXT_RAM_BSS_ATTR static gm_room_session_t s_session;

static void chaos_copy(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static void chaos_bootstrap(void)
{
    static bool initialized = false;

    if (!initialized) {
        TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
        TEST_ASSERT_EQUAL(ESP_OK, mqtt_core_init());
        TEST_ASSERT_EQUAL(ESP_OK, room_catalog_init());
        TEST_ASSERT_EQUAL(ESP_OK, quest_device_init());
        TEST_ASSERT_EQUAL(ESP_OK, room_scenario_init());
        TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_init());
        initialized = true;
    }

    gm_room_session_reset_all();
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_clear());
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_clear());
    room_scenario_reset();
    memset(&s_scenario, 0, sizeof(s_scenario));
    memset(&s_session, 0, sizeof(s_session));
}

static void chaos_add_room(const char *room_id)
{
    room_catalog_entry_t room = {0};
    chaos_copy(room.room_id, sizeof(room.room_id), room_id);
    chaos_copy(room.name, sizeof(room.name), "Chaos room");
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&room));
}

static void chaos_add_device(void)
{
    quest_device_t device = {0};

    chaos_copy(device.id, sizeof(device.id), "relay");
    chaos_copy(device.client_id, sizeof(device.client_id), "relay_client");
    chaos_copy(device.name, sizeof(device.name), "Relay");
    device.enabled = true;

    device.command_count = 1;
    chaos_copy(device.commands[0].id, sizeof(device.commands[0].id), "pulse");
    chaos_copy(device.commands[0].label, sizeof(device.commands[0].label), "Pulse");
    chaos_copy(device.commands[0].capability, sizeof(device.commands[0].capability), "relay");
    chaos_copy(device.commands[0].command, sizeof(device.commands[0].command), "relay.pulse");
    chaos_copy(device.commands[0].default_args_json,
               sizeof(device.commands[0].default_args_json),
               "{\"channel\":1,\"duration_ms\":1000}");
    device.commands[0].manual_allowed = true;
    device.commands[0].scenario_allowed = true;
    device.commands[0].result_required = true;
    device.commands[0].timeout_ms = 3000;
    chaos_copy(device.commands[0].danger_level, sizeof(device.commands[0].danger_level), "normal");

    device.event_count = 3;
    chaos_copy(device.events[0].id, sizeof(device.events[0].id), "door_opened");
    chaos_copy(device.events[0].label, sizeof(device.events[0].label), "Door opened");
    chaos_copy(device.events[0].capability, sizeof(device.events[0].capability), "input");
    chaos_copy(device.events[0].event, sizeof(device.events[0].event), "door_opened");

    chaos_copy(device.events[1].id, sizeof(device.events[1].id), "drawer_opened");
    chaos_copy(device.events[1].label, sizeof(device.events[1].label), "Drawer opened");
    chaos_copy(device.events[1].capability, sizeof(device.events[1].capability), "input");
    chaos_copy(device.events[1].event, sizeof(device.events[1].event), "drawer_opened");

    chaos_copy(device.events[2].id, sizeof(device.events[2].id), "tv_started");
    chaos_copy(device.events[2].label, sizeof(device.events[2].label), "TV started");
    chaos_copy(device.events[2].capability, sizeof(device.events[2].capability), "input");
    chaos_copy(device.events[2].event, sizeof(device.events[2].event), "tv_started");

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&device));
}

static void chaos_init_scenario(const char *id, const char *room_id, const char *name)
{
    memset(&s_scenario, 0, sizeof(s_scenario));
    chaos_copy(s_scenario.id, sizeof(s_scenario.id), id);
    chaos_copy(s_scenario.room_id, sizeof(s_scenario.room_id), room_id);
    chaos_copy(s_scenario.name, sizeof(s_scenario.name), name);
}

static room_scenario_step_t *chaos_add_step(const char *id,
                                            const char *label,
                                            room_scenario_step_type_t type)
{
    room_scenario_step_t *step = NULL;
    TEST_ASSERT_TRUE(s_scenario.step_count < ROOM_SCENARIO_MAX_STEPS);
    step = &s_scenario.steps[s_scenario.step_count++];
    memset(step, 0, sizeof(*step));
    chaos_copy(step->id, sizeof(step->id), id);
    chaos_copy(step->label, sizeof(step->label), label);
    step->type = type;
    step->enabled = true;
    return step;
}

static void chaos_configure_wait_event(room_scenario_wait_device_event_t *wait,
                                       const char *event_id)
{
    chaos_copy(wait->device_id, sizeof(wait->device_id), "relay");
    chaos_copy(wait->event_id, sizeof(wait->event_id), event_id);
}

static void chaos_configure_set_flag(room_scenario_step_t *step, const char *name, bool value)
{
    chaos_copy(step->data.set_flag.name, sizeof(step->data.set_flag.name), name);
    step->data.set_flag.value = value;
}

static void chaos_add_and_start_selected_scenario(const char *room_id)
{
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&s_scenario));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_select_scenario(room_id, s_scenario.id));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_start(room_id));
}

static void chaos_get_session(const char *room_id)
{
    memset(&s_session, 0, sizeof(s_session));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_get(room_id, &s_session));
}

static int chaos_find_flag(const gm_room_session_t *session, const char *name)
{
    if (!session || !name) {
        return -1;
    }
    for (uint8_t i = 0; i < session->scenario_flag_count; ++i) {
        if (strcmp(session->scenario_flags[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static esp_err_t chaos_post_device_event_expect(const char *action_id)
{
    scenehub_event_t message = {0};
    message.type = SCENEHUB_EVENT_DEVICE_CONTROL;
    message.payload_type = SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL;
    chaos_copy(message.payload, sizeof(message.payload), action_id);
    chaos_copy(message.data.device_control.device_id,
               sizeof(message.data.device_control.device_id),
               "relay_client");
    chaos_copy(message.data.device_control.action_id,
               sizeof(message.data.device_control.action_id),
               action_id);
    chaos_copy(message.data.device_control.source,
               sizeof(message.data.device_control.source),
               "event");
    return gm_room_session_scenario_on_event(&message);
}

static esp_err_t chaos_post_command_result_expect(const char *request_id, const char *status)
{
    scenehub_event_t message = {0};
    message.type = SCENEHUB_EVENT_DEVICE_CONTROL;
    message.payload_type = SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL;
    chaos_copy(message.payload, sizeof(message.payload), status);
    chaos_copy(message.data.device_control.device_id,
               sizeof(message.data.device_control.device_id),
               "relay_client");
    chaos_copy(message.data.device_control.action_id,
               sizeof(message.data.device_control.action_id),
               request_id);
    chaos_copy(message.data.device_control.source,
               sizeof(message.data.device_control.source),
               "result");
    return gm_room_session_scenario_on_event(&message);
}

static esp_err_t chaos_post_status_noise_expect(const char *state_text)
{
    scenehub_event_t message = {0};
    TEST_ASSERT_EQUAL(ESP_OK,
                      scenehub_event_make_device_status(&message,
                                                        "relay_client",
                                                        "online",
                                                        "ok",
                                                        state_text,
                                                        1000));
    return gm_room_session_scenario_on_event(&message);
}

static esp_err_t chaos_post_runtime_noise_expect(const char *state_text, bool active)
{
    scenehub_event_t message = {0};
    TEST_ASSERT_EQUAL(ESP_OK,
                      scenehub_event_make_device_runtime(&message,
                                                         "relay_client",
                                                         "control_status",
                                                         state_text,
                                                         active,
                                                         1000));
    return gm_room_session_scenario_on_event(&message);
}

static void test_wait_device_event_survives_status_and_runtime_spam(void)
{
    room_scenario_step_t *step = NULL;

    chaos_bootstrap();
    chaos_add_room("room_a");
    chaos_add_device();
    chaos_init_scenario("scenario_noise_wait", "room_a", "Noise wait");

    step = chaos_add_step("wait_event", "Wait event", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    chaos_configure_wait_event(&step->data.wait_device_event, "door_opened");
    step = chaos_add_step("flag", "Flag", ROOM_SCENARIO_STEP_SET_FLAG);
    chaos_configure_set_flag(step, "advanced", true);

    chaos_add_and_start_selected_scenario("room_a");

    for (uint16_t i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                          chaos_post_status_noise_expect((i % 2U) == 0U ? "idle" : "busy"));
        TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                          chaos_post_runtime_noise_expect((i % 3U) == 0U ? "active" : "idle",
                                                          (i % 3U) == 0U));
    }

    chaos_get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT, s_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_EQUAL(-1, chaos_find_flag(&s_session, "advanced"));

    TEST_ASSERT_EQUAL(ESP_OK, chaos_post_device_event_expect("door_opened"));
    chaos_get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_TRUE(chaos_find_flag(&s_session, "advanced") >= 0);
}

static void test_wait_device_event_does_not_match_other_action_from_same_device(void)
{
    room_scenario_step_t *step = NULL;

    chaos_bootstrap();
    chaos_add_room("room_a");
    chaos_add_device();
    chaos_init_scenario("scenario_wait_exact_action", "room_a", "Wait exact action");

    step = chaos_add_step("wait_event", "Wait event", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    chaos_configure_wait_event(&step->data.wait_device_event, "door_opened");
    step = chaos_add_step("flag", "Flag", ROOM_SCENARIO_STEP_SET_FLAG);
    chaos_configure_set_flag(step, "advanced", true);

    chaos_add_and_start_selected_scenario("room_a");

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, chaos_post_device_event_expect("drawer_opened"));
    chaos_get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT, s_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_EQUAL(-1, chaos_find_flag(&s_session, "advanced"));

    TEST_ASSERT_EQUAL(ESP_OK, chaos_post_device_event_expect("door_opened"));
    chaos_get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_TRUE(chaos_find_flag(&s_session, "advanced") >= 0);
}

static void test_wait_all_device_events_accepts_shuffled_order_and_duplicates(void)
{
    room_scenario_step_t *step = NULL;

    chaos_bootstrap();
    chaos_add_room("room_a");
    chaos_add_device();
    chaos_init_scenario("scenario_wait_all_shuffle", "room_a", "Wait all shuffle");

    step = chaos_add_step("wait_all", "Wait all", ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS);
    step->data.wait_all_device_events.event_count = 3;
    chaos_configure_wait_event(&step->data.wait_all_device_events.events[0], "door_opened");
    chaos_configure_wait_event(&step->data.wait_all_device_events.events[1], "drawer_opened");
    chaos_configure_wait_event(&step->data.wait_all_device_events.events[2], "tv_started");
    step = chaos_add_step("flag", "Flag", ROOM_SCENARIO_STEP_SET_FLAG);
    chaos_configure_set_flag(step, "all_done", true);

    chaos_add_and_start_selected_scenario("room_a");

    TEST_ASSERT_EQUAL(ESP_OK, chaos_post_device_event_expect("tv_started"));
    TEST_ASSERT_EQUAL(ESP_OK, chaos_post_device_event_expect("door_opened"));
    TEST_ASSERT_EQUAL(ESP_OK, chaos_post_device_event_expect("door_opened"));

    chaos_get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_TRUE(s_session.branch_runtimes[0].wait_event_matched[0]);
    TEST_ASSERT_FALSE(s_session.branch_runtimes[0].wait_event_matched[1]);
    TEST_ASSERT_TRUE(s_session.branch_runtimes[0].wait_event_matched[2]);
    TEST_ASSERT_EQUAL(-1, chaos_find_flag(&s_session, "all_done"));

    TEST_ASSERT_EQUAL(ESP_OK, chaos_post_device_event_expect("drawer_opened"));
    chaos_get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_TRUE(chaos_find_flag(&s_session, "all_done") >= 0);
}

static void test_duplicate_command_result_after_success_is_ignored(void)
{
    room_scenario_step_t *step = NULL;
    char request_id[48] = {0};

    chaos_bootstrap();
    chaos_add_room("room_a");
    chaos_add_device();
    chaos_init_scenario("scenario_duplicate_result", "room_a", "Duplicate result");

    step = chaos_add_step("pulse", "Pulse relay", ROOM_SCENARIO_STEP_DEVICE_COMMAND);
    chaos_copy(step->data.device_command.device_id,
               sizeof(step->data.device_command.device_id),
               "relay");
    chaos_copy(step->data.device_command.command_id,
               sizeof(step->data.device_command.command_id),
               "pulse");
    step = chaos_add_step("flag", "Flag", ROOM_SCENARIO_STEP_SET_FLAG);
    chaos_configure_set_flag(step, "command_done", true);

    chaos_add_and_start_selected_scenario("room_a");
    chaos_get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT,
                      s_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_TRUE(s_session.branch_runtimes[0].wait_event_type[0] != '\0');
    chaos_copy(request_id, sizeof(request_id), s_session.branch_runtimes[0].wait_event_type);

    TEST_ASSERT_EQUAL(ESP_OK, chaos_post_command_result_expect(request_id, "accepted"));
    TEST_ASSERT_EQUAL(ESP_OK, chaos_post_command_result_expect(request_id, "done"));

    chaos_get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_TRUE(chaos_find_flag(&s_session, "command_done") >= 0);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, chaos_post_command_result_expect(request_id, "failed"));
    chaos_get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_TRUE(chaos_find_flag(&s_session, "command_done") >= 0);
}

static void test_late_command_result_after_timeout_is_ignored(void)
{
    room_scenario_step_t *step = NULL;
    quest_device_t device = {0};
    char request_id[48] = {0};

    chaos_bootstrap();
    chaos_add_room("room_a");
    chaos_add_device();
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get("relay", &device));
    device.commands[0].timeout_ms = 1;
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&device));

    chaos_init_scenario("scenario_late_result", "room_a", "Late result");

    step = chaos_add_step("pulse", "Pulse relay", ROOM_SCENARIO_STEP_DEVICE_COMMAND);
    chaos_copy(step->data.device_command.device_id,
               sizeof(step->data.device_command.device_id),
               "relay");
    chaos_copy(step->data.device_command.command_id,
               sizeof(step->data.device_command.command_id),
               "pulse");
    step = chaos_add_step("flag", "Flag", ROOM_SCENARIO_STEP_SET_FLAG);
    chaos_configure_set_flag(step, "command_done", true);

    chaos_add_and_start_selected_scenario("room_a");
    chaos_get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT,
                      s_session.branch_runtimes[0].wait_type);
    chaos_copy(request_id, sizeof(request_id), s_session.branch_runtimes[0].wait_event_type);

    vTaskDelay(pdMS_TO_TICKS(20));
    gm_room_session_runtime_process_pending_work();

    chaos_get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_ERROR, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(-1, chaos_find_flag(&s_session, "command_done"));

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, chaos_post_command_result_expect(request_id, "done"));
    chaos_get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_ERROR, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(-1, chaos_find_flag(&s_session, "command_done"));
}

void register_gm_room_session_runtime_chaos_tests(void)
{
    RUN_TEST(test_wait_device_event_survives_status_and_runtime_spam);
    RUN_TEST(test_wait_device_event_does_not_match_other_action_from_same_device);
    RUN_TEST(test_wait_all_device_events_accepts_shuffled_order_and_duplicates);
    RUN_TEST(test_duplicate_command_result_after_success_is_ignored);
    RUN_TEST(test_late_command_result_after_timeout_is_ignored);
}
