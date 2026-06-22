#include "node_mqtt_internal.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "node_admin_control.h"
#include "node_runtime_snapshot.h"
#include "node_runtime_mode.h"
#include "sdkconfig.h"

static const char *TAG = "node_mqtt_command";
static node_control_result_t s_result;
static node_control_result_t *s_admin_result;
static node_rule_store_entry_t *s_rule_entry;
static node_runtime_snapshot_t *s_runtime_snapshot;

static void *alloc_admin_buffer(size_t size)
{
    void *ptr = NULL;

    if (size == 0) {
        return NULL;
    }
#if CONFIG_SPIRAM
    ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, size);
        return ptr;
    }
#endif
    ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

static bool ensure_admin_scratch(void)
{
    if (!s_admin_result) {
        s_admin_result = (node_control_result_t *)alloc_admin_buffer(sizeof(*s_admin_result));
    }
    if (!s_rule_entry) {
        s_rule_entry = (node_rule_store_entry_t *)alloc_admin_buffer(sizeof(*s_rule_entry));
    }
    if (!s_runtime_snapshot) {
        s_runtime_snapshot = (node_runtime_snapshot_t *)alloc_admin_buffer(sizeof(*s_runtime_snapshot));
    }
    return s_admin_result && s_rule_entry && s_runtime_snapshot;
}

static void write_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void clear_result(node_control_result_t *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
}

static void result_done_local(node_control_result_t *result)
{
    if (!result) {
        return;
    }
    write_text(result->status, sizeof(result->status), "done");
    result->error_code[0] = '\0';
}

static void result_failed_local(node_control_result_t *result, const char *code)
{
    if (!result) {
        return;
    }
    write_text(result->status, sizeof(result->status), "failed");
    write_text(result->error_code, sizeof(result->error_code), code);
}

static void result_rejected_local(node_control_result_t *result, const char *code)
{
    if (!result) {
        return;
    }
    write_text(result->status, sizeof(result->status), "rejected");
    write_text(result->error_code, sizeof(result->error_code), code);
}

static bool mqtt_rule_bundle_exceeds_admin_budget(const char *raw_json)
{
    return raw_json && strlen(raw_json) > NODE_RULE_BUNDLE_MQTT_MAX_LEN;
}

static bool json_append(char *buf, size_t cap, size_t *len, const char *fmt, ...)
{
    va_list args;
    int written = 0;

    if (!buf || !len || *len >= cap || !fmt) {
        return false;
    }

    va_start(args, fmt);
    written = vsnprintf(buf + *len, cap - *len, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= cap - *len) {
        return false;
    }
    *len += (size_t)written;
    return true;
}

static bool append_metadata_json(char *buf, size_t cap, size_t *len, const node_rule_bundle_metadata_t *metadata)
{
    const node_rule_bundle_metadata_t empty = {0};
    const node_rule_bundle_metadata_t *meta = metadata ? metadata : &empty;

    return json_append(buf,
                       cap,
                       len,
                       "\"metadata\":{\"has_bundle\":%s,\"version\":%lu,\"generation\":%lu,"
                       "\"bundle_id\":\"%s\",\"mode\":\"%s\",\"raw_size\":%lu}",
                       meta->has_bundle ? "true" : "false",
                       (unsigned long)meta->version,
                       (unsigned long)meta->generation,
                       meta->bundle_id,
                       meta->mode,
                       (unsigned long)meta->raw_size);
}

static void build_metadata_only_result(node_control_result_t *result,
                                       const node_rule_bundle_metadata_t *metadata,
                                       bool restart_required)
{
    size_t len = 0;

    clear_result(result);
    if (!json_append(result->data_json, sizeof(result->data_json), &len, "{") ||
        !append_metadata_json(result->data_json, sizeof(result->data_json), &len, metadata) ||
        !json_append(result->data_json,
                     sizeof(result->data_json),
                     &len,
                     ",\"restart_required\":%s}",
                     restart_required ? "true" : "false")) {
        result_failed_local(result, "internal_error");
        return;
    }
    result_done_local(result);
}

static void build_get_rules_result(node_control_result_t *result, const node_rule_store_entry_t *entry)
{
    const node_runtime_snapshot_t *snapshot = s_runtime_snapshot;
    size_t len = 0;

    clear_result(result);
    if (!snapshot || node_runtime_snapshot_capture(s_runtime_snapshot) != ESP_OK) {
        result_failed_local(result, "internal_error");
        return;
    }
    if (!json_append(result->data_json, sizeof(result->data_json), &len, "{") ||
        !append_metadata_json(result->data_json, sizeof(result->data_json), &len, entry ? &entry->metadata : NULL) ||
        !json_append(result->data_json,
                     sizeof(result->data_json),
                     &len,
                     ",\"paused\":%s,\"bundle\":",
                     snapshot->rules_paused ? "true" : "false")) {
        result_failed_local(result, "internal_error");
        return;
    }

    if (entry && entry->metadata.has_bundle && entry->raw_json[0] != '\0') {
        if (!json_append(result->data_json, sizeof(result->data_json), &len, "%s", entry->raw_json)) {
            result_failed_local(result, "internal_error");
            return;
        }
    } else if (!json_append(result->data_json, sizeof(result->data_json), &len, "null")) {
        result_failed_local(result, "internal_error");
        return;
    }

    if (!json_append(result->data_json, sizeof(result->data_json), &len, "}")) {
        result_failed_local(result, "internal_error");
        return;
    }
    result_done_local(result);
}

