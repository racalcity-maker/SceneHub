#pragma once

#include "quest_device.h"

void quest_device_fill_system_audio(quest_device_t *out);
esp_err_t quest_device_system_audio_command(const char *command_id,
                                            quest_device_command_t *out);
esp_err_t quest_device_system_audio_event(const char *event_id,
                                          quest_device_event_t *out);
esp_err_t quest_device_storage_init(void);
esp_err_t quest_device_replace_all(const quest_device_t *items, size_t count);
