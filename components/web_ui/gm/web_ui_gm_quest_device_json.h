#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include "orch_device_view.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gm_quest_device_catalog_entry_to_json(const orch_quest_device_catalog_entry_t *device,
                                                cJSON *out,
                                                bool include_manifest_json);

#ifdef __cplusplus
}
#endif
