#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "esp_attr.h"
#include "esp_timer.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gm_room_session.h"
#include "mqtt_core.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "../../../components/gm_core/session/gm_room_session_commands_internal.h"
#include "../../../components/gm_core/session/gm_room_session_internal.h"
#include "../../../components/gm_core/session/gm_room_session_projection_internal.h"
#include "../../../components/gm_core/session/gm_room_session_reactive_internal.h"

EXT_RAM_BSS_ATTR static room_scenario_t s_scenario;
EXT_RAM_BSS_ATTR static room_scenario_t s_updated_scenario;
EXT_RAM_BSS_ATTR static gm_room_session_t s_session;

static void test_copy(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static void session_test_bootstrap(void)
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
    memset(&s_updated_scenario, 0, sizeof(s_updated_scenario));
    memset(&s_session, 0, sizeof(s_session));
}

static void add_room(const char *room_id)
{
    room_catalog_entry_t room = {0};
    test_copy(room.room_id, sizeof(room.room_id), room_id);
    test_copy(room.name, sizeof(room.name), "Test room");
    TEST_ASSERT_EQUAL(ESP_OK, room_catalog_upsert(&room));
}

static void add_device_with_events(void)
{
    quest_device_t device = {0};

    test_copy(device.id, sizeof(device.id), "relay");
    test_copy(device.client_id, sizeof(device.client_id), "relay_client");
    test_copy(device.name, sizeof(device.name), "Relay");
    device.enabled = true;

    device.command_count = 1;
    test_copy(device.commands[0].id, sizeof(device.commands[0].id), "pulse");
    test_copy(device.commands[0].label, sizeof(device.commands[0].label), "Pulse");
    test_copy(device.commands[0].capability, sizeof(device.commands[0].capability), "relay");
    test_copy(device.commands[0].command, sizeof(device.commands[0].command), "relay.pulse");
    test_copy(device.commands[0].default_args_json,
              sizeof(device.commands[0].default_args_json),
              "{\"channel\":1,\"duration_ms\":1000}");
    device.commands[0].result_required = true;
    device.commands[0].manual_allowed = true;
    device.commands[0].scenario_allowed = true;
    device.commands[0].timeout_ms = 3000;
    test_copy(device.commands[0].danger_level, sizeof(device.commands[0].danger_level), "normal");

    device.event_count = 4;
    test_copy(device.events[0].id, sizeof(device.events[0].id), "door_opened");
    test_copy(device.events[0].label, sizeof(device.events[0].label), "Door opened");
    test_copy(device.events[0].capability, sizeof(device.events[0].capability), "input");
    test_copy(device.events[0].event, sizeof(device.events[0].event), "door_opened");

    test_copy(device.events[1].id, sizeof(device.events[1].id), "drawer_opened");
    test_copy(device.events[1].label, sizeof(device.events[1].label), "Drawer opened");
    test_copy(device.events[1].capability, sizeof(device.events[1].capability), "input");
    test_copy(device.events[1].event, sizeof(device.events[1].event), "drawer_opened");

    test_copy(device.events[2].id, sizeof(device.events[2].id), "tv_started");
    test_copy(device.events[2].label, sizeof(device.events[2].label), "TV started");
    test_copy(device.events[2].capability, sizeof(device.events[2].capability), "input");
    test_copy(device.events[2].event, sizeof(device.events[2].event), "tv_started");

    test_copy(device.events[3].id, sizeof(device.events[3].id), "sequence_invalid");
    test_copy(device.events[3].label, sizeof(device.events[3].label), "UID sequence invalid");
    test_copy(device.events[3].capability, sizeof(device.events[3].capability), "uid");
    test_copy(device.events[3].event, sizeof(device.events[3].event), "uid.sequence_invalid");

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&device));

    memset(&device, 0, sizeof(device));
    test_copy(device.id, sizeof(device.id), "motion");
    test_copy(device.client_id, sizeof(device.client_id), "motion");
    test_copy(device.name, sizeof(device.name), "Motion");
    device.enabled = true;
    device.event_count = 1;
    test_copy(device.events[0].id, sizeof(device.events[0].id), "motion.detected");
    test_copy(device.events[0].label, sizeof(device.events[0].label), "Motion detected");
    test_copy(device.events[0].capability, sizeof(device.events[0].capability), "sensor");
    test_copy(device.events[0].event, sizeof(device.events[0].event), "motion.detected");

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&device));
}

static void add_non_result_device(const char *device_id, const char *client_id)
{
    quest_device_t device = {0};

    test_copy(device.id, sizeof(device.id), device_id);
    test_copy(device.client_id, sizeof(device.client_id), client_id);
    test_copy(device.name, sizeof(device.name), "No result device");
    device.enabled = true;

    device.command_count = 1;
    test_copy(device.commands[0].id, sizeof(device.commands[0].id), "set");
    test_copy(device.commands[0].label, sizeof(device.commands[0].label), "Set");
    test_copy(device.commands[0].capability, sizeof(device.commands[0].capability), "output");
    test_copy(device.commands[0].command, sizeof(device.commands[0].command), "output.set");
    test_copy(device.commands[0].default_args_json,
              sizeof(device.commands[0].default_args_json),
              "{\"value\":true}");
    device.commands[0].manual_allowed = true;
    device.commands[0].scenario_allowed = true;
    test_copy(device.commands[0].danger_level, sizeof(device.commands[0].danger_level), "normal");

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&device));
}

static room_scenario_step_t *add_step(room_scenario_t *scenario,
                                      const char *id,
                                      const char *label,
                                      room_scenario_step_type_t type)
{
    room_scenario_step_t *step = NULL;
    TEST_ASSERT_TRUE(scenario->step_count < ROOM_SCENARIO_MAX_STEPS);
    step = &scenario->steps[scenario->step_count++];
    memset(step, 0, sizeof(*step));
    test_copy(step->id, sizeof(step->id), id);
    test_copy(step->label, sizeof(step->label), label);
    step->type = type;
    step->enabled = true;
    return step;
}

static void init_scenario(room_scenario_t *scenario, const char *id, const char *room_id, const char *name)
{
    memset(scenario, 0, sizeof(*scenario));
    test_copy(scenario->id, sizeof(scenario->id), id);
    test_copy(scenario->room_id, sizeof(scenario->room_id), room_id);
    test_copy(scenario->name, sizeof(scenario->name), name);
}

static void set_branch(room_scenario_t *scenario,
                       size_t index,
                       const char *id,
                       const char *name,
                       room_scenario_branch_type_t type,
                       uint16_t start,
                       uint16_t count)
{
    room_scenario_branch_t *branch = NULL;
    TEST_ASSERT_TRUE(index < ROOM_SCENARIO_MAX_BRANCHES);
    branch = &scenario->branches[index];
    memset(branch, 0, sizeof(*branch));
    test_copy(branch->id, sizeof(branch->id), id);
    test_copy(branch->name, sizeof(branch->name), name);
    branch->type = type;
    branch->enabled = true;
    branch->required_for_completion = (type == ROOM_SCENARIO_BRANCH_NORMAL);
    branch->step_start_index = start;
    branch->step_count = count;
    if (scenario->branch_count < index + 1) {
        scenario->branch_count = index + 1;
    }
}

static void configure_wait_event(room_scenario_wait_device_event_t *wait,
                                 const char *event_id)
{
    test_copy(wait->device_id, sizeof(wait->device_id), "relay");
    test_copy(wait->event_id, sizeof(wait->event_id), event_id);
}

static void configure_set_flag(room_scenario_step_t *step, const char *name, bool value)
{
    test_copy(step->data.set_flag.name, sizeof(step->data.set_flag.name), name);
    step->data.set_flag.value = value;
}

static void configure_wait_flag(room_scenario_step_t *step,
                                uint8_t index,
                                const char *name,
                                bool value)
{
    TEST_ASSERT_TRUE(index < ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS);
    if (step->data.wait_flags.flag_count < index + 1) {
        step->data.wait_flags.flag_count = index + 1;
    }
    test_copy(step->data.wait_flags.flags[index].name,
              sizeof(step->data.wait_flags.flags[index].name),
              name);
    step->data.wait_flags.flags[index].value = value;
}

static void post_text_event(const char *payload)
{
    scenehub_event_t message = {0};
    message.type = SCENEHUB_EVENT_DEVICE_CONTROL;
    message.payload_type = SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL;
    test_copy(message.payload, sizeof(message.payload), payload);
    test_copy(message.data.device_control.device_id,
              sizeof(message.data.device_control.device_id),
              "relay_client");
    test_copy(message.data.device_control.action_id,
              sizeof(message.data.device_control.action_id),
              payload);
    test_copy(message.data.device_control.source,
              sizeof(message.data.device_control.source),
              "event");
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_on_event(&message));
}

static esp_err_t post_device_control_event_expect(const char *device_id, const char *action_id)
{
    scenehub_event_t message = {0};
    message.type = SCENEHUB_EVENT_DEVICE_CONTROL;
    message.payload_type = SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL;
    test_copy(message.payload, sizeof(message.payload), action_id);
    test_copy(message.data.device_control.device_id,
              sizeof(message.data.device_control.device_id),
              device_id);
    test_copy(message.data.device_control.action_id,
              sizeof(message.data.device_control.action_id),
              action_id);
    test_copy(message.data.device_control.source,
              sizeof(message.data.device_control.source),
              "event");
    return gm_room_session_scenario_on_event(&message);
}

