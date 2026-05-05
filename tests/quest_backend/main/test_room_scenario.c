#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "cJSON.h"
#include "esp_attr.h"
#include "orchestrator_registry.h"
#include "room_scenario.h"

EXT_RAM_BSS_ATTR static orch_room_scenario_detail_t s_room_scenario_details[2];
EXT_RAM_BSS_ATTR static room_scenario_t s_room_scenario_items[4];
EXT_RAM_BSS_ATTR static room_scenario_t s_room_scenario_work[4];

static void set_text(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static void room_scenario_test_bootstrap(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_init());
    room_scenario_reset();
}

static void init_scenario(room_scenario_t *scenario, const char *id, const char *room_id, const char *name)
{
    memset(scenario, 0, sizeof(*scenario));
    set_text(scenario->id, sizeof(scenario->id), id);
    set_text(scenario->room_id, sizeof(scenario->room_id), room_id);
    set_text(scenario->name, sizeof(scenario->name), name);
}

static void fill_device_command_step(room_scenario_step_t *step,
                                     const char *id,
                                     const char *label,
                                     const char *device_id,
                                     const char *command_id)
{
    memset(step, 0, sizeof(*step));
    set_text(step->id, sizeof(step->id), id);
    set_text(step->label, sizeof(step->label), label);
    step->type = ROOM_SCENARIO_STEP_DEVICE_COMMAND;
    step->enabled = true;
    set_text(step->data.device_command.device_id,
             sizeof(step->data.device_command.device_id),
             device_id);
    set_text(step->data.device_command.command_id,
             sizeof(step->data.device_command.command_id),
             command_id);
}

static void fill_wait_time_step(room_scenario_step_t *step,
                                const char *id,
                                const char *label,
                                uint32_t duration_ms)
{
    memset(step, 0, sizeof(*step));
    set_text(step->id, sizeof(step->id), id);
    set_text(step->label, sizeof(step->label), label);
    step->type = ROOM_SCENARIO_STEP_WAIT_TIME;
    step->enabled = true;
    step->data.wait_time.duration_ms = duration_ms;
}

static void fill_wait_device_event_step(room_scenario_step_t *step,
                                        const char *id,
                                        const char *label,
                                        const char *device_id,
                                        const char *event_id)
{
    memset(step, 0, sizeof(*step));
    set_text(step->id, sizeof(step->id), id);
    set_text(step->label, sizeof(step->label), label);
    step->type = ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT;
    step->enabled = true;
    set_text(step->data.wait_device_event.device_id,
             sizeof(step->data.wait_device_event.device_id),
             device_id);
    set_text(step->data.wait_device_event.event_id,
             sizeof(step->data.wait_device_event.event_id),
             event_id);
}

static void fill_operator_step(room_scenario_step_t *step,
                               const char *id,
                               const char *label,
                               const char *prompt,
                               const char *approve_label)
{
    memset(step, 0, sizeof(*step));
    set_text(step->id, sizeof(step->id), id);
    set_text(step->label, sizeof(step->label), label);
    step->type = ROOM_SCENARIO_STEP_OPERATOR_APPROVAL;
    step->enabled = true;
    set_text(step->data.operator_approval.prompt,
             sizeof(step->data.operator_approval.prompt),
             prompt);
    set_text(step->data.operator_approval.approve_label,
             sizeof(step->data.operator_approval.approve_label),
             approve_label);
}

static void fill_operator_message_step(room_scenario_step_t *step,
                                       const char *id,
                                       const char *label,
                                       const char *message)
{
    memset(step, 0, sizeof(*step));
    set_text(step->id, sizeof(step->id), id);
    set_text(step->label, sizeof(step->label), label);
    step->type = ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE;
    step->enabled = true;
    set_text(step->data.operator_message.message,
             sizeof(step->data.operator_message.message),
             message);
}

static void fill_command_group_step(room_scenario_step_t *step,
                                    const char *id,
                                    const char *label,
                                    const char *device_id,
                                    const char *command_id)
{
    memset(step, 0, sizeof(*step));
    set_text(step->id, sizeof(step->id), id);
    set_text(step->label, sizeof(step->label), label);
    step->type = ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP;
    step->enabled = true;
    step->data.device_command_group.command_count = 1;
    set_text(step->data.device_command_group.commands[0].device_id,
             sizeof(step->data.device_command_group.commands[0].device_id),
             device_id);
    set_text(step->data.device_command_group.commands[0].command_id,
             sizeof(step->data.device_command_group.commands[0].command_id),
             command_id);
}

static void fill_set_flag_step(room_scenario_step_t *step,
                               const char *id,
                               const char *label,
                               const char *flag_name,
                               bool value)
{
    memset(step, 0, sizeof(*step));
    set_text(step->id, sizeof(step->id), id);
    set_text(step->label, sizeof(step->label), label);
    step->type = ROOM_SCENARIO_STEP_SET_FLAG;
    step->enabled = true;
    set_text(step->data.set_flag.name,
             sizeof(step->data.set_flag.name),
             flag_name);
    step->data.set_flag.value = value;
}

static void fill_wait_flags_step(room_scenario_step_t *step,
                                 const char *id,
                                 const char *label,
                                 const char *flag_name,
                                 bool value)
{
    memset(step, 0, sizeof(*step));
    set_text(step->id, sizeof(step->id), id);
    set_text(step->label, sizeof(step->label), label);
    step->type = ROOM_SCENARIO_STEP_WAIT_FLAGS;
    step->enabled = true;
    step->data.wait_flags.flag_count = 1;
    set_text(step->data.wait_flags.flags[0].name,
             sizeof(step->data.wait_flags.flags[0].name),
             flag_name);
    step->data.wait_flags.flags[0].value = value;
}

static void fill_wait_any_device_event_step(room_scenario_step_t *step,
                                            const char *id,
                                            const char *label,
                                            const char *device_id,
                                            const char *event_id)
{
    memset(step, 0, sizeof(*step));
    set_text(step->id, sizeof(step->id), id);
    set_text(step->label, sizeof(step->label), label);
    step->type = ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT;
    step->enabled = true;
    step->data.wait_any_device_event.event_count = 1;
    set_text(step->data.wait_any_device_event.events[0].device_id,
             sizeof(step->data.wait_any_device_event.events[0].device_id),
             device_id);
    set_text(step->data.wait_any_device_event.events[0].event_id,
             sizeof(step->data.wait_any_device_event.events[0].event_id),
             event_id);
}

static void fill_wait_all_device_events_step(room_scenario_step_t *step,
                                             const char *id,
                                             const char *label,
                                             const char *device_id,
                                             const char *event_id)
{
    memset(step, 0, sizeof(*step));
    set_text(step->id, sizeof(step->id), id);
    set_text(step->label, sizeof(step->label), label);
    step->type = ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS;
    step->enabled = true;
    step->data.wait_all_device_events.event_count = 1;
    set_text(step->data.wait_all_device_events.events[0].device_id,
             sizeof(step->data.wait_all_device_events.events[0].device_id),
             device_id);
    set_text(step->data.wait_all_device_events.events[0].event_id,
             sizeof(step->data.wait_all_device_events.events[0].event_id),
             event_id);
}

