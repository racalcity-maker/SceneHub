# Architecture

SceneHub is an ESP32-S3 local orchestration hub for quest rooms, interactive
exhibits and show-control installations. It provides the operator panel, room
runtime, device monitoring, scenario execution, game modes, audio control and
persistent JSON configuration.

`mqtt_core` is only the local MQTT broker module. Product-facing docs and UI
should use `SceneHub` / `GM Panel` for the whole system.

## Product Model

The current model has one gameplay owner: the Room Scenario engine.

```text
Physical MQTT client
  -> control contract telemetry
  -> optional quest interface discovery

Quest Device
  -> saved commands/events for one physical client or built-in service

Room Scenario
  -> normal flow branches made from step types
  -> reactive branches for event reactions
  -> commands, waits, operator gates, flags, end-game

Game Mode
  -> selects room scenario
  -> sets game duration and future packs

GM Session
  -> selected mode
  -> timer
  -> scenario runtime snapshot
  -> room/system health
```

Devices do not own quest flow. A device exposes capabilities; room scenarios use
those capabilities.

Current gameplay execution is owned by `gm_core` and `room_scenario`; current
device state comes from `quest_device` and `device_control_ingest`.

## Main Components

| Component | Responsibility |
| --- | --- |
| `network` | Wi-Fi and orchestrator network lifecycle |
| `mqtt_core` | Local MQTT broker: client sessions, publish/subscribe and MQTT packet limits |
| `event_bus` | In-process events for runtime services |
| `quest_common` | Shared current-model string limits and utility helpers |
| `room_catalog` | File-backed room list at `/sdcard/quest/rooms.json` |
| `quest_device` | File-backed Quest Device store and command/event capability model |
| `device_control_ingest` | Control-contract telemetry ingest for observed physical clients |
| `room_scenario` | Scenario model, validation, JSON import/export |
| `command_executor` | Command dispatch boundary for MQTT devices, system audio and local hardware IO |
| `gm_game_profile` | Game Mode model, validation, JSON import/export |
| `gm_core` | Room session runtime, timer, game start/stop/reset, scenario execution |
| `orchestrator_core` | GM read model, health aggregation, audit and timeline |
| `hardware_io` | Built-in relay/MOSFET/input/GPIO control, safe-off and status snapshots |
| `audio_player` | Local audio playback service, background/effect mixer and system audio command handling |
| `web_ui` | HTTP API, auth, GM panel assets and UI endpoints |
| `error_monitor` | Fault collection for dashboard/room health |
| `status_led` | Device status indication |

## Runtime Flow

Game start:

1. Operator selects a Game Mode.
2. `gm_room_session_game_start(room_id)` validates the selected mode.
3. The selected Room Scenario is copied into the session runtime snapshot.
4. The game timer starts.
5. Enabled normal scenario branches start running.
6. Enabled reactive branches start listening for their trigger events.
7. Runtime state is exposed through GM APIs and rendered in Room Control.

Scenario execution:

- `DEVICE_COMMAND` sends one saved Quest Device command through `command_executor`.
- `DEVICE_COMMAND_GROUP` sends several saved commands in order.
- `WAIT_TIME` resumes from the tick handler.
- `WAIT_DEVICE_EVENT`, `WAIT_ANY_DEVICE_EVENT` and `WAIT_ALL_DEVICE_EVENTS`
  resume from device events.
- `OPERATOR_APPROVAL` resumes from operator approval.
- `SET_FLAG` and `WAIT_FLAGS` synchronize branches inside one game run.
- `END_GAME` finishes the game timer/session without automatically stopping
  audio or turning off local hardware outputs.

Game stop:

1. Room scenario runtime stops.
2. Game timer stops.
3. System audio receives a best-effort stop command.
4. Built-in relay/MOSFET/GPIO outputs are forced to safe/off after the GM
   session lock is released; failures are surfaced through `service_status`.
5. Audio output drains a short silence buffer before I2S reset so the DAC does
   not hold the last sample.
6. The room session returns to stopped/finished state.

Game reset also forces built-in relay/MOSFET/GPIO outputs to safe/off after the
GM session lock is released. `END_GAME` does not; scenarios that need finale
cleanup should add explicit `system_audio.stop`, `system_relay.set/off`,
`system_mosfet.all_off` or `system_gpio.set/inactive` steps.

Service runtime faults are promoted into the orchestrator issue list. For
example, a `hardware_io` safe-off failure becomes a system issue visible in GM
state instead of being hidden in logs only.

