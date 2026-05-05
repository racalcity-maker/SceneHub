# GM Panel UI Plan

This document is the product/UI direction for the GM panel. Use it as the reference before adding new GM UI features.

## Goal

The GM panel should be one role-aware quest console.

Operators use it to run and monitor quests. Admins use the same panel with additional build/edit tools for devices, room scenarios and game profiles.

## Screenshots

### Dashboard

![SceneHub dashboard](Pics/dashboard.jpg)

### Room Control

![GM Panel room control](Pics/room.jpg)

### Device Setup

![Quest Device setup](Pics/devices_setup.jpg)

### Scenario Setup

![Room Scenario setup](Pics/scenario_setup.jpg)

### Timeline

![GM Panel timeline](Pics/timeline.jpg)

## Main Principle

Do not split runtime control and configuration into unrelated products.

Use one `/gm` panel with role-based navigation and permissions:

- `USER`: safe operator mode
- `ADMIN`: operator mode plus editor/setup sections

The UI may hide admin-only controls, but backend permissions remain authoritative.

## Implementation Checkpoints

- Main/admin page now has a direct link to `/gm`.
- `/gm` loads session role and exposes admin-only navigation only for `ADMIN`.
- `/gm` keeps runtime sections available for operators.
- Room `Control` now exposes game profile selection and game start/stop/reset.
- Direct room scenario selection/control is admin-only in the UI; operators see scenario runtime status and operator approval.
- Room `Control` now has an operator-first console that keeps profile, game timer, scenario runtime, wait state and operator approval visible together.
- Admin `Profiles` section now has a first CRUD editor for room game profiles.
- Admin `Scenarios` section now has a first JSON-based CRUD editor for room scenarios.
- Admin `Scenarios` section now has a first structured step builder with add/delete/reorder and per-step payload forms, while still saving through the existing JSON scenario API.
- Admin `Storage` section now exposes save/load/import/export for profiles and room scenarios.
- Admin `Device Setup` section now edits Quest Devices directly inside `/gm`; standalone Device Wizard assets are no longer part of the normal GM page.
- GM `Devices` now shows Quest Devices and physical control clients only.
- The main/admin page no longer exposes a separate `Devices` tab; device work is routed to `/gm`.
- GM `Observed` now separates registered/unregistered MQTT clients, treats saved Quest Device `client_id` as registration, and routes admins to `Device Setup`.
- Admin `Device Setup` supports universal quest devices: choose an observed physical client, request `describe_interface`, then import quest commands/events after confirmation.
- Admin `Device Setup` normal path now shows Quest Devices only: commands/events plus observed client binding.
- Saving `Device Setup` also exports visible Quest Device capabilities into the new `quest_devices` store so Room Scenarios can use them through `DEVICE_COMMAND` and `WAIT_DEVICE_EVENT`.
- GM panel frontend logic/styles are now served from `assets/gm_panel.js` and `assets/gm_panel.css`; `web_ui_page.c` keeps only the `/gm` HTML shell.

## Main Admin Page Migration

The current main/admin web UI should eventually stop being the primary device-management workspace.

Target direction:

- Add a clear link from the main/admin page to `/gm`.
- Move device monitoring into the GM panel `Devices` section.
- Move device creation/configuration into the GM panel admin-only `Device Setup` section using Quest Device capabilities.
- Keep simple device observation available to operators in `/gm`.
- Keep device editing/setup controls admin-only.
- The main/admin page should become a lightweight entry point for system/admin utilities and a link into the GM panel.

Migration should be staged:

1. Add link from the main/admin page to `/gm`.
2. Build device monitoring parity inside GM `Devices`.
3. Build admin-only device setup parity inside GM `Device Setup`.
4. Keep the separate Devices page temporarily while parity is incomplete.
5. After parity, remove or downgrade the separate Devices page to a redirect/link.

Do not remove the existing Devices page until the GM panel covers the needed monitoring and setup workflows.

## Operator Mode

Operator mode is focused on live quest operation.

Allowed operator capabilities:

- select room
- select game profile
- start game
- stop game
- reset game
- see timer state and remaining time
- see current room scenario runtime state
- see current scenario step
- see current wait state:
  - none
  - time
  - event
  - operator
