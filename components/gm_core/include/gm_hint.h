#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "quest_common_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;
    uint32_t sent_count;
    uint64_t last_changed_ms;
    char message[QUEST_PAYLOAD_MAX_LEN];
} gm_hint_state_t;

void gm_hint_reset(gm_hint_state_t *hint);
esp_err_t gm_hint_send(gm_hint_state_t *hint, const char *message, uint64_t now_ms);
esp_err_t gm_hint_clear(gm_hint_state_t *hint, uint64_t now_ms);

#ifdef __cplusplus
}
#endif
