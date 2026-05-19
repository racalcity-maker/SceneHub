# GM API Contract

Frozen baseline for the GM panel and future UI editor.

## Conventions

- Base path: `/api/gm`.
- All endpoints require an authenticated session.
- `USER` role can read state and run operator actions.
- `ADMIN` role is required for profile/scenario edit, import/export and filesystem save/load.
- JSON responses include `ok: true` where the current handler returns a structured success object.
- Some runtime endpoints return plain error text through `httpd_resp_send_err`; clients must use HTTP status as the primary failure signal.

## Error Semantics

Common HTTP statuses:

- `400 Bad Request`: missing required field, malformed JSON, invalid JSON collection, invalid number, unsupported version.
- `404 Not Found`: room, scenario, profile or backing file not found.
- `409 Conflict`: request is valid but not allowed in current runtime/config state.
- `422 Unprocessable Entity`: generic room action exists but is not supported by dispatcher.
- `500 Internal Server Error`: memory allocation failure, storage failure or unexpected execution failure.

Structured room-action errors use:

```json
{
  "ok": false,
  "error": "action_disabled",
  "room_id": "room_1",
  "action_id": "start_game"
}
```

Known structured `error` values:

- `invalid_request`
- `room_not_found`
- `action_not_found`
- `action_disabled`
- `not_supported`
- `execution_failed`

## Orchestrator Read Model

The GM panel uses orchestrator endpoints for aggregated device/room/issue state and for observed MQTT control clients.

### Health and Connectivity Semantics

Device objects use two related fields:

- `connectivity`: `online`, `offline`, or `unknown`
- `health`: `ok`, `degraded`, or `fault`

Rules:

- `online` means fresh control-contract telemetry was received within the online timeout.
- `offline` means a registered device was seen before but no fresh heartbeat/status/result is available.
- `unknown` means the Quest Orchestrator has no usable telemetry for that logical device yet.
- Registered quest device `offline` is a critical fault. It creates `device_offline`, marks the device `fault`, and makes the containing room/system `fault`.
- `not observed` / `unknown` during setup is warning/degraded, not a fault.
- UI clients must not show a green `ok` badge for a device whose `connectivity` is `offline` or `unknown`.

### Observed Device Registration

`Observed` lists physical MQTT control-contract clients. A client is considered registered if:

- any saved Quest Device has `client_id` equal to the observed client id.

The Quest Device name is the operator-facing label. The physical client id remains the telemetry/control endpoint.

### `GET /api/orchestrator/control/devices`

Returns physical clients observed through `cp/v1/dev/{device_id}/{heartbeat,status,diag,result}`.

Example response:

```json
{
  "items": [
    {
      "device_id": "relay_room_2",
      "connectivity": "online",
      "health": "ok",
      "fw_version": "1.0.3",
      "boot_id": "4dd5030d9ab1",
      "mode": "normal",
      "state": "ready",
      "last_seen_ms": 123456
    }
  ]
}
```

GM UI registration labels are derived client-side from device config bindings and the orchestrator snapshot.

### `POST /api/gm/device/describe-interface`

Admin-only. Requests device description metadata from an observed physical client.

Request:

```json
{
  "client_id": "relay_room_2"
}
```

Quest Orchestrator publishes:

```text
cp/v1/dev/{client_id}/control/command
```

with command `describe_interface`, then waits for a matching result with the same `request_id`.

Success response:

```json
{
  "ok": true,
  "client_id": "relay_room_2",
  "request_id": "iface_123456",
  "device_description": {
    "version": 1,
    "commands": [],
    "events": []
  }
}
```

Known errors:

- `client_id_required`
- `publish_failed`
- `timeout`
- `device_error`
- `missing_device_description`

The admin UI imports returned commands/events only after confirmation.

## Quest Devices

Quest devices are the new capability-device model. They describe what a physical client or built-in system service can do. They do not own quest flow logic.

### Device Object

