# Architecture

`Quest Orchestrator` is an ESP32-S3 quest orchestration hub. It provides the operator
panel, room runtime, device monitoring, scenario execution, game modes, audio
control and persistent JSON configuration.

`mqtt_core` is only the local MQTT broker module. Product-facing docs and UI
should use `Quest Orchestrator` / `GM Panel` for the whole system.

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
| `gm_game_profile` | Game Mode model, validation, JSON import/export |
| `gm_core` | Room session runtime, timer, game start/stop/reset, scenario execution |
| `orchestrator_core` | GM read model, health aggregation, audit and timeline |
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

- `DEVICE_COMMAND` sends one saved Quest Device command.
- `DEVICE_COMMAND_GROUP` sends several saved commands in order.
- `WAIT_TIME` resumes from the tick handler.
- `WAIT_DEVICE_EVENT`, `WAIT_ANY_DEVICE_EVENT` and `WAIT_ALL_DEVICE_EVENTS`
  resume from device events.
- `OPERATOR_APPROVAL` resumes from operator approval.
- `SET_FLAG` and `WAIT_FLAGS` synchronize branches inside one game run.
- `END_GAME` finishes the game timer/session without automatically stopping
  audio.

Game stop:

1. Room scenario runtime stops.
2. Game timer stops.
3. System audio receives a best-effort stop command.
4. Audio output drains a short silence buffer before I2S reset so the DAC does
   not hold the last sample.
5. The room session returns to stopped/finished state.

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
      "cooldown_ms": 3000,
      "run_once": false,
      "steps": []
    }
  ]
}
```

Internally the C model stores one flat step array plus branch ranges to avoid
duplicating step memory.

Reactive branches are not a second scenario engine. They are one-trigger
reaction loops inside the same scenario snapshot. A reactive branch waits for
one trigger/listen step, runs a short action chain, then returns to listening.
Use several reactive branches for several different triggers. Reactive branches
do not participate in `required_for_completion`, do not block the main flow and
do not finish the game.

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
