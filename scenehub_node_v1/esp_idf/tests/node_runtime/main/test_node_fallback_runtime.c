#include "unity.h"

#include <string.h>

#include "node_fallback_policy.h"

static node_fallback_runtime_status_t s_status;
static node_fallback_policy_transition_t s_transition;

static void reset_status(void)
{
    memset(&s_status, 0, sizeof(s_status));
    memset(&s_transition, 0, sizeof(s_transition));
    s_status.enabled = true;
    s_status.state = NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY;
    s_status.return_policy = NODE_FALLBACK_RETURN_POLICY_AUTO_ON_STABLE_MQTT;
}

TEST_CASE("fallback policy enters offline pending when hub becomes unavailable",
          "[node_runtime][fallback]")
{
    reset_status();
    s_status.wifi_ready = false;
    s_status.mqtt_connected = false;
    s_status.fallback_timeout_ms = 1500;

    node_fallback_policy_evaluate(&s_status, 1000, &s_transition);

    TEST_ASSERT_EQUAL(NODE_FALLBACK_POLICY_ACTION_NONE, s_transition.action);
    TEST_ASSERT_TRUE(s_transition.state_changed);
    TEST_ASSERT_EQUAL(NODE_FALLBACK_RUNTIME_STATE_HUB_OFFLINE_PENDING, s_transition.next_state);
    TEST_ASSERT_EQUAL_UINT32(2500, s_transition.next_deadline_ms);
}

TEST_CASE("fallback policy enters fallback when offline timeout expires",
          "[node_runtime][fallback]")
{
    reset_status();
    s_status.state = NODE_FALLBACK_RUNTIME_STATE_HUB_OFFLINE_PENDING;
    s_status.deadline_ms = 2500;
    s_status.fallback_timeout_ms = 1500;

    node_fallback_policy_evaluate(&s_status, 2500, &s_transition);

    TEST_ASSERT_EQUAL(NODE_FALLBACK_POLICY_ACTION_ENTER_FALLBACK, s_transition.action);
    TEST_ASSERT_TRUE(s_transition.state_changed);
    TEST_ASSERT_EQUAL(NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE, s_transition.next_state);
    TEST_ASSERT_EQUAL_UINT32(0, s_transition.next_deadline_ms);
}

TEST_CASE("fallback policy returns to hub primary immediately on stable mqtt when delay is zero",
          "[node_runtime][fallback]")
{
    reset_status();
    s_status.state = NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE;
    s_status.fallback_rules_active = true;
    s_status.wifi_ready = true;
    s_status.mqtt_connected = true;
    s_status.fallback_return_delay_ms = 0;

    node_fallback_policy_evaluate(&s_status, 5000, &s_transition);

    TEST_ASSERT_EQUAL(NODE_FALLBACK_POLICY_ACTION_EXIT_FALLBACK, s_transition.action);
    TEST_ASSERT_TRUE(s_transition.state_changed);
    TEST_ASSERT_EQUAL(NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY, s_transition.next_state);
    TEST_ASSERT_EQUAL_UINT32(0, s_transition.next_deadline_ms);
}

TEST_CASE("fallback policy respects manual stay active return policy",
          "[node_runtime][fallback]")
{
    reset_status();
    s_status.state = NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE;
    s_status.fallback_rules_active = true;
    s_status.wifi_ready = true;
    s_status.mqtt_connected = true;
    s_status.return_policy = NODE_FALLBACK_RETURN_POLICY_MANUAL_STAY_ACTIVE;
    s_status.fallback_return_delay_ms = 3000;

    node_fallback_policy_evaluate(&s_status, 5000, &s_transition);

    TEST_ASSERT_EQUAL(NODE_FALLBACK_POLICY_ACTION_NONE, s_transition.action);
    TEST_ASSERT_FALSE(s_transition.state_changed);
    TEST_ASSERT_EQUAL(NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE, s_transition.next_state);
}

TEST_CASE("fallback policy enters return pending before leaving fallback when delay is configured",
          "[node_runtime][fallback]")
{
    reset_status();
    s_status.state = NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE;
    s_status.fallback_rules_active = true;
    s_status.wifi_ready = true;
    s_status.mqtt_connected = true;
    s_status.fallback_return_delay_ms = 3000;

    node_fallback_policy_evaluate(&s_status, 5000, &s_transition);

    TEST_ASSERT_EQUAL(NODE_FALLBACK_POLICY_ACTION_NONE, s_transition.action);
    TEST_ASSERT_TRUE(s_transition.state_changed);
    TEST_ASSERT_EQUAL(NODE_FALLBACK_RUNTIME_STATE_HUB_RETURN_PENDING, s_transition.next_state);
    TEST_ASSERT_EQUAL_UINT32(8000, s_transition.next_deadline_ms);
}

TEST_CASE("fallback policy returns to active if hub drops during return pending",
          "[node_runtime][fallback]")
{
    reset_status();
    s_status.state = NODE_FALLBACK_RUNTIME_STATE_HUB_RETURN_PENDING;
    s_status.fallback_rules_active = true;
    s_status.deadline_ms = 8000;
    s_status.wifi_ready = false;
    s_status.mqtt_connected = false;

    node_fallback_policy_evaluate(&s_status, 6000, &s_transition);

    TEST_ASSERT_EQUAL(NODE_FALLBACK_POLICY_ACTION_NONE, s_transition.action);
    TEST_ASSERT_TRUE(s_transition.state_changed);
    TEST_ASSERT_EQUAL(NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE, s_transition.next_state);
    TEST_ASSERT_EQUAL_UINT32(0, s_transition.next_deadline_ms);
}