static void build_pause_result(node_control_result_t *result, bool paused)
{
    int written = 0;

    clear_result(result);
    written = snprintf(result->data_json,
                       sizeof(result->data_json),
                       "{\"paused\":%s}",
                       paused ? "true" : "false");
    if (written < 0 || written >= (int)sizeof(result->data_json)) {
        result_failed_local(result, "internal_error");
        return;
    }
    result_done_local(result);
}

static void build_restart_result(node_control_result_t *result)
{
    int written = 0;

    clear_result(result);
    written = snprintf(result->data_json,
                       sizeof(result->data_json),
                       "{\"restarting\":true}");
    if (written < 0 || written >= (int)sizeof(result->data_json)) {
        result_failed_local(result, "internal_error");
        return;
    }
    result_done_local(result);
}

static void build_reinit_result(node_control_result_t *result)
{
    int written = 0;

    clear_result(result);
    written = snprintf(result->data_json,
                       sizeof(result->data_json),
                       "{\"reinit_requested\":true}");
    if (written < 0 || written >= (int)sizeof(result->data_json)) {
        result_failed_local(result, "internal_error");
        return;
    }
    result_done_local(result);
}

static void publish_rules_changed_mqtt(const char *op,
                                       const node_rule_bundle_metadata_t *metadata,
                                       bool paused)
{
    char args_json[192];
    int written = 0;

    written = snprintf(args_json,
                       sizeof(args_json),
                       "{\"op\":\"%s\",\"mode\":\"%s\",\"bundle_id\":\"%s\",\"generation\":%lu,"
                       "\"paused\":%s,\"has_bundle\":%s}",
                       op ? op : "",
                       (metadata && metadata->mode[0] != '\0') ? metadata->mode
                                                               : node_runtime_mode_name(
                                                                     (node_operation_mode_t)g_node_mqtt_config.operation_mode),
                       (metadata && metadata->has_bundle) ? metadata->bundle_id : "",
                       (unsigned long)((metadata && metadata->has_bundle) ? metadata->generation : 0U),
                       paused ? "true" : "false",
                       (metadata && metadata->has_bundle) ? "true" : "false");
    if (written < 0 || written >= (int)sizeof(args_json)) {
        ESP_LOGW(TAG, "rules.changed payload truncated op=%s", op ? op : "");
        return;
    }
    if (!node_mqtt_publish_lock(pdMS_TO_TICKS(100))) {
        ESP_LOGW(TAG, "rules.changed publish skipped op=%s reason=busy", op ? op : "");
        return;
    }
    if (node_mqtt_publish_event_locked("rules.changed", args_json) != ESP_OK) {
        ESP_LOGW(TAG, "rules.changed publish failed op=%s", op ? op : "");
    }
    node_mqtt_publish_unlock();
}

static void map_admin_error(node_control_result_t *result, esp_err_t err, const char *error_code)
{
    const char *code = (error_code && error_code[0] != '\0') ? error_code : "internal_error";

    clear_result(result);
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_NOT_SUPPORTED) {
        result_rejected_local(result, code);
        return;
    }
    if (strncmp(code, "invalid_", 8) == 0 || strstr(code, "_not_supported") != NULL ||
        strstr(code, "missing_") != NULL || strstr(code, "unknown_") != NULL ||
        strstr(code, "too_many_") != NULL || strstr(code, "exceeds_") != NULL) {
        result_rejected_local(result, code);
        return;
    }
    result_failed_local(result, code);
}

static void process_rule_admin_validate(const node_mqtt_admin_message_t *message, node_control_result_t *result)
{
    node_rule_bundle_metadata_t metadata = {0};
    node_admin_control_result_t admin_result = {0};
    char error_code[NODE_RULE_API_ERROR_MAX_LEN] = {0};
    if (mqtt_rule_bundle_exceeds_admin_budget(message->args_json)) {
        result_rejected_local(result, "bundle_too_large_for_mqtt_admin");
        return;
    }
    esp_err_t err = node_admin_control_validate_rules(message->args_json,
                                                      &metadata,
                                                      error_code,
                                                      sizeof(error_code),
                                                      &admin_result);

    if (err != ESP_OK) {
        map_admin_error(result, err, error_code);
        return;
    }
    build_metadata_only_result(result, &metadata, false);
}

