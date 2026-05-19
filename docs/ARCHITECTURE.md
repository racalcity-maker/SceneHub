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
Product-aware scenario environment checks are owned by
`scenehub_scenario_validation`, not by the `room_scenario` model itself.

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
| `room_scenario` | Scenario model, static/runtime-semantic validation, JSON import/export |
| `scenehub_scenario_validation` | SceneHub-specific scenario environment validation against Quest Device and local hardware capabilities |
| `command_executor` | Command dispatch boundary for MQTT devices, system audio and local hardware IO |
| `gm_game_profile` | Game Mode model, validation, JSON import/export |
| `gm_core` | Room session runtime, timer, game start/stop/reset, scenario execution |
| `scenehub_control` | Write-side application facade for GM/scenario/profile/device actions |
| `scenehub_read_model` | Read-side room/runtime/profile/scenario projections for APIs and UI |
| `orchestrator_core` | GM read model, health aggregation, audit and timeline |
| `hardware_io` | Built-in relay/MOSFET/universal IO control, safe-off and status snapshots |
| `audio_player` | Local audio playback service, background/effect mixer and system audio command handling |
| `web_ui` | HTTP API, auth, GM panel assets and UI endpoints |
| `error_monitor` | Fault collection for dashboard/room health |
| `status_led` | Device status indication |

## Layering Contract

SceneHub uses a one-way command/read/event boundary. This is a durable
architecture rule, not an optimization preference.

The practical risk map for these boundaries is tracked in
`ARCHITECTURE_LAYER_RISK_MAP.md`.
DTO ownership and cleanup targets are tracked in
`DTO_BOUNDARY_INVENTORY.md`.

Target dependency direction:

```text
web_ui
  -> scenehub_control / gm_control / scenehub_read_model
  -> gm_core / room_scenario / quest_device / device_control_ingest
  -> event_bus / mqtt_core / hardware_io / audio_player / storage
```

The practical rule is:

- Commands go down through control/application services.
- Events go up through `event_bus` or component-owned queues.
- Read models only read and project state.
- Web UI only calls services and serializes/deserializes HTTP/WebSocket data.

Allowed dependency shapes:

- `web_ui -> scenehub_control` for write-side actions.
- `web_ui -> scenehub_read_model` for read-side projections.
- `scenehub_control -> gm_core / room_scenario / quest_device` for domain
  writes.
- `gm_core -> command_executor` for external command dispatch.
- `command_executor -> mqtt_core / hardware_io / audio_player` for side
  effects.
- `device_control_ingest -> event_bus` for normalized telemetry/result events.
- `mqtt_core -> event_bus` for inbound MQTT events after origin tagging.
- `scenehub_read_model -> gm_core / quest_device / device_control_ingest` for
  read-only projections.

Dependencies that require extra scrutiny:

- `web_ui` using low-level domain DTOs directly instead of view/control DTOs.
- `scenehub_read_model` depending on session internals, assets, or storage-heavy
  helpers beyond projection needs.
- `gm_core` depending on Quest Device metadata beyond planned command/runtime
  semantics.
- `event_bus` bridges that can reflect inbound transport messages back to the
  same transport.

If a change violates this contract, prefer adding a narrow control/read-model
function over pulling another lower-level type into `web_ui` or `gm_core`.

## DTO Boundary Rules

DTOs are allowed only as layer boundary contracts:

- `quest_device_t`, `room_scenario_t` and profile structs are domain/storage
  DTOs owned by their modules.
- `gm_room_session_*_view_t` structs are core projection DTOs. They are used by
  read-model code, not by HTTP handlers directly.
- `orch_*_entry_t`, `orch_*_view_t` and `orch_*_detail_t` structs are
  read-model DTOs for UI/API serialization.
- `scenehub_control_result_t` and related small info structs are write-side
  control envelopes.
- Web UI code may serialize DTOs, but must not become the owner of domain
  storage DTOs.

A new DTO is rejected by default if it only mirrors another DTO 1:1 without
changing layer, ownership, lifetime, payload width or stability guarantees.

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

- Under the session lock, `gm_core` decides the next command as a small planned
  dispatch.
- The actual external command is executed after unlock through
  `command_executor`.
- If the session is reset/stopped or the branch/action state changes before the
  planned dispatch is applied, the stale plan is dropped as a no-op.
- `DEVICE_COMMAND` sends one saved Quest Device command through `command_executor`.
- `DEVICE_COMMAND_GROUP` sends several saved commands in order.
- `WAIT_TIME` resumes from the GM runtime deadline wake path.
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
4. Built-in relay/MOSFET/IO outputs are forced to safe/off after the GM
   session lock is released; failures are surfaced through `service_status`.
5. Audio output drains a short silence buffer before I2S reset so the DAC does
   not hold the last sample.
6. The room session returns to stopped/finished state.

Game reset also forces built-in relay/MOSFET/IO outputs to safe/off after the
GM session lock is released. `END_GAME` does not; scenarios that need finale
cleanup should add explicit `system_audio.stop`, `system_relay.set/off`,
`system_mosfet.all_off` or `system_io.set/inactive` steps.

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

## Validation Boundary

Scenario validation is intentionally split into two layers:

- `room_scenario` validates the scenario model itself: required fields, branch
  structure, step payload shape, reactive policy semantics, and other bounded
  rules that do not need live product state.
- `scenehub_scenario_validation` validates the same scenario against the active
  SceneHub environment: saved Quest Devices, command/event capability presence,
  and local hardware/system-device availability.

This keeps the scenario model portable and predictable while still letting GM
start flows, write-side APIs, and read-model projections surface real
device/environment issues to operators.

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
`gm_core` has a dedicated FreeRTOS queue that stores `scenehub_event_t`
values directly. The GM session event handler does not allocate/free heap memory
per event.

Event-bus handlers are adapter-only boundaries:

- a handler may validate lightweight preconditions and copy/post the event into
  its own component queue
- a handler must not do broad runtime scans, build snapshots, parse large JSON
  payloads, call hardware, or execute scenario progression inline
- heavy or blocking follow-up work belongs in the receiving component task or
  in an explicit `event_bus_post_job(...)` job, not in the bus dispatch task

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
- Commands go down through control/services; events go up through event queues;
  read-models project only; UI serializes and calls boundaries.
- Event-bus handlers are transport adapters, not execution sites for heavy
  domain logic.
- UI should show names first and hide ids behind advanced/debug sections.
- Scenario step editors should be schema-driven where practical.
- HTTP APIs should remain documented in `gm_api_contract.md`.
