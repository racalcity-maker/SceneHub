#include "node_mqtt_internal.h"

#include <string.h>

#include "freertos/semphr.h"
#include "node_control.h"

static node_control_result_t s_result;

void node_mqtt_handle_command_payload(const char *payload)
{
    char request_id[NODE_MQTT_REQUEST_ID_MAX];
    char command_name[NODE_MQTT_COMMAND_MAX];
    char args_json[NODE_MQTT_ARGS_MAX];

    if (!node_mqtt_json_extract_string(payload, "request_id", request_id, sizeof(request_id)) ||
        !node_mqtt_json_extract_string(payload, "command", command_name, sizeof(command_name)) ||
        !node_mqtt_json_copy_object(payload, "args", args_json, sizeof(args_json))) {
        if (node_mqtt_publish_lock(pdMS_TO_TICKS(500))) {
            node_mqtt_publish_result_fields_locked("", "", "rejected", "invalid_request", NULL);
            node_mqtt_publish_unlock();
        }
        return;
    }

    if (!node_mqtt_publish_lock(pdMS_TO_TICKS(500))) {
        return;
    }

    const node_mqtt_duplicate_entry_t *duplicate = node_mqtt_duplicate_find(request_id);
    if (duplicate) {
        if (strcmp(duplicate->command, command_name) == 0) {
            node_mqtt_publish_result_fields_locked(request_id, command_name, duplicate->status, duplicate->error_code, NULL);
        } else {
            node_mqtt_publish_result_fields_locked(request_id, command_name, "rejected", "invalid_request", NULL);
        }
        node_mqtt_publish_unlock();
        return;
    }

    node_mqtt_publish_unlock();

    node_control_command_t control = {
        .request_id = request_id,
        .command = command_name,
        .args_json = args_json,
    };
    (void)node_control_execute(&control, &s_result);

    if (node_mqtt_publish_lock(pdMS_TO_TICKS(500))) {
        node_mqtt_publish_result_locked(request_id, command_name, &s_result);
        if (strcmp(s_result.status, "done") == 0) {
            node_mqtt_publish_status_locked();
        }
        node_mqtt_duplicate_remember(request_id, command_name, &s_result);
        node_mqtt_publish_unlock();
    }
}