static esp_err_t post_text_event_expect_err(const char *payload)
{
    scenehub_event_t message = {0};
    message.type = SCENEHUB_EVENT_DEVICE_CONTROL;
    message.payload_type = SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL;
    test_copy(message.payload, sizeof(message.payload), payload);
    test_copy(message.data.device_control.device_id,
              sizeof(message.data.device_control.device_id),
              "relay_client");
    test_copy(message.data.device_control.action_id,
              sizeof(message.data.device_control.action_id),
              payload);
    test_copy(message.data.device_control.source,
              sizeof(message.data.device_control.source),
              "event");
    return gm_room_session_scenario_on_event(&message);
}

static void post_command_result(const char *request_id, const char *status)
{
    scenehub_event_t message = {0};
    message.type = SCENEHUB_EVENT_DEVICE_CONTROL;
    message.payload_type = SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL;
    test_copy(message.payload, sizeof(message.payload), status);
    test_copy(message.data.device_control.device_id,
              sizeof(message.data.device_control.device_id),
              "relay_client");
    test_copy(message.data.device_control.action_id,
              sizeof(message.data.device_control.action_id),
              request_id);
    test_copy(message.data.device_control.source,
              sizeof(message.data.device_control.source),
              "result");
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_on_event(&message));
}

static esp_err_t post_flag_changed_expect(const char *flag_name)
{
    scenehub_event_t message = {0};
    TEST_ASSERT_EQUAL(ESP_OK, scenehub_event_make_flag_changed(&message, flag_name, true));
    return gm_room_session_scenario_on_event(&message);
}

static esp_err_t post_operator_event_expect(const char *event_id)
{
    scenehub_event_t message = {0};
    message.type = SCENEHUB_EVENT_WEB_COMMAND;
    message.payload_type = SCENEHUB_EVENT_PAYLOAD_TEXT;
    test_copy(message.topic, sizeof(message.topic), "operator");
    test_copy(message.payload, sizeof(message.payload), event_id);
    return gm_room_session_scenario_on_event(&message);
}

static esp_err_t post_runtime_event_expect(const char *event_id)
{
    scenehub_event_t message = {0};
    message.type = SCENEHUB_EVENT_RUNTIME_CONTROL;
    message.payload_type = SCENEHUB_EVENT_PAYLOAD_TEXT;
    test_copy(message.topic, sizeof(message.topic), "runtime");
    test_copy(message.payload, sizeof(message.payload), event_id);
    return gm_room_session_scenario_on_event(&message);
}

static void add_and_start_selected_scenario(const char *room_id)
{
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&s_scenario));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_select_scenario(room_id, s_scenario.id));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_start(room_id));
}

static void get_session(const char *room_id)
{
    memset(&s_session, 0, sizeof(s_session));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_get(room_id, &s_session));
}

static void prepare_pending_dispatch_plan(const char *room_id,
                                          gm_room_session_command_plan_t *out_plan)
{
    gm_room_session_t *session = NULL;
    gm_room_scenario_branch_runtime_t *branch = NULL;
    room_scenario_step_t *step = NULL;

    TEST_ASSERT_NOT_NULL(out_plan);
    memset(out_plan, 0, sizeof(*out_plan));

    session_test_bootstrap();
    add_room(room_id);
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_dispatch_pending", room_id, "Dispatch pending");

    step = add_step(&s_scenario, "pulse", "Pulse relay", ROOM_SCENARIO_STEP_DEVICE_COMMAND);
    test_copy(step->data.device_command.device_id,
              sizeof(step->data.device_command.device_id),
              "relay");
    test_copy(step->data.device_command.command_id,
              sizeof(step->data.device_command.command_id),
              "pulse");
    step = add_step(&s_scenario, "done", "Done", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "done", true);

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_sessions_lock());
    session = alloc_session_locked(room_id);
    TEST_ASSERT_NOT_NULL(session);
    session->state = GM_SESSION_RUNNING;
    session->running_scenario = s_scenario;
    session->running_scenario_valid = true;
    session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
    session->current_step_index = 0;
    scenario_clear_branch_runtimes_locked(session);
    scenario_clear_flags_locked(session);
    TEST_ASSERT_EQUAL(ESP_OK, scenario_init_branch_runtimes_locked(session));
    TEST_ASSERT_TRUE(session->branch_runtime_count > 0);
    branch = &session->branch_runtimes[0];
    gm_room_session_scenario_branch_load_into_session(session, branch);
    TEST_ASSERT_EQUAL(ESP_OK,
                      gm_room_session_plan_scenario_command_locked(
                          session,
                          0,
                          &session->running_scenario.steps[0].data.device_command,
                          gm_room_session_scenario_now_ms(),
                          out_plan));
    gm_room_session_scenario_branch_save_from_session(branch, session);
    gm_room_session_scenario_update_summary_from_branches_locked(session);
    gm_room_session_sessions_unlock();

    TEST_ASSERT_TRUE(gm_room_session_command_plan_present(out_plan));
}

static void prepare_pending_reactive_dispatch_plan(const char *room_id,
                                                   gm_room_session_command_plan_t *out_plan)
{
    gm_room_session_t *session = NULL;
    gm_room_scenario_branch_runtime_t *branch = NULL;
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *model_branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;

    TEST_ASSERT_NOT_NULL(out_plan);
    memset(out_plan, 0, sizeof(*out_plan));

    session_test_bootstrap();
    add_room(room_id);
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_reactive_dispatch_pending", room_id, "Reactive dispatch pending");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_dispatch", "Reactive dispatch", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    model_branch = &s_scenario.branches[1];
    model_branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(model_branch->trigger.device_id, sizeof(model_branch->trigger.device_id), "motion");
    test_copy(model_branch->trigger.event_id, sizeof(model_branch->trigger.event_id), "motion.detected");
    model_branch->variant_start_index = 0;
    model_branch->variant_count = 1;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "command");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 1;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_DEVICE_COMMAND;
    test_copy(action->data.device_command.device_id,
              sizeof(action->data.device_command.device_id),
              "relay");
    test_copy(action->data.device_command.command_id,
              sizeof(action->data.device_command.command_id),
              "pulse");

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_sessions_lock());
    session = alloc_session_locked(room_id);
    TEST_ASSERT_NOT_NULL(session);
    session->state = GM_SESSION_RUNNING;
    session->running_scenario = s_scenario;
    session->running_scenario_valid = true;
    session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
    session->current_step_index = 0;
    scenario_clear_branch_runtimes_locked(session);
    scenario_clear_flags_locked(session);
    TEST_ASSERT_EQUAL(ESP_OK, scenario_init_branch_runtimes_locked(session));
    TEST_ASSERT_TRUE(session->branch_runtime_count > 1);
    branch = &session->branch_runtimes[1];
    TEST_ASSERT_EQUAL(ESP_OK,
                      gm_room_session_reactive_v2_fire_locked(session,
                                                              branch,
                                                              gm_room_session_scenario_now_ms(),
                                                              out_plan));
    gm_room_session_scenario_update_summary_from_branches_locked(session);
    gm_room_session_sessions_unlock();

    TEST_ASSERT_TRUE(gm_room_session_command_plan_present(out_plan));
}

