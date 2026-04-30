#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * Registers embedded HTTP assets required by the GM panel.
 */
esp_err_t web_ui_devices_register_assets(httpd_handle_t server);
