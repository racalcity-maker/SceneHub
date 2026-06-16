# Node v2 Rule Schema Draft

Node v2 rules describe local hardware-close logic in JSON. The goal is that an
operator or an AI assistant can turn a short natural-language description into a
bounded, validated rule bundle without changing firmware.

This schema is intentionally declarative. It is not a general scripting
language.

This document describes the uploaded `standalone_bundle`. It is not the same
document as `device_description`. Firmware generates `device_description` for
SceneHub from the current hardware config, capability registry and active rule
bundle metadata.

## Core Concepts

- `inputs`: named digital/analog inputs exposed by firmware.
- `outputs`: named relay/MOSFET/LED/audio outputs exposed by firmware.
- `drivers`: named firmware-supported device instances such as NFC reader or
  DFPlayer.
- `events`: named MQTT events the node may publish.
- `state`: local boolean/number/string variables owned by the rule engine.
- `rules`: event-driven logic units.
- `phases`: optional named states for multi-step flows.
- `actions`: validated operations routed through local command handlers.

Firmware owns the physical mapping. JSON uses logical names such as `input_1`,
`relay_1` or `strip_main`.

During compile, firmware resolves logical names to the existing command
arguments used by v1 handlers. For example, a rule may reference `relay_1`, but
the compiled command action for `relay.set` still calls the existing command
handler with `channel: 1`.

Driver instances are also logical names, for example `reader_1` or `music_1`.
Rules may use only commands and events that the firmware declares for that
driver instance.

## Bundle Shape

```json
{
  "version": 2,
  "bundle_id": "example_logic",
  "generation": 1,
  "mode": "standalone",
  "emits": [
    "input_1_confirmed",
    "local_logic_reset"
  ],
  "limits": {
    "max_action_ms": 50,
    "max_parallel_timers": 8
  },
  "initial_state": {
    "input_1_seen": false,
    "input_2_hold_done": false,
    "armed_for_3_4": false
  },
  "rules": []
}
```

## Triggers

Supported trigger kinds:

- `input_edge`: input changed to a target value.
- `input_level`: input is currently at a target value.
- `input_hold`: input stayed at a target value for a duration.
- `all_inputs_level`: all listed inputs are at a target value.
- `state_changed`: local rule state changed.
- `timer`: named timer fired.
- `boot`: node booted.
- `mqtt_command`: optional local command/rule trigger.

Examples:

```json
{
  "kind": "input_edge",
  "input": "input_1",
  "to": 1
}
```

```json
{
  "kind": "input_hold",
  "input": "input_2",
  "value": 1,
  "duration_ms": 3000
}
```

## Conditions

Supported condition kinds:

- `state_equals`
- `input_equals`
- `all_inputs_equal`
- `phase_is`
- `not`
- `all`
- `any`

Example:

```json
{
  "kind": "all",
  "conditions": [
    {
      "kind": "state_equals",
      "key": "input_1_seen",
      "value": true
    },
    {
      "kind": "input_equals",
      "input": "input_2",
      "value": 1
    }
  ]
}
```

## Actions

Supported action kinds:

- `command`: call a declared local command handler.
- `set_state`: set a local rule variable.
- `set_phase`: move the rule bundle to a named phase.
- `emit_event`: publish a device event through the normal SceneHub event topic.
- `start_timer`: start or replace a named timer.
- `cancel_timer`: cancel a named timer.
- `sequence`: run bounded actions in order.

Command actions must map to commands already exposed by firmware, for example:

- `relay.set`
- `relay.pulse`
- `relay.blink`
- `mosfet.set`
- `led.effect`

Example:

```json
{
  "kind": "command",
  "command": "relay.set",
  "args": {
    "output": "relay_1",
    "on": true
  }
}
```

The rule schema uses logical `input`, `output` and `driver` names for authoring.
The compiled runtime representation must not pass those names directly into hot
hardware paths. It resolves them to bounded typed fields or to the existing v1
command argument names before execution.

## Example: Multi-Input Quest Logic

Natural-language source:

```text
When input 1 becomes 1, turn relay 1 on and send an event.
When input 2 stays 1 for 3 seconds, blink relay 2, start an LED strip effect and
send an event.
If input 1 returns to 0, reset the local flow and start a reset LED effect.
After the first two conditions are complete, wait until inputs 3 and 4 are both
1, then do the next action and send a final event.
```

Rule bundle:

```json
{
  "version": 2,
  "bundle_id": "four_input_interlock",
  "generation": 1,
  "mode": "standalone",
  "emits": [
    "input_1_confirmed",
    "input_2_hold_confirmed",
    "local_logic_reset",
    "four_input_interlock_complete"
  ],
  "initial_phase": "waiting_input_1",
  "initial_state": {
    "input_1_seen": false,
    "input_2_hold_done": false
  },
  "rules": [
    {
      "id": "input_1_on",
      "enabled": true,
      "trigger": {
        "kind": "input_edge",
        "input": "input_1",
        "to": 1
      },
      "conditions": {
        "kind": "any",
        "conditions": [
          {
            "kind": "phase_is",
            "phase": "waiting_input_1"
          },
          {
            "kind": "phase_is",
            "phase": "waiting_input_2_hold"
          }
        ]
      },
      "actions": [
        {
          "kind": "command",
          "command": "relay.set",
          "args": {
            "output": "relay_1",
            "on": true
          }
        },
        {
          "kind": "set_state",
          "key": "input_1_seen",
          "value": true
        },
        {
          "kind": "set_phase",
          "phase": "waiting_input_2_hold"
        },
        {
          "kind": "emit_event",
          "event": "input_1_confirmed",
          "args": {
            "input": 1
          }
        }
      ]
    },
    {
      "id": "input_2_hold",
      "enabled": true,
      "trigger": {
        "kind": "input_hold",
        "input": "input_2",
        "value": 1,
        "duration_ms": 3000
      },
      "conditions": {
        "kind": "state_equals",
        "key": "input_1_seen",
        "value": true
      },
      "actions": [
        {
          "kind": "command",
          "command": "relay.blink",
          "args": {
            "output": "relay_2",
            "on_ms": 300,
            "off_ms": 300,
            "count": 5
          }
        },
        {
          "kind": "command",
          "command": "led.effect",
          "args": {
            "output": "strip_main",
            "effect": "success_chase",
            "duration_ms": 2500
          }
        },
        {
          "kind": "set_state",
          "key": "input_2_hold_done",
          "value": true
        },
        {
          "kind": "set_phase",
          "phase": "waiting_inputs_3_4"
        },
        {
          "kind": "emit_event",
          "event": "input_2_hold_confirmed",
          "args": {
            "input": 2,
            "hold_ms": 3000
          }
        }
      ]
    },
    {
      "id": "input_1_reset",
      "enabled": true,
      "trigger": {
        "kind": "input_edge",
        "input": "input_1",
        "to": 0
      },
      "conditions": {
        "kind": "not",
        "condition": {
          "kind": "phase_is",
          "phase": "complete"
        }
      },
      "actions": [
        {
          "kind": "command",
          "command": "relay.set",
          "args": {
            "output": "relay_1",
            "on": false
          }
        },
        {
          "kind": "set_state",
          "key": "input_1_seen",
          "value": false
        },
        {
          "kind": "set_state",
          "key": "input_2_hold_done",
          "value": false
        },
        {
          "kind": "set_phase",
          "phase": "waiting_input_1"
        },
        {
          "kind": "command",
          "command": "led.effect",
          "args": {
            "output": "strip_main",
            "effect": "reset_wave",
            "duration_ms": 1500
          }
        },
        {
          "kind": "emit_event",
          "event": "local_logic_reset",
          "args": {
            "reason": "input_1_off"
          }
        }
      ]
    },
    {
      "id": "inputs_3_4_complete",
      "enabled": true,
      "trigger": {
        "kind": "all_inputs_level",
        "inputs": [
          "input_3",
          "input_4"
        ],
        "value": 1
      },
      "conditions": {
        "kind": "all",
        "conditions": [
          {
            "kind": "phase_is",
            "phase": "waiting_inputs_3_4"
          },
          {
            "kind": "state_equals",
            "key": "input_1_seen",
            "value": true
          },
          {
            "kind": "state_equals",
            "key": "input_2_hold_done",
            "value": true
          }
        ]
      },
      "actions": [
        {
          "kind": "command",
          "command": "relay.pulse",
          "args": {
            "output": "relay_3",
            "duration_ms": 1000
          }
        },
        {
          "kind": "set_phase",
          "phase": "complete"
        },
        {
          "kind": "emit_event",
          "event": "four_input_interlock_complete",
          "args": {
            "inputs": [
              1,
              2,
              3,
              4
            ]
          }
        }
      ]
    }
  ]
}
```

## AI Generation Contract

When asking an AI assistant to create a Node v2 rule bundle, provide:

- node id;
- available inputs by logical name;
- available outputs by logical name;
- supported commands;
- desired events to emit;
- natural-language behavior;
- safety constraints;
- max timing tolerances.

Expected AI output:

- one `standalone_bundle` JSON document;
- no comments inside JSON;
- stable rule ids;
- explicit phases for multi-step flows;
- explicit reset behavior;
- no direct hardware pin numbers unless the firmware exposes them as logical
  names;
- only commands listed in the node capability description.

Prompt template:

```text
Create a SceneHub Node v2 rule bundle.

Available inputs:
- input_1
- input_2
- input_3
- input_4

Available outputs:
- relay_1
- relay_2
- relay_3
- strip_main

Supported commands:
- relay.set(output,on)
- relay.pulse(output,duration_ms)
- relay.blink(output,on_ms,off_ms,count)
- led.effect(output,effect,duration_ms)
- dfplayer.play(driver,track,volume)
- dfplayer.stop(driver)
- nfc.identify(driver)

Behavior:
<describe behavior here>

Return valid standalone_bundle JSON only. Use schema version 2. Use phases for
multi-step logic. Do not use commands outside the supported command list.
```

The AI context document should be generated by the node or by SceneHub from the
node `device_description`. It should contain only capabilities, logical
resources, rule limits and prompt guidance. It must not include Wi-Fi passwords,
MQTT credentials or other secrets.

## Validation Requirements

Firmware must reject a bundle if:

- unknown trigger kind is used;
- unknown condition kind is used;
- unknown action kind is used;
- command is not supported by the node;
- input/output logical name is unknown;
- action args fail command validation;
- phases referenced by rules are inconsistent;
- configured limits exceed firmware limits;
- compiled runtime tables do not fit fixed capacity.