- approve operator gates
- force next/current wait only as an emergency/debug action with confirmation
- send and clear hints
- monitor devices in the selected room
- see device health/connectivity/runtime state
- see offline registered devices as critical room/system faults
- execute safe device actions exposed by backend
- view audit and timeline
- see room/device issues

Operator mode must not expose:

- profile editing
- room scenario editing
- device configuration editing
- import/export
- save/load storage operations
- destructive config operations
- removed device-local setup flows

## Admin Mode

Admin mode includes everything from operator mode plus build/setup tools.

Admin-only capabilities:

- edit game profiles
- create/delete game profiles
- bind profile to room scenario
- edit profile duration
- enable/disable profiles
- import/export profiles
- save/load profiles
- edit room scenarios
- create/delete room scenarios
- edit scenario steps
- validate room scenarios
- import/export room scenarios
- save/load room scenarios
- create/configure Quest Devices
- edit physical client binding and command/event capabilities
- save Quest Device capabilities
- use storage/import/export tools

Admin mode should support a build-and-test workflow:

1. Create/configure Quest Devices.
2. Build a room scenario.
3. Validate the scenario.
4. Build a game profile.
5. Select the profile in the operator view.
6. Start the game and test runtime behavior.
7. Inspect audit, timeline and device status.

## Proposed Navigation

Shared sections:

- `Dashboard`
- `Room`
- `Devices`
- `Issues`
- `Timeline`
- `Audit`

Admin-only sections:

- `Builder`
- `Profiles`
- `Device Setup`
- `Storage`

## Dashboard

Purpose: quick operational overview.

Shows:

- rooms
- active games
- timer summary
- room health
- current profile
- current scenario runtime state
- active wait state
- fault/degraded counts

Operator actions:

- open room

Admin additions:

- quick links to profile/scenario/device setup for a room

## Room View

Purpose: primary operator screen.

Operator controls:

- profile selector
- `Start game`
- `Stop game`
- `Reset game`
- timer display
- current scenario display
- current step display
- waiting-for display
- operator approval button
- emergency `Next` with confirmation
- hint input/send/clear
- room devices summary
- room issues summary

Runtime data sources:

- `GET /api/gm/state`
- `GET /api/gm/room/profiles?room_id=...`
- `POST /api/gm/room/profile/select`
- `POST /api/gm/room/game/start?room_id=...`
- `POST /api/gm/room/game/stop?room_id=...`
- `POST /api/gm/room/game/reset?room_id=...`
- scenario runtime endpoints for emergency/debug controls
- hint endpoints

Admin additions:

- edit selected profile
- edit selected scenario
- validate scenario
- open builder for room

## Devices View

Purpose: monitor and operate devices.

Operator capabilities:

- list devices
- filter by room
- inspect health/connectivity/runtime
- inspect diagnostics and last result
- execute safe actions

Admin additions:

- edit device
- duplicate/create Quest Device
- configure physical client binding
- open device setup

Status semantics:

- `connectivity=online` means fresh control-contract telemetry was received within the online timeout.
- `connectivity=offline` for a registered quest device is a critical fault. It creates a `device_offline` issue and makes the room/system `fault`.
- `connectivity=unknown` / `not observed` during setup is a warning/degraded state, not a fault.
- UI device badges must display `offline`/`unknown` over `health=ok`; stale health must not make an offline device look green.
- Room health aggregates device faults/degraded issues. A room with any offline registered device must not show `OK`.

Registration semantics:

- `Observed` lists physical MQTT control-contract clients (`cp/v1/dev/{device_id}/...`).
- A physical client is considered registered when its id matches a saved Quest Device `client_id`.
- The Quest Device name is what operators see; the physical client id remains the telemetry/control endpoint.

## Builder

Admin-only.

Purpose: edit room scenarios.

Capabilities:

- list scenarios by room
- create scenario
- delete scenario
- edit scenario metadata
- edit normal flow branches separately from reactive reaction branches
- add/remove/reorder steps
- edit step payloads
- run validation
- show validation errors/warnings
- import/export scenario collection
- save/load scenario collection

