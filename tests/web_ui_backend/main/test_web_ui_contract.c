#include <string.h>

#include "unity.h"

#include "cJSON.h"
#include "esp_attr.h"
#include "orchestrator/orchestrator_api_view.h"
#include "web_ui_handlers.h"
#include "ws_runtime.h"

EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_web_snapshot;
EXT_RAM_BSS_ATTR static orch_room_scenario_detail_t s_web_scenario_details[1];
EXT_RAM_BSS_ATTR static orch_control_device_entry_t s_web_control_devices[1];
EXT_RAM_BSS_ATTR static orchestrator_audit_entry_t s_web_audit_entries[1];
EXT_RAM_BSS_ATTR static orchestrator_timeline_entry_t s_web_timeline_entries[1];

static void web_copy(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static cJSON *web_required_item(cJSON *obj, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    TEST_ASSERT_NOT_NULL_MESSAGE(item, name);
    return item;
}

static cJSON *web_required_array(cJSON *obj, const char *name)
{
    cJSON *item = web_required_item(obj, name);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsArray(item), name);
    return item;
}

static cJSON *web_required_object(cJSON *obj, const char *name)
{
    cJSON *item = web_required_item(obj, name);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsObject(item), name);
    return item;
}

static void web_expect_string(cJSON *obj, const char *name, const char *expected)
{
    cJSON *item = web_required_item(obj, name);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsString(item), name);
    TEST_ASSERT_EQUAL_STRING(expected, item->valuestring);
}

static void web_expect_number(cJSON *obj, const char *name, int expected)
{
    cJSON *item = web_required_item(obj, name);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsNumber(item), name);
    TEST_ASSERT_EQUAL(expected, item->valueint);
}

static void web_expect_bool(cJSON *obj, const char *name, bool expected)
{
    cJSON *item = web_required_item(obj, name);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsBool(item), name);
    TEST_ASSERT_EQUAL(expected, cJSON_IsTrue(item));
}

