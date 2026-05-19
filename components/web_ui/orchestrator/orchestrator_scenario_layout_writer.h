#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "room_scenario.h"

esp_err_t orchestrator_scenario_layout_writer_send(httpd_req_t *req,
                                                   const char *room_id,
                                                   const room_scenario_t *scenario);