static int find_flag(const gm_room_session_t *session, const char *name)
{
    for (uint8_t i = 0; session && i < session->scenario_flag_count; ++i) {
        if (strcmp(session->scenario_flags[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void test_wait_any_device_event_advances_on_one_matching_event(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_any", "room_a", "Wait any");

    step = add_step(&s_scenario, "wait_any", "Wait any event", ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT);
    step->data.wait_any_device_event.event_count = 2;
    configure_wait_event(&step->data.wait_any_device_event.events[0], "door_opened");
    configure_wait_event(&step->data.wait_any_device_event.events[1], "drawer_opened");
    step = add_step(&s_scenario, "flag", "Set flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "any_done", true);

    add_and_start_selected_scenario("room_a");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT, s_session.branch_runtimes[0].wait_type);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, post_text_event_expect_err("other_event"));
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[0].scenario_state);

    post_text_event("drawer_opened");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_TRUE(find_flag(&s_session, "any_done") >= 0);
}

static void test_wait_all_device_events_waits_for_every_event(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_all", "room_a", "Wait all");

    step = add_step(&s_scenario, "wait_all", "Wait all events", ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS);
    step->data.wait_all_device_events.event_count = 2;
    configure_wait_event(&step->data.wait_all_device_events.events[0], "door_opened");
    configure_wait_event(&step->data.wait_all_device_events.events[1], "tv_started");
    step = add_step(&s_scenario, "flag", "Set flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "all_done", true);

    add_and_start_selected_scenario("room_a");

    post_text_event("door_opened");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS, s_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_TRUE(s_session.branch_runtimes[0].wait_event_matched[0]);
    TEST_ASSERT_FALSE(s_session.branch_runtimes[0].wait_event_matched[1]);
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "all_done"));

    post_text_event("tv_started");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_TRUE(find_flag(&s_session, "all_done") >= 0);
}

static void test_device_command_waits_for_done_result_before_next_step(void)
{
    room_scenario_step_t *step = NULL;
    char request_id[48] = {0};

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_command_result", "room_a", "Command result");

    step = add_step(&s_scenario, "pulse", "Pulse relay", ROOM_SCENARIO_STEP_DEVICE_COMMAND);
    test_copy(step->data.device_command.device_id,
              sizeof(step->data.device_command.device_id),
              "relay");
    test_copy(step->data.device_command.command_id,
              sizeof(step->data.device_command.command_id),
              "pulse");
    step = add_step(&s_scenario, "flag", "Set flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "command_done", true);

    add_and_start_selected_scenario("room_a");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT, s_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_TRUE(s_session.branch_runtimes[0].wait_event_type[0] != '\0');
    test_copy(request_id, sizeof(request_id), s_session.branch_runtimes[0].wait_event_type);

    post_command_result(request_id, "accepted");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT, s_session.branch_runtimes[0].wait_type);

    post_command_result(request_id, "done");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_TRUE(find_flag(&s_session, "command_done") >= 0);
}

static void test_device_command_rejected_result_fails_step(void)
{
    room_scenario_step_t *step = NULL;
    char request_id[48] = {0};

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_command_rejected", "room_a", "Command rejected");

    step = add_step(&s_scenario, "pulse", "Pulse relay", ROOM_SCENARIO_STEP_DEVICE_COMMAND);
    test_copy(step->data.device_command.device_id,
              sizeof(step->data.device_command.device_id),
              "relay");
    test_copy(step->data.device_command.command_id,
              sizeof(step->data.device_command.command_id),
              "pulse");
    step = add_step(&s_scenario, "flag", "Set flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "command_done", true);

    add_and_start_selected_scenario("room_a");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT, s_session.branch_runtimes[0].wait_type);
    test_copy(request_id, sizeof(request_id), s_session.branch_runtimes[0].wait_event_type);

    post_command_result(request_id, "rejected");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_ERROR, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "command_done"));
}

static void test_device_command_failed_result_fails_step(void)
{
    room_scenario_step_t *step = NULL;
    char request_id[48] = {0};

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_command_failed", "room_a", "Command failed");

    step = add_step(&s_scenario, "pulse", "Pulse relay", ROOM_SCENARIO_STEP_DEVICE_COMMAND);
    test_copy(step->data.device_command.device_id,
              sizeof(step->data.device_command.device_id),
              "relay");
    test_copy(step->data.device_command.command_id,
              sizeof(step->data.device_command.command_id),
              "pulse");
    step = add_step(&s_scenario, "flag", "Set flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "command_done", true);

    add_and_start_selected_scenario("room_a");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT, s_session.branch_runtimes[0].wait_type);
    test_copy(request_id, sizeof(request_id), s_session.branch_runtimes[0].wait_event_type);

    post_command_result(request_id, "failed");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_ERROR, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "command_done"));
}

static void test_device_command_result_timeout_fails_step(void)
{
    room_scenario_step_t *step = NULL;
    quest_device_t device = {0};

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get("relay", &device));
    device.commands[0].timeout_ms = 1;
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&device));
    init_scenario(&s_scenario, "scenario_command_timeout", "room_a", "Command timeout");

    step = add_step(&s_scenario, "pulse", "Pulse relay", ROOM_SCENARIO_STEP_DEVICE_COMMAND);
    test_copy(step->data.device_command.device_id,
              sizeof(step->data.device_command.device_id),
              "relay");
    test_copy(step->data.device_command.command_id,
              sizeof(step->data.device_command.command_id),
              "pulse");
    step = add_step(&s_scenario, "flag", "Set flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "command_done", true);

    add_and_start_selected_scenario("room_a");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT, s_session.branch_runtimes[0].wait_type);

    vTaskDelay(pdMS_TO_TICKS(20));
    gm_room_session_runtime_process_pending_work();
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_ERROR, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "command_done"));
}

static void test_device_command_group_rejects_result_required_commands(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_group_result_required", "room_a", "Group result required");

    step = add_step(&s_scenario, "group", "Group", ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP);
    step->data.device_command_group.command_count = 1;
    test_copy(step->data.device_command_group.commands[0].device_id,
              sizeof(step->data.device_command_group.commands[0].device_id),
              "relay");
    test_copy(step->data.device_command_group.commands[0].command_id,
              sizeof(step->data.device_command_group.commands[0].command_id),
              "pulse");

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&s_scenario));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_select_scenario("room_a", s_scenario.id));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, gm_room_session_scenario_start("room_a"));
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_ERROR, s_session.scenario_state);
    TEST_ASSERT_EQUAL_STRING("device_command_group_result_required_unsupported", s_session.scenario_last_error);
}

static void test_flags_are_shared_between_branches(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_flags", "room_a", "Flags");

    step = add_step(&s_scenario, "wait_flag", "Wait flag", ROOM_SCENARIO_STEP_WAIT_FLAGS);
    configure_wait_flag(step, 0, "branch_b_done", true);
    step = add_step(&s_scenario, "main_flag", "Main flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "main_unlocked", true);
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 2);

    step = add_step(&s_scenario, "branch_flag", "Branch flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "branch_b_done", true);
    set_branch(&s_scenario, 1, "branch_b", "Branch B", ROOM_SCENARIO_BRANCH_NORMAL, 2, 1);

    add_and_start_selected_scenario("room_a");
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "branch_b_done") >= 0);
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "main_unlocked"));

    gm_room_session_runtime_process_pending_work();
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "main_unlocked") >= 0);
}

static void test_reactive_branch_run_once_fires_once(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_reactive_once", "room_a", "Reactive once");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    step = add_step(&s_scenario, "react_wait", "React wait", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");
    step = add_step(&s_scenario, "react_flag", "React flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "reacted_once", true);
    set_branch(&s_scenario, 1, "react", "React", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 2);
    s_scenario.branches[1].run_once = true;

    add_and_start_selected_scenario("room_a");
    post_text_event("door_opened");
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "reacted_once") >= 0);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
    TEST_ASSERT_TRUE(s_session.branch_runtimes[1].fired_once);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, post_text_event_expect_err("door_opened"));
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
}

static void test_reactive_branch_uses_cooldown_between_reactions(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_reactive_cooldown", "room_a", "Reactive cooldown");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    step = add_step(&s_scenario, "react_wait", "React wait", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");
    step = add_step(&s_scenario, "react_flag", "React flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "reacted", true);
    set_branch(&s_scenario, 1, "react", "React", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 2);
    s_scenario.branches[1].cooldown_ms = 60000;

    add_and_start_selected_scenario("room_a");
    post_text_event("door_opened");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_COOLDOWN, s_session.branch_runtimes[1].scenario_state);
    TEST_ASSERT_TRUE(s_session.branch_runtimes[1].cooldown_until_ms > 0);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, post_text_event_expect_err("door_opened"));
}

static void test_reactive_branch_max_fire_count_stops_reaction(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_reactive_max_fire", "room_a", "Reactive max fire");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    step = add_step(&s_scenario, "react_wait", "React wait", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");
    step = add_step(&s_scenario, "react_flag", "React flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "reacted", true);
    set_branch(&s_scenario, 1, "react", "React", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 2);
    s_scenario.branches[1].max_fire_count = 2;

    add_and_start_selected_scenario("room_a");
    post_text_event("door_opened");
    get_session("room_a");
    TEST_ASSERT_EQUAL_UINT32(1, s_session.branch_runtimes[1].fire_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[1].scenario_state);

    post_text_event("door_opened");
    get_session("room_a");
    TEST_ASSERT_EQUAL_UINT32(2, s_session.branch_runtimes[1].fire_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, post_text_event_expect_err("door_opened"));
}

static void test_reactive_v2_device_event_runs_variant_actions(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_reactive_v2", "room_a", "Reactive v2");

    step = add_step(&s_scenario, "activate", "Activate", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "freeze.active", true);
    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 2);

    set_branch(&s_scenario, 1, "rx_freeze", "Freeze reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 2, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "motion");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "motion.detected");
    branch->guard_flag_count = 1;
    test_copy(branch->guard_flags[0].name, sizeof(branch->guard_flags[0].name), "freeze.active");
    branch->guard_flags[0].value = true;
    branch->policy_mode = ROOM_SCENARIO_REACTIVE_POLICY_SINGLE;
    branch->run_once = true;
    branch->max_fire_count = 1;
    branch->variant_start_index = 0;
    branch->variant_count = 1;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "soft");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 1;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_SET_FLAG;
    test_copy(action->data.set_flag.name, sizeof(action->data.set_flag.name), "rx.reacted");
    action->data.set_flag.value = true;

    add_and_start_selected_scenario("room_a");
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "freeze.active") >= 0);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[1].scenario_state);

    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "rx.reacted") >= 0);
    TEST_ASSERT_EQUAL_UINT32(1, s_session.branch_runtimes[1].fire_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
}

