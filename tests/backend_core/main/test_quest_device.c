#include <string.h>

#include "unity.h"

#include "cJSON.h"
#include "quest_device.h"

static void qd_test_set(char *dst, size_t dst_len, const char *src)
{
    size_t len = strlen(src);
    TEST_ASSERT_TRUE(len < dst_len);
    memcpy(dst, src, len + 1);
}

static void qd_test_bootstrap(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_init());
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_clear());
}

static void qd_test_fill_device(quest_device_t *device,
                                const char *id,
                                const char *client_id,
                                const char *name)
{
    memset(device, 0, sizeof(*device));
    qd_test_set(device->id, sizeof(device->id), id);
    qd_test_set(device->client_id, sizeof(device->client_id), client_id);
    qd_test_set(device->name, sizeof(device->name), name);
    device->enabled = true;
}

static void qd_test_fill_command(quest_device_command_t *command,
                                 const char *id,
                                 const char *command_name)
{
    memset(command, 0, sizeof(*command));
    qd_test_set(command->id, sizeof(command->id), id);
    qd_test_set(command->label, sizeof(command->label), id);
    qd_test_set(command->capability, sizeof(command->capability), "relay");
    qd_test_set(command->command, sizeof(command->command), command_name);
    command->manual_allowed = true;
    command->scenario_allowed = true;
    command->requires_confirmation = false;
    command->result_required = true;
    command->timeout_ms = QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS;
    qd_test_set(command->danger_level, sizeof(command->danger_level), "normal");
}

static void qd_test_fill_event(quest_device_event_t *event,
                               const char *id,
                               const char *event_name)
{
    memset(event, 0, sizeof(*event));
    qd_test_set(event->id, sizeof(event->id), id);
    qd_test_set(event->label, sizeof(event->label), id);
    qd_test_set(event->capability, sizeof(event->capability), "input");
    qd_test_set(event->event, sizeof(event->event), event_name);
}

static void test_quest_device_rejects_duplicate_client_id(void)
{
    quest_device_t first = {0};
    quest_device_t second = {0};

    qd_test_bootstrap();

    qd_test_fill_device(&first, "relay", "client_1", "Relay");
    qd_test_fill_command(&first.commands[0], "open", "quest/relay/open");
    first.command_count = 1;
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&first));

    qd_test_fill_device(&second, "altar", "client_1", "Altar");
    qd_test_fill_command(&second.commands[0], "reset", "quest/altar/reset");
    second.command_count = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, quest_device_upsert(&second));

    qd_test_set(first.name, sizeof(first.name), "Relay updated");
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&first));
}

static void test_quest_device_rejects_duplicate_command_id(void)
{
    quest_device_t device = {0};

    qd_test_bootstrap();

    qd_test_fill_device(&device, "relay", "client_1", "Relay");
    qd_test_fill_command(&device.commands[0], "open", "quest/relay/open");
    qd_test_fill_command(&device.commands[1], "open", "quest/relay/open_again");
    device.command_count = 2;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, quest_device_upsert(&device));
}

static void test_quest_device_rejects_duplicate_event_id(void)
{
    quest_device_t device = {0};

    qd_test_bootstrap();

    qd_test_fill_device(&device, "uid_gate", "client_1", "UID Gate");
    qd_test_fill_event(&device.events[0], "success", "quest/uid/event");
    qd_test_fill_event(&device.events[1], "success", "quest/uid/event_again");
    device.event_count = 2;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, quest_device_upsert(&device));
}

