#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "command_executor.h"
#include "esp_err.h"
#include "quest_device.h"

void *command_executor_alloc(size_t size);
esp_err_t command_executor_fail(char *error,
                                size_t error_size,
                                esp_err_t err,
                                const char *message);
uint64_t command_executor_now_ms(void);

esp_err_t command_executor_params_get_string(const char *params_json,
                                             const char *key,
                                             char *out,
                                             size_t out_size,
                                             bool required);
esp_err_t command_executor_params_get_int(const char *params_json,
                                          const char *key,
                                          int *out,
                                          bool required);
esp_err_t command_executor_params_get_bool(const char *params_json,
                                           const char *key,
                                           bool *out,
                                           bool required);
bool command_executor_command_name_is(const quest_device_command_t *command,
                                      const char *name);

esp_err_t command_executor_track_pending(const char *request_id,
                                         const char *source_id,
                                         const char *command,
                                         uint32_t timeout_ms);
void command_executor_clear_pending(const char *request_id);

esp_err_t command_executor_execute_audio(const command_executor_request_t *request,
                                         const quest_device_command_t *command,
                                         char *error,
                                         size_t error_size);
esp_err_t command_executor_execute_mqtt(const quest_device_t *device,
                                        const quest_device_command_t *command,
                                        const command_executor_request_t *request,
                                        command_executor_dispatch_t *out_dispatch,
                                        char *error,
                                        size_t error_size);