static void test_reactive_v2_device_event_matches_client_id_and_event_name(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_reactive_v2_alias", "room_a", "Reactive v2 alias");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_uid", "UID reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "relay");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "sequence_invalid");
    branch->policy_mode = ROOM_SCENARIO_REACTIVE_POLICY_SINGLE;
    branch->variant_start_index = 0;
    branch->variant_count = 1;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "main");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 1;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_SET_FLAG;
    test_copy(action->data.set_flag.name, sizeof(action->data.set_flag.name), "rx.uid_invalid");
    action->data.set_flag.value = true;

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("relay_client", "uid.sequence_invalid"));
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "rx.uid_invalid") >= 0);
    TEST_ASSERT_EQUAL_UINT32(1, s_session.branch_runtimes[1].fire_count);
}

static void test_reactive_v2_guard_mismatch_ignores_event(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_reactive_v2_guard", "room_a", "Reactive v2 guard");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_freeze", "Freeze reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "motion");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "motion.detected");
    branch->guard_flag_count = 1;
    test_copy(branch->guard_flags[0].name, sizeof(branch->guard_flags[0].name), "freeze.active");
    branch->guard_flags[0].value = true;
    branch->variant_start_index = 0;
    branch->variant_count = 1;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "soft");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 1;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_SET_FLAG;
    test_copy(action->data.set_flag.name, sizeof(action->data.set_flag.name), "rx.reacted");
    action->data.set_flag.value = true;

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "rx.reacted"));
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[1].scenario_state);
}

static void test_reactive_v2_flag_changed_trigger_runs_variant(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_reactive_v2_flag", "room_a", "Reactive v2 flag");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_flag", "Flag reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_FLAG_CHANGED;
    test_copy(branch->trigger.flag_name, sizeof(branch->trigger.flag_name), "room.ready");
    branch->run_once = true;
    branch->max_fire_count = 1;
    branch->variant_start_index = 0;
    branch->variant_count = 1;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "flag");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 1;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_SET_FLAG;
    test_copy(action->data.set_flag.name, sizeof(action->data.set_flag.name), "rx.flagged");
    action->data.set_flag.value = true;

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, post_flag_changed_expect("room.ready"));
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "rx.flagged") >= 0);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
}

static void test_reactive_v2_timeout_result_policy_sets_flag(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;
    char request_id[48] = {0};

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_reactive_v2_timeout", "room_a", "Reactive v2 timeout");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_timeout", "Timeout reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "motion");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "motion.detected");
    branch->result_on_timeout = ROOM_SCENARIO_REACTIVE_RESULT_SET_FLAG;
    test_copy(branch->result_flag, sizeof(branch->result_flag), "rx.timeout");
    branch->run_once = true;
    branch->max_fire_count = 1;
    branch->variant_start_index = 0;
    branch->variant_count = 1;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "command");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 1;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_DEVICE_COMMAND;
    test_copy(action->data.device_command.device_id,
              sizeof(action->data.device_command.device_id),
              "relay");
    test_copy(action->data.device_command.command_id,
              sizeof(action->data.device_command.command_id),
              "pulse");

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT, s_session.branch_runtimes[1].wait_type);
    test_copy(request_id, sizeof(request_id), s_session.branch_runtimes[1].wait_event_type);

    post_command_result(request_id, "timeout");
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "rx.timeout") >= 0);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
}

static void test_reactive_v2_done_result_policy_sets_flag(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;
    char request_id[48] = {0};

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_reactive_v2_done", "room_a", "Reactive v2 done");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_done", "Done reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "motion");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "motion.detected");
    branch->result_on_done = ROOM_SCENARIO_REACTIVE_RESULT_SET_FLAG;
    test_copy(branch->result_flag, sizeof(branch->result_flag), "rx.done");
    branch->run_once = true;
    branch->max_fire_count = 1;
    branch->variant_start_index = 0;
    branch->variant_count = 1;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "command");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 1;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_DEVICE_COMMAND;
    test_copy(action->data.device_command.device_id,
              sizeof(action->data.device_command.device_id),
              "relay");
    test_copy(action->data.device_command.command_id,
              sizeof(action->data.device_command.command_id),
              "pulse");

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT, s_session.branch_runtimes[1].wait_type);
    test_copy(request_id, sizeof(request_id), s_session.branch_runtimes[1].wait_event_type);

    post_command_result(request_id, "done");
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "rx.done") >= 0);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
}

static void test_reactive_v2_single_can_repeat_ignores_stale_max_fire_count(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;
    char request_id[48] = {0};

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_rx_v2_repeat", "room_a", "Reactive v2 repeat result");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_repeat", "Repeat reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "motion");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "motion.detected");
    branch->policy_mode = ROOM_SCENARIO_REACTIVE_POLICY_SINGLE;
    branch->run_once = false;
    branch->max_fire_count = 1;
    branch->variant_start_index = 0;
    branch->variant_count = 1;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "command");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 1;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_DEVICE_COMMAND;
    test_copy(action->data.device_command.device_id,
              sizeof(action->data.device_command.device_id),
              "relay");
    test_copy(action->data.device_command.command_id,
              sizeof(action->data.device_command.command_id),
              "pulse");

    add_and_start_selected_scenario("room_a");
    get_session("room_a");
    TEST_ASSERT_EQUAL_UINT32(0, s_session.branch_runtimes[1].max_fire_count);

    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT, s_session.branch_runtimes[1].wait_type);
    test_copy(request_id, sizeof(request_id), s_session.branch_runtimes[1].wait_event_type);

    post_command_result(request_id, "done");
    get_session("room_a");
    TEST_ASSERT_EQUAL_UINT32(1, s_session.branch_runtimes[1].fire_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[1].scenario_state);

    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT, s_session.branch_runtimes[1].wait_type);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(request_id, s_session.branch_runtimes[1].wait_event_type));
}

static void test_reactive_v2_fail_result_policy_fails_scenario_and_clears_wait(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;
    char request_id[48] = {0};

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_reactive_v2_fail", "room_a", "Reactive v2 fail");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_fail", "Fail reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "motion");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "motion.detected");
    branch->result_on_fail = ROOM_SCENARIO_REACTIVE_RESULT_FAIL_SCENARIO;
    branch->max_fire_count = 1;
    branch->variant_start_index = 0;
    branch->variant_count = 1;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "command");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 1;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_DEVICE_COMMAND;
    test_copy(action->data.device_command.device_id,
              sizeof(action->data.device_command.device_id),
              "relay");
    test_copy(action->data.device_command.command_id,
              sizeof(action->data.device_command.command_id),
              "pulse");

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT, s_session.branch_runtimes[1].wait_type);
    test_copy(request_id, sizeof(request_id), s_session.branch_runtimes[1].wait_event_type);

    post_command_result(request_id, "failed");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_ERROR, s_session.scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_ERROR, s_session.branch_runtimes[1].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_NONE, s_session.branch_runtimes[1].wait_type);
    TEST_ASSERT_EQUAL_STRING("failed", s_session.scenario_last_error);
}

static void test_reactive_v2_queue_one_replays_after_active_reaction(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_reactive_v2_queue", "room_a", "Reactive v2 queue");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_queue", "Queued reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "motion");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "motion.detected");
    branch->reentry_mode = ROOM_SCENARIO_REENTRY_QUEUE_ONE;
    branch->variant_start_index = 0;
    branch->variant_count = 1;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "wait");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 1;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_WAIT_TIME;
    action->data.wait_time.duration_ms = 1;

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_TRUE(s_session.branch_runtimes[1].pending_trigger);
    TEST_ASSERT_EQUAL_UINT32(1, s_session.branch_runtimes[1].fire_count);

    vTaskDelay(pdMS_TO_TICKS(5));
    gm_room_session_runtime_process_pending_work();
    get_session("room_a");
    TEST_ASSERT_FALSE(s_session.branch_runtimes[1].pending_trigger);
    TEST_ASSERT_EQUAL_UINT32(2, s_session.branch_runtimes[1].fire_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_NONE, s_session.branch_runtimes[1].wait_type);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[1].scenario_state);

    vTaskDelay(pdMS_TO_TICKS(5));
    gm_room_session_runtime_process_pending_work();
    get_session("room_a");
    TEST_ASSERT_EQUAL_UINT32(2, s_session.branch_runtimes[1].fire_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_NONE, s_session.branch_runtimes[1].wait_type);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[1].scenario_state);
}

static void test_reactive_v2_cooldown_starts_at_fire_and_suppresses_trigger(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_reactive_v2_cooldown", "room_a", "Reactive v2 cooldown");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_cooldown", "Cooldown reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "motion");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "motion.detected");
    branch->cooldown_ms = 1000;
    branch->max_fire_count = 2;
    branch->variant_start_index = 0;
    branch->variant_count = 1;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "flag");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 1;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_SET_FLAG;
    test_copy(action->data.set_flag.name, sizeof(action->data.set_flag.name), "rx.cooldown");
    action->data.set_flag.value = true;

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_EQUAL_UINT32(1, s_session.branch_runtimes[1].fire_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_COOLDOWN, s_session.branch_runtimes[1].scenario_state);

    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_FALSE(s_session.branch_runtimes[1].pending_trigger);
    TEST_ASSERT_EQUAL_UINT32(1, s_session.branch_runtimes[1].fire_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_COOLDOWN, s_session.branch_runtimes[1].scenario_state);
}