static void test_room_scenario_add_and_get_full_flow(void)
{
    room_scenario_t *scenario = &s_room_scenario_work[0];
    room_scenario_t *loaded = &s_room_scenario_work[1];

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));

    init_scenario(scenario, "escape_main", "room_a", "Escape main");
    fill_device_command_step(&scenario->steps[0], "intro", "Run intro", "audio", "intro");
    fill_wait_device_event_step(&scenario->steps[1], "wait_p1", "Wait puzzle", "puzzle_1", "puzzle_1_done");
    fill_device_command_step(&scenario->steps[2], "final_light", "Final light", "light", "final");
    fill_wait_time_step(&scenario->steps[3], "delay", "Wait delay", 3000);
    fill_operator_step(&scenario->steps[4], "approve", "Approve", "Confirm players solved puzzle", "Continue");
    fill_device_command_step(&scenario->steps[5], "door", "Open door", "door", "open");
    scenario->step_count = 6;

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(scenario));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_get("escape_main", loaded));

    TEST_ASSERT_EQUAL_STRING("escape_main", loaded->id);
    TEST_ASSERT_EQUAL_STRING("room_a", loaded->room_id);
    TEST_ASSERT_EQUAL_STRING("Escape main", loaded->name);
    TEST_ASSERT_EQUAL_UINT(6, loaded->step_count);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_DEVICE_COMMAND, loaded->steps[0].type);
    TEST_ASSERT_EQUAL_STRING("audio", loaded->steps[0].data.device_command.device_id);
    TEST_ASSERT_EQUAL_STRING("intro", loaded->steps[0].data.device_command.command_id);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT, loaded->steps[1].type);
    TEST_ASSERT_EQUAL_STRING("puzzle_1", loaded->steps[1].data.wait_device_event.device_id);
    TEST_ASSERT_EQUAL_STRING("puzzle_1_done", loaded->steps[1].data.wait_device_event.event_id);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_WAIT_TIME, loaded->steps[3].type);
    TEST_ASSERT_EQUAL_UINT32(3000, loaded->steps[3].data.wait_time.duration_ms);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_OPERATOR_APPROVAL, loaded->steps[4].type);
    TEST_ASSERT_EQUAL_STRING("Confirm players solved puzzle", loaded->steps[4].data.operator_approval.prompt);
    TEST_ASSERT_EQUAL_STRING("Continue", loaded->steps[4].data.operator_approval.approve_label);
    TEST_ASSERT_TRUE(loaded->steps[5].enabled);
}

static void test_room_scenario_list_by_room_filters_and_counts(void)
{
    room_scenario_t *room_a_one = &s_room_scenario_work[0];
    room_scenario_t *room_a_two = &s_room_scenario_work[1];
    room_scenario_t *room_b_one = &s_room_scenario_work[2];
    room_scenario_t *items = s_room_scenario_items;
    size_t count = 0;

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));
    memset(items, 0, sizeof(s_room_scenario_items));
    init_scenario(room_a_one, "room_a_one", "room_a", "A one");
    init_scenario(room_a_two, "room_a_two", "room_a", "A two");
    init_scenario(room_b_one, "room_b_one", "room_b", "B one");

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(room_a_one));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(room_b_one));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(room_a_two));

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_list_by_room("room_a", items, 4, &count));
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_EQUAL_STRING("room_a_one", items[0].id);
    TEST_ASSERT_EQUAL_STRING("room_a_two", items[1].id);
}

static void test_room_scenario_registry_lists_details_read_only(void)
{
    room_scenario_t *scenario = &s_room_scenario_work[0];
    orch_room_scenario_detail_t *details = s_room_scenario_details;
    size_t count = 0;

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));
    memset(details, 0, sizeof(s_room_scenario_details));

    init_scenario(scenario, "escape_main", "room_a", "Escape main");
    fill_device_command_step(&scenario->steps[0], "intro", "Run intro", "audio", "intro");
    fill_operator_step(&scenario->steps[1], "approve", "Approve", "Confirm players solved puzzle", "Continue");
    scenario->step_count = 2;
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(scenario));

    TEST_ASSERT_EQUAL(ESP_OK,
                      orchestrator_registry_list_room_scenario_details("room_a", details, 2, &count));
    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_EQUAL_STRING("room_a", details[0].summary.room_id);
    TEST_ASSERT_EQUAL_STRING("escape_main", details[0].summary.id);
    TEST_ASSERT_EQUAL_STRING("Escape main", details[0].summary.name);
    TEST_ASSERT_EQUAL_UINT(2, details[0].summary.step_count);
    TEST_ASSERT_EQUAL(ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND, details[0].steps[0].type);
    TEST_ASSERT_EQUAL_STRING("audio", details[0].steps[0].device_id);
    TEST_ASSERT_EQUAL_STRING("intro", details[0].steps[0].command_id);
    TEST_ASSERT_EQUAL(ORCH_ROOM_SCENARIO_STEP_OPERATOR_APPROVAL, details[0].steps[1].type);
    TEST_ASSERT_EQUAL_STRING("Confirm players solved puzzle", details[0].steps[1].operator_prompt);
    TEST_ASSERT_EQUAL_STRING("Continue", details[0].steps[1].operator_approve_label);
}

static void test_room_scenario_replace_existing_id(void)
{
    room_scenario_t *first = &s_room_scenario_work[0];
    room_scenario_t *second = &s_room_scenario_work[1];
    room_scenario_t *loaded = &s_room_scenario_work[2];
    room_scenario_t *items = s_room_scenario_items;
    size_t count = 0;

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));
    memset(items, 0, sizeof(s_room_scenario_items));

    init_scenario(first, "same", "room_a", "First");
    init_scenario(second, "same", "room_a", "Second");
    fill_wait_time_step(&second->steps[0], "delay", "Delay", 100);
    second->step_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(first));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(second));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_get("same", loaded));
    TEST_ASSERT_EQUAL_STRING("Second", loaded->name);
    TEST_ASSERT_EQUAL_UINT(1, loaded->step_count);

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_list_by_room("room_a", items, 2, &count));
    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_EQUAL_STRING("same", items[0].id);
}

static void test_room_scenario_validation_rejects_invalid_steps(void)
{
    room_scenario_t *scenario = &s_room_scenario_work[0];

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));

    init_scenario(scenario, "invalid", "room_a", "Invalid");
    scenario->id[0] = '\0';
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_scenario_add(scenario));

    init_scenario(scenario, "invalid", "room_a", "Invalid");
    fill_wait_time_step(&scenario->steps[0], "delay", "Delay", 0);
    scenario->step_count = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_scenario_add(scenario));

    init_scenario(scenario, "invalid", "room_a", "Invalid");
    fill_wait_device_event_step(&scenario->steps[0], "wait", "Wait", "source", "");
    scenario->step_count = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_scenario_add(scenario));
    room_scenario_reset();

    init_scenario(scenario, "invalid", "room_a", "Invalid");
    fill_operator_step(&scenario->steps[0], "approve", "Approve", "", "Continue");
    scenario->step_count = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_scenario_add(scenario));

    init_scenario(scenario, "invalid", "room_a", "Invalid");
    fill_device_command_step(&scenario->steps[0], "run", "Run", "device", "");
    scenario->step_count = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_scenario_add(scenario));

    init_scenario(scenario, "invalid", "room_a", "Invalid");
    scenario->step_count = ROOM_SCENARIO_MAX_STEPS + 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, room_scenario_add(scenario));
}

