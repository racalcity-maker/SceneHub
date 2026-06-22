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

## Identifier Policy

Technical ids are not free-form text.

Current rule:

- command ids
- event ids
- emitted local-event names
- state keys
- phase names
- timer names
- driver ids

must match the bounded identifier whitelist:

```text
[A-Za-z0-9_.:-]{1,32}
```

Practical effect:

- use names such as `open_room_2`, `reader_1_master_card`, `phase.locked`;
- do not use spaces, quotes or slash-separated ad hoc ids;
- labels remain human-facing and may use normal printable text.

## Engine Model

This schema is authored as JSON, but the runtime target is a compiled reactive
state machine.

The practical model is:

- rules react to events;
- conditions read current inputs, phase, timer status and local state;
- actions mutate state, phase, timers or emit validated commands/events;
- long waits are represented by timers or later events, not by blocked rule
  execution.

This is a deliberate constraint. It keeps the node deterministic and allows
firmware to reject bundles that cannot fit fixed runtime capacity.

## What "Complex Logic" Means Here

The engine should be able to express:

- `if` / `else`;
- `or` / `and` / `not`;
- multi-step flows;
- waiting for another input after an earlier condition;
- timeout branches;
- explicit reset paths.

The engine should not support:

- loops;
- recursion;
- inline scripts;
- arbitrary expression strings;
- a blocking `wait until ...` instruction.

If authoring input says "wait until input 4 becomes active", the intended form
is usually:

- set phase to something like `waiting_input_4`;
- optionally start a timer for timeout/failure;
- let a later `input_edge`, `input_level` or `timer` trigger continue the flow.

That is the preferred way to build complex quest logic without introducing a
script runtime.

## Bundle Shape