static void test_system_audio_play_command_exposes_background_repeat_params(void)
{
    quest_device_command_t command = {0};

    qd_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get_command(QUEST_DEVICE_SYSTEM_AUDIO_ID, "play", &command));
    TEST_ASSERT_EQUAL_STRING("audio", command.capability);
    TEST_ASSERT_EQUAL_STRING("audio.play", command.command);
    TEST_ASSERT_EQUAL_UINT8(4, command.param_count);
    TEST_ASSERT_EQUAL_STRING("file", command.params[0].key);
    TEST_ASSERT_EQUAL(QUEST_DEVICE_COMMAND_PARAM_AUDIO_FILE_SELECT, command.params[0].type);
    TEST_ASSERT_EQUAL_STRING("volume", command.params[1].key);
    TEST_ASSERT_EQUAL(QUEST_DEVICE_COMMAND_PARAM_NUMBER, command.params[1].type);
    TEST_ASSERT_EQUAL_STRING("channel", command.params[2].key);
    TEST_ASSERT_EQUAL(QUEST_DEVICE_COMMAND_PARAM_TEXT, command.params[2].type);
    TEST_ASSERT_EQUAL_STRING("repeat", command.params[3].key);
    TEST_ASSERT_EQUAL(QUEST_DEVICE_COMMAND_PARAM_CHECKBOX, command.params[3].type);
    TEST_ASSERT_TRUE(command.params[3].optional);
}

static void test_system_relay_commands_are_exposed_as_system_device(void)
{
    quest_device_t device = {0};
    quest_device_command_t command = {0};

    qd_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get(QUEST_DEVICE_SYSTEM_RELAY_ID, &device));
    TEST_ASSERT_TRUE(device.system_device);
    TEST_ASSERT_TRUE(device.enabled);
    TEST_ASSERT_EQUAL_STRING("System Relay", device.name);
    TEST_ASSERT_EQUAL_UINT8(4, device.command_count);

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get_command(QUEST_DEVICE_SYSTEM_RELAY_ID, "pulse", &command));
    TEST_ASSERT_EQUAL_STRING("relay", command.capability);
    TEST_ASSERT_EQUAL_STRING("relay.pulse", command.command);
    TEST_ASSERT_TRUE(command.manual_allowed);
    TEST_ASSERT_TRUE(command.scenario_allowed);
    TEST_ASSERT_FALSE(command.result_required);
    TEST_ASSERT_EQUAL_UINT8(2, command.param_count);
    TEST_ASSERT_EQUAL_STRING("channel", command.params[0].key);
    TEST_ASSERT_EQUAL(QUEST_DEVICE_COMMAND_PARAM_NUMBER, command.params[0].type);
    TEST_ASSERT_EQUAL_STRING("duration_ms", command.params[1].key);
    TEST_ASSERT_EQUAL(QUEST_DEVICE_COMMAND_PARAM_NUMBER, command.params[1].type);

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get_command(QUEST_DEVICE_SYSTEM_RELAY_ID, "toggle", &command));
    TEST_ASSERT_EQUAL_STRING("relay.toggle", command.command);
    TEST_ASSERT_TRUE(command.manual_allowed);
    TEST_ASSERT_FALSE(command.scenario_allowed);
}

static void test_system_mosfet_commands_are_exposed_as_system_device(void)
{
    quest_device_t device = {0};
    quest_device_command_t command = {0};

    qd_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get(QUEST_DEVICE_SYSTEM_MOSFET_ID, &device));
    TEST_ASSERT_TRUE(device.system_device);
    TEST_ASSERT_TRUE(device.enabled);
    TEST_ASSERT_EQUAL_STRING("System MOSFET", device.name);
    TEST_ASSERT_EQUAL_UINT8(6, device.command_count);

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get_command(QUEST_DEVICE_SYSTEM_MOSFET_ID, "fade", &command));
    TEST_ASSERT_EQUAL_STRING("mosfet", command.capability);
    TEST_ASSERT_EQUAL_STRING("mosfet.fade", command.command);
    TEST_ASSERT_TRUE(command.manual_allowed);
    TEST_ASSERT_TRUE(command.scenario_allowed);
    TEST_ASSERT_FALSE(command.result_required);
    TEST_ASSERT_EQUAL_UINT8(3, command.param_count);
    TEST_ASSERT_EQUAL_STRING("channel", command.params[0].key);
    TEST_ASSERT_EQUAL(QUEST_DEVICE_COMMAND_PARAM_NUMBER, command.params[0].type);
    TEST_ASSERT_EQUAL_STRING("target", command.params[1].key);
    TEST_ASSERT_EQUAL(QUEST_DEVICE_COMMAND_PARAM_NUMBER, command.params[1].type);
    TEST_ASSERT_EQUAL_STRING("duration_ms", command.params[2].key);
    TEST_ASSERT_EQUAL(QUEST_DEVICE_COMMAND_PARAM_NUMBER, command.params[2].type);

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get_command(QUEST_DEVICE_SYSTEM_MOSFET_ID, "all_off", &command));
    TEST_ASSERT_EQUAL_STRING("mosfet.all_off", command.command);
    TEST_ASSERT_TRUE(command.manual_allowed);
    TEST_ASSERT_TRUE(command.scenario_allowed);
    TEST_ASSERT_EQUAL_UINT8(0, command.param_count);
}

