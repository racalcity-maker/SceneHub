#pragma once

#include <stdint.h>

#include "node_fallback_runtime.h"

typedef enum {
    NODE_FALLBACK_POLICY_ACTION_NONE = 0,
    NODE_FALLBACK_POLICY_ACTION_ENTER_FALLBACK,
    NODE_FALLBACK_POLICY_ACTION_EXIT_FALLBACK,
} node_fallback_policy_action_t;

typedef struct {
    node_fallback_policy_action_t action;
    node_fallback_runtime_state_t next_state;
    uint32_t next_deadline_ms;
    bool state_changed;
} node_fallback_policy_transition_t;

void node_fallback_policy_evaluate(const node_fallback_runtime_status_t *status,
                                   uint32_t now_ms,
                                   node_fallback_policy_transition_t *out_transition);
