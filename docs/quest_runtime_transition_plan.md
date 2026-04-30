# Quest Runtime Cleanup Plan

This file tracks the move to the current clean model:

```text
Quest Devices -> Room Scenarios -> Game Modes -> GM Session
```

The goal is one clear place for quest logic: Room Scenarios. Devices describe
commands/events; scenarios decide when to use them.

Product naming rule: call the whole product `Quest Orchestrator` and the
operator/admin web surface `GM Panel`. `broker` is reserved for the local MQTT
broker module only.

## Current Product Rules

- [x] Normal admin workflow uses Quest Devices, not device-local puzzle logic.
- [x] Room Scenarios are the only quest-flow engine.
- [x] Game Modes select a scenario and duration.
- [x] Device ids are hidden from normal operators/admins where names are enough.
- [x] Device Setup edits capability metadata: commands, events, physical client.
- [x] Operators control games from Room Control.
- [x] Admins can edit Device Setup, Scenarios, Game Modes and Storage.

## Quest Devices

- [x] File-backed Quest Device store.
- [x] Save/load to `/sdcard/quest/quest_devices.json`.
- [x] HTTP CRUD/export/import/save/load endpoints.
- [x] Manual buttons generated from saved Quest Device commands.
- [x] Physical clients are registered when referenced by saved `client_id`.
- [x] Offline registered devices are critical faults.
- [x] Device interface discovery through `describe_interface`.
- [x] Discovery import requires admin confirmation.
- [x] Duplicate checks for device id, client id, command id and event id.
- [x] Orchestrator device snapshot reads Quest Devices directly.
- [ ] MQTT command parameters are applied to payloads, or UI limits parameters
      to internal commands only.

## Rooms

- [x] File-backed room catalog.
- [x] Save/load to `/sdcard/quest/rooms.json`.
- [x] Room create/delete from GM panel.
- [x] Room health derives Quest Device faults from selected scenario/runtime.

## Scenario Model

- [x] `DEVICE_COMMAND`
- [x] `DEVICE_COMMAND_GROUP`
- [x] `WAIT_DEVICE_EVENT`
- [x] `WAIT_ANY_DEVICE_EVENT`
- [x] `WAIT_ALL_DEVICE_EVENTS`
- [x] `WAIT_TIME`
- [x] `OPERATOR_APPROVAL`
- [x] `SHOW_OPERATOR_MESSAGE`
- [x] `SET_FLAG`
- [x] `WAIT_FLAGS`
- [x] `END_GAME`
- [x] Room scenario limits set to 24 scenarios and 48 steps per scenario.
- [x] Scenario validation before save/start.
- [x] Running scenario snapshot on game start.
- [x] Save/load to `/sdcard/quest/room_scenarios.json`.
- [x] HTTP export/import/save/load.
- [x] Wait-step timeout for `WAIT_DEVICE_EVENT` and `WAIT_FLAGS`.
- [x] Operator skip metadata for wait steps.
- [x] Duplicate step id validation.
- [x] Integer-only `duration_ms` validation.
- [x] Command execution checks `device.enabled`.

## Scenario Branches

- [x] Up to 8 branches per scenario.
- [x] Branches are linear internally.
- [x] Several branches may run in one game.
- [x] Branches share one game-run flag set.
- [x] Backend stores one flat step array plus branch ranges.
- [x] JSON editor uses visible `branches[].steps[]`.
- [x] Flat `steps[]` imports as one branch.
- [x] Scenario editor shows branches as tabs.
- [x] Scenario editor can add/delete/rename branches.
- [x] Runtime API exposes per-branch progress data.
- [x] Runtime progress UI shows all branches together.
- [x] Three branches fit comfortably in a full-width runtime row.

## Reactive Branches

Reactive branches are a separate branch type for event reactions. They belong to
the same Room Scenario, share the same game-run flags, but are not part of the
quest completion path.

- [x] Add branch type: `normal` and `reactive`.
- [x] Persist branch type in JSON import/export.
- [x] Expose branch type in runtime/API JSON.
- [x] Keep normal branches as the main quest flow.
- [x] Keep reactive branches in a separate UI group named `Reactions`.
- [x] Reactive branch uses one trigger/listen step plus a short action chain.
- [x] One reactive branch represents one reaction; multiple triggers should use
      multiple reactive branches.
- [x] Reactive branches do not use `required_for_completion`.
- [x] Reactive branches do not complete the game and do not block `END_GAME`.
- [x] Reactive branches use the same game-run flag store as normal branches.
- [x] Reactive branches may execute `SET_FLAG` to unlock/activate normal
      branches through `WAIT_FLAGS`.
- [x] Reactive branch runtime supports waiting/listening, running and cooldown.
- [x] Add branch-level `cooldown_ms`.
- [x] Add optional branch-level `run_once`.
- [x] After the action chain finishes, a reactive branch returns to listening
      unless `run_once` is enabled.
- [x] Runtime progress separates `Scenario flow` from `Reactions`.
- [x] Validation rejects reactive branches without a trigger/listen step.
- [x] Validation rejects unguarded reactive `WAIT_FLAGS` loops unless
      `run_once` or `cooldown_ms` is set.
- [x] Validation warns when reactive branches carry
      `required_for_completion=true`; import/runtime normalize it to false.