static void test_system_io_commands_and_events_are_exposed_as_system_device(void)
{
    quest_device_t device = {0};
    quest_device_command_t command = {0};
    quest_device_event_t event = {0};

    qd_test_bootstrap();

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get(QUEST_DEVICE_SYSTEM_IO_ID, &device));
    TEST_ASSERT_TRUE(device.system_device);
    TEST_ASSERT_TRUE(device.enabled);
    TEST_ASSERT_EQUAL_STRING("System IO", device.name);
    TEST_ASSERT_EQUAL_STRING("internal", device.client_id);
    TEST_ASSERT_EQUAL_UINT8(5, device.command_count);
    TEST_ASSERT_EQUAL_UINT8(20, device.event_count);

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get_command(QUEST_DEVICE_SYSTEM_IO_ID, "set", &command));
    TEST_ASSERT_EQUAL_STRING("io.set", command.command);
    TEST_ASSERT_TRUE(command.manual_allowed);
    TEST_ASSERT_TRUE(command.scenario_allowed);
    TEST_ASSERT_EQUAL_UINT8(2, command.param_count);

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get_command(QUEST_DEVICE_SYSTEM_IO_ID, "toggle", &command));
    TEST_ASSERT_EQUAL_STRING("io.toggle", command.command);
    TEST_ASSERT_TRUE(command.manual_allowed);
    TEST_ASSERT_FALSE(command.scenario_allowed);

    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get_event(QUEST_DEVICE_SYSTEM_IO_ID, "ch1_active", &event));
    TEST_ASSERT_EQUAL_STRING("io.ch1_active", event.event);
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get_event(QUEST_DEVICE_SYSTEM_IO_ID, "ch4_low", &event));
    TEST_ASSERT_EQUAL_STRING("io.ch4_low", event.event);
}

static void test_quest_device_json_rejects_legacy_topic_payload_command(void)
{
    cJSON *root = cJSON_Parse("{\"id\":\"relay\",\"client_id\":\"node_1\",\"name\":\"Relay\","
                              "\"commands\":[{\"id\":\"open\",\"label\":\"Open\","
                              "\"topic\":\"quest/relay/cmd\",\"payload\":\"open\"}],"
                              "\"events\":[]}");
    quest_device_t device = {0};
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, quest_device_from_json(root, &device));
    cJSON_Delete(root);
}

