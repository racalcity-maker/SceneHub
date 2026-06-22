#include "unity.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "node_capability.h"
#include "node_config.h"

static node_config_t s_capability_config;
static char s_capability_json[16384];
static char s_status_json[4096];

static void set_text(char *dst, size_t dst_size, const char *src)
{
    TEST_ASSERT_NOT_NULL(dst);
    TEST_ASSERT_NOT_NULL(src);
    snprintf(dst, dst_size, "%s", src);
}

TEST_CASE("node capability device description escapes labels and names",
          "[node_runtime][capability]")
{
    size_t written = 0;
    cJSON *root = NULL;
    cJSON *device = NULL;
    cJSON *resources = NULL;
    cJSON *relays = NULL;
    cJSON *inputs = NULL;
    cJSON *led_strips = NULL;
    cJSON *relay0 = NULL;
    cJSON *input0 = NULL;
    cJSON *strip0 = NULL;

    memset(&s_capability_config, 0, sizeof(s_capability_config));
    memset(s_capability_json, 0, sizeof(s_capability_json));
    node_config_set_factory_defaults(&s_capability_config);
    set_text(s_capability_config.node_id, sizeof(s_capability_config.node_id), "scenehub_node_s3");
    set_text(s_capability_config.node_name, sizeof(s_capability_config.node_name), "Door \"A\" \\ Alpha");

    s_capability_config.relays[0].enabled = true;
    s_capability_config.relays[0].channel = 1;
    set_text(s_capability_config.relays[0].label,
             sizeof(s_capability_config.relays[0].label),
             "Relay \"A\" \\ 1");

    s_capability_config.universal_io[0].enabled = true;
    s_capability_config.universal_io[0].channel = 1;
    s_capability_config.universal_io[0].role = NODE_PIN_UNIVERSAL_INPUT;
    set_text(s_capability_config.universal_io[0].label,
             sizeof(s_capability_config.universal_io[0].label),
             "Girkon \"1\" \\ reed");

    s_capability_config.led_strips[0].enabled = true;
    s_capability_config.led_strips[0].channel = 1;
    s_capability_config.led_strips[0].pixel_count = 5;
    set_text(s_capability_config.led_strips[0].label,
             sizeof(s_capability_config.led_strips[0].label),
             "LED \"Line\" \\ 1");

    TEST_ASSERT_EQUAL(ESP_OK,
                      node_capability_write_device_description(&s_capability_config,
                                                               s_capability_json,
                                                               sizeof(s_capability_json),
                                                               &written));
    TEST_ASSERT_TRUE(written > 0);

    root = cJSON_Parse(s_capability_json);
    TEST_ASSERT_NOT_NULL(root);

    device = cJSON_GetObjectItemCaseSensitive(root, "device");
    resources = cJSON_GetObjectItemCaseSensitive(root, "resources");
    relays = cJSON_GetObjectItemCaseSensitive(resources, "relays");
    inputs = cJSON_GetObjectItemCaseSensitive(resources, "inputs");
    led_strips = cJSON_GetObjectItemCaseSensitive(resources, "led_strips");
    relay0 = cJSON_GetArrayItem(relays, 0);
    input0 = cJSON_GetArrayItem(inputs, 0);
    strip0 = cJSON_GetArrayItem(led_strips, 0);

    TEST_ASSERT_TRUE(cJSON_IsObject(device));
    TEST_ASSERT_TRUE(cJSON_IsArray(relays));
    TEST_ASSERT_TRUE(cJSON_IsArray(inputs));
    TEST_ASSERT_TRUE(cJSON_IsArray(led_strips));
    TEST_ASSERT_TRUE(cJSON_IsObject(relay0));
    TEST_ASSERT_TRUE(cJSON_IsObject(input0));
    TEST_ASSERT_TRUE(cJSON_IsObject(strip0));
    TEST_ASSERT_EQUAL_STRING("Door \"A\" \\ Alpha",
                             cJSON_GetObjectItemCaseSensitive(device, "name")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Relay \"A\" \\ 1",
                             cJSON_GetObjectItemCaseSensitive(relay0, "label")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Girkon \"1\" \\ reed",
                             cJSON_GetObjectItemCaseSensitive(input0, "label")->valuestring);
    TEST_ASSERT_EQUAL_STRING("LED \"Line\" \\ 1",
                             cJSON_GetObjectItemCaseSensitive(strip0, "label")->valuestring);

    cJSON_Delete(root);
}

TEST_CASE("node capability status json stays parseable without active runtime",
          "[node_runtime][capability]")
{
    size_t written = 0;
    cJSON *root = NULL;
    cJSON *rules = NULL;
    cJSON *fallback = NULL;
    cJSON *operation_mode = NULL;

    memset(&s_capability_config, 0, sizeof(s_capability_config));
    memset(s_status_json, 0, sizeof(s_status_json));
    node_config_set_factory_defaults(&s_capability_config);
    s_capability_config.operation_mode = NODE_OPERATION_MODE_STANDALONE;
    s_capability_config.standalone_mqtt_enabled = true;

    TEST_ASSERT_EQUAL(ESP_OK,
                      node_capability_write_node_status_json(&s_capability_config,
                                                             s_status_json,
                                                             sizeof(s_status_json),
                                                             &written));
    TEST_ASSERT_TRUE(written > 0);

    root = cJSON_Parse(s_status_json);
    TEST_ASSERT_NOT_NULL(root);
    operation_mode = cJSON_GetObjectItemCaseSensitive(root, "operation_mode");
    rules = cJSON_GetObjectItemCaseSensitive(root, "rules");
    fallback = cJSON_GetObjectItemCaseSensitive(root, "fallback");

    TEST_ASSERT_TRUE(cJSON_IsString(operation_mode));
    TEST_ASSERT_TRUE(cJSON_IsObject(rules));
    TEST_ASSERT_TRUE(cJSON_IsObject(fallback));
    TEST_ASSERT_EQUAL_STRING("standalone", operation_mode->valuestring);

    cJSON_Delete(root);
}