static void web_prepare_snapshot(void)
{
    memset(&s_web_snapshot, 0, sizeof(s_web_snapshot));

    s_web_snapshot.generation = 42;
    web_copy(s_web_snapshot.active_profile, sizeof(s_web_snapshot.active_profile), "profile_main");
    s_web_snapshot.room_count = 1;
    s_web_snapshot.device_count = 1;
    s_web_snapshot.issue_count = 1;
    s_web_snapshot.active_session_count = 1;
    s_web_snapshot.active_hint_count = 1;
    s_web_snapshot.has_degraded = true;

    web_copy(s_web_snapshot.rooms[0].room_id, sizeof(s_web_snapshot.rooms[0].room_id), "room_a");
    web_copy(s_web_snapshot.rooms[0].title, sizeof(s_web_snapshot.rooms[0].title), "Room A");
    web_copy(s_web_snapshot.rooms[0].session_state, sizeof(s_web_snapshot.rooms[0].session_state), "running");
    web_copy(s_web_snapshot.rooms[0].timer_state, sizeof(s_web_snapshot.rooms[0].timer_state), "running");
    web_copy(s_web_snapshot.rooms[0].hint_message, sizeof(s_web_snapshot.rooms[0].hint_message), "Look up");
    web_copy(s_web_snapshot.rooms[0].selected_profile_id,
             sizeof(s_web_snapshot.rooms[0].selected_profile_id),
             "profile_main");
    web_copy(s_web_snapshot.rooms[0].selected_scenario_id,
             sizeof(s_web_snapshot.rooms[0].selected_scenario_id),
             "scenario_main");
    web_copy(s_web_snapshot.rooms[0].running_scenario_id,
             sizeof(s_web_snapshot.rooms[0].running_scenario_id),
             "scenario_main");
    web_copy(s_web_snapshot.rooms[0].scenario_wait_event_type,
             sizeof(s_web_snapshot.rooms[0].scenario_wait_event_type),
             "door_opened");
    web_copy(s_web_snapshot.rooms[0].scenario_wait_source_id,
             sizeof(s_web_snapshot.rooms[0].scenario_wait_source_id),
             "quest/relay/event");
    web_copy(s_web_snapshot.rooms[0].scenario_current_step_text,
             sizeof(s_web_snapshot.rooms[0].scenario_current_step_text),
             "Open relay");
    web_copy(s_web_snapshot.rooms[0].scenario_wait_summary,
             sizeof(s_web_snapshot.rooms[0].scenario_wait_summary),
             "relay: door_opened");
    web_copy(s_web_snapshot.rooms[0].scenario_flags[0].name,
             sizeof(s_web_snapshot.rooms[0].scenario_flags[0].name),
             "door_opened_seen");
    s_web_snapshot.rooms[0].scenario_flags[0].value = true;
    web_copy(s_web_snapshot.rooms[0].scenario_device_ids[0],
             sizeof(s_web_snapshot.rooms[0].scenario_device_ids[0]),
             "relay");
    s_web_snapshot.rooms[0].scenario_device_count = 1;
    web_copy(s_web_snapshot.rooms[0].related_issue_ids[0],
             sizeof(s_web_snapshot.rooms[0].related_issue_ids[0]),
             "issue_1");
    s_web_snapshot.rooms[0].related_issue_count = 1;
    web_copy(s_web_snapshot.rooms[0].scenario_branches[0].id,
             sizeof(s_web_snapshot.rooms[0].scenario_branches[0].id),
             "main");
    web_copy(s_web_snapshot.rooms[0].scenario_branches[0].name,
             sizeof(s_web_snapshot.rooms[0].scenario_branches[0].name),
             "Main");
    web_copy(s_web_snapshot.rooms[0].scenario_branches[0].current_step_text,
             sizeof(s_web_snapshot.rooms[0].scenario_branches[0].current_step_text),
             "Open relay");
    web_copy(s_web_snapshot.rooms[0].scenario_branches[0].wait_summary,
             sizeof(s_web_snapshot.rooms[0].scenario_branches[0].wait_summary),
             "relay: door_opened");
    s_web_snapshot.rooms[0].sort_order = 3;
    s_web_snapshot.rooms[0].device_count = 1;
    s_web_snapshot.rooms[0].active_device_count = 1;
    s_web_snapshot.rooms[0].issue_count = 1;
    s_web_snapshot.rooms[0].session_present = true;
    s_web_snapshot.rooms[0].hint_active = true;
    s_web_snapshot.rooms[0].hint_sent_count = 2;
    s_web_snapshot.rooms[0].timer_duration_ms = 90000;
    s_web_snapshot.rooms[0].timer_remaining_ms = 80000;
    s_web_snapshot.rooms[0].selected_profile_duration_ms = 90000;
    s_web_snapshot.rooms[0].scenario_runtime_state = ORCH_ROOM_SCENARIO_RUNTIME_WAITING;
    s_web_snapshot.rooms[0].scenario_total_steps = 3;
    s_web_snapshot.rooms[0].scenario_done_steps = 1;
    s_web_snapshot.rooms[0].scenario_wait_type = ORCH_ROOM_SCENARIO_WAIT_DEVICE_EVENT;
    s_web_snapshot.rooms[0].scenario_flag_count = 1;
    s_web_snapshot.rooms[0].scenario_branch_count = 1;
    s_web_snapshot.rooms[0].scenario_branches[0].active = true;
    s_web_snapshot.rooms[0].scenario_branches[0].type = ROOM_SCENARIO_BRANCH_NORMAL;
    web_copy(s_web_snapshot.rooms[0].scenario_branches[0].type_text,
             sizeof(s_web_snapshot.rooms[0].scenario_branches[0].type_text),
             "normal");
    web_copy(s_web_snapshot.rooms[0].scenario_branches[0].reentry_mode_text,
             sizeof(s_web_snapshot.rooms[0].scenario_branches[0].reentry_mode_text),
             "ignore");
    s_web_snapshot.rooms[0].scenario_branches[0].current_step_state = ORCH_ROOM_SCENARIO_STEP_STATE_WAITING;
    s_web_snapshot.rooms[0].scenario_branches[0].state = ORCH_ROOM_SCENARIO_RUNTIME_WAITING;
    s_web_snapshot.rooms[0].scenario_branches[0].wait_type = ORCH_ROOM_SCENARIO_WAIT_DEVICE_EVENT;
    s_web_snapshot.rooms[0].health = ORCH_HEALTH_DEGRADED;
    web_copy(s_web_snapshot.rooms[0].health_text,
             sizeof(s_web_snapshot.rooms[0].health_text),
             "degraded");
    web_copy(s_web_snapshot.rooms[0].scenario_runtime_state_text,
             sizeof(s_web_snapshot.rooms[0].scenario_runtime_state_text),
             "waiting");
    web_copy(s_web_snapshot.rooms[0].scenario_wait_type_text,
             sizeof(s_web_snapshot.rooms[0].scenario_wait_type_text),
             "event");
    web_copy(s_web_snapshot.rooms[0].scenario_branches[0].current_step_state_text,
             sizeof(s_web_snapshot.rooms[0].scenario_branches[0].current_step_state_text),
             "waiting");
    web_copy(s_web_snapshot.rooms[0].scenario_branches[0].state_text,
             sizeof(s_web_snapshot.rooms[0].scenario_branches[0].state_text),
             "waiting");
    web_copy(s_web_snapshot.rooms[0].scenario_branches[0].wait_type_text,
             sizeof(s_web_snapshot.rooms[0].scenario_branches[0].wait_type_text),
             "event");

    web_copy(s_web_snapshot.devices[0].device_id, sizeof(s_web_snapshot.devices[0].device_id), "relay");
    web_copy(s_web_snapshot.devices[0].client_id, sizeof(s_web_snapshot.devices[0].client_id), "relay_client");
    web_copy(s_web_snapshot.devices[0].display_name, sizeof(s_web_snapshot.devices[0].display_name), "Relay");
    web_copy(s_web_snapshot.devices[0].state, sizeof(s_web_snapshot.devices[0].state), "armed");
    web_copy(s_web_snapshot.devices[0].fw_version, sizeof(s_web_snapshot.devices[0].fw_version), "1.2.3");
    web_copy(s_web_snapshot.devices[0].badges[0],
             sizeof(s_web_snapshot.devices[0].badges[0]),
             "armed");
    s_web_snapshot.devices[0].connectivity = ORCH_CONNECTIVITY_ONLINE;
    s_web_snapshot.devices[0].health = ORCH_HEALTH_OK;
    s_web_snapshot.devices[0].runtime_state = ORCH_RUNTIME_STATE_ARMED;
    s_web_snapshot.devices[0].badge_count = 1;
    s_web_snapshot.devices[0].has_runtime = true;
    web_copy(s_web_snapshot.devices[0].connectivity_text,
             sizeof(s_web_snapshot.devices[0].connectivity_text),
             "online");
    web_copy(s_web_snapshot.devices[0].health_text,
             sizeof(s_web_snapshot.devices[0].health_text),
             "ok");
    web_copy(s_web_snapshot.devices[0].runtime_state_text,
             sizeof(s_web_snapshot.devices[0].runtime_state_text),
             "armed");

    web_copy(s_web_snapshot.issues[0].issue_id, sizeof(s_web_snapshot.issues[0].issue_id), "issue_1");
    web_copy(s_web_snapshot.issues[0].room_id, sizeof(s_web_snapshot.issues[0].room_id), "room_a");
    web_copy(s_web_snapshot.issues[0].code, sizeof(s_web_snapshot.issues[0].code), "ROOM_DEGRADED");
    web_copy(s_web_snapshot.issues[0].title, sizeof(s_web_snapshot.issues[0].title), "Room degraded");
    s_web_snapshot.issues[0].scope = ORCH_ISSUE_SCOPE_ROOM;
    s_web_snapshot.issues[0].severity = ORCH_ISSUE_SEVERITY_WARNING;
    s_web_snapshot.issues[0].active = true;
    web_copy(s_web_snapshot.issues[0].scope_text,
             sizeof(s_web_snapshot.issues[0].scope_text),
             "room");
    web_copy(s_web_snapshot.issues[0].severity_text,
             sizeof(s_web_snapshot.issues[0].severity_text),
             "warning");
}