static void test_room_scenario_rejects_duplicate_step_id(void)
{
    room_scenario_t *scenario = &s_room_scenario_work[0];
    room_scenario_validation_report_t report = {0};

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));

    init_scenario(scenario, "duplicate_steps", "room_a", "Duplicate steps");
    fill_wait_time_step(&scenario->steps[0], "same", "Wait one", 1000);
    fill_wait_time_step(&scenario->steps[1], "same", "Wait two", 2000);
    scenario->step_count = 2;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_scenario_add(scenario));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_validate(scenario, &report));
    TEST_ASSERT_FALSE(report.valid);
    TEST_ASSERT_TRUE(report.issue_count >= 1);
    TEST_ASSERT_EQUAL_STRING("STEP_ID_DUPLICATE", report.issues[0].code);
}

static void test_room_scenario_allows_same_step_id_in_different_branches(void)
{
    room_scenario_t *scenario = &s_room_scenario_work[0];
    room_scenario_validation_report_t report = {0};

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));

    init_scenario(scenario, "branch_steps", "room_a", "Branch steps");
    fill_wait_time_step(&scenario->steps[0], "step_1", "Branch one wait", 1000);
    fill_wait_time_step(&scenario->steps[1], "step_1", "Branch two wait", 2000);
    scenario->step_count = 2;
    scenario->branch_count = 2;

    set_text(scenario->branches[0].id, sizeof(scenario->branches[0].id), "main");
    set_text(scenario->branches[0].name, sizeof(scenario->branches[0].name), "Main");
    scenario->branches[0].enabled = true;
    scenario->branches[0].required_for_completion = true;
    scenario->branches[0].step_start_index = 0;
    scenario->branches[0].step_count = 1;

    set_text(scenario->branches[1].id, sizeof(scenario->branches[1].id), "branch_2");
    set_text(scenario->branches[1].name, sizeof(scenario->branches[1].name), "Branch 2");
    scenario->branches[1].enabled = true;
    scenario->branches[1].required_for_completion = true;
    scenario->branches[1].step_start_index = 1;
    scenario->branches[1].step_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_validate(scenario, &report));
    TEST_ASSERT_TRUE(report.valid);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(scenario));
}

static void test_room_scenario_rejects_duplicate_branch_id(void)
{
    room_scenario_t *scenario = &s_room_scenario_work[0];
    room_scenario_validation_report_t report = {0};

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));

    init_scenario(scenario, "duplicate_branches", "room_a", "Duplicate branches");
    fill_wait_time_step(&scenario->steps[0], "wait_a", "Wait A", 1000);
    fill_wait_time_step(&scenario->steps[1], "wait_b", "Wait B", 1000);
    scenario->step_count = 2;
    scenario->branch_count = 2;

    set_text(scenario->branches[0].id, sizeof(scenario->branches[0].id), "same");
    set_text(scenario->branches[0].name, sizeof(scenario->branches[0].name), "A");
    scenario->branches[0].enabled = true;
    scenario->branches[0].step_start_index = 0;
    scenario->branches[0].step_count = 1;

    set_text(scenario->branches[1].id, sizeof(scenario->branches[1].id), "same");
    set_text(scenario->branches[1].name, sizeof(scenario->branches[1].name), "B");
    scenario->branches[1].enabled = true;
    scenario->branches[1].step_start_index = 1;
    scenario->branches[1].step_count = 1;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_scenario_add(scenario));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_validate(scenario, &report));
    TEST_ASSERT_FALSE(report.valid);
    TEST_ASSERT_TRUE(report.issue_count >= 1);
    TEST_ASSERT_EQUAL_STRING("BRANCH_ID_DUPLICATE", report.issues[0].code);
}

static void test_room_scenario_rejects_branch_step_range_outside_steps(void)
{
    room_scenario_t *scenario = &s_room_scenario_work[0];
    room_scenario_validation_report_t report = {0};

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));

    init_scenario(scenario, "bad_range", "room_a", "Bad range");
    fill_wait_time_step(&scenario->steps[0], "wait", "Wait", 1000);
    scenario->step_count = 1;
    scenario->branch_count = 1;

    set_text(scenario->branches[0].id, sizeof(scenario->branches[0].id), "main");
    set_text(scenario->branches[0].name, sizeof(scenario->branches[0].name), "Main");
    scenario->branches[0].enabled = true;
    scenario->branches[0].step_start_index = 0;
    scenario->branches[0].step_count = 2;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, room_scenario_add(scenario));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_validate(scenario, &report));
    TEST_ASSERT_FALSE(report.valid);
    TEST_ASSERT_TRUE(report.issue_count >= 1);
    TEST_ASSERT_EQUAL_STRING("BRANCH_STEP_RANGE_INVALID", report.issues[0].code);
}

static void test_room_scenario_rejects_reactive_branch_without_trigger_step(void)
{
    room_scenario_t *scenario = &s_room_scenario_work[0];
    room_scenario_validation_report_t report = {0};

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));

    init_scenario(scenario, "bad_reactive", "room_a", "Bad reactive");
    fill_wait_time_step(&scenario->steps[0], "delay", "Delay", 1000);
    scenario->step_count = 1;
    scenario->branch_count = 1;

    set_text(scenario->branches[0].id, sizeof(scenario->branches[0].id), "react");
    set_text(scenario->branches[0].name, sizeof(scenario->branches[0].name), "React");
    scenario->branches[0].type = ROOM_SCENARIO_BRANCH_REACTIVE;
    scenario->branches[0].enabled = true;
    scenario->branches[0].run_once = true;
    scenario->branches[0].step_start_index = 0;
    scenario->branches[0].step_count = 1;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_scenario_add(scenario));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_validate(scenario, &report));
    TEST_ASSERT_FALSE(report.valid);
    TEST_ASSERT_TRUE(report.issue_count >= 1);
    TEST_ASSERT_EQUAL_STRING("REACTIVE_BRANCH_TRIGGER_REQUIRED", report.issues[0].code);
}