static void test_reactive_v2_rotate_policy_uses_next_variant(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_reactive_v2_rotate", "room_a", "Reactive v2 rotate");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_rotate", "Rotate reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "motion");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "motion.detected");
    branch->policy_mode = ROOM_SCENARIO_REACTIVE_POLICY_ROTATE;
    branch->max_fire_count = 2;
    branch->variant_start_index = 0;
    branch->variant_count = 2;

    s_scenario.reactive_variant_count = 2;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "first");
    variant->action_start_index = 0;
    variant->action_count = 1;
    variant = &s_scenario.reactive_variants[1];
    test_copy(variant->id, sizeof(variant->id), "second");
    variant->action_start_index = 1;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 2;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_SET_FLAG;
    test_copy(action->data.set_flag.name, sizeof(action->data.set_flag.name), "rx.first");
    action->data.set_flag.value = true;
    action = &s_scenario.reactive_actions[1];
    action->type = ROOM_SCENARIO_STEP_SET_FLAG;
    test_copy(action->data.set_flag.name, sizeof(action->data.set_flag.name), "rx.second");
    action->data.set_flag.value = true;

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "rx.first") >= 0);
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "rx.second"));
    TEST_ASSERT_EQUAL_UINT8(0, s_session.branch_runtimes[1].last_variant_index);

    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "rx.second") >= 0);
    TEST_ASSERT_EQUAL_UINT8(1, s_session.branch_runtimes[1].last_variant_index);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
}

static void test_reactive_v2_escalate_policy_stays_on_last_variant(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_reactive_v2_escalate", "room_a", "Reactive v2 escalate");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_escalate", "Escalate reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "motion");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "motion.detected");
    branch->policy_mode = ROOM_SCENARIO_REACTIVE_POLICY_ESCALATE;
    branch->max_fire_count = 3;
    branch->variant_start_index = 0;
    branch->variant_count = 2;

    s_scenario.reactive_variant_count = 2;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "soft");
    variant->action_start_index = 0;
    variant->action_count = 1;
    variant = &s_scenario.reactive_variants[1];
    test_copy(variant->id, sizeof(variant->id), "hard");
    variant->action_start_index = 1;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 2;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE;
    test_copy(action->data.operator_message.message,
              sizeof(action->data.operator_message.message),
              "soft");
    action = &s_scenario.reactive_actions[1];
    action->type = ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE;
    test_copy(action->data.operator_message.message,
              sizeof(action->data.operator_message.message),
              "hard");

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_EQUAL_UINT8(0, s_session.branch_runtimes[1].last_variant_index);
    TEST_ASSERT_EQUAL_STRING("soft", s_session.scenario_operator_message);

    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_EQUAL_UINT8(1, s_session.branch_runtimes[1].last_variant_index);
    TEST_ASSERT_EQUAL_STRING("hard", s_session.scenario_operator_message);

    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_EQUAL_UINT8(1, s_session.branch_runtimes[1].last_variant_index);
    TEST_ASSERT_EQUAL_UINT32(3, s_session.branch_runtimes[1].fire_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
}

static void test_reactive_v2_on_complete_sets_flag_and_message(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_reactive_v2_complete", "room_a", "Reactive v2 complete");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_complete", "Complete reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "motion");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "motion.detected");
    branch->run_once = true;
    branch->max_fire_count = 1;
    branch->variant_start_index = 0;
    branch->variant_count = 1;
    branch->on_complete_action_start_index = 1;
    branch->on_complete_action_count = 2;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "main");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 3;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_SET_FLAG;
    test_copy(action->data.set_flag.name, sizeof(action->data.set_flag.name), "rx.body");
    action->data.set_flag.value = true;
    action = &s_scenario.reactive_actions[1];
    action->type = ROOM_SCENARIO_STEP_SET_FLAG;
    test_copy(action->data.set_flag.name, sizeof(action->data.set_flag.name), "rx.complete");
    action->data.set_flag.value = true;
    action = &s_scenario.reactive_actions[2];
    action->type = ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE;
    test_copy(action->data.operator_message.message,
              sizeof(action->data.operator_message.message),
              "reaction complete");

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "rx.body") >= 0);
    TEST_ASSERT_TRUE(find_flag(&s_session, "rx.complete") >= 0);
    TEST_ASSERT_EQUAL_STRING("reaction complete", s_session.scenario_operator_message);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
}

static void test_reactive_v2_operator_and_runtime_triggers_run_variants(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_reactive_v2_non_device", "room_a", "Reactive v2 non-device");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_operator", "Operator reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_OPERATOR_EVENT;
    test_copy(branch->trigger.operator_event, sizeof(branch->trigger.operator_event), "panic");
    branch->run_once = true;
    branch->max_fire_count = 1;
    branch->variant_start_index = 0;
    branch->variant_count = 1;

    set_branch(&s_scenario, 2, "rx_runtime", "Runtime reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[2];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_RUNTIME_EVENT;
    test_copy(branch->trigger.runtime_event, sizeof(branch->trigger.runtime_event), "timer.expired");
    branch->run_once = true;
    branch->max_fire_count = 1;
    branch->variant_start_index = 1;
    branch->variant_count = 1;

    s_scenario.reactive_variant_count = 2;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "operator");
    variant->action_start_index = 0;
    variant->action_count = 1;
    variant = &s_scenario.reactive_variants[1];
    test_copy(variant->id, sizeof(variant->id), "runtime");
    variant->action_start_index = 1;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 2;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_SET_FLAG;
    test_copy(action->data.set_flag.name, sizeof(action->data.set_flag.name), "rx.operator");
    action->data.set_flag.value = true;
    action = &s_scenario.reactive_actions[1];
    action->type = ROOM_SCENARIO_STEP_SET_FLAG;
    test_copy(action->data.set_flag.name, sizeof(action->data.set_flag.name), "rx.runtime");
    action->data.set_flag.value = true;

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, post_operator_event_expect("panic"));
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "rx.operator") >= 0);
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "rx.runtime"));

    TEST_ASSERT_EQUAL(ESP_OK, post_runtime_event_expect("timer.expired"));
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "rx.runtime") >= 0);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[2].scenario_state);
}

static void test_reactive_v2_sequential_command_group_runs_and_completes(void)
{
    room_scenario_step_t *step = NULL;
    room_scenario_branch_t *branch = NULL;
    room_scenario_reactive_variant_t *variant = NULL;
    room_scenario_reactive_action_t *action = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_non_result_device("lamp", "lamp_client");
    init_scenario(&s_scenario, "scenario_reactive_v2_group", "room_a", "Reactive v2 group");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    set_branch(&s_scenario, 1, "rx_group", "Group reaction", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 0);
    branch = &s_scenario.branches[1];
    branch->trigger.kind = ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT;
    test_copy(branch->trigger.device_id, sizeof(branch->trigger.device_id), "motion");
    test_copy(branch->trigger.event_id, sizeof(branch->trigger.event_id), "motion.detected");
    branch->run_once = true;
    branch->max_fire_count = 1;
    branch->variant_start_index = 0;
    branch->variant_count = 1;
    branch->on_complete_action_start_index = 1;
    branch->on_complete_action_count = 1;

    s_scenario.reactive_variant_count = 1;
    variant = &s_scenario.reactive_variants[0];
    test_copy(variant->id, sizeof(variant->id), "group");
    variant->action_start_index = 0;
    variant->action_count = 1;

    s_scenario.reactive_action_count = 2;
    action = &s_scenario.reactive_actions[0];
    action->type = ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP;
    action->group_mode = ROOM_SCENARIO_COMMAND_GROUP_SEQUENTIAL;
    action->group_command_start_index = 0;
    action->group_command_count = 1;
    action = &s_scenario.reactive_actions[1];
    action->type = ROOM_SCENARIO_STEP_SET_FLAG;
    test_copy(action->data.set_flag.name, sizeof(action->data.set_flag.name), "rx.group_done");
    action->data.set_flag.value = true;

    s_scenario.reactive_group_command_count = 1;
    test_copy(s_scenario.reactive_group_commands[0].device_id,
              sizeof(s_scenario.reactive_group_commands[0].device_id),
              "lamp");
    test_copy(s_scenario.reactive_group_commands[0].command_id,
              sizeof(s_scenario.reactive_group_commands[0].command_id),
              "set");

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, post_device_control_event_expect("motion", "motion.detected"));
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "rx.group_done") >= 0);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
}

static void test_reactive_branch_does_not_block_required_completion(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_reactive_optional", "room_a", "Reactive optional");

    step = add_step(&s_scenario, "main_done", "Main done", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "main_done", true);
    step = add_step(&s_scenario, "react_wait", "React wait", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");
    step = add_step(&s_scenario, "react_flag", "React flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "reacted", true);

    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);
    set_branch(&s_scenario, 1, "react", "React", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 2);
    s_scenario.branches[1].run_once = true;

    add_and_start_selected_scenario("room_a");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[1].scenario_state);
    TEST_ASSERT_TRUE(find_flag(&s_session, "main_done") >= 0);
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "reacted"));
}