static void test_web_ui_gm_state_contract_contains_summary_rooms_devices_and_issues(void)
{
    cJSON *root = NULL;
    cJSON *summary = NULL;
    cJSON *rooms = NULL;
    cJSON *room = NULL;
    cJSON *devices = NULL;
    cJSON *device = NULL;
    cJSON *issues = NULL;
    cJSON *issue = NULL;
    cJSON *branches = NULL;
    cJSON *flags = NULL;

    web_prepare_snapshot();

    root = orchestrator_api_view_gm_state(&s_web_snapshot);
    TEST_ASSERT_NOT_NULL(root);
    web_expect_bool(root, "ok", true);
    web_expect_number(root, "generation", 42);
    web_expect_string(root, "active_profile", "profile_main");

    summary = web_required_object(root, "summary");
    web_expect_number(summary, "rooms_total", 1);
    web_expect_number(summary, "devices_total", 1);
    web_expect_number(summary, "issues_total", 1);
    web_expect_number(summary, "active_sessions", 1);
    web_expect_number(summary, "active_hints", 1);
    web_expect_bool(summary, "has_degraded", true);
    web_expect_bool(summary, "has_fault", false);

    rooms = web_required_array(root, "rooms");
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(rooms));
    room = cJSON_GetArrayItem(rooms, 0);
    TEST_ASSERT_NOT_NULL(room);
    web_expect_string(room, "room_id", "room_a");
    web_expect_string(room, "title", "Room A");
    web_expect_number(room, "sort_order", 3);
    web_expect_string(room, "health", "degraded");
    web_expect_string(room, "session_state", "running");
    web_expect_string(room, "timer_state", "running");
    web_expect_bool(room, "hint_active", true);
    web_expect_string(room, "selected_profile_id", "profile_main");
    web_expect_string(room, "running_scenario_id", "scenario_main");
    web_expect_string(room, "scenario_runtime_state", "waiting");
    web_expect_number(room, "scenario_total_steps", 3);
    web_expect_number(room, "scenario_done_steps", 1);
    web_expect_string(room, "scenario_wait_type", "event");
    web_expect_string(room, "scenario_current_step_text", "Open relay");
    web_expect_string(room, "scenario_wait_summary", "relay: door_opened");
    web_expect_string(room, "scenario_wait_event_type", "door_opened");
    TEST_ASSERT_EQUAL(1, cJSON_GetObjectItem(room, "scenario_device_count")->valueint);
    TEST_ASSERT_EQUAL_STRING("relay",
                             cJSON_GetArrayItem(web_required_array(room, "scenario_device_ids"), 0)->valuestring);
    TEST_ASSERT_EQUAL(1, cJSON_GetObjectItem(room, "related_issue_count")->valueint);
    TEST_ASSERT_EQUAL_STRING("issue_1",
                             cJSON_GetArrayItem(web_required_array(room, "related_issue_ids"), 0)->valuestring);
    flags = web_required_array(room, "scenario_flags");
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(flags));
    web_expect_string(cJSON_GetArrayItem(flags, 0), "name", "door_opened_seen");
    branches = web_required_array(room, "scenario_branches");
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(branches));
    web_expect_string(cJSON_GetArrayItem(branches, 0), "id", "main");
    web_expect_string(cJSON_GetArrayItem(branches, 0), "state", "waiting");
    web_expect_string(cJSON_GetArrayItem(branches, 0), "current_step_text", "Open relay");
    web_expect_string(cJSON_GetArrayItem(branches, 0), "wait_summary", "relay: door_opened");

    devices = web_required_array(root, "devices");
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(devices));
    device = cJSON_GetArrayItem(devices, 0);
    TEST_ASSERT_NOT_NULL(device);
    web_expect_string(device, "device_id", "relay");
    web_expect_string(device, "client_id", "relay_client");
    web_expect_string(device, "display_name", "Relay");
    web_expect_string(device, "kind", "control_contract");
    web_expect_string(device, "health", "ok");
    web_expect_string(device, "connectivity", "online");
    web_expect_string(device, "runtime_state", "armed");
    TEST_ASSERT_TRUE(cJSON_IsArray(web_required_item(device, "badges")));
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(web_required_array(device, "badges")));
    TEST_ASSERT_EQUAL_STRING("armed", cJSON_GetArrayItem(web_required_array(device, "badges"), 0)->valuestring);

    issues = web_required_array(root, "issues");
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(issues));
    issue = cJSON_GetArrayItem(issues, 0);
    TEST_ASSERT_NOT_NULL(issue);
    web_expect_string(issue, "issue_id", "issue_1");
    web_expect_string(issue, "scope", "room");
    web_expect_string(issue, "severity", "warning");
    web_expect_string(issue, "code", "ROOM_DEGRADED");
    web_expect_bool(issue, "active", true);

    cJSON_Delete(root);
}

