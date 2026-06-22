#include "node_fallback_policy.h"

#include <string.h>

static bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static bool hub_available(const node_fallback_runtime_status_t *status)
{
    return status && status->wifi_ready && status->mqtt_connected;
}

void node_fallback_policy_evaluate(const node_fallback_runtime_status_t *status,
                                   uint32_t now_ms,
                                   node_fallback_policy_transition_t *out_transition)
{
    node_fallback_policy_transition_t transition = {0};
    bool available = hub_available(status);

    if (!out_transition) {
        return;
    }
    memset(&transition, 0, sizeof(transition));
    transition.next_state = status ? status->state : NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY;
    transition.next_deadline_ms = status ? status->deadline_ms : 0;

    if (!status) {
        *out_transition = transition;
        return;
    }

    if (!status->enabled) {
        if (status->state != NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY) {
            transition.next_state = NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY;
            transition.next_deadline_ms = 0;
            transition.state_changed = true;
        }
        if (status->fallback_rules_active) {
            transition.action = NODE_FALLBACK_POLICY_ACTION_EXIT_FALLBACK;
        }
        *out_transition = transition;
        return;
    }

    switch (status->state) {
    case NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY:
        if (!available && status->fallback_timeout_ms > 0) {
            transition.next_state = NODE_FALLBACK_RUNTIME_STATE_HUB_OFFLINE_PENDING;
            transition.next_deadline_ms = now_ms + status->fallback_timeout_ms;
            transition.state_changed = true;
        }
        break;

    case NODE_FALLBACK_RUNTIME_STATE_HUB_OFFLINE_PENDING:
        if (available) {
            transition.next_state = NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY;
            transition.next_deadline_ms = 0;
            transition.state_changed = true;
        } else if (time_reached(now_ms, status->deadline_ms)) {
            transition.action = NODE_FALLBACK_POLICY_ACTION_ENTER_FALLBACK;
            transition.next_state = NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE;
            transition.next_deadline_ms = 0;
            transition.state_changed = true;
        }
        break;

    case NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE:
        if (available) {
            if (status->return_policy == NODE_FALLBACK_RETURN_POLICY_MANUAL_STAY_ACTIVE) {
                break;
            }
            if (status->fallback_return_delay_ms == 0) {
                transition.action = NODE_FALLBACK_POLICY_ACTION_EXIT_FALLBACK;
                transition.next_state = NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY;
                transition.next_deadline_ms = 0;
                transition.state_changed = true;
                break;
            }
            transition.next_state = NODE_FALLBACK_RUNTIME_STATE_HUB_RETURN_PENDING;
            transition.next_deadline_ms = now_ms + status->fallback_return_delay_ms;
            transition.state_changed = true;
        }
        break;

    case NODE_FALLBACK_RUNTIME_STATE_HUB_RETURN_PENDING:
        if (!available) {
            transition.next_state = NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE;
            transition.next_deadline_ms = 0;
            transition.state_changed = true;
        } else if (time_reached(now_ms, status->deadline_ms)) {
            transition.action = NODE_FALLBACK_POLICY_ACTION_EXIT_FALLBACK;
            transition.next_state = NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY;
            transition.next_deadline_ms = 0;
            transition.state_changed = true;
        }
        break;

    default:
        transition.next_state = NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY;
        transition.next_deadline_ms = 0;
        transition.state_changed = true;
        break;
    }

    *out_transition = transition;
}