static void test_multiple_reactive_branches_on_same_event_conflict_without_firing(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_multi_react", "room_a", "Multi react");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    step = add_step(&s_scenario, "react_a_wait", "React A wait", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");
    step = add_step(&s_scenario, "react_a_flag", "React A flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "react_a", true);
    step = add_step(&s_scenario, "react_b_wait", "React B wait", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");
    step = add_step(&s_scenario, "react_b_flag", "React B flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "react_b", true);

    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);
    set_branch(&s_scenario, 1, "react_a", "React A", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 2);
    s_scenario.branches[1].run_once = true;
    set_branch(&s_scenario, 2, "react_b", "React B", ROOM_SCENARIO_BRANCH_REACTIVE, 3, 2);
    s_scenario.branches[2].run_once = true;

    add_and_start_selected_scenario("room_a");
    post_text_event("door_opened");

    get_session("room_a");
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "react_a"));
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "react_b"));
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[1].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[2].scenario_state);
}

static void test_disabled_reactive_branch_ignores_matching_event(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_disabled_react", "room_a", "Disabled react");

    step = add_step(&s_scenario, "main_wait", "Main wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    step = add_step(&s_scenario, "react_wait", "React wait", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");
    step = add_step(&s_scenario, "react_flag", "React flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "disabled_reacted", true);

    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);
    set_branch(&s_scenario, 1, "react", "React", ROOM_SCENARIO_BRANCH_REACTIVE, 1, 2);
    s_scenario.branches[1].enabled = false;
    s_scenario.branches[1].run_once = true;

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, post_text_event_expect_err("door_opened"));

    get_session("room_a");
    TEST_ASSERT_FALSE(s_session.branch_runtimes[1].active);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_STOPPED, s_session.branch_runtimes[1].scenario_state);
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "disabled_reacted"));
}

static void test_next_branch_skips_only_selected_wait(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_skip", "room_a", "Skip branch");

    step = add_step(&s_scenario, "main_wait", "Main wait flags", ROOM_SCENARIO_STEP_WAIT_FLAGS);
    configure_wait_flag(step, 0, "branch_skipped", true);
    set_branch(&s_scenario, 0, "main", "Main", ROOM_SCENARIO_BRANCH_NORMAL, 0, 1);

    step = add_step(&s_scenario, "branch_wait", "Branch wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;
    step->allow_operator_skip = true;
    test_copy(step->operator_skip_label, sizeof(step->operator_skip_label), "Skip branch");
    step = add_step(&s_scenario, "branch_flag", "Branch flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "branch_skipped", true);
    set_branch(&s_scenario, 1, "branch_b", "Branch B", ROOM_SCENARIO_BRANCH_NORMAL, 1, 2);

    add_and_start_selected_scenario("room_a");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_FLAGS, s_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_TIME, s_session.branch_runtimes[1].wait_type);

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_next_branch("room_a", "branch_b"));
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "branch_skipped") >= 0);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_FLAGS, s_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
}

static void test_running_snapshot_ignores_store_updates(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_snapshot", "room_a", "Original");
    step = add_step(&s_scenario, "wait", "Wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;

    add_and_start_selected_scenario("room_a");

    init_scenario(&s_updated_scenario, "scenario_snapshot", "room_a", "Updated");
    step = add_step(&s_updated_scenario, "updated_wait", "Updated wait", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 1000;
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&s_updated_scenario));

    get_session("room_a");
    TEST_ASSERT_EQUAL_STRING("Original", s_session.running_scenario.name);
    TEST_ASSERT_EQUAL_STRING("wait", s_session.running_scenario.steps[0].id);
    TEST_ASSERT_EQUAL(60000, s_session.running_scenario.steps[0].data.wait_time.duration_ms);
}

static void test_wait_device_event_timeout_continues_and_sets_message(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_event_timeout", "room_a", "Event timeout");

    step = add_step(&s_scenario, "wait_event", "Wait event", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");
    step->data.wait_device_event.timeout_ms = 1;
    test_copy(step->data.wait_device_event.timeout_message,
              sizeof(step->data.wait_device_event.timeout_message),
              "Door timeout");
    step = add_step(&s_scenario, "timeout_flag", "Timeout flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "event_timeout_done", true);

    add_and_start_selected_scenario("room_a");
    vTaskDelay(pdMS_TO_TICKS(20));
    gm_room_session_runtime_process_pending_work();

    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "event_timeout_done") >= 0);
    TEST_ASSERT_EQUAL_STRING("Door timeout", s_session.scenario_operator_message);
}

static void test_wait_flags_timeout_continues_and_sets_message(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_flags_timeout", "room_a", "Flags timeout");

    step = add_step(&s_scenario, "wait_flags", "Wait flags", ROOM_SCENARIO_STEP_WAIT_FLAGS);
    configure_wait_flag(step, 0, "missing_flag", true);
    step->data.wait_flags.timeout_ms = 1;
    test_copy(step->data.wait_flags.timeout_message,
              sizeof(step->data.wait_flags.timeout_message),
              "Flags timeout");
    step = add_step(&s_scenario, "timeout_flag", "Timeout flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "flags_timeout_done", true);

    add_and_start_selected_scenario("room_a");
    vTaskDelay(pdMS_TO_TICKS(20));
    gm_room_session_runtime_process_pending_work();

    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "flags_timeout_done") >= 0);
    TEST_ASSERT_EQUAL_STRING("Flags timeout", s_session.scenario_operator_message);
}

static void test_wrong_event_spam_does_not_advance_wait(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_wrong_spam", "room_a", "Wrong spam");

    step = add_step(&s_scenario, "wait_event", "Wait event", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");
    step = add_step(&s_scenario, "flag", "Set flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "advanced", true);

    add_and_start_selected_scenario("room_a");
    for (uint8_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, post_text_event_expect_err("drawer_opened"));
    }

    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT, s_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "advanced"));
}

static void test_duplicate_matching_event_after_advance_is_ignored(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_duplicate_event", "room_a", "Duplicate event");

    step = add_step(&s_scenario, "wait_event", "Wait event", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");
    step = add_step(&s_scenario, "flag", "Set flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "matched_once", true);
    step = add_step(&s_scenario, "hold", "Hold", ROOM_SCENARIO_STEP_WAIT_TIME);
    step->data.wait_time.duration_ms = 60000;

    add_and_start_selected_scenario("room_a");
    post_text_event("door_opened");
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "matched_once") >= 0);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_TIME, s_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_EQUAL(2, s_session.branch_runtimes[0].current_step_index);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, post_text_event_expect_err("door_opened"));
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_TIME, s_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_EQUAL(2, s_session.branch_runtimes[0].current_step_index);
}

static void test_repeated_reset_while_waiting_is_idempotent(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_reset_wait", "room_a", "Reset wait");

    step = add_step(&s_scenario, "wait_event", "Wait event", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_reset("room_a"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_reset("room_a"));

    get_session("room_a");
    TEST_ASSERT_FALSE(s_session.running_scenario_valid);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_IDLE, s_session.scenario_state);
    TEST_ASSERT_EQUAL(0, s_session.branch_runtime_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_NONE, s_session.wait_type);
}

static void test_repeated_stop_while_waiting_is_idempotent(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_stop_wait", "room_a", "Stop wait");

    step = add_step(&s_scenario, "wait_event", "Wait event", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");

    add_and_start_selected_scenario("room_a");
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_stop("room_a"));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_stop("room_a"));

    get_session("room_a");
    TEST_ASSERT_FALSE(s_session.running_scenario_valid);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_STOPPED, s_session.scenario_state);
    TEST_ASSERT_EQUAL(0, s_session.branch_runtime_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_NONE, s_session.wait_type);
}

static void test_dispatch_planned_command_after_scenario_reset_is_noop(void)
{
    gm_room_session_command_plan_t plan = {0};

    prepare_pending_dispatch_plan("room_a", &plan);
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_reset("room_a"));

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_dispatch_planned_command(&plan));
    TEST_ASSERT_FALSE(gm_room_session_command_plan_present(&plan));

    get_session("room_a");
    TEST_ASSERT_FALSE(s_session.running_scenario_valid);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_IDLE, s_session.scenario_state);
    TEST_ASSERT_EQUAL_UINT8(0, s_session.branch_runtime_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_NONE, s_session.wait_type);
}

static void test_dispatch_planned_command_after_scenario_stop_is_noop(void)
{
    gm_room_session_command_plan_t plan = {0};

    prepare_pending_dispatch_plan("room_a", &plan);
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_stop("room_a"));

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_dispatch_planned_command(&plan));
    TEST_ASSERT_FALSE(gm_room_session_command_plan_present(&plan));

    get_session("room_a");
    TEST_ASSERT_FALSE(s_session.running_scenario_valid);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_STOPPED, s_session.scenario_state);
    TEST_ASSERT_EQUAL_UINT8(0, s_session.branch_runtime_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_NONE, s_session.wait_type);
}