static void test_web_ui_meta_contract_contains_identity_capabilities_and_limits(void)
{
    cJSON *root = NULL;
    cJSON *capabilities = NULL;
    cJSON *limits = NULL;
    cJSON *firmware_version = NULL;

    root = web_ui_build_meta_json();
    TEST_ASSERT_NOT_NULL(root);

    web_expect_string(root, "product_id", "scenehub-controller");
    web_expect_string(root, "device_id", "scenehub");
    web_expect_string(root, "device_name", "SceneHub");
    web_expect_string(root, "hostname", "scenehub");
    web_expect_number(root, "api_version", 1);

    firmware_version = web_required_item(root, "firmware_version");
    TEST_ASSERT_TRUE(cJSON_IsString(firmware_version));
    TEST_ASSERT_TRUE(firmware_version->valuestring[0] != '\0');

    capabilities = web_required_object(root, "capabilities");
    web_expect_bool(capabilities, "gm", true);
    web_expect_bool(capabilities, "ota", true);
    web_expect_bool(capabilities, "audio", true);
    web_expect_bool(capabilities, "hardware_io", true);
    web_expect_bool(capabilities, "ws", ws_runtime_available());

    limits = web_required_object(root, "limits");
    web_expect_number(limits, "max_rooms", 2);
    web_expect_number(limits, "max_devices", 20);
    web_expect_number(limits, "max_ws_clients", ws_runtime_max_clients());

    cJSON_Delete(root);
}

