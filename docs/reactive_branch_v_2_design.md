# Reactive Branch v2

Reactive Branch v2 is the current SceneHub model for room reactions. It replaces the old "first step is the trigger" reactive branch shape for new work.

## Purpose

Normal scenario branches describe quest progress. Reactive branches describe side reactions that may run while the main flow keeps waiting or running.

Examples:

- play a short sound when players make a wrong action;
- escalate hints after repeated invalid attempts;
- blink lights when a sensor is triggered;
- set a runtime flag after a side event.

Reactive Branch v2 is intentionally not a second scenario engine. It has a
trigger, optional guards, a policy, and a bounded list of actions.

```text
trigger -> guard_flags -> policy -> selected variant -> actions -> result_policy
```

## JSON Shape

```json
{
  "id": "reaction_1",
  "name": "Wrong UID reaction",
  "type": "reactive",
  "enabled": true,
  "required_for_completion": false,
  "trigger": {
    "kind": "device_event",
    "device_id": "uid_gate_1",
    "event_id": "sequence_invalid"
  },
  "guard_flags": [],
  "policy": {
    "mode": "single",
    "cooldown_ms": 0,
    "max_fire_count": 0
  },
  "reentry": {
    "mode": "ignore"
  },
  "variants": [
    {
      "id": "variant_1",
      "label": "Actions",
      "actions": []
    }
  ],
  "result_policy": {
    "on_done": "continue",
    "on_fail": "fail_reaction",
    "on_timeout": "fail_reaction"
  }
}
```

## Implemented

- Trigger kinds: `device_event`, `any_device_events`, `all_device_events`,
  `flag_changed`, `operator_event`, `runtime_event`.
- Device-event matching accepts saved Quest Device ids and physical client ids.
- Device-event matching accepts saved event ids and physical event names.
- Guard flags use strict `all` semantics.
- Policy modes: `single`, `rotate`, `random`, `escalate`.
- Cooldown starts when the reaction starts executing.
- Reentry modes: `ignore`, `queue_one`.
- `max_fire_count` limits completed executions.
- One incoming event may start at most one active reaction. Conflicting matches are rejected and logged as a warning.
- Actions: `DEVICE_COMMAND`, sequential `DEVICE_COMMAND_GROUP`, `WAIT_TIME`,
  `WAIT_DEVICE_EVENT`, `WAIT_ANY_DEVICE_EVENT`, `WAIT_ALL_DEVICE_EVENTS`,
  `WAIT_FLAGS`, `SET_FLAG`, `SHOW_OPERATOR_MESSAGE`, `FAIL_REACTION`,
  `RESET_REACTION`.
- `DEVICE_COMMAND` goes through `command_executor`.
- Result-required commands wait for terminal command results.
- `accepted` does not advance an action.
- `done`, `failed`, `rejected`, and `timeout` are handled through `result_policy`.
- Wait actions can time out with `continue`, `fail_reaction`, or
  `reset_reaction`.
- `result_policy` supports `continue`, `set_flag`, `fail_reaction`, and
  `fail_scenario`; `retry` is parsed/exported but currently fails the reaction
  as unsupported runtime behavior.
- GM scenario builder can create and edit v2 reactions.
- GM room control renders v2 reaction progress from `variants[].actions`.

## Runtime Rules

- A reaction only fires when it is enabled, its trigger matches, guards pass, cooldown is clear, and `max_fire_count` is not exhausted.
- A reaction does not replace or advance the main scenario flow.
- `any_device_events` fires on the first matching event.
- `all_device_events` records seen trigger events and fires only after every
  configured event has arrived since the last trigger reset.
- `accepted` keeps the current action pending.
- `done` advances to the next action.
- `failed`, `rejected`, and `timeout` follow `result_policy`.
- Reactive wait actions suspend only the reaction branch. The main flow keeps
  its own runtime state.
- `FAIL_REACTION` moves the reaction branch to error state.
- `RESET_REACTION` clears pending trigger/wait/cooldown/cursor state and moves
  the reaction branch back to waiting.
- During cooldown, triggers are ignored unless `reentry.mode` is `queue_one`.
- `queue_one` stores only one pending trigger.
- If multiple active reactions match the same incoming event, none of them runs. This prevents ambiguous behavior.

## Editor Rules

- New reactive branches should always be created as v2 reactions.
- The editor should show reaction type, trigger, conditions, and actions in that order.
- Advanced controls such as reentry, timeout behavior, and max fire count should stay collapsed unless needed.
- A newly created reaction should not add a default `SET_FLAG` action.
- Normal branches and reaction branches must be saved without dropping each other.

## Reserved

Not implemented yet:

- `reentry.restart`
- `reentry.parallel`
- `result_policy.retry`
- parallel `DEVICE_COMMAND_GROUP` result aggregation
- separate `CLEAR_FLAG` action type; use `SET_FLAG` with `"value": false`
- `EMIT_EVENT`
- `OPERATOR_APPROVAL` and `GOTO` inside v2 variant actions

## Non-Goals

Do not add these to Reactive Branch v2:

- local scripting engine;
- hidden generated branches;
- Node-RED-style graph editor;
- recursive scenario flow;
- support for the old reactive trigger-step model as a product feature.