static void test_dispatch_planned_command_after_step_desync_is_noop(void)
{
    gm_room_session_command_plan_t plan = {0};
    gm_room_session_t *session = NULL;
    gm_room_scenario_branch_runtime_t *branch = NULL;

    prepare_pending_dispatch_plan("room_a", &plan);

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_sessions_lock());
    session = find_session_mutable_locked("room_a");
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_TRUE(session->branch_runtime_count > 0);
    branch = &session->branch_runtimes[0];
    gm_room_session_scenario_branch_load_into_session(session, branch);
    session->current_step_index = 1;
    session->scenario_state = GM_ROOM_SCENARIO_WAITING;
    session->wait_type = GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT;
    test_copy(session->wait_event_type,
              sizeof(session->wait_event_type),
              "__dispatch_pending__");
    gm_room_session_scenario_branch_save_from_session(branch, session);
    gm_room_session_sessions_unlock();

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_dispatch_planned_command(&plan));
    TEST_ASSERT_FALSE(gm_room_session_command_plan_present(&plan));

    get_session("room_a");
    TEST_ASSERT_TRUE(s_session.running_scenario_valid);
    TEST_ASSERT_EQUAL_UINT8(1, s_session.branch_runtimes[0].current_step_index);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT,
                      s_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_EQUAL_STRING("__dispatch_pending__",
                             s_session.branch_runtimes[0].wait_event_type);
}

static void test_dispatch_planned_reactive_command_after_scenario_reset_is_noop(void)
{
    gm_room_session_command_plan_t plan = {0};

    prepare_pending_reactive_dispatch_plan("room_a", &plan);
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_reset("room_a"));

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_dispatch_planned_command(&plan));
    TEST_ASSERT_FALSE(gm_room_session_command_plan_present(&plan));

    get_session("room_a");
    TEST_ASSERT_FALSE(s_session.running_scenario_valid);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_IDLE, s_session.scenario_state);
    TEST_ASSERT_EQUAL_UINT8(0, s_session.branch_runtime_count);
}

static void test_dispatch_planned_reactive_command_after_scenario_stop_is_noop(void)
{
    gm_room_session_command_plan_t plan = {0};

    prepare_pending_reactive_dispatch_plan("room_a", &plan);
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_stop("room_a"));

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_dispatch_planned_command(&plan));
    TEST_ASSERT_FALSE(gm_room_session_command_plan_present(&plan));

    get_session("room_a");
    TEST_ASSERT_FALSE(s_session.running_scenario_valid);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_STOPPED, s_session.scenario_state);
    TEST_ASSERT_EQUAL_UINT8(0, s_session.branch_runtime_count);
}

static void test_dispatch_planned_reactive_command_after_action_desync_is_noop(void)
{
    gm_room_session_command_plan_t plan = {0};
    gm_room_session_t *session = NULL;

    prepare_pending_reactive_dispatch_plan("room_a", &plan);

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_sessions_lock());
    session = find_session_mutable_locked("room_a");
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_TRUE(session->branch_runtime_count > 1);
    session->branch_runtimes[1].reactive_current_action = 1;
    gm_room_session_sessions_unlock();

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_dispatch_planned_command(&plan));
    TEST_ASSERT_FALSE(gm_room_session_command_plan_present(&plan));

    get_session("room_a");
    TEST_ASSERT_TRUE(s_session.running_scenario_valid);
    TEST_ASSERT_EQUAL_UINT8(1, s_session.branch_runtimes[1].reactive_current_action);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT,
                      s_session.branch_runtimes[1].wait_type);
    TEST_ASSERT_EQUAL_STRING("__dispatch_pending__",
                             s_session.branch_runtimes[1].wait_event_type);
}

static void test_repeated_start_restarts_runtime_cleanly(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    add_device_with_events();
    init_scenario(&s_scenario, "scenario_restart", "room_a", "Restart");

    step = add_step(&s_scenario, "wait_event", "Wait event", ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT);
    configure_wait_event(&step->data.wait_device_event, "door_opened");
    step = add_step(&s_scenario, "flag", "Set flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "matched", true);

    add_and_start_selected_scenario("room_a");
    post_text_event("door_opened");
    get_session("room_a");
    TEST_ASSERT_TRUE(find_flag(&s_session, "matched") >= 0);

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_start("room_a"));
    get_session("room_a");
    TEST_ASSERT_EQUAL(-1, find_flag(&s_session, "matched"));
    TEST_ASSERT_EQUAL(1, s_session.branch_runtime_count);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAITING, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT, s_session.branch_runtimes[0].wait_type);
    TEST_ASSERT_EQUAL(0, s_session.branch_runtimes[0].current_step_index);
}

static void test_next_on_done_scenario_returns_invalid_state(void)
{
    room_scenario_step_t *step = NULL;

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_next_done", "room_a", "Next done");

    step = add_step(&s_scenario, "flag", "Set flag", ROOM_SCENARIO_STEP_SET_FLAG);
    configure_set_flag(step, "done", true);

    add_and_start_selected_scenario("room_a");
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[0].scenario_state);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, gm_room_session_scenario_next("room_a"));
    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[0].scenario_state);
    TEST_ASSERT_TRUE(find_flag(&s_session, "done") >= 0);
}

static void test_end_game_freezes_timer_remaining(void)
{
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);

    session_test_bootstrap();
    add_room("room_a");
    init_scenario(&s_scenario, "scenario_end", "room_a", "End game");
    (void)add_step(&s_scenario, "end", "End", ROOM_SCENARIO_STEP_END_GAME);

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(&s_scenario));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_start("room_a", 60000, now_ms));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_select_scenario("room_a", s_scenario.id));
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_start("room_a"));

    get_session("room_a");
    TEST_ASSERT_EQUAL(GM_SESSION_FINISHED, s_session.state);
    TEST_ASSERT_EQUAL(GM_TIMER_FINISHED, s_session.timer.state);
    TEST_ASSERT_TRUE(s_session.timer.remaining_ms > 0);
}

static void test_audio_background_rejects_mp3_before_file_lookup(void)
{
    session_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED,
                      gm_room_session_execute_device_command(QUEST_DEVICE_SYSTEM_AUDIO_ID,
                                                             "play",
                                                             "{\"file\":\"/sdcard/music/theme.mp3\","
                                                             "\"channel\":\"background\","
                                                             "\"volume\":70,"
                                                             "\"repeat\":true}"));
}

static void test_audio_background_accepts_wav_extension_then_checks_file(void)
{
    session_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                      gm_room_session_execute_device_command(QUEST_DEVICE_SYSTEM_AUDIO_ID,
                                                             "play",
                                                             "{\"file\":\"/sdcard/music/theme.wav\","
                                                             "\"channel\":\"background\","
                                                             "\"volume\":70,"
                                                             "\"repeat\":true}"));
}

static void test_audio_effect_allows_mp3_extension_then_checks_file(void)
{
    session_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                      gm_room_session_execute_device_command(QUEST_DEVICE_SYSTEM_AUDIO_ID,
                                                             "play",
                                                             "{\"file\":\"/sdcard/sfx/click.mp3\","
                                                             "\"channel\":\"effect\","
                                                             "\"volume\":70}"));
}

static void test_audio_stop_commands_are_idempotent_without_active_track(void)
{
    session_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK,
                      gm_room_session_execute_device_command(QUEST_DEVICE_SYSTEM_AUDIO_ID,
                                                             "stop",
                                                             "{\"channel\":\"background\"}"));
    TEST_ASSERT_EQUAL(ESP_OK,
                      gm_room_session_execute_device_command(QUEST_DEVICE_SYSTEM_AUDIO_ID,
                                                             "stop",
                                                             "{\"channel\":\"effect\"}"));
    TEST_ASSERT_EQUAL(ESP_OK,
                      gm_room_session_execute_device_command(QUEST_DEVICE_SYSTEM_AUDIO_ID,
                                                             "stop",
                                                             "{\"channel\":\"all\"}"));
}

static void test_audio_play_rejects_empty_file_param(void)
{
    session_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      gm_room_session_execute_device_command(QUEST_DEVICE_SYSTEM_AUDIO_ID,
                                                             "play",
                                                             "{\"file\":\"\","
                                                             "\"channel\":\"effect\","
                                                             "\"volume\":70}"));
}

static void test_audio_play_rejects_unknown_channel(void)
{
    session_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      gm_room_session_execute_device_command(QUEST_DEVICE_SYSTEM_AUDIO_ID,
                                                             "play",
                                                             "{\"file\":\"/sdcard/sfx/click.mp3\","
                                                             "\"channel\":\"voice\","
                                                             "\"volume\":70}"));
}

static void test_audio_set_volume_requires_volume_param(void)
{
    session_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      gm_room_session_execute_device_command(QUEST_DEVICE_SYSTEM_AUDIO_ID,
                                                             "set_volume",
                                                             "{}"));
}

