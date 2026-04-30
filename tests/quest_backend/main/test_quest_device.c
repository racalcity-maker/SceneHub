#include <string.h>

#include "unity.h"

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
                                 const char *topic)
{
    memset(command, 0, sizeof(*command));
    qd_test_set(command->id, sizeof(command->id), id);
    qd_test_set(command->label, sizeof(command->label), id);
    qd_test_set(command->kind, sizeof(command->kind), "mqtt_publish");
    qd_test_set(command->topic, sizeof(command->topic), topic);
    command->button_enabled = true;
}

static void qd_test_fill_event(quest_device_event_t *event,
                               const char *id,
                               const char *topic)
{
    memset(event, 0, sizeof(*event));
    qd_test_set(event->id, sizeof(event->id), id);
    qd_test_set(event->label, sizeof(event->label), id);
    qd_test_set(event->topic, sizeof(event->topic), topic);
    qd_test_set(event->event_type, sizeof(event->event_type), id);
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

void register_quest_device_tests(void)
{
    RUN_TEST(test_quest_device_rejects_duplicate_client_id);
    RUN_TEST(test_quest_device_rejects_duplicate_command_id);
    RUN_TEST(test_quest_device_rejects_duplicate_event_id);
}