```json
{
  "version": 2,
  "bundle_id": "example_logic",
  "generation": 1,
  "mode": "standalone",
  "exports": {
    "commands": [],
    "events": []
  },
  "drivers": [],
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

## Bundle Export Layer

The bundle may project a scenario-facing control/event layer for SceneHub.

This layer exists so operators can work with semantic controls such as
`open_room_2` instead of raw resources such as `relay_2`.

Preferred contract:

- `exports.commands[]` declares scenario-facing runtime commands;
- `exports.events[]` declares scenario-facing outward event metadata;
- raw hardware resources remain firmware-owned and are not renamed internally;
- `claims[]` is a UI hint that this export is the preferred semantic control
  for one or more raw resources.

Example:

```json
{
  "exports": {
    "commands": [
      {
        "id": "open_room_2",
        "label": "Open Room 2",
        "kind": "runtime_command",
        "claims": [
          "relay_2"
        ]
      }
    ],
    "events": [
      {
        "id": "room_2_open_started",
        "label": "Room 2 Open Started"
      },
      {
        "id": "room_2_open_finished",
        "label": "Room 2 Open Finished"
      }
    ]
  }
}
```

Initial export rules:

- exported command ids must be stable and unique within the bundle;
- exported event ids must be stable and unique within the bundle;
- exported event ids should also be present in `emits[]`;
- exported command/event ids must satisfy the identifier whitelist;
- `claims[]` may reference logical resources such as `relay_2`, `mosfet_1`,
  `strip_1` or driver ids;
- exports are metadata for `device_description`, not a separate scripting
  engine.

Preferred runtime mapping:

- SceneHub invokes exported scenario commands;
- firmware routes them into the rule engine through validated runtime command
  entry;
- rules react through `mqtt_command` or another explicit exported-command
  dispatch path;
- physical side effects still happen only through normal validated `command`
  actions.

This means a bundle can expose `open_room_2`, while the actual rule action may
still be `relay.pulse(output=relay_2, ...)`.

## Driver Instances

The bundle may optionally declare firmware-supported driver instances.

Initial driver direction is intentionally narrow. The first target is a minimal
`nfc_reader` backed by a `pn532`-class implementation.

Example:

```json
{
  "drivers": [
    {
      "id": "reader_1",
      "type": "nfc_reader",
      "driver": "pn532",
      "bus": "i2c_1",
      "config": {
        "poll_interval_ms": 100,
        "debounce_ms": 250,
        "known_cards": [
          {
            "uid": "04AABBCCDD",
            "token_id": 1,
            "event": "reader_1_master_card"
          },
          {
            "uid": "11223344",
            "token_id": 2,
            "event": "reader_1_guest_card"
          }
        ]
      }
    }
  ]
}
```

Initial `known_cards` policy:

- `uid` is the raw normalized card identity string used by the driver;
- `token_id` is the preferred bounded identity value exposed to the engine;
- `event` is optional and lets the driver emit a more specific logical event for
  known cards.
- if `event` is set, it must satisfy the identifier whitelist.

The core engine should usually branch on `token_id` or on the logical event
name, not on raw UID string comparison.

## Triggers

Supported trigger kinds:

- `input_edge`: input changed to a target value.
- `input_level`: input is currently at a target value.
- `input_hold`: input stayed at a target value for a duration.
- `all_inputs_level`: all listed inputs are at a target value.
- `state_changed`: local rule state changed.
- `timer`: named timer fired.
- `boot`: node booted.
- `local_event`: a routed local engine/driver event occurred.
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

```json
{
  "kind": "local_event",
  "event": "reader_1_card_seen"
}
```

## Conditions

Supported condition kinds:

- `state_equals`
- `input_equals`
- `event_field_equals`
- `all_inputs_equal`
- `phase_is`
- `not`
- `all`
- `any`

These condition kinds are the core boolean language of the engine. `if relay_1
is on OR input_2 is active` should compile to `any`, not to a free-form
expression string.

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

`event_field_equals` reads the current event context while the rule is handling
that event. It is the preferred way to branch on bounded payload fields such as
RFID/NFC `token_id`.

Example:

```json
{
  "kind": "event_field_equals",
  "field": "token_id",
  "value": 2
}
```

## NFC Hold Pattern

The preferred NFC/RFID hold model is:

- `reader_1_card_seen` starts a named timer;
- phase/state marks the active hold session;
- `reader_1_card_removed` cancels the timer and resets phase/state;
- a later `timer` rule performs the success path only if phase/state still
  matches.

This is intentional. The driver should report compact reader events, not grow a
catalog of special hold-duration events.

Current schema note:

- `event_field_equals` currently supports only `token_id` equality;
- there is no first-class `token_id in [...]` or `token_id != 0` yet.

So for "any known card" today, use one of these bounded approaches:

- explicit `any` with one `event_field_equals(token_id)` clause per known card;
- configured logical events such as `reader_1_master_card`;
- one small rule per known token when that is clearer.

Bundle examples:

- [nfc_hold_5s_any_known_card.json](examples/node_v2_bundles/nfc_hold_5s_any_known_card.json)
- [nfc_hold_10s_token_1.json](examples/node_v2_bundles/nfc_hold_10s_token_1.json)
- [nfc_hold_until_removed_reset.json](examples/node_v2_bundles/nfc_hold_until_removed_reset.json)

Practical notes for these examples:

- they assume the reader logical id is `reader_1`;
- they assume the LED logical output is `strip_1`;
- adapt token ids and logical output names to the current
  `device_description` before upload.

## Actions

Supported action kinds:

- `command`: call a declared local command handler.
- `set_state`: set a local rule variable.
- `set_phase`: move the rule bundle to a named phase.
- `emit_event`: publish a device event through the normal SceneHub event topic.
- `start_timer`: start or replace a named timer.
- `cancel_timer`: cancel a named timer.
- `choose`: pick one of two bounded action branches using a validated
  condition.
- `sequence`: run bounded actions in order.

`choose` is the declarative replacement for inline `if/else`. It should stay
bounded: no unbounded nesting, no loops, no arbitrary computed expressions.

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

## State Types

Initial local state types should stay simple and fixed:

- boolean;
- integer/number;
- short string identifiers when unavoidable.

Large dynamic objects, nested maps and arrays should not become runtime state in
the first engine version.

## Waiting and Delays

The engine should not block inside a rule while "waiting" for time or another
input. Instead:

- use `start_timer` plus a later `timer` trigger for time-based continuation;
- use `set_phase` plus another input-driven rule for event-based continuation;
- use `choose` only for immediate branch selection inside the current action
  evaluation.

This means author-facing bundles may describe a human idea like "wait 5 seconds
then if input 2 is still active continue, else reset", but the compiled form is
still phase/timer/rule based.

## Driver Event Payloads

Driver-routed events may carry a bounded payload for the current event context.

For initial `nfc_reader` support, the preferred payload shape is:

```json
{
  "reader": "reader_1",
  "token_id": 2,
  "uid": "11223344",
  "uid_len": 4,
  "seen_count": 1
}
```

Payload policy:

- `token_id` is the preferred field for rule conditions;
- `uid` is mainly for diagnostics and observability;
- rule conditions should avoid generic raw UID string matching in the first
  engine version;
- if `event` is configured on a known card, the driver may emit a more specific
  event name in addition to the generic `reader_1_card_seen`.

## Example: RFID/NFC Branching

Natural-language source:

```text
When the master card is shown on reader 1, pulse relay 1.
When the guest card is shown on reader 1, turn on relay 2.
When any unknown card is shown, emit a diagnostic event.
```

Rule bundle slice:

```json
{
  "version": 2,
  "bundle_id": "reader_logic",
  "generation": 1,
  "mode": "standalone",
  "drivers": [
    {
      "id": "reader_1",
      "type": "nfc_reader",
      "driver": "pn532",
      "bus": "i2c_1",
      "config": {
        "poll_interval_ms": 100,
        "debounce_ms": 250,
        "known_cards": [
          {
            "uid": "04AABBCCDD",
            "token_id": 1,
            "event": "reader_1_master_card"
          },
          {
            "uid": "11223344",
            "token_id": 2,
            "event": "reader_1_guest_card"
          }
        ]
      }
    }
  ],
  "emits": [
    "unknown_card_seen"
  ],
  "rules": [
    {
      "id": "master_card",
      "enabled": true,
      "trigger": {
        "kind": "local_event",
        "event": "reader_1_card_seen"
      },
      "conditions": {
        "kind": "event_field_equals",
        "field": "token_id",
        "value": 1
      },
      "actions": [
        {
          "kind": "command",
          "command": "relay.pulse",
          "args": {
            "output": "relay_1",
            "duration_ms": 500
          }
        }
      ]
    },
    {
      "id": "guest_card",
      "enabled": true,
      "trigger": {
        "kind": "local_event",
        "event": "reader_1_card_seen"
      },
      "conditions": {
        "kind": "event_field_equals",
        "field": "token_id",
        "value": 2
      },
      "actions": [
        {
          "kind": "command",
          "command": "relay.set",
          "args": {
            "output": "relay_2",
            "on": true
          }
        }
      ]
    },
    {
      "id": "unknown_card",
      "enabled": true,
      "trigger": {
        "kind": "local_event",
        "event": "reader_1_unknown_card"
      },
      "actions": [
        {
          "kind": "emit_event",
          "event": "unknown_card_seen",
          "args": {
            "reader": "reader_1"
          }
        }
      ]
    }
  ]
}
```

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
- phase/timer modeling for waits and timeouts instead of blocking waits;
- `token_id` or logical event based branching for RFID/NFC instead of raw UID
  comparisons;
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
multi-step logic. Model waits using phases and timers, not blocking delays. Do
not use commands outside the supported command list.
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
- driver type or driver event name is unknown;
- `known_cards` contains duplicate `uid` or duplicate `token_id` within one
  reader instance;
- action args fail command validation;
- phases referenced by rules are inconsistent;
- `choose` or `sequence` nesting exceeds firmware limits;
- a wait pattern would require blocking runtime execution instead of timer/event
  continuation;
- configured limits exceed firmware limits;
- compiled runtime tables do not fit fixed capacity.
