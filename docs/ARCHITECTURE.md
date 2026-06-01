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
| `scenehub_config` | Compile-time Kconfig defaults shared by reduced builds and firmware components |
| `config_store` | Mutable runtime/NVS configuration such as network, auth and broker settings |
| `mqtt_core` | Local MQTT broker: client sessions, publish/subscribe and MQTT packet limits |
| `event_bus` | In-process events for runtime services |
| `quest_common` | Shared current-model string limits and utility helpers |
| `room_catalog` | File-backed room list at `/sdcard/quest/rooms.json` |
| `quest_device` | File-backed Quest Device store and command/event capability model |
| `device_control_ingest` | Control-contract telemetry ingest for observed physical clients |
| `room_scenario` | Scenario model, static/runtime-semantic validation, JSON import/export |
| `scenehub_scenario_validation` | SceneHub-specific scenario environment validation against Quest Device and local hardware capabilities |
| `command_executor` | Command dispatch boundary for MQTT devices, system audio and local hardware IO |
| `gm_profile_store` | Game Mode profile store, validation, JSON import/export. Public model name: `gm_game_profile_t` |
| `gm_core` | Room session runtime, timer, game start/stop/reset, scenario execution |
| `scenehub_control` | Write-side application facade for GM/scenario/profile/device actions |
| `scenehub_read_model` | Read-side room/runtime/profile/scenario projections for APIs and UI |
| `orchestrator_core` | GM read model, health aggregation, audit and timeline |
| `hardware_io` | Built-in relay/MOSFET/universal IO control, safe-off and status snapshots |
| `audio_player` | Local audio playback service, background/effect mixer and system audio command handling |
| `web_ui` | HTTP API, auth, GM panel assets and UI endpoints |
| `error_monitor` | Fault collection for dashboard/room health |
| `status_led` | Device status indication |

## Network Recovery Policy

Normal provisioned hub boot is STA-only when saved Wi-Fi config exists. Setup AP
is not an automatic transient Wi-Fi failure fallback.

Setup AP may start only through explicit recovery paths:

- the configured reset/setup pin is held during boot;
- Wi-Fi config is empty, so factory-default recovery needs setup mode.

Runtime reset/setup pin long-hold restores defaults and reboots. The reset/setup
GPIO is owned by `system_reset_policy`, not by Web UI auth code.

When setup AP is requested, it starts as pure AP. STA connect is enabled only by
the normal saved-config path or after applying Wi-Fi settings from setup mode.
ESP-IDF Wi-Fi driver storage is RAM-only so stale driver-owned credentials
cannot bypass `config_store` policy.

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
  -> scenehub_control / scenehub_read_model
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
- `gm_core -> registered command-plan dispatch hook` for external command
  dispatch after session-lock release.
- `scenehub_control -> command_executor` for product command execution.
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
- `scenehub_control` becoming a new god-module after absorbing orchestration
  moved out of `gm_core`.
- `gm_room_session.h` exposing too much runtime shape to control/read/UI
  layers through one broad public header.
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
3. `scenehub_control` resolves the complete Room Scenario event-ref catalog
   from Quest Device metadata.
4. The selected Room Scenario and prepared event-ref catalog are copied into
   the session runtime snapshot.
5. The game timer starts.
6. Enabled normal scenario branches start running.
7. Enabled reactive branches start listening for their trigger events.
8. Runtime state is exposed through GM APIs and rendered in Room Control.

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

## GM Core Boundary

The GM decomposition baseline is complete. `gm_core` is now the runtime-domain
owner for room-session state, timers, scenario progression, waits, flags,
operator approval, hints, reactive state and command plans.

`gm_core` must not own product/application orchestration:

- profile, scenario and sidebar persistence live outside `gm_core`;
- game start/stop/reset orchestration enters through `scenehub_control`;
- external command execution enters through a registered command-plan dispatch
  hook and runs after the session lock is released;
- Quest Device metadata is resolved before runtime start, not during wait or
  reactive matching;
- the old `gm_api` / `gm_control` write facades and unsupported compatibility
  wrappers are retired.

Prepared runtime inputs are the supported boundary. `scenehub_control` loads and
validates product data, builds the prepared event-ref catalog, converts storage
profile data into the runtime profile DTO, then calls the prepared GM session
entrypoints. Runtime matching uses only the copied session snapshot. Accidental
compact/event id variants such as `@1` remain invalid data; runtime does not
add tolerant matching for them.

The current public `gm_room_session.h` is still a broad umbrella over session
control entrypoints, view DTOs, command-plan port types, prepared-start DTOs and
some runtime-shaped structs. That is acceptable for the completed cleanup
baseline, but future boundary work should split it before adding more public
surface:

- `gm_room_session.h` for public control/session entrypoints;
- `gm_room_session_views.h` for projection/read DTOs;
- `gm_room_session_command_plan.h` for the command-plan dispatch port;
- `gm_room_session_types.h` for shared enums and narrow common types.

External layers should prefer view/control/port headers over learning the full
runtime shape.

## SceneHub Control Boundary

`scenehub_control` is the write-side application facade. It is allowed to
orchestrate profile/scenario loading, validation, prepared runtime inputs,
command dispatch and persistence calls, but it must not become a second runtime
engine or a new owner for every domain DTO.

The current broad public `scenehub_control.h` umbrella is acceptable while the
API is still stabilizing. Future growth should split public entrypoints by
family before adding more unrelated surface:

- GM/session/timer/hint actions;
- scenarios;
- devices;
- profiles;
- sidebar presets;
- hardware IO.

Internal source files should continue to stay family-oriented. New behavior
should prefer a narrow family helper over adding more unrelated logic to the
main facade file.

## Configuration Boundary

`scenehub_config` owns compile-time firmware/Kconfig defaults only: GPIO pins,
audio/SD defaults, MQTT limits, auth bootstrap defaults and other build-time
constants. It is a header-only dependency used to keep reduced builds and full
firmware builds on the same default contract.

`config_store` owns mutable runtime settings persisted in NVS. Do not move
runtime state, prepared scenario snapshots, catalogs or operator-edited data
into `scenehub_config`.

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
- HTTP API behavior should follow `API_HTTP_POLICY.md`; route-level reference
  docs must be regenerated from current Web UI handlers instead of maintained
  as stale hand-written snapshots.
