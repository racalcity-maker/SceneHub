#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "quest_common_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GM_CTRL_ERR_BASE            0x7600
#define GM_CTRL_ERR_ROOM_NOT_FOUND  (GM_CTRL_ERR_BASE + 1)
#define GM_CTRL_ERR_ACTION_NOT_FOUND (GM_CTRL_ERR_BASE + 2)
#define GM_CTRL_ERR_ACTION_DISABLED (GM_CTRL_ERR_BASE + 3)
#define GM_CTRL_ERR_NOT_SUPPORTED   (GM_CTRL_ERR_BASE + 4)
#define GM_CTRL_ERR_EXECUTION_FAILED (GM_CTRL_ERR_BASE + 5)
#define GM_CTRL_ERR_ROOM_UNHEALTHY  (GM_CTRL_ERR_BASE + 6)

#define GM_ROOM_ACTION_ID_MAX_LEN    32
#define GM_ROOM_ACTION_LABEL_MAX_LEN 48
#define GM_ROOM_ACTION_COUNT         8

typedef struct {
    char action_id[GM_ROOM_ACTION_ID_MAX_LEN];
    char label[GM_ROOM_ACTION_LABEL_MAX_LEN];
    bool enabled;
} gm_room_action_desc_t;

esp_err_t gm_control_list_room_actions(const char *room_id,
                                       gm_room_action_desc_t *out_actions,
                                       size_t max_actions,
                                       size_t *out_count);
esp_err_t gm_control_execute_room_action(const char *room_id, const char *action_id);
esp_err_t gm_control_execute_room_action_with_source(const char *source,
                                                     const char *room_id,
                                                     const char *action_id);

#ifdef __cplusplus
}
#endif
