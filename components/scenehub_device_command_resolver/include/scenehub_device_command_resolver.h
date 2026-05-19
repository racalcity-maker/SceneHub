#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "quest_device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char device_id[QUEST_DEVICE_ID_MAX_LEN];
    char client_id[QUEST_DEVICE_CLIENT_ID_MAX_LEN];
    char device_name[QUEST_DEVICE_NAME_MAX_LEN];
    quest_device_command_t command;
    bool compact_manifest;
} scenehub_resolved_device_command_t;

esp_err_t scenehub_device_command_resolve(const char *device_id,
                                          const char *command_id,
                                          const char *params_json,
                                          bool require_enabled,
                                          scenehub_resolved_device_command_t *out,
                                          char *error,
                                          size_t error_size);
esp_err_t scenehub_device_command_validate_params(const quest_device_command_t *command,
                                                  const char *params_json,
                                                  char *error,
                                                  size_t error_size);

#ifdef __cplusplus
}
#endif
