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
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"

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

    device.event_count = 3;
    test_copy(device.events[0].id, sizeof(device.events[0].id), "door_opened");
    test_copy(device.events[0].label, sizeof(device.events[0].label), "Door opened");
    test_copy(device.events[0].topic, sizeof(device.events[0].topic), "quest/relay/event");
    test_copy(device.events[0].payload, sizeof(device.events[0].payload), "door_opened");
    test_copy(device.events[0].event_type, sizeof(device.events[0].event_type), "door_opened");

    test_copy(device.events[1].id, sizeof(device.events[1].id), "drawer_opened");
    test_copy(device.events[1].label, sizeof(device.events[1].label), "Drawer opened");
    test_copy(device.events[1].topic, sizeof(device.events[1].topic), "quest/relay/event");
    test_copy(device.events[1].payload, sizeof(device.events[1].payload), "drawer_opened");
    test_copy(device.events[1].event_type, sizeof(device.events[1].event_type), "drawer_opened");

    test_copy(device.events[2].id, sizeof(device.events[2].id), "tv_started");
    test_copy(device.events[2].label, sizeof(device.events[2].label), "TV started");
    test_copy(device.events[2].topic, sizeof(device.events[2].topic), "quest/relay/event");
    test_copy(device.events[2].payload, sizeof(device.events[2].payload), "tv_started");
    test_copy(device.events[2].event_type, sizeof(device.events[2].event_type), "tv_started");

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
    event_bus_message_t message = {0};
    message.type = EVENT_MQTT_MESSAGE;
    message.payload_type = EVENT_BUS_PAYLOAD_TEXT;
    test_copy(message.topic, sizeof(message.topic), "quest/relay/event");
    test_copy(message.payload, sizeof(message.payload), payload);
    TEST_ASSERT_EQUAL(ESP_OK, gm_room_session_scenario_on_event(&message));
}

static esp_err_t post_text_event_expect_err(const char *payload)
{
    event_bus_message_t message = {0};
    message.type = EVENT_MQTT_MESSAGE;
    message.payload_type = EVENT_BUS_PAYLOAD_TEXT;
    test_copy(message.topic, sizeof(message.topic), "quest/relay/event");
    test_copy(message.payload, sizeof(message.payload), payload);
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

    gm_room_session_scenario_tick();
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

static void test_multiple_reactive_branches_on_same_event_fire_together(void)
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
    TEST_ASSERT_TRUE(find_flag(&s_session, "react_a") >= 0);
    TEST_ASSERT_TRUE(find_flag(&s_session, "react_b") >= 0);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[1].scenario_state);
    TEST_ASSERT_EQUAL(GM_ROOM_SCENARIO_DONE, s_session.branch_runtimes[2].scenario_state);
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
    gm_room_session_scenario_tick();

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
    gm_room_session_scenario_tick();

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

void register_gm_room_session_tests(void)
{
    RUN_TEST(test_wait_any_device_event_advances_on_one_matching_event);
    RUN_TEST(test_wait_all_device_events_waits_for_every_event);
    RUN_TEST(test_flags_are_shared_between_branches);
    RUN_TEST(test_reactive_branch_run_once_fires_once);
    RUN_TEST(test_reactive_branch_uses_cooldown_between_reactions);
    RUN_TEST(test_reactive_branch_does_not_block_required_completion);
    RUN_TEST(test_multiple_reactive_branches_on_same_event_fire_together);
    RUN_TEST(test_disabled_reactive_branch_ignores_matching_event);
    RUN_TEST(test_next_branch_skips_only_selected_wait);
    RUN_TEST(test_running_snapshot_ignores_store_updates);
    RUN_TEST(test_wait_device_event_timeout_continues_and_sets_message);
    RUN_TEST(test_wait_flags_timeout_continues_and_sets_message);
    RUN_TEST(test_wrong_event_spam_does_not_advance_wait);
    RUN_TEST(test_duplicate_matching_event_after_advance_is_ignored);
    RUN_TEST(test_repeated_reset_while_waiting_is_idempotent);
    RUN_TEST(test_repeated_stop_while_waiting_is_idempotent);
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
}
