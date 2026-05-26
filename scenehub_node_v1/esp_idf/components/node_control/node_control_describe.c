#include "node_control_internal.h"

#include <string.h>

#include "esp_log.h"
#include "node_capability.h"
#include "node_hardware_io.h"

static const char *TAG = "node_control_desc";

esp_err_t execute_get_status(node_control_result_t *result)
{
    node_hardware_io_status_t status = node_hardware_io_get_status();
    int n = snprintf(result->data_json,
                     sizeof(result->data_json),
                     "{\"hardware\":{\"relays\":%u,\"mosfets\":%u,\"universal_inputs\":%u,"
                     "\"universal_outputs\":%u,\"led_strips\":%u}}",
                     (unsigned)status.configured_relays,
                     (unsigned)status.configured_mosfets,
                     (unsigned)status.configured_universal_inputs,
                     (unsigned)status.configured_universal_outputs,
                     (unsigned)status.configured_led_strips);
    if (n < 0 || n >= (int)sizeof(result->data_json)) {
        return result_rejected(result, "internal_error");
    }
    result_done(result);
    return ESP_OK;
}

esp_err_t execute_describe_interface(node_control_result_t *result)
{
    size_t written = 0;
    static const char prefix[] = "{\"device_description\":";
    static const char suffix[] = "}";
    const size_t prefix_len = sizeof(prefix) - 1;
    const size_t suffix_len = sizeof(suffix) - 1;

    if (sizeof(result->data_json) <= prefix_len + suffix_len) {
        return result_rejected(result, "internal_error");
    }

    memcpy(result->data_json, prefix, prefix_len);
    esp_err_t err = node_capability_write_device_description(&g_node_control_config,
                                                             result->data_json + prefix_len,
                                                             sizeof(result->data_json) - prefix_len - suffix_len,
                                                             &written);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "device_description does not fit result buffer cap=%u err=%s",
                 (unsigned)sizeof(result->data_json),
                 esp_err_to_name(err));
        return result_rejected(result, "internal_error");
    }
    memcpy(result->data_json + prefix_len + written, suffix, suffix_len + 1);
    result_done(result);
    return ESP_OK;
}