static void process_rule_admin_apply(const node_mqtt_admin_message_t *message, node_control_result_t *result)
{
    node_rule_bundle_metadata_t metadata = {0};
    node_admin_control_result_t admin_result = {0};
    char error_code[NODE_RULE_API_ERROR_MAX_LEN] = {0};
    if (mqtt_rule_bundle_exceeds_admin_budget(message->args_json)) {
        result_rejected_local(result, "bundle_too_large_for_mqtt_admin");
        return;
    }
    esp_err_t err = node_admin_control_apply_rules(message->args_json,
                                                   &metadata,
                                                   error_code,
                                                   sizeof(error_code),
                                                   &admin_result);

    if (err != ESP_OK) {
        map_admin_error(result, err, error_code);
        return;
    }
    build_metadata_only_result(result, &metadata, true);
    publish_rules_changed_mqtt("apply", &metadata, false);
}

static void process_rule_admin_get(node_control_result_t *result)
{
    esp_err_t err = ESP_OK;

    if (!s_rule_entry) {
        map_admin_error(result, ESP_ERR_NO_MEM, "no_mem");
        return;
    }
    err = node_admin_control_get_rules(s_rule_entry);

    if (err != ESP_OK) {
        map_admin_error(result, err, "internal_error");
        return;
    }
    if (s_rule_entry->metadata.raw_size > NODE_RULE_BUNDLE_MQTT_MAX_LEN) {
        result_rejected_local(result, "bundle_too_large_for_mqtt_admin");
        return;
    }
    build_get_rules_result(result, s_rule_entry);
}

static void process_rule_admin_clear(node_control_result_t *result)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_clear_rules(&admin_result);

    if (err != ESP_OK) {
        map_admin_error(result, err, "internal_error");
        return;
    }
    build_metadata_only_result(result, NULL, false);
    publish_rules_changed_mqtt("clear", NULL, false);
}

static void process_rule_admin_pause_resume(node_control_result_t *result, bool paused)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = paused ? node_admin_control_pause_rules(&admin_result)
                           : node_admin_control_resume_rules(&admin_result);

    if (err != ESP_OK) {
        map_admin_error(result, err, "internal_error");
        return;
    }
    build_pause_result(result, paused);
    if (!s_rule_entry || node_admin_control_get_rules(s_rule_entry) != ESP_OK) {
        if (s_rule_entry) {
            memset(s_rule_entry, 0, sizeof(*s_rule_entry));
        }
    }
    publish_rules_changed_mqtt(paused ? "pause" : "resume",
                               s_rule_entry ? &s_rule_entry->metadata : NULL,
                               paused);
}

static void process_node_admin_reboot(node_control_result_t *result)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_restart(&admin_result);

    if (err != ESP_OK) {
        map_admin_error(result, err, "internal_error");
        return;
    }
    build_restart_result(result);
}

static void process_node_admin_nfc_reinit(node_control_result_t *result)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_reinit_nfc(&admin_result);

    if (err != ESP_OK) {
        map_admin_error(result, err, "internal_error");
        return;
    }
    build_reinit_result(result);
}

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

bool node_mqtt_command_is_admin(const char *command)
{
    return command &&
           (strncmp(command, "node.rules.", 11) == 0 || strcmp(command, "node.reboot") == 0 ||
            strcmp(command, "node.nfc.reinit") == 0);
}

