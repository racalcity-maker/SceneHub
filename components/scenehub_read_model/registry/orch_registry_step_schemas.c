#include "orchestrator_registry_internal.h"

#include <string.h>

static esp_err_t orch_step_schema_add_field(orch_room_scenario_step_schema_t *schema,
                                            const char *key,
                                            const char *type,
                                            const char *label,
                                            const char *depends_on,
                                            bool required)
{
    orch_room_scenario_field_schema_t *field = NULL;
    if (!schema || !key || !type || !label) {
        return ESP_ERR_INVALID_ARG;
    }
    if (schema->field_count >= ORCH_ROOM_SCENARIO_MAX_SCHEMA_FIELDS) {
        return ESP_ERR_NO_MEM;
    }
    field = &schema->fields[schema->field_count++];
    quest_str_copy(field->key, sizeof(field->key), key);
    quest_str_copy(field->type, sizeof(field->type), type);
    quest_str_copy(field->label, sizeof(field->label), label);
    if (depends_on) {
        quest_str_copy(field->depends_on, sizeof(field->depends_on), depends_on);
    }
    field->required = required;
    return ESP_OK;
}

static esp_err_t orch_step_schema_add_wait_skip_fields(orch_room_scenario_step_schema_t *schema)
{
    esp_err_t err = orch_step_schema_add_field(schema,
                                               "allow_operator_skip",
                                               "checkbox",
                                               "Allow operator skip",
                                               NULL,
                                               false);
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(schema,
                                         "operator_skip_label",
                                         "text",
                                         "Skip label",
                                         "allow_operator_skip",
                                         false);
    }
    return err;
}

static esp_err_t orch_step_schema_init(orch_room_scenario_step_schema_t *schema,
                                       const char *type,
                                       const char *label,
                                       const char *description)
{
    if (!schema || !type || !label) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(schema, 0, sizeof(*schema));
    quest_str_copy(schema->type, sizeof(schema->type), type);
    quest_str_copy(schema->label, sizeof(schema->label), label);
    if (description) {
        quest_str_copy(schema->description, sizeof(schema->description), description);
    }
    return ESP_OK;
}

esp_err_t orchestrator_registry_list_scenario_step_schemas(orch_room_scenario_step_schema_t *out_schemas,
                                                           size_t max_schemas,
                                                           size_t *out_count)
{
    esp_err_t err = ESP_OK;
    size_t index = 0;

    if (!out_schemas || max_schemas < ORCH_ROOM_SCENARIO_MAX_STEP_SCHEMAS || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_schemas, 0, sizeof(*out_schemas) * max_schemas);

    err = orch_step_schema_init(&out_schemas[index],
                                "DEVICE_COMMAND",
                                "Device command",
                                "Send a saved capability command to a quest or system device");
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index], "device_id", "device_select", "Device", NULL, true);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index],
                                         "command_id",
                                         "device_command_select",
                                         "Command",
                                         "device_id",
                                         true);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index],
                                         "params",
                                         "params_object",
                                         "Parameters",
                                         "command_id",
                                         false);
    }
    if (err != ESP_OK) {
        return err;
    }
    ++index;

    err = orch_step_schema_init(&out_schemas[index],
                                "DEVICE_COMMAND_GROUP",
                                "Command group",
                                "Send several saved capability commands in order");
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index], "commands", "command_group", "Commands", NULL, true);
    }
    if (err != ESP_OK) {
        return err;
    }
    ++index;

    err = orch_step_schema_init(&out_schemas[index],
                                "WAIT_DEVICE_EVENT",
                                "Wait device event",
                                "Wait for a saved capability event from a quest or system device");
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index], "device_id", "device_select", "Device", NULL, true);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index],
                                         "event_id",
                                         "device_event_select",
                                         "Event",
                                         "device_id",
                                         true);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index],
                                         "timeout_ms",
                                         "optional_duration_ms",
                                         "Timeout",
                                         NULL,
                                         false);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index],
                                         "timeout_message",
                                         "textarea",
                                         "Timeout message",
                                         NULL,
                                         false);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_wait_skip_fields(&out_schemas[index]);
    }
    if (err != ESP_OK) {
        return err;
    }
    ++index;

    err = orch_step_schema_init(&out_schemas[index],
                                "WAIT_ANY_DEVICE_EVENT",
                                "Wait any device event",
                                "Wait until one event from a small list arrives");
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index], "events", "event_group", "Events", NULL, true);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_wait_skip_fields(&out_schemas[index]);
    }
    if (err != ESP_OK) {
        return err;
    }
    ++index;

    err = orch_step_schema_init(&out_schemas[index],
                                "WAIT_ALL_DEVICE_EVENTS",
                                "Wait all device events",
                                "Wait until every event in a small list arrives");
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index], "events", "event_group", "Events", NULL, true);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_wait_skip_fields(&out_schemas[index]);
    }
    if (err != ESP_OK) {
        return err;
    }
    ++index;

    err = orch_step_schema_init(&out_schemas[index],
                                "WAIT_TIME",
                                "Wait time",
                                "Pause the scenario for a fixed duration");
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index], "duration_ms", "duration_ms", "Duration", NULL, true);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_wait_skip_fields(&out_schemas[index]);
    }
    if (err != ESP_OK) {
        return err;
    }
    ++index;

    err = orch_step_schema_init(&out_schemas[index],
                                "OPERATOR_APPROVAL",
                                "Operator approval",
                                "Pause until the operator approves continuing");
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index], "prompt", "text", "Prompt", NULL, true);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index],
                                         "approve_label",
                                         "text",
                                         "Button label",
                                         NULL,
                                         false);
    }
    if (err != ESP_OK) {
        return err;
    }
    ++index;

    err = orch_step_schema_init(&out_schemas[index],
                                "SHOW_OPERATOR_MESSAGE",
                                "Show operator message",
                                "Show a note to the operator and continue");
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index], "message", "textarea", "Message", NULL, true);
    }
    if (err != ESP_OK) {
        return err;
    }
    ++index;

    err = orch_step_schema_init(&out_schemas[index],
                                "SET_FLAG",
                                "Set flag",
                                "Set a runtime boolean flag for this scenario run");
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index], "flag_name", "text", "Flag", NULL, true);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index], "value", "checkbox", "Value", NULL, true);
    }
    if (err != ESP_OK) {
        return err;
    }
    ++index;

    err = orch_step_schema_init(&out_schemas[index],
                                "WAIT_FLAGS",
                                "Wait flags",
                                "Wait until selected scenario flags have expected values");
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index], "flags", "flag_list", "Flags", NULL, true);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index],
                                         "timeout_ms",
                                         "optional_duration_ms",
                                         "Timeout",
                                         NULL,
                                         false);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_field(&out_schemas[index],
                                         "timeout_message",
                                         "textarea",
                                         "Timeout message",
                                         NULL,
                                         false);
    }
    if (err == ESP_OK) {
        err = orch_step_schema_add_wait_skip_fields(&out_schemas[index]);
    }
    if (err != ESP_OK) {
        return err;
    }
    ++index;

    err = orch_step_schema_init(&out_schemas[index],
                                "END_GAME",
                                "End game",
                                "Finish the game timer without stopping audio automatically");
    if (err != ESP_OK) {
        return err;
    }
    ++index;

    *out_count = index;
    return ESP_OK;
}