## Branches

A scenario may contain up to eight branches. Each branch is linear, but multiple
branches can run during the same game. Branches share one game-run flag store,
so one branch can set `book_done=true` and another branch can wait for it.

Branches have an explicit product role:

- `normal` branches describe the quest flow and may be required for completion.
- `reactive` branches describe event reactions and are shown separately as
  reactions in the GM Panel.

Branch JSON is edited as:

```json
{
  "branches": [
    {
      "id": "main",
      "name": "Main",
      "type": "normal",
      "enabled": true,
      "required_for_completion": true,
      "steps": []
    },
    {
      "id": "wrong_card_reaction",
      "name": "Wrong card reaction",
      "type": "reactive",
      "enabled": true,
      "trigger": {
        "kind": "device_event",
        "device_id": "uid_gate",
        "event_id": "sequence_invalid"
      },
      "guard_flags": [],
      "policy": {
        "mode": "escalate",
        "cooldown_ms": 3000,
        "max_fire_count": 0
      },
      "reentry": {
        "mode": "ignore"
      },
      "variants": [
        {
          "id": "level_1",
          "label": "Level 1",
          "actions": []
        }
      ],
      "result_policy": {
        "on_done": "continue",
        "on_fail": "fail_reaction",
        "on_timeout": "fail_reaction"
      }
    }
  ]
}
```

Internally the C model stores normal steps and Reactive Branch v2 action
variants in bounded arrays to keep runtime snapshots predictable on ESP32-S3.

Reactive branches are not a second scenario engine. Reactive Branch v2 uses:

```text
trigger -> guard_flags -> policy -> selected variant -> actions -> result_policy
```

A reaction listens for a trigger, checks guards, selects a variant with
`single`, `rotate`, `random` or `escalate`, runs sequential actions, then
returns to listening or cooldown. Use several reactive branches for several
different triggers. Reactive branches do not participate in
`required_for_completion`, do not block the main flow and do not finish the game.

Result-required reaction commands use `command_executor`. `accepted` keeps the
action pending; terminal `done` advances the action; `failed`, `rejected` and
`timeout` follow the branch result policy.

For `Same actions`, `Can repeat` is represented as `max_fire_count=0`; `Run once`
is represented as `run_once=true` and `max_fire_count=1`.

Reactive branches use the same game-run flag store as normal branches. This is
intentional: a reaction may execute `SET_FLAG secret_path_unlocked=true`, and a
normal branch may later continue from `WAIT_FLAGS secret_path_unlocked=true`.
Use explicit normal-flow `END_GAME` steps for game completion; do not hide game
completion behind reactive flags.

## Persistence

Current persistent files:

- `/sdcard/quest/rooms.json`
- `/sdcard/quest/quest_devices.json`
- `/sdcard/quest/room_scenarios.json`
- `/sdcard/quest/game_profiles.json`

Stores use replace-all import policy where possible: invalid import must not
clear the existing store.

## GM Panel

Operators should mostly use Room Control:

- game status
- selected mode
- timer
- current runtime state
- scenario progress
- reaction status
- operator approval
- hints
- device issues
- manual buttons

Admin-only sections:

- Game Modes
- Scenarios
- Device Setup
- Storage

Device Setup edits Quest Devices only: name, physical client id, commands,
events, manual buttons and optional interface discovery import.

## Event Hot Paths

`event_bus` owns the shared internal event pool/job queue. Scenario matching in
`gm_core` has a dedicated FreeRTOS queue that stores `event_bus_message_t`
values directly. The GM session event handler does not allocate/free heap memory
per event.

Command side effects are separated from scenario state progression by
`command_executor`. Runtime decides that a command should run; the executor
resolves the backend, creates request ids, tracks pending result-required
commands and emits normalized command-result events.

## Device Health

Physical clients report control-contract telemetry:

- heartbeat
- status
- diagnostics
- command result

A saved Quest Device with no fresh telemetry is a critical fault. During setup,
an unseen client is degraded/warning until it sends the first valid telemetry.

## Design Rules

- New gameplay behavior belongs in Room Scenarios.
- Devices expose capabilities; they do not own quest flow.
- Game Modes are selection presets, not a second scenario engine.
- UI should show names first and hide ids behind advanced/debug sections.
- Scenario step editors should be schema-driven where practical.
- HTTP APIs should remain documented in `gm_api_contract.md`.