static void test_web_ui_room_scenarios_contract_contains_steps_params_and_validation(void)
{
    cJSON *root = NULL;
    cJSON *scenarios = NULL;
    cJSON *scenario = NULL;
    cJSON *steps = NULL;
    cJSON *step = NULL;
    cJSON *branches = NULL;
    cJSON *branch = NULL;
    cJSON *params = NULL;
    cJSON *issues = NULL;

    memset(s_web_scenario_details, 0, sizeof(s_web_scenario_details));
    web_copy(s_web_scenario_details[0].summary.room_id,
             sizeof(s_web_scenario_details[0].summary.room_id),
             "room_a");
    web_copy(s_web_scenario_details[0].summary.id,
             sizeof(s_web_scenario_details[0].summary.id),
             "scenario_a");
    web_copy(s_web_scenario_details[0].summary.name,
             sizeof(s_web_scenario_details[0].summary.name),
             "Scenario A");
    s_web_scenario_details[0].summary.step_count = 1;
    s_web_scenario_details[0].summary.valid = false;
    s_web_scenario_details[0].summary.validation_issue_count = 1;
    web_copy(s_web_scenario_details[0].steps[0].id,
             sizeof(s_web_scenario_details[0].steps[0].id),
             "cmd");
    web_copy(s_web_scenario_details[0].steps[0].label,
             sizeof(s_web_scenario_details[0].steps[0].label),
             "Open relay");
    web_copy(s_web_scenario_details[0].steps[0].device_id,
             sizeof(s_web_scenario_details[0].steps[0].device_id),
             "relay");
    web_copy(s_web_scenario_details[0].steps[0].command_id,
             sizeof(s_web_scenario_details[0].steps[0].command_id),
             "open");
    web_copy(s_web_scenario_details[0].steps[0].params_json,
             sizeof(s_web_scenario_details[0].steps[0].params_json),
             "{\"pulse_ms\":250}");
    s_web_scenario_details[0].steps[0].type = ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND;
    web_copy(s_web_scenario_details[0].steps[0].type_text,
             sizeof(s_web_scenario_details[0].steps[0].type_text),
             "device_command");
    s_web_scenario_details[0].steps[0].enabled = true;
    s_web_scenario_details[0].branch_count = 1;
    web_copy(s_web_scenario_details[0].branches[0].id,
             sizeof(s_web_scenario_details[0].branches[0].id),
             "main");
    web_copy(s_web_scenario_details[0].branches[0].name,
             sizeof(s_web_scenario_details[0].branches[0].name),
             "Main");
    s_web_scenario_details[0].branches[0].type = ROOM_SCENARIO_BRANCH_NORMAL;
    web_copy(s_web_scenario_details[0].branches[0].type_text,
             sizeof(s_web_scenario_details[0].branches[0].type_text),
             "normal");
    s_web_scenario_details[0].branches[0].enabled = true;
    s_web_scenario_details[0].branches[0].required_for_completion = true;
    s_web_scenario_details[0].branches[0].step_start_index = 0;
    s_web_scenario_details[0].branches[0].step_count = 1;
    web_copy(s_web_scenario_details[0].validation_issues[0].code,
             sizeof(s_web_scenario_details[0].validation_issues[0].code),
             "DEVICE_MISSING");
    web_copy(s_web_scenario_details[0].validation_issues[0].message,
             sizeof(s_web_scenario_details[0].validation_issues[0].message),
             "Device missing");
    s_web_scenario_details[0].validation_issues[0].level = ROOM_SCENARIO_VALIDATION_ERROR;
    web_copy(s_web_scenario_details[0].validation_issues[0].level_text,
             sizeof(s_web_scenario_details[0].validation_issues[0].level_text),
             "error");

    root = orchestrator_api_view_room_scenarios("room_a", s_web_scenario_details, 1);
    TEST_ASSERT_NOT_NULL(root);
    web_expect_string(root, "room_id", "room_a");
    web_expect_number(root, "count", 1);
    scenarios = web_required_array(root, "scenarios");
    scenario = cJSON_GetArrayItem(scenarios, 0);
    TEST_ASSERT_NOT_NULL(scenario);
    web_expect_string(scenario, "id", "scenario_a");
    web_expect_string(scenario, "name", "Scenario A");
    web_expect_number(scenario, "step_count", 1);
    web_expect_bool(scenario, "valid", false);
    web_expect_number(scenario, "validation_issue_count", 1);
    steps = web_required_array(scenario, "steps");
    step = cJSON_GetArrayItem(steps, 0);
    TEST_ASSERT_NOT_NULL(step);
    web_expect_string(step, "id", "cmd");
    web_expect_string(step, "type", "device_command");
    web_expect_bool(step, "enabled", true);
    web_expect_string(step, "device_id", "relay");
    web_expect_string(step, "command_id", "open");
    params = web_required_object(step, "params");
    web_expect_number(params, "pulse_ms", 250);
    TEST_ASSERT_TRUE(cJSON_IsArray(web_required_item(step, "events")));
    TEST_ASSERT_TRUE(cJSON_IsArray(web_required_item(step, "flags")));
    branches = web_required_array(scenario, "branches");
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(branches));
    branch = cJSON_GetArrayItem(branches, 0);
    TEST_ASSERT_NOT_NULL(branch);
    web_expect_string(branch, "id", "main");
    steps = web_required_array(branch, "steps");
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(steps));
    issues = web_required_array(scenario, "validation_issues");
    web_expect_string(cJSON_GetArrayItem(issues, 0), "level", "error");
    web_expect_string(cJSON_GetArrayItem(issues, 0), "code", "DEVICE_MISSING");

    cJSON_Delete(root);
}

