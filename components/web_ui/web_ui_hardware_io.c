#include "web_ui_utils.h"
#include "web_ui_handlers.h"

#include "cJSON.h"
#include "esp_http_server.h"
#include "hardware_io.h"
#include "service_status.h"

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
        cJSON_AddItemToArray(array, item);
    }
    return array;
}

static cJSON *hardware_io_input_status_json(void)
{
    static hardware_io_input_status_t items[HARDWARE_IO_INPUT_CHANNEL_COUNT];
    size_t count = 0;
    cJSON *array = cJSON_CreateArray();
    if (!array) {
        return NULL;
    }
    if (hardware_io_input_get_status(items, HARDWARE_IO_INPUT_CHANNEL_COUNT, &count) != ESP_OK) {
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
        cJSON_AddBoolToObject(item, "physical_high", items[i].physical_high);
        cJSON_AddBoolToObject(item, "active", items[i].active);
        cJSON_AddNumberToObject(item, "debounce_ms", items[i].debounce_ms);
        cJSON_AddNumberToObject(item, "last_change_ms", items[i].last_change_ms);
        cJSON_AddItemToArray(array, item);
    }
    return array;
}

static cJSON *hardware_io_gpio_status_json(void)
{
    static hardware_io_gpio_status_t items[HARDWARE_IO_GPIO_CHANNEL_COUNT];
    size_t count = 0;
    cJSON *array = cJSON_CreateArray();
    if (!array) {
        return NULL;
    }
    if (hardware_io_gpio_get_status(items, HARDWARE_IO_GPIO_CHANNEL_COUNT, &count) != ESP_OK) {
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
        cJSON_AddNumberToObject(item, "last_change_ms", items[i].last_change_ms);
        cJSON_AddItemToArray(array, item);
    }
    return array;
}

esp_err_t hardware_io_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *service = NULL;
    cJSON *relay = NULL;
    cJSON *mosfet = NULL;
    cJSON *input = NULL;
    cJSON *gpio = NULL;
    bool available = hardware_io_is_available();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
    }
    service = hardware_io_service_json(available);
    relay = hardware_io_relay_status_json();
    mosfet = hardware_io_mosfet_status_json();
    input = hardware_io_input_status_json();
    gpio = hardware_io_gpio_status_json();
    if (!service || !relay || !mosfet || !input || !gpio) {
        if (service) {
            cJSON_Delete(service);
        }
        if (relay) {
            cJSON_Delete(relay);
        }
        if (mosfet) {
            cJSON_Delete(mosfet);
        }
        if (input) {
            cJSON_Delete(input);
        }
        if (gpio) {
            cJSON_Delete(gpio);
        }
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
    }
    cJSON_AddBoolToObject(root, "ok", available);
    cJSON_AddItemToObject(root, "service", service);
    cJSON_AddItemToObject(root, "relays", relay);
    cJSON_AddItemToObject(root, "mosfets", mosfet);
    cJSON_AddItemToObject(root, "inputs", input);
    cJSON_AddItemToObject(root, "gpios", gpio);
    return web_ui_send_json(req, root);
}
