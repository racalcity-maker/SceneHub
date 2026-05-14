#include "web_ui_utils.h"
#include "web_ui_handlers.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "hardware_io.h"
#include "quest_device.h"
#include "room_scenario.h"
#include "service_status.h"

#define HARDWARE_IO_MODE_BODY_MAX_BYTES 256

static cJSON *hardware_io_service_json(bool available)
{
    service_status_entry_t entry = {0};
    cJSON *service = cJSON_CreateObject();
    if (!service) {
        return NULL;
    }
    (void)service_status_get(SERVICE_STATUS_HARDWARE_IO, &entry);
    cJSON_AddBoolToObject(service, "available", available);
    cJSON_AddBoolToObject(service, "init_attempted", entry.init_attempted);
    cJSON_AddBoolToObject(service, "init_ok", entry.init_ok);
    cJSON_AddBoolToObject(service, "start_attempted", entry.start_attempted);
    cJSON_AddBoolToObject(service, "start_ok", entry.start_ok);
    cJSON_AddBoolToObject(service, "fault", entry.fault);
    cJSON_AddNumberToObject(service, "last_error", entry.last_error);
    if (!available) {
        cJSON_AddStringToObject(service, "error", "hardware_io_unavailable");
    } else if (entry.fault) {
        cJSON_AddStringToObject(service, "error", "hardware_io_fault");
    } else {
        cJSON_AddStringToObject(service, "error", "");
    }
    return service;
}

static cJSON *hardware_io_relay_status_json(void)
{
    static hardware_io_relay_status_t items[HARDWARE_IO_RELAY_CHANNEL_COUNT];
    size_t count = 0;
    cJSON *array = cJSON_CreateArray();
    if (!array) {
        return NULL;
    }
    if (hardware_io_relay_get_status(items, HARDWARE_IO_RELAY_CHANNEL_COUNT, &count) != ESP_OK) {
        return array;
    }
    for (size_t i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(array);
            return NULL;
        }
        cJSON_AddNumberToObject(item, "channel", items[i].channel);
        cJSON_AddNumberToObject(item, "gpio", items[i].gpio);
        cJSON_AddBoolToObject(item, "enabled", items[i].enabled);
        cJSON_AddBoolToObject(item, "active_low", items[i].active_low);
        cJSON_AddBoolToObject(item, "on", items[i].on);
        cJSON_AddBoolToObject(item, "effect_active", items[i].effect_active);
        cJSON_AddItemToArray(array, item);
    }
    return array;
}

static cJSON *hardware_io_mosfet_status_json(void)
{
    static hardware_io_mosfet_status_t items[HARDWARE_IO_MOSFET_CHANNEL_COUNT];
    size_t count = 0;
    cJSON *array = cJSON_CreateArray();
    if (!array) {
        return NULL;
    }
    if (hardware_io_mosfet_get_status(items, HARDWARE_IO_MOSFET_CHANNEL_COUNT, &count) != ESP_OK) {
        return array;
    }
    for (size_t i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(array);
            return NULL;
        }
        cJSON_AddNumberToObject(item, "channel", items[i].channel);
        cJSON_AddNumberToObject(item, "gpio", items[i].gpio);
        cJSON_AddBoolToObject(item, "enabled", items[i].enabled);
        cJSON_AddNumberToObject(item, "value", items[i].value);
        cJSON_AddNumberToObject(item, "pwm_freq_hz", items[i].pwm_freq_hz);
        cJSON_AddBoolToObject(item, "pulse_active", items[i].pulse_active);
        cJSON_AddBoolToObject(item, "fade_active", items[i].fade_active);
        cJSON_AddBoolToObject(item, "effect_active", items[i].effect_active);
        cJSON_AddItemToArray(array, item);
    }
    return array;
}

static cJSON *hardware_io_io_status_json(void)
{
    static hardware_io_io_status_t items[HARDWARE_IO_IO_CHANNEL_COUNT];
    size_t count = 0;
    cJSON *array = cJSON_CreateArray();
    if (!array) {
        return NULL;
    }
    if (hardware_io_io_get_status(items, HARDWARE_IO_IO_CHANNEL_COUNT, &count) != ESP_OK) {
        return array;
    }
    for (size_t i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(array);
            return NULL;
        }
        cJSON_AddNumberToObject(item, "channel", items[i].channel);
        cJSON_AddNumberToObject(item, "gpio", items[i].gpio);
        cJSON_AddBoolToObject(item, "enabled", items[i].enabled);
        cJSON_AddNumberToObject(item, "mode", items[i].mode);
        cJSON_AddBoolToObject(item, "active_low", items[i].active_low);
        cJSON_AddBoolToObject(item, "physical_high", items[i].physical_high);
        cJSON_AddBoolToObject(item, "active", items[i].active);
        cJSON_AddBoolToObject(item, "pulse_active", items[i].pulse_active);
        cJSON_AddBoolToObject(item, "effect_active", items[i].effect_active);
        cJSON_AddNumberToObject(item, "last_change_ms", items[i].last_change_ms);
        cJSON_AddItemToArray(array, item);
    }
    return array;
}

