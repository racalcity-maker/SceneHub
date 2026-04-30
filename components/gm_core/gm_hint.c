#include "gm_hint.h"

#include <string.h>

#include "quest_common_utils.h"

void gm_hint_reset(gm_hint_state_t *hint)
{
    if (!hint) {
        return;
    }
    memset(hint, 0, sizeof(*hint));
}

esp_err_t gm_hint_send(gm_hint_state_t *hint, const char *message, uint64_t now_ms)
{
    if (!hint || !message || !message[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    hint->active = true;
    hint->sent_count++;
    hint->last_changed_ms = now_ms;
    quest_str_copy(hint->message, sizeof(hint->message), message);
    return ESP_OK;
}

esp_err_t gm_hint_clear(gm_hint_state_t *hint, uint64_t now_ms)
{
    if (!hint) {
        return ESP_ERR_INVALID_ARG;
    }
    hint->active = false;
    hint->last_changed_ms = now_ms;
    hint->message[0] = '\0';
    return ESP_OK;
}
