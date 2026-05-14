#pragma once

#include "esp_err.h"
#include "room_scenario.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t scenehub_scenario_validate_environment(
    const room_scenario_t *scenario,
    room_scenario_validation_report_t *out);
esp_err_t scenehub_scenario_validate(const room_scenario_t *scenario,
                                     room_scenario_validation_report_t *out);

#ifdef __cplusplus
}
#endif