static const char *hardware_io_io_mode_name(hardware_io_io_mode_t mode)
{
    switch (mode) {
    case HARDWARE_IO_IO_MODE_INPUT:
        return "input";
    case HARDWARE_IO_IO_MODE_OUTPUT:
        return "output";
    case HARDWARE_IO_IO_MODE_DISABLED:
    default:
        return "disabled";
    }
}

static bool hardware_io_io_mode_from_json(const cJSON *item, hardware_io_io_mode_t *out)
{
    if (!out) {
        return false;
    }
    if (cJSON_IsNumber(item)) {
        int value = item->valueint;
        if (value < HARDWARE_IO_IO_MODE_DISABLED || value > HARDWARE_IO_IO_MODE_OUTPUT) {
            return false;
        }
        *out = (hardware_io_io_mode_t)value;
        return true;
    }
    if (!cJSON_IsString(item) || !item->valuestring) {
        return false;
    }
    if (strcmp(item->valuestring, "disabled") == 0) {
        *out = HARDWARE_IO_IO_MODE_DISABLED;
        return true;
    }
    if (strcmp(item->valuestring, "input") == 0) {
        *out = HARDWARE_IO_IO_MODE_INPUT;
        return true;
    }
    if (strcmp(item->valuestring, "output") == 0) {
        *out = HARDWARE_IO_IO_MODE_OUTPUT;
        return true;
    }
    return false;
}

static esp_err_t hardware_io_read_json(httpd_req_t *req, cJSON **out_root)
{
    char *body = NULL;
    size_t received = 0;
    cJSON *root = NULL;
    if (!req || !out_root) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_root = NULL;
    if (req->content_len <= 0 || req->content_len > HARDWARE_IO_MODE_BODY_MAX_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    body = web_ui_calloc((size_t)req->content_len + 1, 1);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    while (received < (size_t)req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            web_ui_free(body);
            return ESP_FAIL;
        }
        received += (size_t)r;
    }
    root = cJSON_ParseWithLength(body, received);
    web_ui_free(body);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    *out_root = root;
    return ESP_OK;
}

static int hardware_io_event_channel(const char *event_id)
{
    int channel = 0;
    if (!event_id || sscanf(event_id, "ch%d_", &channel) != 1) {
        return 0;
    }
    return channel;
}

static int hardware_io_params_channel(const cJSON *obj)
{
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(obj, "params");
    const cJSON *channel = params ? cJSON_GetObjectItemCaseSensitive(params, "channel") : NULL;
    if (!cJSON_IsNumber(channel)) {
        return 0;
    }
    return channel->valueint;
}

static bool hardware_io_command_uses_output(const char *command_id)
{
    return command_id &&
           (strcmp(command_id, "set") == 0 ||
            strcmp(command_id, "pulse") == 0 ||
            strcmp(command_id, "blink") == 0 ||
            strcmp(command_id, "toggle") == 0);
}

static bool hardware_io_mode_blocks_input(hardware_io_io_mode_t requested)
{
    return requested != HARDWARE_IO_IO_MODE_INPUT;
}

static bool hardware_io_mode_blocks_output(hardware_io_io_mode_t requested)
{
    return requested != HARDWARE_IO_IO_MODE_OUTPUT;
}

static bool hardware_io_scan_io_refs(const cJSON *node,
                                     uint8_t channel,
                                     hardware_io_io_mode_t requested,
                                     const char *scenario_id,
                                     char *message,
                                     size_t message_size)
{
    if (!node) {
        return false;
    }
    if (cJSON_IsObject(node)) {
        const cJSON *device_id = cJSON_GetObjectItemCaseSensitive(node, "device_id");
        if (cJSON_IsString(device_id) &&
            device_id->valuestring &&
            strcmp(device_id->valuestring, QUEST_DEVICE_SYSTEM_IO_ID) == 0) {
            const cJSON *event_id = cJSON_GetObjectItemCaseSensitive(node, "event_id");
            const cJSON *command_id = cJSON_GetObjectItemCaseSensitive(node, "command_id");
            int ref_channel = 0;
            if (cJSON_IsString(event_id) && event_id->valuestring) {
                ref_channel = hardware_io_event_channel(event_id->valuestring);
                if (ref_channel == channel && hardware_io_mode_blocks_input(requested)) {
                    snprintf(message,
                             message_size,
                             "IO %u is used as input in scenario %s",
                             (unsigned)channel,
                             scenario_id && scenario_id[0] ? scenario_id : "?");
                    return true;
                }
            }
            if (cJSON_IsString(command_id) &&
                command_id->valuestring &&
                hardware_io_command_uses_output(command_id->valuestring)) {
                ref_channel = hardware_io_params_channel(node);
                if (ref_channel == channel && hardware_io_mode_blocks_output(requested)) {
                    snprintf(message,
                             message_size,
                             "IO %u is used as output in scenario %s",
                             (unsigned)channel,
                             scenario_id && scenario_id[0] ? scenario_id : "?");
                    return true;
                }
            }
        }
        for (const cJSON *child = node->child; child; child = child->next) {
            if (hardware_io_scan_io_refs(child, channel, requested, scenario_id, message, message_size)) {
                return true;
            }
        }
        return false;
    }
    if (cJSON_IsArray(node)) {
        const cJSON *child = NULL;
        cJSON_ArrayForEach(child, node) {
            if (hardware_io_scan_io_refs(child, channel, requested, scenario_id, message, message_size)) {
                return true;
            }
        }
    }
    return false;
}