static void test_web_ui_audit_and_timeline_contracts_are_stable(void)
{
    cJSON *audit = NULL;
    cJSON *timeline = NULL;
    cJSON *items = NULL;

    memset(s_web_audit_entries, 0, sizeof(s_web_audit_entries));
    s_web_audit_entries[0].timestamp_ms = 1234;
    web_copy(s_web_audit_entries[0].source, sizeof(s_web_audit_entries[0].source), "gm");
    web_copy(s_web_audit_entries[0].device_id, sizeof(s_web_audit_entries[0].device_id), "relay");
    web_copy(s_web_audit_entries[0].action_id, sizeof(s_web_audit_entries[0].action_id), "open");
    s_web_audit_entries[0].success = false;
    web_copy(s_web_audit_entries[0].error_code, sizeof(s_web_audit_entries[0].error_code), "timeout");

    audit = orchestrator_api_view_audit_recent(s_web_audit_entries, 1);
    TEST_ASSERT_NOT_NULL(audit);
    web_expect_bool(audit, "ok", true);
    web_expect_number(audit, "count", 1);
    items = web_required_array(audit, "items");
    web_expect_number(cJSON_GetArrayItem(items, 0), "timestamp_ms", 1234);
    web_expect_string(cJSON_GetArrayItem(items, 0), "source", "gm");
    web_expect_string(cJSON_GetArrayItem(items, 0), "device_id", "relay");
    web_expect_string(cJSON_GetArrayItem(items, 0), "action_id", "open");
    web_expect_bool(cJSON_GetArrayItem(items, 0), "success", false);
    web_expect_string(cJSON_GetArrayItem(items, 0), "error_code", "timeout");
    cJSON_Delete(audit);

    memset(s_web_timeline_entries, 0, sizeof(s_web_timeline_entries));
    s_web_timeline_entries[0].timestamp_ms = 2345;
    s_web_timeline_entries[0].type = ORCH_TIMELINE_TYPE_ACTION_FAILED;
    web_copy(s_web_timeline_entries[0].type_text,
             sizeof(s_web_timeline_entries[0].type_text),
             "action_failed");
    s_web_timeline_entries[0].severity = ORCH_TIMELINE_SEVERITY_ERROR;
    web_copy(s_web_timeline_entries[0].severity_text,
             sizeof(s_web_timeline_entries[0].severity_text),
             "error");
    web_copy(s_web_timeline_entries[0].source, sizeof(s_web_timeline_entries[0].source), "gm");
    web_copy(s_web_timeline_entries[0].room_id, sizeof(s_web_timeline_entries[0].room_id), "room_a");
    web_copy(s_web_timeline_entries[0].device_id, sizeof(s_web_timeline_entries[0].device_id), "relay");
    web_copy(s_web_timeline_entries[0].title, sizeof(s_web_timeline_entries[0].title), "Action failed");
    web_copy(s_web_timeline_entries[0].details, sizeof(s_web_timeline_entries[0].details), "Timeout");

    timeline = orchestrator_api_view_timeline_recent(s_web_timeline_entries, 1);
    TEST_ASSERT_NOT_NULL(timeline);
    web_expect_bool(timeline, "ok", true);
    web_expect_number(timeline, "count", 1);
    items = web_required_array(timeline, "items");
    web_expect_number(cJSON_GetArrayItem(items, 0), "timestamp_ms", 2345);
    web_expect_string(cJSON_GetArrayItem(items, 0), "type", "action_failed");
    web_expect_string(cJSON_GetArrayItem(items, 0), "severity", "error");
    web_expect_string(cJSON_GetArrayItem(items, 0), "room_id", "room_a");
    web_expect_string(cJSON_GetArrayItem(items, 0), "device_id", "relay");
    web_expect_string(cJSON_GetArrayItem(items, 0), "title", "Action failed");
    cJSON_Delete(timeline);
}