Builder must preserve the scenario API contract from `docs/gm_api_contract.md`.

Reactive branch UI target:

- show normal branches under `Scenario flow`;
- show reactive branches under `Reactions`;
- treat one reactive branch as one reaction;
- expose branch-level `cooldown_ms` and `run_once`;
- show reactive runtime states as `listening`, `running`, `cooldown` or
  `disabled`;
- do not mix reactive branches into the main completion/progress row.

Reactive Branch v2 editor status:

- New reactive branches created by the structured builder use the Reactive
  Branch v2 model.
- The old first-step-as-trigger reactive branch model is not a product target
  for new scenarios.
- The backend runtime and JSON contract already support Reactive Branch v2
  fields: `trigger`, `guard_flags`, `policy`, `reentry`, `variants`,
  `result_policy` and `on_complete`.
- The dedicated v2 editor exposes branch-level controls for trigger, guards,
  policy, reentry, result policy and variants/actions instead of using the
  legacy first-step-as-trigger model.
- The builder must continue preserving v2 fields when loading/saving a
  scenario so existing v2 JSON is not downgraded or destroyed by an edit.

## Profiles

Admin-only.

Purpose: edit game profiles.

Capabilities:

- list profiles by room
- create profile
- delete profile
- edit name
- edit duration
- bind scenario
- edit hint/audio pack ids
- enable/disable profile
- validate profile
- import/export profile collection
- save/load profile collection

Profiles must preserve the profile API contract from `docs/gm_api_contract.md`.

## Device Setup

Admin-only.

Purpose: create and configure Quest Devices from physical client capabilities.

Capabilities:

- create device
- select or enter physical client id
- request on-demand `describe_interface`
- import discovered command/event capabilities after confirmation
- manually edit command/event capabilities
- preview manual buttons and scenario capabilities
- validate device capability config
- save/delete Quest Device config

Universal quest device workflow:

1. Create or edit a Quest Device.
2. Select the physical smart client.
3. Press `Get config`.
4. SceneHub sends `describe_interface` to the physical client.
5. If a valid `device_description` is returned, admin confirms import.
6. Imported `commands[]` become manual buttons and `DEVICE_COMMAND` options.
7. Imported `events[]` become `WAIT_DEVICE_EVENT` options.

Do not require admins to type physical client ids or quest topics manually when discovery data is available.

Long-term target: device setup, scenario builder and profile editor should work together inside the same GM panel.

## Storage

Admin-only.

Purpose: explicit persistence/import/export tools.

Capabilities:

- export room scenarios
- import room scenarios
- save room scenarios to filesystem
- load room scenarios from filesystem
- export game profiles
- import game profiles
- save game profiles to filesystem
- load game profiles from filesystem
- future: device config import/export shortcuts

## Permission Rules

UI visibility:

- Hide admin sections for non-admin users.
- Hide admin-only controls inside shared views for non-admin users.
- Show safe runtime controls to operators.

Backend enforcement:

- Admin endpoints stay guarded as admin-only.
- Operator endpoints stay guarded as user-level.
- Never rely only on frontend hiding for security.

## Design Rules

Operator runtime UI should be calm and operational:

- no marketing layout
- no landing page
- dense but readable room state
- obvious primary action
- clear disabled states
- confirmation for emergency/debug overrides
- current wait state must be visible without scrolling in room view

Admin editor UI should be structured and validation-first:

- show validation before start
- show broken references clearly
- avoid partial import behavior
- make save/load/import/export explicit
- keep runtime testing close to editor context

## Implementation Order

Recommended order:

1. Add role-aware navigation to `/gm` using `/api/session/info`.
2. Rework Room view into operator-first profile/game runtime flow.
3. Add profile selector and game start/stop/reset controls.
4. Keep scenario runtime controls as secondary/debug controls.
5. Add admin-only Profiles editor.
6. Add admin-only Room Scenario builder.
7. Add admin-only Device Setup integration.
8. Add Storage/admin import-export screen.

## References

- API contract: `docs/gm_api_contract.md`
- Architecture overview: `docs/ARCHITECTURE.md`