static void test_room_scenario_rejects_reactive_branch_disabled_first_non_trigger_step(void)
{
    room_scenario_t *scenario = &s_room_scenario_work[0];
    room_scenario_validation_report_t report = {0};

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));

    init_scenario(scenario, "bad_disabled_first", "room_a", "Bad disabled first");
    fill_wait_time_step(&scenario->steps[0], "delay", "Delay", 1000);
    scenario->steps[0].enabled = false;
    fill_wait_device_event_step(&scenario->steps[1], "trigger", "Trigger", "relay", "opened");
    scenario->step_count = 2;
    scenario->branch_count = 1;

    set_text(scenario->branches[0].id, sizeof(scenario->branches[0].id), "react");
    set_text(scenario->branches[0].name, sizeof(scenario->branches[0].name), "React");
    scenario->branches[0].type = ROOM_SCENARIO_BRANCH_REACTIVE;
    scenario->branches[0].enabled = true;
    scenario->branches[0].run_once = true;
    scenario->branches[0].step_start_index = 0;
    scenario->branches[0].step_count = 2;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_scenario_add(scenario));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_validate(scenario, &report));
    TEST_ASSERT_FALSE(report.valid);
    TEST_ASSERT_TRUE(report.issue_count >= 1);
    TEST_ASSERT_EQUAL_STRING("REACTIVE_BRANCH_TRIGGER_REQUIRED", report.issues[0].code);
}

static void test_room_scenario_rejects_reactive_wait_flags_without_guard(void)
{
    room_scenario_t *scenario = &s_room_scenario_work[0];
    room_scenario_validation_report_t report = {0};

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));

    init_scenario(scenario, "bad_reactive_flags", "room_a", "Bad reactive flags");
    fill_wait_flags_step(&scenario->steps[0], "wait_flag", "Wait flag", "armed", true);
    scenario->step_count = 1;
    scenario->branch_count = 1;

    set_text(scenario->branches[0].id, sizeof(scenario->branches[0].id), "react");
    set_text(scenario->branches[0].name, sizeof(scenario->branches[0].name), "React");
    scenario->branches[0].type = ROOM_SCENARIO_BRANCH_REACTIVE;
    scenario->branches[0].enabled = true;
    scenario->branches[0].step_start_index = 0;
    scenario->branches[0].step_count = 1;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_scenario_add(scenario));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_validate(scenario, &report));
    TEST_ASSERT_FALSE(report.valid);
    TEST_ASSERT_TRUE(report.issue_count >= 1);
    TEST_ASSERT_EQUAL_STRING("REACTIVE_FLAGS_NEEDS_GUARD", report.issues[0].code);
}

static void test_room_scenario_validation_code_buffer_fits_long_codes(void)
{
    TEST_ASSERT_TRUE(strlen("REACTIVE_BRANCH_TRIGGER_REQUIRED") < ROOM_SCENARIO_VALIDATION_CODE_MAX_LEN);
}

static void test_room_scenario_removed_raw_wait_event_type_is_rejected(void)
{
    room_scenario_step_type_t type = ROOM_SCENARIO_STEP_WAIT_TIME;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, room_scenario_step_type_from_str("WAIT_EVENT", &type));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, room_scenario_step_type_from_str("RUN_DEVICE_SCENARIO", &type));
}

static void test_room_scenario_single_json_round_trip(void)
{
    room_scenario_t *scenario = &s_room_scenario_work[0];
    room_scenario_t *loaded = &s_room_scenario_work[1];
    cJSON *json = NULL;
    cJSON *steps = NULL;

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));

    init_scenario(scenario, "easy_flow", "room_1", "Easy Flow");
    fill_device_command_step(&scenario->steps[0], "intro", "Intro", "system_audio", "play");
    set_text(scenario->steps[0].data.device_command.params_json,
             sizeof(scenario->steps[0].data.device_command.params_json),
             "{\"file\":\"/sdcard/music/theme.wav\","
             "\"channel\":\"background\","
             "\"volume\":70,"
             "\"repeat\":true}");
    fill_wait_time_step(&scenario->steps[1], "delay", "Delay", 3000);
    scenario->step_count = 2;

    json = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_to_json(scenario, json));
    TEST_ASSERT_EQUAL_STRING("easy_flow", cJSON_GetObjectItemCaseSensitive(json, "id")->valuestring);
    steps = cJSON_GetObjectItemCaseSensitive(json, "steps");
    TEST_ASSERT_TRUE(cJSON_IsArray(steps));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(steps));

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_from_json(json, loaded));
    TEST_ASSERT_EQUAL_STRING("easy_flow", loaded->id);
    TEST_ASSERT_EQUAL_STRING("Easy Flow", loaded->name);
    TEST_ASSERT_EQUAL_STRING("room_1", loaded->room_id);
    TEST_ASSERT_EQUAL_UINT(2, loaded->step_count);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_DEVICE_COMMAND, loaded->steps[0].type);
    TEST_ASSERT_EQUAL_STRING("system_audio", loaded->steps[0].data.device_command.device_id);
    TEST_ASSERT_EQUAL_STRING("{\"file\":\"/sdcard/music/theme.wav\",\"channel\":\"background\",\"volume\":70,\"repeat\":true}",
                             loaded->steps[0].data.device_command.params_json);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_WAIT_TIME, loaded->steps[1].type);
    TEST_ASSERT_EQUAL_UINT32(3000, loaded->steps[1].data.wait_time.duration_ms);
    cJSON_Delete(json);
}

static void test_room_scenario_export_empty_store(void)
{
    cJSON *root = NULL;
    cJSON *version = NULL;
    cJSON *array = NULL;
    uint32_t generation = 0;

    room_scenario_test_bootstrap();
    generation = room_scenario_generation();

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_store_export_json(&root));
    TEST_ASSERT_NOT_NULL(root);
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    array = cJSON_GetObjectItemCaseSensitive(root, "room_scenarios");
    TEST_ASSERT_TRUE(cJSON_IsNumber(version));
    TEST_ASSERT_EQUAL_INT(1, version->valueint);
    TEST_ASSERT_TRUE(cJSON_IsArray(array));
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(array));
    TEST_ASSERT_EQUAL_UINT32(generation, room_scenario_generation());
    cJSON_Delete(root);
}