static void test_quest_device_json_accepts_command_event_contract(void)
{
    cJSON *root = cJSON_Parse("{\"id\":\"relay\",\"client_id\":\"node_1\",\"name\":\"Relay\","
                              "\"commands\":[{\"id\":\"pulse\",\"label\":\"Pulse\","
                              "\"capability\":\"relay\",\"command\":\"relay.pulse\",\"default_args\":{\"channel\":1},"
                              "\"policy\":{\"manual_allowed\":true,\"scenario_allowed\":true,"
                              "\"requires_confirmation\":false,\"result_required\":true,"
                              "\"timeout_ms\":3000,\"danger_level\":\"normal\"},"
                              "\"args_schema\":[{\"key\":\"channel\",\"label\":\"Channel\",\"type\":\"number\"}]}],"
                              "\"events\":[{\"id\":\"pressed\",\"label\":\"Pressed\","
                              "\"capability\":\"input\",\"event\":\"input.pressed\",\"match\":{\"channel\":1}}]}");
    quest_device_t device = {0};
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_from_json(root, &device));
    TEST_ASSERT_EQUAL_STRING("relay", device.commands[0].capability);
    TEST_ASSERT_EQUAL_STRING("relay.pulse", device.commands[0].command);
    TEST_ASSERT_TRUE(device.commands[0].manual_allowed);
    TEST_ASSERT_TRUE(device.commands[0].scenario_allowed);
    TEST_ASSERT_EQUAL_STRING("{\"channel\":1}", device.commands[0].default_args_json);
    TEST_ASSERT_EQUAL_STRING("input", device.events[0].capability);
    TEST_ASSERT_EQUAL_STRING("input.pressed", device.events[0].event);
    TEST_ASSERT_EQUAL_STRING("{\"channel\":1}", device.events[0].match_json);
    cJSON_Delete(root);
}

static void test_quest_device_compact_manifest_event_templates_are_resolvable(void)
{
    cJSON *root = cJSON_Parse("{\"id\":\"node\",\"client_id\":\"dcc-node\",\"name\":\"Node\",\"enabled\":true,"
                              "\"device_description\":{"
                              "\"manifest_version\":2,\"format\":\"compact_resources\","
                              "\"node_kind\":\"virtual_uid_gate\","
                              "\"capability_contract\":\"scenehub.node.compact.v1\","
                              "\"resources\":{\"relays\":[],\"mosfets\":[],\"inputs\":[],\"outputs\":[],\"led_strips\":[]},"
                              "\"command_templates\":[],"
                              "\"event_templates\":[{\"id\":\"uid.sequence_valid\","
                              "\"label\":\"UID sequence valid\",\"source\":\"inputs\","
                              "\"event\":\"uid.sequence_valid\",\"args_schema_ref\":\"empty\"}],"
                              "\"schemas\":{\"empty\":[]}}}");
    quest_device_t device = {0};
    quest_device_event_t event = {0};

    qd_test_bootstrap();

    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_from_json(root, &device));
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_upsert(&device));
    TEST_ASSERT_EQUAL(ESP_OK, quest_device_get_event("node", "uid.sequence_valid", &event));
    TEST_ASSERT_EQUAL_STRING("uid.sequence_valid", event.id);
    TEST_ASSERT_EQUAL_STRING("UID sequence valid", event.label);
    TEST_ASSERT_EQUAL_STRING("inputs", event.capability);
    TEST_ASSERT_EQUAL_STRING("uid.sequence_valid", event.event);
    cJSON_Delete(root);
}

void register_quest_device_tests(void)
{
    RUN_TEST(test_quest_device_rejects_duplicate_client_id);
    RUN_TEST(test_quest_device_rejects_duplicate_command_id);
    RUN_TEST(test_quest_device_rejects_duplicate_event_id);
    RUN_TEST(test_system_audio_play_command_exposes_background_repeat_params);
    RUN_TEST(test_system_relay_commands_are_exposed_as_system_device);
    RUN_TEST(test_system_mosfet_commands_are_exposed_as_system_device);
    RUN_TEST(test_system_io_commands_and_events_are_exposed_as_system_device);
    RUN_TEST(test_quest_device_json_rejects_legacy_topic_payload_command);
    RUN_TEST(test_quest_device_json_accepts_command_event_contract);
    RUN_TEST(test_quest_device_compact_manifest_event_templates_are_resolvable);
}
