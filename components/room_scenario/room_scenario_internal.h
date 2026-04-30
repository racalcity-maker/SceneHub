#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "room_scenario.h"

bool room_scenario_valid_step_type(room_scenario_step_type_t type);
esp_err_t room_scenario_validate_structural(const room_scenario_t *scenario);