static void test_room_scenario_export_all_step_types(void)
{
    room_scenario_t *scenario = &s_room_scenario_work[0];
    cJSON *root = NULL;
    cJSON *array = NULL;
    cJSON *scenario_obj = NULL;
    cJSON *steps = NULL;

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));

    init_scenario(scenario, "easy_flow", "room_1", "Easy Flow");
    fill_device_command_step(&scenario->steps[0], "intro", "Intro", "audio", "intro");
    fill_wait_device_event_step(&scenario->steps[1], "wait_1", "Wait puzzle", "box_1", "puzzle_done");
    fill_wait_time_step(&scenario->steps[2], "delay", "Delay", 3000);
    fill_operator_step(&scenario->steps[3], "approve", "Operator check", "Continue?", "Continue");
    fill_command_group_step(&scenario->steps[4], "group", "Group", "relay", "open");
    fill_operator_message_step(&scenario->steps[5], "message", "Message", "Players moved to room 2");
    fill_set_flag_step(&scenario->steps[6], "flag", "Flag", "altar_done", true);
    fill_wait_flags_step(&scenario->steps[7], "wait_flags", "Wait flags", "altar_done", true);
    fill_wait_any_device_event_step(&scenario->steps[8], "wait_any", "Wait any", "keypad", "success");
    fill_wait_all_device_events_step(&scenario->steps[9], "wait_all", "Wait all", "altar", "done");
    scenario->step_count = 10;
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(scenario));

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_export_json(&root));
    array = cJSON_GetObjectItemCaseSensitive(root, "room_scenarios");
    TEST_ASSERT_TRUE(cJSON_IsArray(array));
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(array));
    scenario_obj = cJSON_GetArrayItem(array, 0);
    TEST_ASSERT_EQUAL_STRING("easy_flow",
                             cJSON_GetObjectItemCaseSensitive(scenario_obj, "id")->valuestring);
    steps = cJSON_GetObjectItemCaseSensitive(scenario_obj, "steps");
    TEST_ASSERT_EQUAL_INT(10, cJSON_GetArraySize(steps));
    TEST_ASSERT_EQUAL_STRING("DEVICE_COMMAND",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 0),
                                                              "type")->valuestring);
    TEST_ASSERT_EQUAL_STRING("WAIT_DEVICE_EVENT",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 1),
                                                              "type")->valuestring);
    TEST_ASSERT_EQUAL_STRING("puzzle_done",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 1),
                                                              "event_id")->valuestring);
    TEST_ASSERT_EQUAL_STRING("WAIT_TIME",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 2),
                                                              "type")->valuestring);
    TEST_ASSERT_EQUAL_INT(3000,
                          cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 2),
                                                           "duration_ms")->valueint);
    TEST_ASSERT_EQUAL_STRING("OPERATOR_APPROVAL",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 3),
                                                              "type")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Continue?",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 3),
                                                              "prompt")->valuestring);
    TEST_ASSERT_EQUAL_STRING("DEVICE_COMMAND_GROUP",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 4),
                                                              "type")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 4),
                                                                    "commands")));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(
                         cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 4), "commands"),
                         0),
                     "params"));
    TEST_ASSERT_EQUAL_STRING("SHOW_OPERATOR_MESSAGE",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 5),
                                                              "type")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Players moved to room 2",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 5),
                                                              "message")->valuestring);
    TEST_ASSERT_EQUAL_STRING("SET_FLAG",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 6),
                                                              "type")->valuestring);
    TEST_ASSERT_EQUAL_STRING("altar_done",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 6),
                                                              "flag_name")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 6),
                                                                     "value")));
    TEST_ASSERT_EQUAL_STRING("WAIT_FLAGS",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 7),
                                                              "type")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 7),
                                                                    "flags")));
    TEST_ASSERT_EQUAL_STRING("WAIT_ANY_DEVICE_EVENT",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 8),
                                                              "type")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 8),
                                                                    "events")));
    TEST_ASSERT_EQUAL_STRING("WAIT_ALL_DEVICE_EVENTS",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 9),
                                                              "type")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 9),
                                                                    "events")));
    cJSON_Delete(root);
}

static void test_room_scenario_command_group_json_preserves_params(void)
{
    const char *json =
        "{\"id\":\"group_params\",\"name\":\"Group params\",\"room_id\":\"room_1\",\"steps\":["
        "{\"id\":\"group\",\"label\":\"Group\",\"enabled\":true,\"type\":\"DEVICE_COMMAND_GROUP\","
        "\"commands\":[{\"device_id\":\"relay\",\"command_id\":\"pulse\","
        "\"params\":{\"channel\":2,\"duration_ms\":750}}]}]}";
    cJSON *root = NULL;
    cJSON *out = NULL;
    cJSON *steps = NULL;
    cJSON *commands = NULL;
    cJSON *params = NULL;
    room_scenario_t *scenario = &s_room_scenario_work[0];

    room_scenario_test_bootstrap();
    memset(scenario, 0, sizeof(*scenario));

    root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_from_json(root, scenario));
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL_UINT(1, scenario->step_count);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP, scenario->steps[0].type);
    TEST_ASSERT_EQUAL_STRING("{\"channel\":2,\"duration_ms\":750}",
                             scenario->steps[0].data.device_command_group.commands[0].params_json);

    out = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_to_json(scenario, out));
    steps = cJSON_GetObjectItemCaseSensitive(out, "steps");
    commands = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(steps, 0), "commands");
    params = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(commands, 0), "params");
    TEST_ASSERT_TRUE(cJSON_IsObject(params));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItemCaseSensitive(params, "channel")->valueint);
    TEST_ASSERT_EQUAL_INT(750, cJSON_GetObjectItemCaseSensitive(params, "duration_ms")->valueint);
    cJSON_Delete(out);
}

static void test_room_scenario_import_valid_json_restores_scenarios(void)
{
    const char *json =
        "{\"version\":1,\"room_scenarios\":[{\"id\":\"easy_flow\",\"name\":\"Easy Flow\","
        "\"room_id\":\"room_1\",\"steps\":["
        "{\"id\":\"intro\",\"label\":\"Intro\",\"enabled\":true,\"type\":\"DEVICE_COMMAND\","
        "\"device_id\":\"audio\",\"command_id\":\"intro\"},"
        "{\"id\":\"wait_1\",\"label\":\"Wait puzzle\",\"enabled\":true,\"type\":\"WAIT_DEVICE_EVENT\","
        "\"device_id\":\"box_1\",\"event_id\":\"puzzle_done\"},"
        "{\"id\":\"delay\",\"label\":\"Delay\",\"enabled\":true,\"type\":\"WAIT_TIME\","
        "\"duration_ms\":3000},"
        "{\"id\":\"approve\",\"label\":\"Operator check\",\"enabled\":true,"
        "\"type\":\"OPERATOR_APPROVAL\",\"prompt\":\"Continue?\",\"approve_label\":\"Continue\"},"
        "{\"id\":\"flag\",\"label\":\"Flag\",\"enabled\":true,\"type\":\"SET_FLAG\","
        "\"flag_name\":\"altar_done\",\"value\":true},"
        "{\"id\":\"wait_flags\",\"label\":\"Wait flags\",\"enabled\":true,\"type\":\"WAIT_FLAGS\","
        "\"flags\":[{\"flag_name\":\"altar_done\",\"value\":true}]},"
        "{\"id\":\"wait_any\",\"label\":\"Wait any\",\"enabled\":true,\"type\":\"WAIT_ANY_DEVICE_EVENT\","
        "\"events\":[{\"device_id\":\"keypad\",\"event_id\":\"success\"}]},"
        "{\"id\":\"wait_all\",\"label\":\"Wait all\",\"enabled\":true,\"type\":\"WAIT_ALL_DEVICE_EVENTS\","
        "\"events\":[{\"device_id\":\"altar\",\"event_id\":\"done\"}]}"
        "]}]}";
    cJSON *root = NULL;
    room_scenario_t *loaded = &s_room_scenario_work[0];

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));

    root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_store_import_json(root));
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_get("easy_flow", loaded));
    TEST_ASSERT_EQUAL_STRING("Easy Flow", loaded->name);
    TEST_ASSERT_EQUAL_STRING("room_1", loaded->room_id);
    TEST_ASSERT_EQUAL_UINT(8, loaded->step_count);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_DEVICE_COMMAND, loaded->steps[0].type);
    TEST_ASSERT_EQUAL_STRING("audio", loaded->steps[0].data.device_command.device_id);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT, loaded->steps[1].type);
    TEST_ASSERT_EQUAL_STRING("box_1", loaded->steps[1].data.wait_device_event.device_id);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_WAIT_TIME, loaded->steps[2].type);
    TEST_ASSERT_EQUAL_UINT32(3000, loaded->steps[2].data.wait_time.duration_ms);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_OPERATOR_APPROVAL, loaded->steps[3].type);
    TEST_ASSERT_EQUAL_STRING("Continue?", loaded->steps[3].data.operator_approval.prompt);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_SET_FLAG, loaded->steps[4].type);
    TEST_ASSERT_EQUAL_STRING("altar_done", loaded->steps[4].data.set_flag.name);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_WAIT_FLAGS, loaded->steps[5].type);
    TEST_ASSERT_EQUAL_UINT8(1, loaded->steps[5].data.wait_flags.flag_count);
    TEST_ASSERT_EQUAL_STRING("altar_done", loaded->steps[5].data.wait_flags.flags[0].name);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT, loaded->steps[6].type);
    TEST_ASSERT_EQUAL_UINT8(1, loaded->steps[6].data.wait_any_device_event.event_count);
    TEST_ASSERT_EQUAL_STRING("keypad", loaded->steps[6].data.wait_any_device_event.events[0].device_id);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS, loaded->steps[7].type);
    TEST_ASSERT_EQUAL_UINT8(1, loaded->steps[7].data.wait_all_device_events.event_count);
    TEST_ASSERT_EQUAL_STRING("altar", loaded->steps[7].data.wait_all_device_events.events[0].device_id);
}