- [ ] Add a disabled/listening label polish pass in runtime UI.
- [x] Reactive branch editor uses a `When -> Then` model instead of the full
      normal scenario step list.
- [x] `+ Reaction` creates an empty reaction. The admin must add a trigger
      explicitly.
- [x] Empty reactions show an `Add trigger first` state.
- [x] The first reactive step is limited to trigger/listen steps:
      `WAIT_DEVICE_EVENT`, `WAIT_ANY_DEVICE_EVENT`, `WAIT_ALL_DEVICE_EVENTS`,
      `WAIT_FLAGS`.
- [x] Reactive action steps are limited to:
      `DEVICE_COMMAND`, `DEVICE_COMMAND_GROUP`, `WAIT_TIME`,
      `SHOW_OPERATOR_MESSAGE`, `SET_FLAG`.
- [x] `SET_FLAG` stays available in reactions so a reaction can activate or
      unlock a normal branch through shared scenario flags.
- [x] Hide normal-flow-only steps from reactions: `OPERATOR_APPROVAL` and
      `END_GAME`.
- [x] Keep branch delete as a compact destructive action in branch settings,
      not a full-width control.

## Game Modes

- [x] File-backed Game Mode store.
- [x] Save/load to `/sdcard/quest/game_profiles.json`.
- [x] HTTP CRUD/export/import/save/load/select endpoints.
- [x] Selecting a mode selects scenario and duration.
- [x] Game start uses selected mode.
- [ ] Profile UI makes scenario selection more obvious and less redundant.

## Audio

- [x] Built-in System Audio Quest Device.
- [x] Scenario can play audio through `DEVICE_COMMAND`.
- [x] Manual stop/pause/resume controls.
- [x] Game stop sends best-effort audio stop.
- [x] Default play volume is 70.
- [x] Add audio-player channel model: background WAV and effect WAV/MP3.
- [x] Add optional repeat for background WAV tracks.
- [x] Define overlap/mixing policy for two simultaneous tracks.
- [x] Stop/reset drains silence before I2S reset to avoid DAC last-sample hold.

## UI

- [x] GM panel is the primary operator surface.
- [x] Main admin page links to GM panel.
- [x] Device Setup lives in GM panel and is admin-only.
- [x] Scenario editor uses device/command/event dropdowns.
- [x] Dirty-state protection for editors.
- [x] Validate draft before saving scenarios.
- [x] Scenario step list is compact and uses icons.
- [x] Runtime Room Control is the operator-first screen.
- [x] Scenario runtime branch progress UI.
- [ ] More compact Game Mode editor.
- [ ] Better empty states for first-time setup.

## Capacity

- [x] MQTT broker limit set to 20 simultaneous clients.
- [x] Quest Device store limit set to 20 devices.
- [x] Current-model observed clients use `QUEST_DEVICE_MAX_DEVICES`, not legacy `DEVICE_MANAGER_MAX_DEVICES`.
- [x] GM Session room capacity uses `ROOM_CATALOG_MAX_ROOMS`, not legacy device capacity.
- [x] Orchestrator room snapshot capacity uses `ROOM_CATALOG_MAX_ROOMS`, not legacy device capacity.
- [x] Orchestrator current-device snapshot capacity uses `QUEST_DEVICE_MAX_DEVICES`.

## Documentation

- [x] API contract is in `gm_api_contract.md`.
- [x] Device control contract is in `device_control_contract_v1.md`.
- [x] Architecture is current-model only.
- [x] README describes the current clean runtime model and `quest_common`.
- [x] Previous setup guides removed.
- [x] New Quest Device and Room Scenario setup guides added.

## Cleanup Remaining

- [x] Product bootstrap no longer starts legacy `device_manager` or
      `automation_engine`.
- [x] `orchestrator_core` and `web_ui` no longer require legacy
      `device_manager` / `automation_engine` components.
- [x] Common string limits and copy helper moved to `quest_common`.
- [x] Current-model components no longer include `device_model` headers.
- [x] Public docs are focused on Quest Devices, Room Scenarios, Game Modes and
      GM Session.
- [x] `room_scenario` type mapping and validation moved out of the store file.
- [x] `room_scenario` single-scenario JSON serialization moved out of the
      store file.
- [x] `room_scenario` filesystem persistence moved out of the store file.
- [x] `gm_room_session` device/system command execution moved out of the
      session runtime file.
- [x] `gm_room_session` event matching and event worker moved out of the
      session runtime file.
- [x] `gm_room_session` scenario VM/start/stop/tick moved into a runtime file.
- [x] `gm_room_session` game/profile/scenario selection moved out of the
      session store file.
- [x] Split large C modules into smaller ownership units:
      `gm_room_session.c` runtime/control/events/commands and
      `room_scenario.c` store/validation/json/persistence.
- [ ] Remove any unused C modules from the build after confirming no current
      endpoints/runtime paths depend on them.
- [x] Archived unused legacy component directories outside the build graph:
      `legacy/components/device_model`, `legacy/components/device_manager`,
      `legacy/components/device_runtime`, `legacy/components/automation_engine`.
- [x] Replaced misleading `tests/device_manager` current path with
      `tests/quest_backend`; legacy tests moved under `legacy/tests/`.
- [x] GM room scenario event worker queue stores `event_bus_message_t` by value;
      no malloc/free on the device event hot path.