```json
{
  "id": "altar_1",
  "client_id": "dcc-altar-1",
  "name": "Altar",
  "enabled": true,
  "system_device": false,
  "commands": [
    {
      "id": "reset",
      "label": "Reset altar",
      "capability": "relay",
      "command": "relay.pulse",
      "default_args": {
        "channel": 1,
        "duration_ms": 1000
      },
      "policy": {
        "manual_allowed": true,
        "scenario_allowed": true,
        "requires_confirmation": false,
        "result_required": true,
        "timeout_ms": 3000,
        "danger_level": "normal"
      },
      "args_schema": []
    }
  ],
  "events": [
    {
      "id": "completed",
      "label": "Altar completed",
      "capability": "input",
      "event": "input.pressed",
      "match": {
        "channel": 1
      }
    }
  ]
}
```

System devices are returned by the same API when requested. The first system device is `system_audio`.
It exposes `play`, `stop`, `pause`, `resume`, `set_volume`, `playback_finished`, and `playback_failed` through the same command/event shape as physical quest devices. `play` supports background/effect channel semantics; background is WAV-only, effects may be WAV or MP3.

Current device capacity:

- MQTT broker: `20` simultaneous clients.
- saved Quest Devices: `20`, excluding built-in system devices when APIs request `include_system=false`.
- observed physical clients: `20`, tied to `QUEST_DEVICE_MAX_DEVICES`.
- room/session snapshots: `2`, tied to `ROOM_CATALOG_MAX_ROOMS`.

### `GET /api/gm/devices`

User-readable. Lists quest devices.

Query:

- `include_system=true|false`, default `true`

Response:

```json
{
  "ok": true,
  "generation": 1,
  "include_system": true,
  "devices": []
}
```

### `POST /api/gm/device/save`

Admin-only. Creates or replaces one quest device. The request body may be either the device object or `{ "device": {...} }`.

System devices cannot be saved or replaced through this endpoint.

Successful saves are persisted immediately to `/sdcard/quest/quest_devices.json`. The store serializes mutating writes and file persistence so concurrent save/delete/import requests do not share the same temporary file.

### `POST /api/gm/device/delete`

Admin-only.

Request:

```json
{
  "device_id": "altar_1"
}
```

Successful deletes are persisted immediately to `/sdcard/quest/quest_devices.json`. The same store-level write serialization applies.

### `POST /api/gm/device/command/run`

User-readable. Executes a saved quest-device command capability as a manual operator action.

Request:

```json
{
  "device_id": "relay_room_2",
  "command_id": "open_door",
  "params": {}
}
```

Rules:

- Device must exist and be enabled.
- Command must exist.
- Command must have `policy.manual_allowed: true`.
- System-device commands are allowed when exposed as manual buttons.
- Optional `params` is passed to parameterized command handlers.

Response:

```json
{
  "ok": true,
  "device_id": "relay_room_2",
  "device_name": "Room 2 relay",
  "command_id": "open_door",
  "command_label": "Open room 2 door"
}
```

This requests capabilities from a physical client, but does not automatically save them. UI/import code must still confirm and save a quest device.

### `GET /api/gm/devices/export`

Admin-only. Exports saved quest devices as:

```json
{
  "version": 1,
  "quest_devices": []
}
```

System devices are built-in and are not included in export.

### `POST /api/gm/devices/import`

Admin-only. Replace-all import for saved quest devices. Invalid JSON does not clear the existing store.

Successful imports are persisted immediately to `/sdcard/quest/quest_devices.json`. The same store-level write serialization applies.

### `POST /api/gm/devices/save`

Admin-only. Saves quest devices to `/sdcard/quest/quest_devices.json`.

### `POST /api/gm/devices/load`

Admin-only. Loads quest devices from `/sdcard/quest/quest_devices.json`.

The same file is loaded automatically during firmware startup after the quest-device store is initialized.

## Room State

### `GET /api/gm/rooms`

Returns the file-backed room catalog from `/sdcard/quest/rooms.json`.

Response:

```json
[
  {
    "room_id": "room_1",
    "name": "Room 1",
    "device_count": 0
  }
]
```

### `POST /api/gm/room/save`

Admin-only. Creates or updates a room in `/sdcard/quest/rooms.json`.

Request:

```json
{
  "room_id": "room_1",
  "name": "Room 1"
}
```

Response:

```json
{
  "status": "ok",
  "room_id": "room_1",
  "name": "Room 1"
}
```

### `POST /api/gm/room/delete`

Admin-only. Deletes a room from the room catalog.

Request:

```json
{
  "room_id": "room_1",
  "delete_content": true
}
```

Response:

```json
{
  "status": "ok",
  "room_id": "room_1",
  "removed_rooms": 1,
  "removed_profiles": 1,
  "removed_scenarios": 1
}
```

Notes:

- Quest Devices are not deleted by this endpoint.
- `delete_content=true` also removes room scenarios and game profiles for that room.

## Game Profiles

### Profile Object

```json
{
  "id": "easy",
  "name": "Easy",
  "room_id": "room_1",
  "scenario_id": "easy_flow",
  "duration_ms": 3600000,
  "hint_pack_id": "easy",
  "audio_pack_id": "classic",
  "enabled": true
}
```

Required fields:

- `id`
- `name`
- `room_id`
- `scenario_id`
- `duration_ms > 0`

Validation for `POST /api/gm/room/profile/save` also requires:

- room exists
- scenario exists
- `scenario.room_id == profile.room_id`
- selected scenario validation has no errors

Collection import is structural-only and does not require rooms/scenarios to exist yet.

### `GET /api/gm/room/profiles?room_id=room_1`

Returns profiles for one room.

Response:

```json
{
  "ok": true,
  "room_id": "room_1",
  "generation": 5,
  "selected_profile_id": "easy",
  "profiles": [
    {
      "id": "easy",
      "name": "Easy",
      "room_id": "room_1",
      "scenario_id": "easy_flow",
      "duration_ms": 3600000,
      "hint_pack_id": "easy",
      "audio_pack_id": "classic",
      "enabled": true,
      "valid": true
    }
  ]
}
```

### `POST /api/gm/room/profile/select`

Request:

```json
{
  "room_id": "room_1",
  "profile_id": "easy"
}
```

Response:

```json
{
  "ok": true,
  "room_id": "room_1",
  "selected_profile_id": "easy"
}
```

Effects:

- validates profile
- stores selected profile fields in room session
- selects `profile.scenario_id`
- resets idle timer duration to `profile.duration_ms`

### `POST /api/gm/room/profile/save`

Request may be either the profile object directly or wrapped in `profile`.

```json
{
  "profile": {
    "id": "easy",
    "name": "Easy",
    "room_id": "room_1",
    "scenario_id": "easy_flow",
    "duration_ms": 3600000,
    "hint_pack_id": "easy",
    "audio_pack_id": "classic",
    "enabled": true
  }
}
```

Response:

```json
{
  "ok": true,
  "generation": 6,
  "profile": {
    "id": "easy",
    "name": "Easy",
    "room_id": "room_1",
    "scenario_id": "easy_flow",
    "duration_ms": 3600000,
    "hint_pack_id": "easy",
    "audio_pack_id": "classic",
    "enabled": true
  }
}
```

Successful saves are persisted immediately to `/sdcard/quest/game_profiles.json`. The store serializes mutating writes and file persistence so concurrent save/delete/import requests do not share the same temporary file.

### `POST /api/gm/room/profile/delete`

Request:

```json
{
  "profile_id": "easy"
}
```

Successful deletes are persisted immediately to `/sdcard/quest/game_profiles.json`. The same store-level write serialization applies.

Response:

```json
{
  "ok": true,
  "deleted_profile_id": "easy",
  "generation": 7
}
```

### `GET /api/gm/profiles/export`

Exports the whole game profile store.

Response:

```json
{
  "version": 1,
  "game_profiles": [
    {
      "id": "easy",
      "name": "Easy",
      "room_id": "room_1",
      "scenario_id": "easy_flow",
      "duration_ms": 3600000,
      "hint_pack_id": "easy",
      "audio_pack_id": "classic",
      "enabled": true
    }
  ]
}
```

### `POST /api/gm/profiles/import`

Request body is the same collection JSON as export.

Import policy:

- replace-all
- validates full JSON structurally before replacing
- invalid import leaves existing store untouched
- successful import is persisted immediately to `/sdcard/quest/game_profiles.json`
- store-level write serialization applies to save/delete/import/load operations

Response:

```json
{
  "ok": true,
  "operation": "import",
  "profile_count": 1,
  "generation": 8
}
```

### `POST /api/gm/profiles/save`

Saves current profile store to `/sdcard/quest/game_profiles.json`.

Response:

```json
{
  "ok": true,
  "operation": "save",
  "path": "/sdcard/quest/game_profiles.json",
  "generation": 8
}
```

### `POST /api/gm/profiles/load`

Loads profile store from `/sdcard/quest/game_profiles.json`.

Response:

```json
{
  "ok": true,
  "operation": "load",
  "path": "/sdcard/quest/game_profiles.json",
  "generation": 9
}
```

## Room Scenarios

### Scenario Object

```json
{
  "id": "easy_flow",
  "name": "Easy Flow",
  "room_id": "room_1",
  "steps": [
    {
      "id": "intro",
      "label": "Intro",
      "enabled": true,
      "type": "DEVICE_COMMAND",
      "device_id": "system_audio",
      "command_id": "play",
      "params": {
        "file": "/audio/intro.mp3"
      }
    }
  ]
}
```

Supported step types:

- `DEVICE_COMMAND`
- `DEVICE_COMMAND_GROUP`
- `WAIT_TIME`
- `WAIT_DEVICE_EVENT`
- `WAIT_ANY_DEVICE_EVENT`
- `WAIT_ALL_DEVICE_EVENTS`
- `OPERATOR_APPROVAL`
- `SHOW_OPERATOR_MESSAGE`
- `SET_FLAG`
- `WAIT_FLAGS`
- `END_GAME`

Flat scenarios may use a top-level `steps[]` array. New branched scenarios should use `branches[]`; internally the backend stores one scenario-level step array and branch ranges, so branches do not duplicate step memory:

```json
{
  "id": "harry_potter",
  "name": "Harry Potter",
  "room_id": "room_1",
  "branches": [
    {
      "id": "room_1",
      "name": "Room 1",
      "type": "normal",
      "enabled": true,
      "required_for_completion": true,
      "steps": []
    },
    {
      "id": "optional_basement",
      "name": "Optional basement",
      "type": "normal",
      "enabled": true,
      "required_for_completion": false,
      "steps": []
    },
    {
      "id": "wrong_card_reaction",
      "name": "Wrong card reaction",
      "type": "reactive",
      "enabled": true,
      "cooldown_ms": 3000,
      "run_once": false,
      "steps": [
        {
          "id": "wait_wrong_card",
          "type": "WAIT_DEVICE_EVENT",
          "device_id": "uid_gate_1",
          "event_id": "wrong_card"
        },
        {
          "id": "play_wrong_card_sound",
          "type": "DEVICE_COMMAND",
          "device_id": "system_audio",
          "command_id": "play",
          "params": {
            "channel": "effect",
            "file": "/sdcard/sfx/wrong_card.mp3",
            "volume": 70
          }
        }
      ]
    }
  ]
}
```

Branch fields:

- `type`: `normal` or `reactive`. Missing `type` is treated as `normal` for
  compatibility with existing scenario JSON.
- `required_for_completion`: valid only for `normal` branches.
- `cooldown_ms`: valid for `reactive` branches. After a reaction fires, the
  branch ignores matching events until cooldown expires.
- `run_once`: valid for `reactive` branches. If true, the reaction disables
  itself after the first successful run.
