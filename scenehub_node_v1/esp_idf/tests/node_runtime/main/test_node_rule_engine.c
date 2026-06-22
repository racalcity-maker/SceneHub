#include "unity.h"

#include <stdio.h>
#include <string.h>

#include "node_rule_schema.h"

static const char *s_valid_export_bundle =
    "{"
    "\"version\":2,"
    "\"bundle_id\":\"secret_door_bundle\","
    "\"generation\":1,"
    "\"mode\":\"standalone\","
    "\"exports\":{"
      "\"commands\":["
        "{"
          "\"id\":\"open_secret_door\","
          "\"label\":\"Open secret door\","
          "\"kind\":\"runtime_command\""
        "}"
      "],"
      "\"events\":["
        "{"
          "\"id\":\"secret_door_opened\","
          "\"label\":\"Secret door opened\""
        "}"
      "]"
    "},"
    "\"emits\":[\"secret_door_opened\"],"
    "\"initial_state\":{\"opened\":false},"
    "\"rules\":["
      "{"
        "\"id\":\"boot_init\","
        "\"enabled\":true,"
        "\"trigger\":{\"kind\":\"boot\"},"
        "\"actions\":["
          "{"
            "\"kind\":\"emit_event\","
            "\"event\":\"secret_door_opened\""
          "}"
        "]"
      "},"
      "{"
        "\"id\":\"open_secret_door_cmd\","
        "\"enabled\":true,"
        "\"trigger\":{\"kind\":\"mqtt_command\",\"command\":\"open_secret_door\"},"
        "\"actions\":["
          "{"
            "\"kind\":\"set_state\","
            "\"key\":\"opened\","
            "\"value\":true"
          "}"
        "]"
      "}"
    "]"
    "}";
static char s_invalid_bundle[1024];

TEST_CASE("node rule schema accepts minimal exported command bundle", "[node_runtime][rules]")
{
    node_rule_bundle_metadata_t metadata = {0};
    char error_code[32] = {0};

    TEST_ASSERT_EQUAL(ESP_OK,
                      node_rule_schema_validate_bundle(s_valid_export_bundle,
                                                       &metadata,
                                                       error_code,
                                                       sizeof(error_code)));
    TEST_ASSERT_TRUE(metadata.has_bundle);
    TEST_ASSERT_EQUAL_UINT32(2, metadata.version);
    TEST_ASSERT_EQUAL_UINT32(1, metadata.generation);
    TEST_ASSERT_EQUAL_STRING("secret_door_bundle", metadata.bundle_id);
    TEST_ASSERT_EQUAL_STRING("", error_code);
}

TEST_CASE("node rule schema rejects exported command ids with spaces", "[node_runtime][rules]")
{
    node_rule_bundle_metadata_t metadata = {0};
    char error_code[32] = {0};

    memset(s_invalid_bundle, 0, sizeof(s_invalid_bundle));
    snprintf(s_invalid_bundle,
             sizeof(s_invalid_bundle),
             "%s",
             s_valid_export_bundle);

    {
        const char *bad_id = "\"id\":\"open secret door\"";
        char *start = strstr(s_invalid_bundle, "\"id\":\"open_secret_door\"");

        TEST_ASSERT_NOT_NULL(start);
        memset(start, ' ', strlen("\"id\":\"open_secret_door\""));
        memcpy(start, bad_id, strlen(bad_id));
    }

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      node_rule_schema_validate_bundle(s_invalid_bundle,
                                                       &metadata,
                                                       error_code,
                                                       sizeof(error_code)));
    TEST_ASSERT_FALSE(metadata.has_bundle);
    TEST_ASSERT_EQUAL_STRING("invalid_bundle_shape", error_code);
}