static void test_room_session_describe_runtime_prefers_flow_progress_and_wait_summary(void)
{
    gm_room_scenario_runtime_semantics_t summary = {0};

    memset(&s_session, 0, sizeof(s_session));
    s_session.running_scenario_valid = true;
    s_session.running_scenario.step_count = 3;
    test_copy(s_session.running_scenario.steps[0].label,
              sizeof(s_session.running_scenario.steps[0].label),
              "Open relay");
    test_copy(s_session.running_scenario.steps[1].label,
              sizeof(s_session.running_scenario.steps[1].label),
              "Watch door");
    test_copy(s_session.running_scenario.steps[2].label,
              sizeof(s_session.running_scenario.steps[2].label),
              "Reaction");
    s_session.scenario_state = GM_ROOM_SCENARIO_WAITING;
    s_session.current_step_index = 1;
    s_session.wait_type = GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT;
    test_copy(s_session.wait_source_id, sizeof(s_session.wait_source_id), "relay");
    test_copy(s_session.wait_event_type, sizeof(s_session.wait_event_type), "door_opened");
    s_session.branch_runtime_count = 2;
    s_session.branch_runtimes[0].type = ROOM_SCENARIO_BRANCH_NORMAL;
    s_session.branch_runtimes[0].step_start_index = 0;
    s_session.branch_runtimes[0].step_count = 2;
    s_session.branch_runtimes[0].current_step_index = 1;
    s_session.branch_runtimes[0].scenario_state = GM_ROOM_SCENARIO_WAITING;
    s_session.branch_runtimes[1].type = ROOM_SCENARIO_BRANCH_REACTIVE;
    s_session.branch_runtimes[1].step_start_index = 2;
    s_session.branch_runtimes[1].step_count = 1;
    s_session.branch_runtimes[1].current_step_index = 2;
    s_session.branch_runtimes[1].scenario_state = GM_ROOM_SCENARIO_RUNNING;

    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_describe_runtime(&s_session, &summary));
    TEST_ASSERT_EQUAL_UINT16(2, summary.total_steps);
    TEST_ASSERT_EQUAL_UINT16(1, summary.done_steps);
    TEST_ASSERT_EQUAL_STRING("Watch door", summary.current_step_text);
    TEST_ASSERT_EQUAL_STRING("relay: door_opened", summary.wait_summary);
}

static void test_room_session_describe_branch_runtime_reports_error_step_and_skip_label(void)
{
    gm_room_scenario_branch_semantics_t summary = {0};

    memset(&s_session, 0, sizeof(s_session));
    s_session.running_scenario_valid = true;
    s_session.running_scenario.step_count = 3;
    test_copy(s_session.running_scenario.steps[2].label,
              sizeof(s_session.running_scenario.steps[2].label),
              "Final lock");
    s_session.running_scenario.steps[2].allow_operator_skip = true;
    test_copy(s_session.running_scenario.steps[2].operator_skip_label,
              sizeof(s_session.running_scenario.steps[2].operator_skip_label),
              "Skip lock");
    s_session.branch_runtime_count = 1;
    s_session.branch_runtimes[0].type = ROOM_SCENARIO_BRANCH_NORMAL;
    s_session.branch_runtimes[0].step_start_index = 1;
    s_session.branch_runtimes[0].step_count = 2;
    s_session.branch_runtimes[0].current_step_index = 2;
    s_session.branch_runtimes[0].scenario_state = GM_ROOM_SCENARIO_WAITING;
    s_session.branch_runtimes[0].wait_type = GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT;
    test_copy(s_session.branch_runtimes[0].wait_source_id,
              sizeof(s_session.branch_runtimes[0].wait_source_id),
              "relay");
    test_copy(s_session.branch_runtimes[0].wait_event_type,
              sizeof(s_session.branch_runtimes[0].wait_event_type),
              "door_opened");

    TEST_ASSERT_EQUAL(ESP_OK,
                      gm_room_session_describe_branch_runtime(&s_session,
                                                             &s_session.branch_runtimes[0],
                                                             &summary));
    TEST_ASSERT_EQUAL_UINT16(1, summary.current_local_step_index);
    TEST_ASSERT_EQUAL_UINT16(1, summary.done_steps);
    TEST_ASSERT_EQUAL_UINT16(2, summary.total_steps);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_STEP_STATE_WAITING, summary.current_step_state);
    TEST_ASSERT_EQUAL_INT16(-1, summary.failed_step_index);
    TEST_ASSERT_EQUAL_STRING("Final lock", summary.current_step_text);
    TEST_ASSERT_EQUAL_STRING("relay: door_opened", summary.wait_summary);
    TEST_ASSERT_TRUE(summary.wait_operator_skip_allowed);
    TEST_ASSERT_EQUAL_STRING("Skip lock", summary.wait_operator_skip_label);

    s_session.branch_runtimes[0].scenario_state = GM_ROOM_SCENARIO_ERROR;
    TEST_ASSERT_EQUAL(ESP_OK,
                      gm_room_session_describe_branch_runtime(&s_session,
                                                             &s_session.branch_runtimes[0],
                                                             &summary));
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_STEP_STATE_ERROR, summary.current_step_state);
    TEST_ASSERT_EQUAL_INT16(1, summary.failed_step_index);
}

void register_gm_room_session_tests(void)
{
    RUN_TEST(test_wait_any_device_event_advances_on_one_matching_event);
    RUN_TEST(test_wait_all_device_events_waits_for_every_event);
    RUN_TEST(test_device_command_waits_for_done_result_before_next_step);
    RUN_TEST(test_device_command_rejected_result_fails_step);
    RUN_TEST(test_device_command_failed_result_fails_step);
    RUN_TEST(test_device_command_result_timeout_fails_step);
    RUN_TEST(test_device_command_group_rejects_result_required_commands);
    RUN_TEST(test_flags_are_shared_between_branches);
    RUN_TEST(test_reactive_branch_run_once_fires_once);
    RUN_TEST(test_reactive_branch_uses_cooldown_between_reactions);
    RUN_TEST(test_reactive_branch_max_fire_count_stops_reaction);
    RUN_TEST(test_reactive_v2_device_event_runs_variant_actions);
    RUN_TEST(test_reactive_v2_device_event_matches_client_id_and_event_name);
    RUN_TEST(test_reactive_v2_guard_mismatch_ignores_event);
    RUN_TEST(test_reactive_v2_flag_changed_trigger_runs_variant);
    RUN_TEST(test_reactive_v2_timeout_result_policy_sets_flag);
    RUN_TEST(test_reactive_v2_done_result_policy_sets_flag);
    RUN_TEST(test_reactive_v2_single_can_repeat_ignores_stale_max_fire_count);
    RUN_TEST(test_reactive_v2_fail_result_policy_fails_scenario_and_clears_wait);
    RUN_TEST(test_reactive_v2_queue_one_replays_after_active_reaction);
    RUN_TEST(test_reactive_v2_cooldown_starts_at_fire_and_suppresses_trigger);
    RUN_TEST(test_reactive_v2_rotate_policy_uses_next_variant);
    RUN_TEST(test_reactive_v2_escalate_policy_stays_on_last_variant);
    RUN_TEST(test_reactive_v2_on_complete_sets_flag_and_message);
    RUN_TEST(test_reactive_v2_operator_and_runtime_triggers_run_variants);
    RUN_TEST(test_reactive_v2_sequential_command_group_runs_and_completes);
    RUN_TEST(test_reactive_branch_does_not_block_required_completion);
    RUN_TEST(test_multiple_reactive_branches_on_same_event_conflict_without_firing);
    RUN_TEST(test_disabled_reactive_branch_ignores_matching_event);
    RUN_TEST(test_next_branch_skips_only_selected_wait);
    RUN_TEST(test_running_snapshot_ignores_store_updates);
    RUN_TEST(test_wait_device_event_timeout_continues_and_sets_message);
    RUN_TEST(test_wait_flags_timeout_continues_and_sets_message);
    RUN_TEST(test_wrong_event_spam_does_not_advance_wait);
    RUN_TEST(test_duplicate_matching_event_after_advance_is_ignored);
    RUN_TEST(test_repeated_reset_while_waiting_is_idempotent);
    RUN_TEST(test_repeated_stop_while_waiting_is_idempotent);
    RUN_TEST(test_dispatch_planned_command_after_scenario_reset_is_noop);
    RUN_TEST(test_dispatch_planned_command_after_scenario_stop_is_noop);
    RUN_TEST(test_dispatch_planned_command_after_step_desync_is_noop);
    RUN_TEST(test_dispatch_planned_reactive_command_after_scenario_reset_is_noop);
    RUN_TEST(test_dispatch_planned_reactive_command_after_scenario_stop_is_noop);
    RUN_TEST(test_dispatch_planned_reactive_command_after_action_desync_is_noop);
    RUN_TEST(test_repeated_start_restarts_runtime_cleanly);
    RUN_TEST(test_next_on_done_scenario_returns_invalid_state);
    RUN_TEST(test_end_game_freezes_timer_remaining);
    RUN_TEST(test_audio_background_rejects_mp3_before_file_lookup);
    RUN_TEST(test_audio_background_accepts_wav_extension_then_checks_file);
    RUN_TEST(test_audio_effect_allows_mp3_extension_then_checks_file);
    RUN_TEST(test_audio_stop_commands_are_idempotent_without_active_track);
    RUN_TEST(test_audio_play_rejects_empty_file_param);
    RUN_TEST(test_audio_play_rejects_unknown_channel);
    RUN_TEST(test_audio_set_volume_requires_volume_param);
    RUN_TEST(test_room_session_describe_runtime_prefers_flow_progress_and_wait_summary);
    RUN_TEST(test_room_session_describe_branch_runtime_reports_error_step_and_skip_label);
}