static void test_room_scenario_import_invalid_json_keeps_existing_store(void)
{
    const char *json =
        "{\"version\":1,\"room_scenarios\":[{\"name\":\"Broken\","
        "\"room_id\":\"room_1\",\"steps\":[]}]}";
    room_scenario_t *existing = &s_room_scenario_work[0];
    room_scenario_t *loaded = &s_room_scenario_work[1];
    cJSON *root = NULL;

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));
    init_scenario(existing, "existing", "room_1", "Existing");
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(existing));

    root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, room_scenario_import_json(root));
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_get("existing", loaded));
    TEST_ASSERT_EQUAL_STRING("Existing", loaded->name);
}

static void test_room_scenario_import_over_limit_fails_safely(void)
{
    room_scenario_t *existing = &s_room_scenario_work[0];
    room_scenario_t *loaded = &s_room_scenario_work[1];
    cJSON *root = NULL;
    cJSON *array = NULL;

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));
    init_scenario(existing, "existing", "room_1", "Existing");
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(existing));

    root = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(root);
    cJSON_AddNumberToObject(root, "version", 1);
    array = cJSON_AddArrayToObject(root, "room_scenarios");
    TEST_ASSERT_NOT_NULL(array);
    for (size_t i = 0; i <= ROOM_SCENARIO_MAX_SCENARIOS; ++i) {
        cJSON *scenario_obj = cJSON_CreateObject();
        TEST_ASSERT_NOT_NULL(scenario_obj);
        TEST_ASSERT_TRUE(cJSON_AddItemToArray(array, scenario_obj));
    }

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, room_scenario_import_json(root));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_get("existing", loaded));
    TEST_ASSERT_EQUAL_STRING("Existing", loaded->name);
    cJSON_Delete(root);
}

static void test_room_scenario_import_unknown_step_type_fails(void)
{
    const char *json =
        "{\"version\":1,\"room_scenarios\":[{\"id\":\"broken\",\"name\":\"Broken\","
        "\"room_id\":\"room_1\",\"steps\":[{\"id\":\"bad\",\"type\":\"WAIT_ALL\"}]}]}";
    cJSON *root = NULL;

    room_scenario_test_bootstrap();

    root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, room_scenario_import_json(root));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, room_scenario_get("broken", &s_room_scenario_work[0]));
    cJSON_Delete(root);
}

static void test_room_scenario_import_fractional_duration_fails(void)
{
    const char *json =
        "{\"version\":1,\"room_scenarios\":[{\"id\":\"broken\",\"name\":\"Broken\","
        "\"room_id\":\"room_1\",\"steps\":[{\"id\":\"wait\",\"label\":\"Wait\","
        "\"enabled\":true,\"type\":\"WAIT_TIME\",\"duration_ms\":1500.9}]}]}";
    cJSON *root = NULL;

    room_scenario_test_bootstrap();

    root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, room_scenario_import_json(root));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, room_scenario_get("broken", &s_room_scenario_work[0]));
    cJSON_Delete(root);
}

static void test_room_scenario_branch_json_round_trip(void)
{
    const char *json =
        "{\"id\":\"branched\",\"name\":\"Branched\",\"room_id\":\"room_1\",\"branches\":["
        "{\"id\":\"branch_a\",\"name\":\"Branch A\",\"enabled\":true,\"required_for_completion\":true,"
        "\"steps\":[{\"id\":\"wait_a\",\"label\":\"Wait A\",\"enabled\":true,\"type\":\"WAIT_TIME\","
        "\"duration_ms\":1000}]},"
        "{\"id\":\"finish\",\"name\":\"Finish\",\"enabled\":true,\"required_for_completion\":false,"
        "\"steps\":[{\"id\":\"end\",\"label\":\"End game\",\"enabled\":true,\"type\":\"END_GAME\"}]}"
        "]}";
    cJSON *root = NULL;
    cJSON *out = NULL;
    cJSON *branches = NULL;
    room_scenario_t *scenario = &s_room_scenario_work[0];
    room_scenario_step_type_t type = ROOM_SCENARIO_STEP_WAIT_TIME;

    memset(scenario, 0, sizeof(*scenario));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_step_type_from_str("END_GAME", &type));
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_END_GAME, type);

    root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_from_json(root, scenario));
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL_UINT(2, scenario->branch_count);
    TEST_ASSERT_EQUAL_UINT(2, scenario->step_count);
    TEST_ASSERT_EQUAL_STRING("branch_a", scenario->branches[0].id);
    TEST_ASSERT_EQUAL_UINT16(0, scenario->branches[0].step_start_index);
    TEST_ASSERT_EQUAL_UINT16(1, scenario->branches[0].step_count);
    TEST_ASSERT_EQUAL_STRING("finish", scenario->branches[1].id);
    TEST_ASSERT_EQUAL_UINT16(1, scenario->branches[1].step_start_index);
    TEST_ASSERT_EQUAL_UINT16(1, scenario->branches[1].step_count);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_WAIT_TIME, scenario->steps[0].type);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_STEP_END_GAME, scenario->steps[1].type);

    out = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_to_json(scenario, out));
    branches = cJSON_GetObjectItemCaseSensitive(out, "branches");
    TEST_ASSERT_TRUE(cJSON_IsArray(branches));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(branches));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(out, "steps"));
    cJSON_Delete(out);
}

