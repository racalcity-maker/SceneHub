#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t system_reset_policy_init(void);
bool system_reset_policy_boot_setup_requested(void);