- `policy.mode`: `single`, `rotate`, `random`, or `escalate`.
- `policy.max_fire_count`: `0` means unlimited. For `Same actions`, `Can repeat`
  saves `0`; `Run once` saves `1`.
- `trigger`, `guard_flags`, `variants`, `result_policy` describe Reactive
  Branch v2 reactions.

Reactive branch contract:

- one reactive branch represents one reaction;
- different triggers should be modeled as different reactive branches;
- a reactive branch starts in `listening`, executes the selected variant actions
  when the trigger matches, then returns to `listening` or `cooldown` unless
  `run_once` or `max_fire_count` completes it;
- reactive branches share the same game-run flag store as normal branches;
- reactive branches may execute `SET_FLAG`, allowing normal branches to wait on
  those flags through `WAIT_FLAGS`;
- reactive branches are shown separately from normal flow branches in the GM
  Panel;
- reactive branches do not block `END_GAME`, do not finish the game and do not
  count toward quest completion.
- result-required reactive commands advance only on terminal `done`; `accepted`
  keeps the action pending.

Step payloads:

```json
{
  "type": "DEVICE_COMMAND",
  "device_id": "relay_room_2",
  "command_id": "open_door",
  "params": {}
}
```

For `system_audio`:

```json
{
  "type": "DEVICE_COMMAND",
  "device_id": "system_audio",
  "command_id": "play",
  "params": {
    "file": "/audio/intro.mp3",
    "channel": "effect",
    "volume": 80,
    "repeat": false
  }
}
```

Supported `system_audio` commands:

- `play`: starts the selected audio file. `params.channel=background` plays WAV-only background audio and replaces the previous background track. `params.repeat=true` repeats only background WAV tracks. `params.channel=effect` plays a WAV/MP3 effect and replaces the previous effect.
- `stop`: stops playback. With no `params.channel`, or with `channel=all`, it stops both background and effect. `channel=background` or `channel=effect` may be used for targeted stop. Stop/reset drains a short silence buffer before I2S reset so the DAC does not hold the last sample.
- `pause`: pauses current playback.
- `resume`: resumes paused playback.
- `set_volume`: changes global audio volume.

```json
{
  "type": "WAIT_TIME",
  "duration_ms": 3000
}
```

```json
{
  "type": "WAIT_DEVICE_EVENT",
  "device_id": "altar_1",
  "event_id": "completed",
  "timeout_ms": 30000,
  "timeout_message": "Check altar manually",
  "allow_operator_skip": true,
  "operator_skip_label": "Skip altar"
}
```

`WAIT_DEVICE_EVENT` resolves the saved quest-device event capability. For SceneHub-native devices it waits on the saved `event` name and the physical `client_id`. For `system_audio`, `playback_finished` maps to the internal audio-finished event.

Wait steps may expose an operator-only skip button:

- `allow_operator_skip`: when `true`, Room Control shows a runtime skip button while this step is waiting.
- `operator_skip_label`: optional button text, for example `Skip altar`.

This is an operational override, not branching scenario logic. The button completes the current wait through the same runtime path as manual `next`.

Unknown or removed step types are rejected by JSON import, draft validation and save.

Current room scenario limits:

- maximum room scenarios in the store: `24`
- maximum steps per scenario across all branches: `48`
- maximum branches per scenario: `8`, including normal and future reactive branches

```json
{
  "type": "OPERATOR_APPROVAL",
  "prompt": "Continue?",
  "approve_label": "Continue"
}
```

```json
{
  "type": "END_GAME"
}
```

`END_GAME` finishes the game timer and marks the game complete. It does not stop audio automatically; use a separate `DEVICE_COMMAND` to `system_audio.stop` when silence is required. It also does not force local relay/MOSFET/IO safe-off; use explicit `system_relay`, `system_mosfet` or `system_io` commands for finale cleanup.

Runtime responses include branch progress when a scenario is running. Branch
runtime is controller-owned; browser and desktop clients should render branch
state directly instead of inferring `done/current/waiting` from indexes.
Static branch `steps[]` remain part of the scenario catalog rather than being
duplicated into each live runtime payload:

```json
{
  "scenario_branch_count": 2,
  "scenario_branches": [
    {
      "index": 0,
      "id": "branch_a",
      "name": "Branch A",
      "type": "normal",
      "active": true,
      "required_for_completion": false,
      "step_start_index": 0,
      "step_count": 3,
      "total_steps": 3,
      "current_step_index": 2,
      "current_step_local_index": 2,
      "done_steps": 2,
      "completed_step_count": 2,
      "failed_step_index": -1,
      "current_step_state": "waiting",
      "state": "waiting",
      "wait_type": "any_events"
    }
  ]
}
```

Step state values:

- `pending`
- `current`
- `waiting`
- `done`
- `error`
- `skipped` reserved for future use

Reactive branches use the same `scenario_branches[]` array with `type:
"reactive"`. They still expose `done_steps`, `total_steps` and live wait/state
metadata, but an idle trigger-listening branch may legitimately have no
`current` step.

### `GET /api/gm/room/scenarios?room_id=room_1`

Returns scenarios for one room. Each item includes validation summary and step data.

Expected top-level shape:

```json
{
  "room_id": "room_1",
  "scenarios": [
    {
      "id": "easy_flow",
      "name": "Easy Flow",
      "room_id": "room_1",
      "step_count": 1,
      "valid": true,
      "validation_issue_count": 0,
      "validation_issues": [],
      "steps": []
    }
  ]
}
```

### `POST /api/gm/room/scenario/select`

Request:

```json
{
  "room_id": "room_1",
  "scenario_id": "easy_flow"
}
```

Response:

```json
{
  "ok": true,
  "room_id": "room_1",
  "selected_scenario_id": "easy_flow"
}
```

Direct scenario selection clears selected profile fields in runtime state.

### `GET /api/gm/room/scenario-editor/catalog?room_id=room_1`

Admin-only. Returns compact data used by the scenario editor dropdowns.

Response:

```json
{
  "room_id": "room_1",
  "quest_devices": [
    {
      "id": "altar_1",
      "client_id": "dcc-altar-1",
      "name": "Altar",
      "enabled": true,
      "system_device": false,
      "commands": [],
      "events": []
    },
    {
      "id": "system_audio",
      "name": "System audio",
      "enabled": true,
      "system_device": true,
      "commands": [],
      "events": []
    }
  ],
  "step_schemas": [
    {
      "type": "DEVICE_COMMAND",
      "label": "Device command",
      "fields": [
        { "key": "device_id", "type": "device_select", "label": "Device", "required": true },
        { "key": "command_id", "type": "device_command_select", "label": "Command", "depends_on": "device_id", "required": true },
        { "key": "params", "type": "params_object", "label": "Parameters", "depends_on": "command_id", "required": false }
      ]
    },
    {
      "type": "WAIT_DEVICE_EVENT",
      "label": "Wait device event",
      "fields": [
        { "key": "device_id", "type": "device_select", "label": "Device", "required": true },
        { "key": "event_id", "type": "device_event_select", "label": "Event", "depends_on": "device_id", "required": true }
      ]
    }
  ]
}
```

The catalog intentionally exposes quest-device capabilities only. Device-local scenario and raw event preset fields are not part of this contract.

### `POST /api/gm/room/scenario/validate`

Admin-only. Validates a draft scenario without saving it.

Request:

```json
{
  "scenario": {
    "id": "easy_flow",
    "name": "Easy Flow",
    "room_id": "room_1",
    "steps": []
  }
}
```

Response:

```json
{
  "ok": true,
  "scenario_id": "easy_flow",
  "valid": true,
  "issue_count": 0,
  "error_count": 0,
  "warning_count": 0,
  "issues": []
}
```

### `POST /api/gm/room/scenario/save`

Admin-only. Creates or replaces one scenario by `id`.

Request:

```json
{
  "scenario": {
    "id": "easy_flow",
    "name": "Easy Flow",
    "room_id": "room_1",
    "steps": []
  }
}
```

The handler also accepts the scenario object as the request root.

Response:

```json
{
  "ok": true,
  "generation": 13,
  "scenario": {
    "id": "easy_flow",
    "name": "Easy Flow",
    "room_id": "room_1",
    "steps": []
  }
}
```

Successful saves are persisted immediately to `/sdcard/quest/room_scenarios.json`. The store serializes mutating writes and file persistence so concurrent save/delete/import requests do not share the same temporary file.

### `POST /api/gm/room/scenario/delete`

Admin-only.

Request:

```json
{
  "scenario_id": "easy_flow"
}
```

Successful deletes are persisted immediately to `/sdcard/quest/room_scenarios.json`. The same store-level write serialization applies.

Response:

```json
{
  "ok": true,
  "deleted_scenario_id": "easy_flow",
  "generation": 14
}
```

### Runtime Scenario Commands

Endpoints:

- `POST /api/gm/room/scenario/start?room_id=room_1`
- `POST /api/gm/room/scenario/stop?room_id=room_1`
- `POST /api/gm/room/scenario/next?room_id=room_1`
- `POST /api/gm/room/scenario/approve?room_id=room_1`
- `POST /api/gm/room/scenario/reset?room_id=room_1`

Response:

```json
{
  "ok": true,
  "room_id": "room_1",
  "selected_profile_id": "easy",
  "selected_profile_name": "Easy",
  "selected_profile_scenario_id": "easy_flow",
  "selected_scenario_id": "easy_flow",
  "selected_scenario_name": "Easy Flow",
  "running_scenario_id": "easy_flow",
  "running_scenario_name": "Easy Flow",
  "running_scenario_generation": 12,
  "scenario_runtime_state": "waiting",
  "scenario_wait_type": "event",
  "scenario_wait_until_ms": 0,
  "scenario_wait_started_at_ms": 123456,
  "scenario_wait_operator_prompt": "",
  "scenario_wait_operator_label": "",
  "scenario_last_error": ""
}
```

### `GET /api/gm/room/runtime?room_id=room_1`

Returns room-scoped runtime state used by browser and desktop clients for live
scenario rendering. Branch runtime metadata is live here; branch `steps[]`
remain part of the static scenario catalog and are not duplicated into this
payload.

Query options:

- default: full runtime detail for the active room-control view
- `detail=summary`: dedicated lightweight summary path for dashboard/rooms
  refresh; omits heavy arrays such as runtime flags, wait-event groups,
  issue-id lists, branch runtime metadata, and asset readiness detail

Relevant runtime fields:

- `scenario_runtime_state`
- `scenario_wait_type`
- `scenario_wait_until_ms`
- `scenario_wait_started_at_ms`
- `scenario_wait_events[]`
- `scenario_wait_flags[]`
- `scenario_branches[]`

Example:

```json
{
  "ok": true,
  "runtime_schema_version": 1,
  "room_id": "room_1",
  "running_scenario_id": "easy_flow",
  "running_scenario_name": "Easy Flow",
  "scenario_runtime_state": "waiting",
  "scenario_wait_type": "event",
  "scenario_wait_until_ms": 0,
  "scenario_wait_started_at_ms": 123456,
  "scenario_branches": [
    {
      "index": 0,
      "id": "main",
      "name": "Main",
      "type": "normal",
      "active": true,
      "required_for_completion": true,
      "step_start_index": 0,
      "step_count": 5,
      "total_steps": 5,
      "current_step_index": 2,
      "current_step_local_index": 2,
      "done_steps": 2,
      "completed_step_count": 2,
      "failed_step_index": -1,
      "current_step_state": "waiting",
      "state": "waiting",
      "wait_type": "event",
      "wait_until_ms": 0
    }
  ]
}
```

### `GET /api/gm/rooms/runtime`