static void test_room_scenario_reactive_policy_json_round_trip(void)
{
    const char *json =
        "{\"id\":\"reactive_policy\",\"name\":\"Reactive policy\",\"room_id\":\"room_1\",\"branches\":["
        "{\"id\":\"rx_motion\",\"name\":\"Motion reaction\",\"type\":\"reactive\",\"enabled\":true,"
        "\"required_for_completion\":false,\"priority\":20,"
        "\"policy\":{\"mode\":\"single\",\"cooldown_ms\":1500,\"max_fire_count\":3},"
        "\"reentry\":{\"mode\":\"queue_one\"},"
        "\"steps\":["
        "{\"id\":\"trigger\",\"label\":\"Trigger\",\"enabled\":true,\"type\":\"WAIT_DEVICE_EVENT\","
        "\"device_id\":\"motion\",\"event_id\":\"motion.detected\"},"
        "{\"id\":\"message\",\"label\":\"Message\",\"enabled\":true,\"type\":\"SHOW_OPERATOR_MESSAGE\","
        "\"message\":\"Motion detected\"}"
        "]}]}";
    cJSON *root = NULL;
    cJSON *out = NULL;
    cJSON *branches = NULL;
    cJSON *branch = NULL;
    cJSON *policy = NULL;
    cJSON *reentry = NULL;
    room_scenario_t *scenario = &s_room_scenario_work[0];

    memset(scenario, 0, sizeof(*scenario));

    root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_from_json(root, scenario));
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL_UINT(1, scenario->branch_count);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_BRANCH_REACTIVE, scenario->branches[0].type);
    TEST_ASSERT_EQUAL_UINT16(20, scenario->branches[0].priority);
    TEST_ASSERT_EQUAL_UINT32(1500, scenario->branches[0].cooldown_ms);
    TEST_ASSERT_EQUAL_UINT32(3, scenario->branches[0].max_fire_count);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_REENTRY_QUEUE_ONE, scenario->branches[0].reentry_mode);

    out = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_to_json(scenario, out));
    branches = cJSON_GetObjectItemCaseSensitive(out, "branches");
    branch = cJSON_GetArrayItem(branches, 0);
    policy = cJSON_GetObjectItemCaseSensitive(branch, "policy");
    reentry = cJSON_GetObjectItemCaseSensitive(branch, "reentry");
    TEST_ASSERT_TRUE(cJSON_IsObject(policy));
    TEST_ASSERT_EQUAL_INT(1500, cJSON_GetObjectItemCaseSensitive(policy, "cooldown_ms")->valueint);
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetObjectItemCaseSensitive(policy, "max_fire_count")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsObject(reentry));
    TEST_ASSERT_EQUAL_STRING("queue_one",
                             cJSON_GetObjectItemCaseSensitive(reentry, "mode")->valuestring);
    cJSON_Delete(out);
}

static void test_room_scenario_reactive_v2_json_round_trip(void)
{
    const char *json =
        "{\"id\":\"reactive_v2\",\"name\":\"Reactive v2\",\"room_id\":\"room_1\",\"branches\":["
        "{\"id\":\"rx_freeze\",\"name\":\"Freeze reaction\",\"type\":\"reactive\",\"enabled\":true,"
        "\"priority\":10,"
        "\"trigger\":{\"kind\":\"device_event\",\"device_id\":\"motion\",\"event_id\":\"motion.detected\"},"
        "\"guard_flags\":[{\"flag\":\"freeze.active\",\"value\":true}],"
        "\"policy\":{\"mode\":\"escalate\",\"cooldown_ms\":2000,\"max_fire_count\":3},"
        "\"reentry\":{\"mode\":\"queue_one\"},"
        "\"variants\":[{\"id\":\"soft\",\"label\":\"Soft\",\"actions\":["
        "{\"type\":\"DEVICE_COMMAND\",\"device_id\":\"audio\",\"command_id\":\"effect.play\","
        "\"params\":{\"file\":\"/sfx/whisper.mp3\"}},"
        "{\"type\":\"SET_FLAG\",\"flag\":\"rx.soft\",\"value\":true}"
        "]}],"
        "\"result_policy\":{\"on_done\":\"continue\",\"on_fail\":\"fail_reaction\","
        "\"on_timeout\":\"set_flag\",\"timeout_flag\":\"rx.timeout\"},"
        "\"on_complete\":[{\"type\":\"SET_FLAG\",\"flag\":\"rx.done\",\"value\":true}]"
        "}]}";
    cJSON *root = NULL;
    cJSON *out = NULL;
    cJSON *branches = NULL;
    cJSON *branch = NULL;
    cJSON *variants = NULL;
    cJSON *actions = NULL;
    cJSON *complete = NULL;
    room_scenario_t *scenario = &s_room_scenario_work[0];

    memset(scenario, 0, sizeof(*scenario));

    root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_from_json(root, scenario));
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL_UINT(1, scenario->branch_count);
    TEST_ASSERT_EQUAL_UINT(0, scenario->step_count);
    TEST_ASSERT_EQUAL_UINT(1, scenario->reactive_variant_count);
    TEST_ASSERT_EQUAL_UINT(3, scenario->reactive_action_count);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT, scenario->branches[0].trigger.kind);
    TEST_ASSERT_EQUAL_STRING("motion", scenario->branches[0].trigger.device_id);
    TEST_ASSERT_EQUAL_STRING("motion.detected", scenario->branches[0].trigger.event_id);
    TEST_ASSERT_EQUAL_UINT8(1, scenario->branches[0].guard_flag_count);
    TEST_ASSERT_EQUAL_STRING("freeze.active", scenario->branches[0].guard_flags[0].name);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_REACTIVE_POLICY_ESCALATE, scenario->branches[0].policy_mode);
    TEST_ASSERT_EQUAL_UINT32(2000, scenario->branches[0].cooldown_ms);
    TEST_ASSERT_EQUAL_UINT32(3, scenario->branches[0].max_fire_count);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_REENTRY_QUEUE_ONE, scenario->branches[0].reentry_mode);
    TEST_ASSERT_EQUAL(ROOM_SCENARIO_REACTIVE_RESULT_SET_FLAG, scenario->branches[0].result_on_timeout);
    TEST_ASSERT_EQUAL_STRING("rx.timeout", scenario->branches[0].result_flag);
    TEST_ASSERT_EQUAL_UINT8(1, scenario->branches[0].on_complete_action_count);

    out = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_to_json(scenario, out));
    branches = cJSON_GetObjectItemCaseSensitive(out, "branches");
    branch = cJSON_GetArrayItem(branches, 0);
    TEST_ASSERT_TRUE(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(branch, "trigger")));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(branch, "steps"));
    variants = cJSON_GetObjectItemCaseSensitive(branch, "variants");
    TEST_ASSERT_TRUE(cJSON_IsArray(variants));
    actions = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(variants, 0), "actions");
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(actions));
    complete = cJSON_GetObjectItemCaseSensitive(branch, "on_complete");
    TEST_ASSERT_TRUE(cJSON_IsArray(complete));
    TEST_ASSERT_EQUAL_STRING("rx.done",
                             cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(complete, 0),
                                                              "flag")->valuestring);
    cJSON_Delete(out);
}

