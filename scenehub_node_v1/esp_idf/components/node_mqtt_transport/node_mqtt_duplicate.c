#include "node_mqtt_internal.h"

#include <stdio.h>
#include <string.h>

static node_mqtt_duplicate_entry_t s_duplicate_cache[NODE_MQTT_DUP_CACHE_SIZE];
static size_t s_duplicate_next;

const node_mqtt_duplicate_entry_t *node_mqtt_duplicate_find(const char *request_id)
{
    if (!request_id || request_id[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < NODE_MQTT_DUP_CACHE_SIZE; ++i) {
        if (s_duplicate_cache[i].used && strcmp(s_duplicate_cache[i].request_id, request_id) == 0) {
            return &s_duplicate_cache[i];
        }
    }
    return NULL;
}

void node_mqtt_duplicate_remember(const char *request_id,
                                  const char *command,
                                  const node_control_result_t *result)
{
    if (!request_id || request_id[0] == '\0' || !command || !result) {
        return;
    }
    node_mqtt_duplicate_entry_t *entry = &s_duplicate_cache[s_duplicate_next++ % NODE_MQTT_DUP_CACHE_SIZE];
    entry->used = true;
    snprintf(entry->request_id, sizeof(entry->request_id), "%s", request_id);
    snprintf(entry->command, sizeof(entry->command), "%s", command);
    snprintf(entry->status, sizeof(entry->status), "%s", result->status);
    snprintf(entry->error_code, sizeof(entry->error_code), "%s", result->error_code);
}
