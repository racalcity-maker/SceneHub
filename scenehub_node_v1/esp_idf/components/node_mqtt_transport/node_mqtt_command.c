#include "node_mqtt_internal.h"

#include <string.h>

#include "freertos/semphr.h"
#include "node_control.h"

static node_control_result_t s_result;

bool node_mqtt_parse_command_payload(const char *payload, node_mqtt_command_message_t *out_message)
{
    if (!payload || !out_message) {
        return false;
    }
    memset(out_message, 0, sizeof(*out_message));

    if (!node_mqtt_json_extract_string(payload, "request_id", out_message->request_id, sizeof(out_message->request_id)) ||
        !node_mqtt_json_extract_string(payload, "command", out_message->command, sizeof(out_message->command)) ||
        !node_mqtt_json_copy_object(payload, "args", out_message->args_json, sizeof(out_message->args_json))) {
        return false;
    }
    out_message->valid = true;
    return true;
}

void node_mqtt_process_command_message(const node_mqtt_command_message_t *message)
{
    const node_mqtt_duplicate_entry_t *duplicate = NULL;

    if (!message) {
        return;
    }
    if (!message->valid) {
        if (node_mqtt_publish_lock(portMAX_DELAY)) {
            node_mqtt_publish_result_fields_locked("", "", "rejected", "invalid_request", NULL);
            node_mqtt_publish_unlock();
        }
        return;
    }

    duplicate = node_mqtt_duplicate_find(message->request_id);
    if (duplicate) {
        if (node_mqtt_publish_lock(portMAX_DELAY)) {
            if (strcmp(duplicate->command, message->command) == 0) {
                node_mqtt_publish_result_fields_locked(message->request_id,
                                                       message->command,
                                                       duplicate->status,
                                                       duplicate->error_code,
                                                       NULL);
            } else {
                node_mqtt_publish_result_fields_locked(message->request_id,
                                                       message->command,
                                                       "rejected",
                                                       "invalid_request",
                                                       NULL);
            }
            node_mqtt_publish_unlock();
        }
        return;
    }

    node_control_command_t control = {
        .request_id = message->request_id,
        .command = message->command,
        .args_json = message->args_json,
        .source = NODE_CONTROL_SOURCE_HUB,
    };
    (void)node_control_execute(&control, &s_result);

    if (node_mqtt_publish_lock(portMAX_DELAY)) {
        node_mqtt_publish_result_locked(message->request_id, message->command, &s_result);
        if (strcmp(s_result.status, "done") == 0 ||
            strcmp(s_result.status, "started") == 0 ||
            strcmp(s_result.status, "accepted") == 0) {
            node_mqtt_publish_status_locked();
        }
        node_mqtt_duplicate_remember(message->request_id, message->command, &s_result);
        node_mqtt_publish_unlock();
    }
}