static void test_web_ui_control_devices_contract_maps_connectivity_and_health(void)
{
    cJSON *root = NULL;
    cJSON *items = NULL;
    cJSON *device = NULL;

    memset(s_web_control_devices, 0, sizeof(s_web_control_devices));
    web_copy(s_web_control_devices[0].device_id, sizeof(s_web_control_devices[0].device_id), "relay");
    web_copy(s_web_control_devices[0].fw_version,
             sizeof(s_web_control_devices[0].fw_version),
             "1.2.3");
    web_copy(s_web_control_devices[0].boot_id,
             sizeof(s_web_control_devices[0].boot_id),
             "boot-a");
    web_copy(s_web_control_devices[0].mode, sizeof(s_web_control_devices[0].mode), "game");
    web_copy(s_web_control_devices[0].state, sizeof(s_web_control_devices[0].state), "armed");
    s_web_control_devices[0].connectivity = ORCH_CONNECTIVITY_ONLINE;
    s_web_control_devices[0].health = ORCH_HEALTH_DEGRADED;
    s_web_control_devices[0].last_seen_ms = 1000;
    s_web_control_devices[0].has_heartbeat = true;
    s_web_control_devices[0].has_status = true;
    web_copy(s_web_control_devices[0].connectivity_text,
             sizeof(s_web_control_devices[0].connectivity_text),
             "online");
    web_copy(s_web_control_devices[0].health_text,
             sizeof(s_web_control_devices[0].health_text),
             "degraded");

    root = orchestrator_api_view_control_devices(s_web_control_devices, 1, 2000);
    TEST_ASSERT_NOT_NULL(root);
    web_expect_bool(root, "ok", true);
    web_expect_number(root, "count", 1);
    items = web_required_array(root, "items");
    device = cJSON_GetArrayItem(items, 0);
    TEST_ASSERT_NOT_NULL(device);
    web_expect_string(device, "device_id", "relay");
    web_expect_string(device, "connectivity", "online");
    web_expect_string(device, "health", "degraded");
    web_expect_number(device, "last_seen_ms", 1000);
    web_expect_string(device, "fw_version", "1.2.3");
    web_expect_string(device, "boot_id", "boot-a");
    web_expect_string(device, "mode", "game");
    web_expect_string(device, "state", "armed");
    web_expect_bool(device, "has_heartbeat", true);
    web_expect_bool(device, "has_status", true);
    web_expect_bool(device, "has_diag", false);
    web_expect_bool(device, "has_result", false);
    cJSON_Delete(root);
}

void register_web_ui_contract_tests(void)
{
    RUN_TEST(test_web_ui_gm_state_contract_contains_summary_rooms_devices_and_issues);
    RUN_TEST(test_web_ui_meta_contract_contains_identity_capabilities_and_limits);
    RUN_TEST(test_web_ui_room_scenarios_contract_contains_steps_params_and_validation);
    RUN_TEST(test_web_ui_audit_and_timeline_contracts_are_stable);
    RUN_TEST(test_web_ui_control_devices_contract_maps_connectivity_and_health);
}