static void test_room_scenario_reactive_v2_requires_variant_actions(void)
{
    const char *json =
        "{\"id\":\"reactive_v2_bad\",\"name\":\"Reactive v2 bad\",\"room_id\":\"room_1\",\"branches\":["
        "{\"id\":\"rx_bad\",\"name\":\"Bad reaction\",\"type\":\"reactive\",\"enabled\":true,"
        "\"trigger\":{\"kind\":\"device_event\",\"device_id\":\"motion\",\"event_id\":\"motion.detected\"},"
        "\"variants\":[{\"id\":\"empty\",\"actions\":[]}]"
        "}]}";
    cJSON *root = NULL;

    root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, room_scenario_from_json(root, &s_room_scenario_work[0]));
    cJSON_Delete(root);
}

static void test_room_scenario_reactive_v2_allows_empty_on_complete(void)
{
    const char *json =
        "{\"id\":\"reactive_v2_empty_complete\",\"name\":\"Reactive v2 empty complete\","
        "\"room_id\":\"room_1\",\"branches\":["
        "{\"id\":\"rx_empty_complete\",\"name\":\"Empty complete reaction\","
        "\"type\":\"reactive\",\"enabled\":true,"
        "\"trigger\":{\"kind\":\"device_event\",\"device_id\":\"motion\","
        "\"event_id\":\"motion.detected\"},"
        "\"variants\":[{\"id\":\"one\",\"actions\":["
        "{\"type\":\"SET_FLAG\",\"flag\":\"rx.done\",\"value\":true}"
        "]}],"
        "\"on_complete\":[]"
        "}]}";
    cJSON *root = NULL;
    room_scenario_t *scenario = &s_room_scenario_work[0];

    memset(scenario, 0, sizeof(*scenario));
    root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_from_json(root, scenario));
    TEST_ASSERT_EQUAL_UINT8(0, scenario->branches[0].on_complete_action_count);
    cJSON_Delete(root);
}

static void test_room_scenario_generation_increments_on_import_and_clear(void)
{
    const char *json =
        "{\"version\":1,\"room_scenarios\":[{\"id\":\"one\",\"name\":\"One\","
        "\"room_id\":\"room_1\",\"steps\":[]}]}";
    cJSON *root = NULL;
    uint32_t generation = 0;

    room_scenario_test_bootstrap();
    generation = room_scenario_generation();

    root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_import_json(root));
    cJSON_Delete(root);
    TEST_ASSERT_EQUAL_UINT32(generation + 1, room_scenario_generation());

    generation = room_scenario_generation();
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_clear());
    TEST_ASSERT_EQUAL_UINT32(generation + 1, room_scenario_generation());
}

static void test_room_scenario_load_missing_file_keeps_existing_store(void)
{
    room_scenario_t *existing = &s_room_scenario_work[0];
    room_scenario_t *loaded = &s_room_scenario_work[1];

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));
    init_scenario(existing, "existing", "room_1", "Existing");
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(existing));

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                      room_scenario_store_load_from_path("missing_room_scenarios_test.json"));
    TEST_ASSERT_EQUAL(ESP_OK, room_scenario_get("existing", loaded));
    TEST_ASSERT_EQUAL_STRING("Existing", loaded->name);
    TEST_ASSERT_EQUAL_STRING("/sdcard/quest/room_scenarios.json", ROOM_SCENARIO_STORAGE_PATH);
}

static void test_room_scenario_capacity_and_small_list_buffer(void)
{
    room_scenario_t *items = s_room_scenario_items;
    room_scenario_t *overflow = &s_room_scenario_work[1];
    size_t count = 0;

    room_scenario_test_bootstrap();
    memset(s_room_scenario_work, 0, sizeof(s_room_scenario_work));
    memset(items, 0, sizeof(s_room_scenario_items));

    for (size_t i = 0; i < ROOM_SCENARIO_MAX_SCENARIOS; ++i) {
        char id[ROOM_SCENARIO_ID_MAX_LEN] = {0};
        room_scenario_t *scenario = &s_room_scenario_work[0];
        snprintf(id, sizeof(id), "scenario_%02u", (unsigned)i);
        init_scenario(scenario, id, "room_a", "Capacity item");
        TEST_ASSERT_EQUAL(ESP_OK, room_scenario_add(scenario));
    }

    init_scenario(overflow, "overflow", "room_a", "Overflow");
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, room_scenario_add(overflow));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, room_scenario_list_by_room("room_a", items, 2, &count));
    TEST_ASSERT_EQUAL_UINT(ROOM_SCENARIO_MAX_SCENARIOS, count);
    TEST_ASSERT_EQUAL_STRING("scenario_00", items[0].id);
    TEST_ASSERT_EQUAL_STRING("scenario_01", items[1].id);
}

void register_room_scenario_tests(void)
{
    RUN_TEST(test_room_scenario_add_and_get_full_flow);
    RUN_TEST(test_room_scenario_list_by_room_filters_and_counts);
    RUN_TEST(test_room_scenario_registry_lists_details_read_only);
    RUN_TEST(test_room_scenario_replace_existing_id);
    RUN_TEST(test_room_scenario_validation_rejects_invalid_steps);
    RUN_TEST(test_room_scenario_rejects_duplicate_step_id);
    RUN_TEST(test_room_scenario_allows_same_step_id_in_different_branches);
    RUN_TEST(test_room_scenario_rejects_duplicate_branch_id);
    RUN_TEST(test_room_scenario_rejects_branch_step_range_outside_steps);
    RUN_TEST(test_room_scenario_rejects_reactive_branch_without_trigger_step);
    RUN_TEST(test_room_scenario_rejects_reactive_branch_disabled_first_non_trigger_step);
    RUN_TEST(test_room_scenario_rejects_reactive_wait_flags_without_guard);
    RUN_TEST(test_room_scenario_validation_code_buffer_fits_long_codes);
    RUN_TEST(test_room_scenario_removed_raw_wait_event_type_is_rejected);
    RUN_TEST(test_room_scenario_single_json_round_trip);
    RUN_TEST(test_room_scenario_export_empty_store);
    RUN_TEST(test_room_scenario_export_all_step_types);
    RUN_TEST(test_room_scenario_import_valid_json_restores_scenarios);
    RUN_TEST(test_room_scenario_import_invalid_json_keeps_existing_store);
    RUN_TEST(test_room_scenario_import_over_limit_fails_safely);
    RUN_TEST(test_room_scenario_import_unknown_step_type_fails);
    RUN_TEST(test_room_scenario_import_fractional_duration_fails);
    RUN_TEST(test_room_scenario_command_group_json_preserves_params);
    RUN_TEST(test_room_scenario_branch_json_round_trip);
    RUN_TEST(test_room_scenario_reactive_policy_json_round_trip);
    RUN_TEST(test_room_scenario_reactive_v2_json_round_trip);
    RUN_TEST(test_room_scenario_reactive_v2_requires_variant_actions);
    RUN_TEST(test_room_scenario_reactive_v2_allows_empty_on_complete);
    RUN_TEST(test_room_scenario_generation_increments_on_import_and_clear);
    RUN_TEST(test_room_scenario_load_missing_file_keeps_existing_store);
    RUN_TEST(test_room_scenario_capacity_and_small_list_buffer);
}