static esp_err_t hardware_io_check_mode_conflict(uint8_t channel,
                                                 hardware_io_io_mode_t requested,
                                                 char *message,
                                                 size_t message_size)
{
    cJSON *root = NULL;
    const cJSON *scenarios = NULL;
    const cJSON *scenario = NULL;
    esp_err_t err = room_scenario_store_export_json(&root);
    if (err != ESP_OK) {
        return err;
    }
    scenarios = cJSON_GetObjectItemCaseSensitive(root, "room_scenarios");
    cJSON_ArrayForEach(scenario, scenarios) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(scenario, "id");
        const char *scenario_id = cJSON_IsString(id) ? id->valuestring : "?";
        if (hardware_io_scan_io_refs(scenario,
                                     channel,
                                     requested,
                                     scenario_id,
                                     message,
                                     message_size)) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_STATE;
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t hardware_io_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *service = NULL;
    cJSON *relay = NULL;
    cJSON *mosfet = NULL;
    cJSON *io = NULL;
    bool available = hardware_io_is_available();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
    }
    service = hardware_io_service_json(available);
    relay = hardware_io_relay_status_json();
    mosfet = hardware_io_mosfet_status_json();
    io = hardware_io_io_status_json();
    if (!service || !relay || !mosfet || !io) {
        if (service) {
            cJSON_Delete(service);
        }
        if (relay) {
            cJSON_Delete(relay);
        }
        if (mosfet) {
            cJSON_Delete(mosfet);
        }
        if (io) {
            cJSON_Delete(io);
        }
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
    }
    cJSON_AddBoolToObject(root, "ok", available);
    cJSON_AddItemToObject(root, "service", service);
    cJSON_AddItemToObject(root, "relays", relay);
    cJSON_AddItemToObject(root, "mosfets", mosfet);
    cJSON_AddItemToObject(root, "ios", io);
    return web_ui_send_json(req, root);
}

esp_err_t hardware_io_io_mode_handler(httpd_req_t *req)
{
    cJSON *input = NULL;
    cJSON *root = NULL;
    const cJSON *channel_item = NULL;
    const cJSON *mode_item = NULL;
    hardware_io_io_mode_t mode = HARDWARE_IO_IO_MODE_DISABLED;
    char conflict[128] = {0};
    uint8_t channel = 0;
    esp_err_t err = hardware_io_read_json(req, &input);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid hardware io request");
    }
    channel_item = cJSON_GetObjectItemCaseSensitive(input, "channel");
    mode_item = cJSON_GetObjectItemCaseSensitive(input, "mode");
    if (!cJSON_IsNumber(channel_item) ||
        channel_item->valueint < 1 ||
        channel_item->valueint > HARDWARE_IO_IO_CHANNEL_COUNT ||
        !hardware_io_io_mode_from_json(mode_item, &mode)) {
        cJSON_Delete(input);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid io mode request");
    }
    channel = (uint8_t)channel_item->valueint;
    if (!hardware_io_is_available()) {
        cJSON_Delete(input);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "hardware_io_unavailable");
    }
    err = hardware_io_check_mode_conflict(channel, mode, conflict, sizeof(conflict));
    if (err == ESP_ERR_INVALID_STATE) {
        cJSON_Delete(input);
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, conflict, HTTPD_RESP_USE_STRLEN);
    }
    if (err != ESP_OK) {
        cJSON_Delete(input);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "io mode validation failed");
    }
    err = hardware_io_io_set_mode(channel, mode);
    cJSON_Delete(input);
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_STATE) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "io mode cannot be applied");
    }
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "io mode update failed");
    }
    root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "channel", channel);
    cJSON_AddStringToObject(root, "mode", hardware_io_io_mode_name(mode));
    return web_ui_send_json(req, root);
}