bool node_mqtt_parse_admin_command_payload(const char *payload, node_mqtt_admin_message_t *out_message)
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
        if (node_mqtt_publish_result_fields_reliable("", "", "rejected", "invalid_request", NULL) != ESP_OK) {
            ESP_LOGW(TAG, "failed to publish invalid_request result");
        }
        return;
    }

    duplicate = node_mqtt_duplicate_find(message->request_id);
    if (duplicate) {
        esp_err_t err;
        if (strcmp(duplicate->command, message->command) == 0) {
            err = node_mqtt_publish_result_fields_reliable(message->request_id,
                                                          message->command,
                                                          duplicate->status,
                                                          duplicate->error_code,
                                                          NULL);
        } else {
            err = node_mqtt_publish_result_fields_reliable(message->request_id,
                                                          message->command,
                                                          "rejected",
                                                          "invalid_request",
                                                          NULL);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "failed to publish duplicate result for %s", message->request_id);
        }
        return;
    }

    if (strcmp(message->command, "describe_interface") == 0) {
        clear_result(&s_result);
        if (node_mqtt_publish_describe_interface_result_reliable(message->request_id,
                                                                 "done",
                                                                 "",
                                                                 &g_node_mqtt_config) == ESP_OK) {
            result_done_local(&s_result);
            if (node_mqtt_publish_lock(portMAX_DELAY)) {
                node_mqtt_publish_status_locked();
                node_mqtt_publish_unlock();
            }
        } else {
            result_failed_local(&s_result, "internal_error");
            if (node_mqtt_publish_result_fields_reliable(message->request_id,
                                                         message->command,
                                                         s_result.status,
                                                         s_result.error_code,
                                                         NULL) != ESP_OK) {
                ESP_LOGW(TAG, "failed to publish describe_interface failure for %s", message->request_id);
            }
        }
        node_mqtt_duplicate_remember(message->request_id, message->command, &s_result);
        return;
    }

    node_control_command_t control = {
        .request_id = message->request_id,
        .command = message->command,
        .args_json = message->args_json,
        .source = NODE_CONTROL_SOURCE_HUB,
    };
    (void)node_control_submit(&control, &s_result);

    if (node_mqtt_publish_result_reliable(message->request_id, message->command, &s_result) == ESP_OK) {
        if (node_mqtt_publish_lock(portMAX_DELAY)) {
            if (strcmp(s_result.status, "done") == 0 ||
                strcmp(s_result.status, "started") == 0 ||
                strcmp(s_result.status, "accepted") == 0) {
                node_mqtt_publish_status_locked();
            }
            node_mqtt_publish_unlock();
        }
    } else {
        ESP_LOGW(TAG, "failed to publish command result for %s", message->request_id);
    }
    node_mqtt_duplicate_remember(message->request_id, message->command, &s_result);
}

void node_mqtt_process_admin_command_message(const node_mqtt_admin_message_t *message)
{
    const node_mqtt_duplicate_entry_t *duplicate = NULL;

    if (!message) {
        return;
    }
    if (!message->valid) {
        if (node_mqtt_publish_result_fields_reliable("", "", "rejected", "invalid_request", NULL) != ESP_OK) {
            ESP_LOGW(TAG, "failed to publish admin invalid_request result");
        }
        return;
    }

    duplicate = node_mqtt_duplicate_find(message->request_id);
    if (duplicate) {
        esp_err_t err;
        if (strcmp(duplicate->command, message->command) == 0) {
            err = node_mqtt_publish_result_fields_reliable(message->request_id,
                                                          message->command,
                                                          duplicate->status,
                                                          duplicate->error_code,
                                                          NULL);
        } else {
            err = node_mqtt_publish_result_fields_reliable(message->request_id,
                                                          message->command,
                                                          "rejected",
                                                          "invalid_request",
                                                          NULL);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "failed to publish duplicate admin result for %s", message->request_id);
        }
        return;
    }

    if (!ensure_admin_scratch()) {
        if (node_mqtt_publish_result_fields_reliable(message->request_id,
                                                     message->command,
                                                     "failed",
                                                     "no_mem",
                                                     NULL) != ESP_OK) {
            ESP_LOGW(TAG, "failed to publish no_mem admin result for %s", message->request_id);
        }
        return;
    }

    clear_result(s_admin_result);
    if (strcmp(message->command, "node.rules.validate") == 0) {
        process_rule_admin_validate(message, s_admin_result);
    } else if (strcmp(message->command, "node.rules.apply") == 0) {
        process_rule_admin_apply(message, s_admin_result);
    } else if (strcmp(message->command, "node.rules.get") == 0) {
        process_rule_admin_get(s_admin_result);
    } else if (strcmp(message->command, "node.rules.clear") == 0) {
        process_rule_admin_clear(s_admin_result);
    } else if (strcmp(message->command, "node.rules.pause") == 0) {
        process_rule_admin_pause_resume(s_admin_result, true);
    } else if (strcmp(message->command, "node.rules.resume") == 0) {
        process_rule_admin_pause_resume(s_admin_result, false);
    } else if (strcmp(message->command, "node.reboot") == 0) {
        process_node_admin_reboot(s_admin_result);
    } else if (strcmp(message->command, "node.nfc.reinit") == 0) {
        process_node_admin_nfc_reinit(s_admin_result);
    } else {
        result_rejected_local(s_admin_result, "not_supported");
    }

    if (node_mqtt_publish_result_reliable(message->request_id, message->command, s_admin_result) == ESP_OK) {
        if (node_mqtt_publish_lock(portMAX_DELAY)) {
            if (strcmp(s_admin_result->status, "done") == 0 ||
                strcmp(s_admin_result->status, "started") == 0 ||
                strcmp(s_admin_result->status, "accepted") == 0) {
                node_mqtt_publish_status_locked();
            }
            node_mqtt_publish_unlock();
        }
    } else {
        ESP_LOGW(TAG, "failed to publish admin command result for %s", message->request_id);
    }
    node_mqtt_duplicate_remember(message->request_id, message->command, s_admin_result);
}