Returns a bulk room-runtime summary payload for multi-room refreshes so the UI
does not need `N` parallel `/api/gm/room/runtime?detail=summary` requests.

Example:

```json
{
  "ok": true,
  "runtime_schema_version": 1,
  "rooms": [
    {
      "room_id": "room_1",
      "session_present": true,
      "session_state": "running",
      "timer_state": "running",
      "timer_duration_ms": 3600000,
      "timer_remaining_ms": 2400000,
      "running_scenario_id": "easy_flow",
      "running_scenario_generation": 12,
      "scenario_runtime_state": "waiting",
      "scenario_wait_type": "event",
      "scenario_device_count": 3
    }
  ]
}
```

Runtime wait type values:

- `none`
- `time`
- `event` for `WAIT_DEVICE_EVENT`
- `any_events` for `WAIT_ANY_DEVICE_EVENT`
- `all_events` for `WAIT_ALL_DEVICE_EVENTS`
- `flags`
- `operator`

### `GET /api/gm/room/scenarios/export`

Exports all room scenarios.

Response:

```json
{
  "version": 1,
  "room_scenarios": []
}
```

### `POST /api/gm/room/scenarios/import`

Imports all room scenarios.

Import policy:

- replace-all
- validates full JSON structurally before replacing
- invalid import leaves existing store untouched
- successful import is persisted immediately to `/sdcard/quest/room_scenarios.json`
- store-level write serialization applies to save/delete/import/load operations

Response:

```json
{
  "ok": true,
  "operation": "import",
  "scenario_count": 1,
  "generation": 10
}
```

### `POST /api/gm/room/scenarios/save`

Saves scenario store to `/sdcard/quest/room_scenarios.json`.

### `POST /api/gm/room/scenarios/load`

Loads scenario store from `/sdcard/quest/room_scenarios.json`.

Common response:

```json
{
  "ok": true,
  "operation": "save",
  "path": "/sdcard/quest/room_scenarios.json",
  "generation": 10
}
```

## Game Runtime

### Profile-Backed Game Commands

Endpoints:

- `POST /api/gm/room/game/start?room_id=room_1`
- `POST /api/gm/room/game/stop?room_id=room_1`
- `POST /api/gm/room/game/reset?room_id=room_1`

These dispatch through `gm_control` and are audited.

Success response:

```json
{
  "ok": true,
  "room_id": "room_1",
  "action_id": "start_game"
}
```

`start_game` requires a selected profile. It reloads/validates the profile and scenario, applies profile duration, starts the timer and starts room scenario runtime.

`stop_game` stops scenario runtime, finishes the timer/session, stops audio and forces built-in relay/MOSFET/IO outputs to safe/off after the GM session lock is released.

`reset_game` resets scenario runtime and timer duration, stops audio and forces built-in relay/MOSFET/IO outputs to safe/off after the GM session lock is released. If a profile is selected, the profile duration is re-applied.

## Timer Runtime

Endpoints:

- `POST /api/gm/room/timer/start?room_id=room_1&duration_ms=3600000`
- `POST /api/gm/room/timer/pause?room_id=room_1`
- `POST /api/gm/room/timer/resume?room_id=room_1`
- `POST /api/gm/room/timer/reset?room_id=room_1`
- `POST /api/gm/room/timer/reset?room_id=room_1&duration_ms=3600000`
- `POST /api/gm/room/timer/add?room_id=room_1&delta_ms=60000`
- `POST /api/gm/room/session/finish?room_id=room_1`

Response:

```json
{
  "status": "started",
  "room_id": "room_1",
  "session_state": "running",
  "timer_state": "running",
  "timer_duration_ms": 3600000,
  "timer_remaining_ms": 3600000,
  "session_started_at_ms": 123456
}
```

## Hint Runtime

### `POST /api/gm/room/hint/send`

Request:

```json
{
  "room_id": "room_1",
  "message": "Try the left lock"
}
```

### `POST /api/gm/room/hint/clear?room_id=room_1`

Responses follow the hint handler contract and include the updated room/session hint state.
