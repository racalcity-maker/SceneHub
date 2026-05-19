# SceneHub Node v2 Design

Node v2 is a future extension where a physical node can accept custom JSON
logic and execute local behavior without reflashing firmware.

This is not a replacement for SceneHub scenarios. SceneHub remains the room
orchestrator. Node v2 is for local, bounded, hardware-close behavior such as
input debouncing, relay effects, local interlocks and small autonomous actions.

The concrete rule schema draft and AI-generation contract live in
`NODE_V2_RULE_SCHEMA_DRAFT.md`.

Node v2 may also expose a small set of firmware-built device drivers such as
digital sensors, NFC readers, DFPlayer and LED strips. Driver support is
intentionally bounded: the node is not trying to become ESPHome.

## Goals

- Configure local behavior through JSON.
- Avoid firmware rebuilds for simple node logic changes.
- Keep the v1 MQTT contract unchanged.
- Keep hardware safety in firmware, not in user JSON.
- Keep execution bounded and predictable.
- Allow SceneHub to inspect the active rule bundle/version.

## Non-Goals

- No full JavaScript/Lua/Python runtime in the first version.
- No unbounded expressions or recursion.
- No dynamic module loading.
- No room scenario replacement inside the node.
- No direct hardware access from raw JSON.
- No arbitrary driver loading from JSON.
- No broad home-automation component marketplace in the node.

## JSON Rule Model

Initial shape:

```json
{
  "version": 2,
  "bundle_id": "relay_room_2_rules",
  "generation": 1,
  "rules": [
    {
      "id": "pulse_on_drawer_open",
      "enabled": true,
      "trigger": {
        "kind": "input_event",
        "event": "drawer_1_opened"
      },
      "conditions": [
        {
          "kind": "state_equals",
          "key": "armed",
          "value": true
        }
      ],
      "actions": [
        {
          "kind": "command",
          "command": "relay.pulse",
          "args": {
            "channel": 1,
            "duration_ms": 500
          }
        }
      ]
    }
  ]
}
```

The stored JSON is admin/config data. Runtime execution uses compiled bounded
tables, not raw JSON tree traversal.

For multi-step logic, prefer explicit phases and state flags over hidden
control flow. Example use cases:

- input edge triggers relay output and event publish;
- input held for a duration triggers blink/effect actions;
- reset input returns the local flow to its initial phase;
- after several local conditions are complete, wait for additional inputs and
  emit a final event.

## Driver Model

Drivers are compiled into firmware and registered in a `driver_registry`.
Configuration may create instances of those drivers, but only from the supported
driver list.

Initial driver categories:

- `digital_input`: buttons, reed switches, optocoupled inputs.
- `relay_output`: relay boards and dry-contact outputs.
- `mosfet_output`: DC output channels.
- `led_strip`: bounded LED effects, not arbitrary animation scripts.
- `nfc_reader`: UID/card/tag events.
- `dfplayer`: simple audio playback over UART.
- `sensor`: simple scalar sensors such as distance, light or temperature.

Driver instance example:

```json
{
  "drivers": [
    {
      "id": "reader_1",
      "type": "nfc_reader",
      "bus": "uart_1",
      "config": {
        "baud": 115200
      },
      "events": {
        "card_seen": "reader_1_card_seen",
        "card_removed": "reader_1_card_removed"
      }
    },
    {
      "id": "music_1",
      "type": "dfplayer",
      "bus": "uart_2",
      "config": {
        "baud": 9600,
        "volume": 22
      }
    }
  ]
}
```

Rules use logical driver ids and declared commands/events. They do not call
UART/I2C/SPI/GPIO primitives directly.

Driver commands must still go through `action_router`, for example:

- `nfc.identify`
- `dfplayer.play`
- `dfplayer.stop`
- `led.effect`
- `sensor.calibrate`

Driver events enter the same event router as GPIO inputs:

- `reader_1_card_seen`
- `reader_1_card_removed`
- `sensor_threshold_crossed`
- `dfplayer_finished`

## Driver Safety Rules

- A driver must validate all config before activation.
- A bad driver config must not break already active rules.
- Driver polling must have a fixed budget.
- Driver callbacks must enqueue events and return quickly.
- Driver commands must be idempotent where physical side effects can repeat.
- Long-running driver work must report `accepted` then a terminal result.
- Driver diagnostics must be visible in status/diag.

## Driver Scope Guardrail

Add new drivers only when they are needed for quest-room nodes and can be kept
small. If a feature needs broad device discovery, arbitrary protocols,
third-party integrations or cloud-style automation, it belongs outside Node v2.

## Rule Lifecycle

1. SceneHub sends `node.rules.validate` with a candidate bundle.
2. Node validates schema, capabilities and capacity limits.
3. SceneHub sends `node.rules.apply` with the accepted bundle.
4. Node compiles the bundle into inactive tables.
5. Node atomically swaps the active bundle.
6. Node publishes status with `rules_bundle_id`, `rules_generation` and
   `rules_status`.

Optional commands:

- `node.rules.get`
- `node.rules.clear`
- `node.rules.pause`
- `node.rules.resume`

## Execution Model

Rules are event driven:

- input event;
- timer event;
- MQTT command event;
- local state change;
- boot/reconnect event.

Actions are routed through the same validated command handlers used by normal
MQTT commands.

Allowed action kinds:

- `command`
- `set_state`
- `emit_event`
- `delay`
- `cancel_timer`

Unsafe action kinds are not allowed unless represented as normal declared
commands with policy and validation.

## Safety Limits

Every active bundle has hard limits:

- maximum rule count;
- maximum actions per rule;
- maximum queued actions;
- maximum timers;
- maximum delay duration;
- maximum action execution time per tick;
- maximum JSON size;
- maximum string pool bytes.

If runtime limits are exceeded:

- stop the offending rule;
- publish diagnostics;
- keep hardware in a safe state;
- keep the node connected and observable.

## SceneHub Integration

Node v2 should expose rule support in `device_description`:

```json
{
  "commands": [
    {
      "id": "rules_apply",
      "label": "Apply node rules",
      "capability": "node_rules",
      "command": "node.rules.apply",
      "policy": {
        "manual_allowed": false,
        "scenario_allowed": false,
        "requires_confirmation": true,
        "result_required": true,
        "timeout_ms": 5000,
        "danger_level": "danger"
      }
    }
  ]
}
```

Normal room scenarios should continue to use high-level device commands and
events. Rule editing is an admin/config operation, not a scenario step path.

## Versioning

- Rule bundle schema has its own `version`.
- Node reports supported rule schema versions in status.
- Node rejects unsupported versions before storing.
- Future migrations must happen before activation and must preserve the last
  known-good active bundle.

## First Implementation Slice

V2 should be built in small steps:

1. Add rule bundle storage with validate/get/clear only.
2. Add compile-to-table without execution.
3. Add input-event trigger and one `command` action.
4. Add timers/delay.
5. Add state variables and conditions.
6. Add diagnostics and rule status projection.

Do not add a general scripting VM until fixed-table rules prove insufficient.
