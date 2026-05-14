# GM Panel Refactor Plan

This checklist tracks the incremental cleanup of the GM Panel JavaScript.

The goal is not to rewrite the whole UI at once. New hardware IO screens and
new editor work should use the cleaned-up patterns first, then old areas can be
migrated gradually.

## Status

- [x] P0: Add shared UI primitives.
- [x] P0: Add base helpers before UI/API modules.
- [x] P0: Add a shared API client wrapper.
- [x] P0: Add a shared `data-action` router.
- [x] P1: Move manual buttons and room runtime actions to `data-action`.
- [x] P1: Move profiles, storage, quest devices and scenario editor buttons to `data-action`.
- [x] P1: Add a schema-form helper for repeated editor forms.
- [x] P1: Build new hardware IO UI only on the new primitives/router/forms.
- [x] P2: Split scenario builder into focused modules.
- [x] P2: Split Reactive V2 editor out of the scenario builder.
- [x] P2: Split scenario runtime/progress helpers out of state helpers.
- [x] P2: Move global GM panel state into a namespaced `GM` object.

Remaining work is no longer a broad panel rewrite. It is limited to:

- GM runtime task stack/PSRAM scratch cleanup.
- Runtime/static state separation.
- Frontend request ordering and stale-response protection.
- More selective rendering for runtime, sidebar and static views.
- Polling/backoff cleanup for hidden tabs.
- API response helper cleanup.
- Optional schema-form `json` field support.
- Hardware IO channel configuration UI.
- Splitting the still-large `collectScenarioEditor()` collector.

## P0 - GM Runtime Task Memory

Recent runtime polling work moved scenario advancement out of HTTP GET handlers
and into the `gm_room_runtime` task. That is the right architecture, but the
runtime task must be sized and structured for the worst real scenario path.

Known failure:

- `gm_room_runtime` stack overflowed while advancing a room scenario after
  runtime/next actions.
- Heap was not exhausted (`free_int` and `free_psram` were still healthy). The
  failure was task stack pressure.
- The old HTTP path often ran through the web server task, which has a larger
  stack than the new runtime task.

Target:

- [x] Increase `GM_ROOM_SESSION_RUNTIME_TASK_STACK` to a safe value for current
  behavior. Start with at least the event task stack size, then verify with
  high-water diagnostics.
- [x] Add runtime task stack high-water diagnostics while this area is being
  stabilized.
- [x] Move hot-path timeout event scratch buffers out of the runtime task stack.
- [x] Prefer static PSRAM-backed scratch storage for repeated scenario/runtime
  work instead of allocating and freeing buffers every tick.
- [x] Avoid repeated `heap_caps_calloc()` / `heap_caps_free()` in hot runtime
  paths when a long-lived scratch object or per-room runtime storage is enough.
- [x] Keep one-time initialization allocations explicit and fail clearly if
  required scratch memory is unavailable.
- [x] Review command executor calls from runtime tick and identify which large
  structures can be made heap/PSRAM-backed or reused.
- [x] Reuse PSRAM-backed `quest_device_t` / `quest_device_command_t` scratch in
  `command_executor_execute()` instead of allocating both objects for every
  command.
- [x] Review command executor parameter parsing. It still parses `params_json`
  once per requested field; a later pass can parse once per command if needed.
- [x] Parse `params_json` once per `command_executor_execute()` call for
  audio/hardware parameter helper lookups.

Acceptance:

- [x] `gm_room_runtime` does not overflow stack during normal, reactive and
  hardware/audio command scenarios.
- [x] Runtime tick does not allocate/free large scenario buffers every 100 ms.
- [x] Diagnostics can show remaining stack headroom during on-device testing.
- [x] Failure to allocate runtime scratch memory degrades clearly instead of
  crashing later in the game.

## P0 - Runtime State Contract

Recent GM Panel optimizations split frequent room runtime refreshes from heavier
GM snapshot/static data loads. This reduced RAM/CPU pressure, but it also made
runtime field drift easier: a missing field in `/api/gm/room/runtime` or
`mergeRoomRuntimeState()` can leave the UI with stale state until a full Refresh.

Known example:

- `session_present` was present in the full GM snapshot but missing from the
  lightweight room runtime path, so Reset game could stay disabled after a game
  finished until the operator pressed the top Refresh button.

Target:

- [x] Add a single `ROOM_RUNTIME_FIELDS` constant in the frontend.
- [x] Use the same field list in `mergeRoomRuntimeState()`.
- [x] Make `/api/gm/room/runtime` return an idle runtime contract for an
  existing room with no active session instead of `404 room or scenario not
  found`.
- [x] Keep `/api/gm/room/runtime` as a read-only runtime endpoint; scenario
  advancement belongs to the backend runtime tick task, not HTTP GET handlers.
- [x] Add/maintain backend contract tests for every field that controls visible
  room buttons or runtime progress.
- [x] Consider a compact runtime schema/version field so the frontend can detect
  contract drift clearly.

Acceptance:

- [x] Start/stop/reset/continue/timer buttons update correctly from runtime-only
  refreshes with no full GM snapshot refresh.
- [x] WAIT_FLAGS/branch progress advances from the runtime tick task, not from
  polling side effects.
- [x] Adding a runtime UI field requires changing one frontend field list, not
  several scattered lists.

## P0 - Request Ordering

Current problem: broad snapshot loads, active room runtime polling, version
polling and button actions can overlap. Busy flags prevent duplicate polling,
but they do not stop an older response from overwriting newer state after a
slower network round trip.

Target:

- [x] Add monotonic request sequence tokens for GM snapshot and room runtime
  loads.
- [x] Ignore stale responses when a newer request for the same state scope has
  already completed.
- [x] Keep button actions optimistic only for status text; canonical runtime
  state still comes from the backend response.

Acceptance:

- [x] A slow `/api/gm/state` response cannot overwrite newer
  `/api/gm/room/runtime` data.
- [x] Rapid button clicks followed by polling do not leave controls in a stale
  enabled/disabled state.

## P1 - Selective Rendering

Current state:

- `render()` still replaces all of `#gm_content`.
- `render()` now calls `renderRightSidebar(false)`, and the sidebar skips DOM
  replacement when its render key has not changed.
- Active Room Control has a targeted runtime panel patch, but other paths still
  use broad full renders.

Target:

- [x] Split `render()` into view composition and targeted patch helpers.
- [x] Keep full render for navigation, editor save/delete and structural data
  changes.
- [x] Patch room runtime only when the runtime render key changes.
- [x] Patch right sidebar only when quest-device/manual-button data changes.
- [x] Patch visible clocks independently, as they are already updated locally.

Acceptance:

- [x] Manual device commands do not re-render the room control view.
- [x] Timer/runtime polling does not rebuild the manual sidebar.
- [x] Full render remains the fallback for unknown or structural changes.

## P1 - Polling And Static Data

Current state:

- Active room runtime polls every 1 second.
- GM versions poll every 10 seconds outside active Room Control.
- Visible clocks update locally every 250 ms.
- Static data is cached with a TTL and lazy-loaded by view.
- Version polling now refreshes changed static slices directly: devices/ingest,
  scenarios and profiles do not force a full GM snapshot reload.
- Full `/api/gm/state` refresh is now explicit `loadGMFullSnapshot()` use for
  bootstrap, structural refresh and recovery only. Routine runtime refresh uses
  `room.runtime`, rooms-runtime refresh or `system.summary`.

Target:

- [x] Add `document.hidden` backoff/skip for runtime and version polling.
- [x] Continue local clock updates only when relevant clock elements are visible.
- [x] Use `/api/gm/versions` to refresh only changed static slices where
  possible, instead of full `loadGMFullSnapshot()` calls.
- [x] Refresh sidebar/static device data by versions while keeping active Room
  Control runtime-only rendering.

Acceptance:

- [x] Hidden browser tabs do not keep unnecessary runtime polling load.
- [x] Device/manual-button changes can appear without pressing full Refresh.
- [x] Active game runtime remains responsive without heavy snapshot reloads.

## P1 - Audio File Loading

Current problem: the audio file dropdown can trigger a recursive `/sdcard` scan.
That is useful but expensive, and it should not run unless the scenario editor
actually needs file choices.

Target:

- [x] Load audio files only when the scenario editor opens or an audio file
  dropdown/Refresh files action needs them.
- [x] Keep cached file results until explicit refresh or storage/version change.
- [x] Keep behavior the same for users: existing dropdowns still show available
  files once loaded.

Acceptance:

- [x] Opening non-scenario views never starts an SD-card audio scan.
- [x] Scenario editor remains usable before the scan completes.

## P1 - Generated Bundle Safety

Current problem: `assets/gm_panel.js` is generated from split source parts, but
it is easy to edit a part and forget to rebuild the bundle before committing.

Current state:

- `components/web_ui/assets/check_gm_panel_bundle.py` rebuilds the expected
  bundle in memory from the `GM_PANEL_PARTS` list in
  `components/web_ui/CMakeLists.txt` and compares it with
  `components/web_ui/assets/gm_panel.js`.
- The CMake bundle generation remains the source of the firmware asset.
- Manual check command:
  `python components\web_ui\assets\check_gm_panel_bundle.py`

Target:

- [x] Add a lightweight check script or test that rebuilds to a temp file and
  compares it with `assets/gm_panel.js`.
- [x] Keep the current CMake bundle generation.
- [x] Document the exact rebuild command or add a small wrapper script.

Acceptance:

- [x] CI/tests can detect a stale generated `gm_panel.js`.
- [x] Developers have one obvious command to check the GM panel bundle.

## P0 - UI Primitives

Current problem: most render functions hand-write the same HTML patterns:

- buttons;
- icon buttons;
- action rows;
- cards;
- field stacks;
- inputs;
- selects;
- checkboxes;
- badges;
- details blocks;
- empty states.

Target file:

- [x] Add `components/web_ui/assets/gm_panel/gm_panel_00_base.js`.
- [x] Add `components/web_ui/assets/gm_panel/gm_panel_00_ui.js`.
- [x] Add it before the existing GM panel parts in `components/web_ui/CMakeLists.txt`.

Helpers:

- [x] `esc(v)`
- [x] `enc(value)`
- [x] `kebab(value)`
- [x] `boolAttr(name, value)`
- [x] `jsonAttr(value)`
- [x] `uiAttrs(attrs)`
- [x] `uiDataset(dataset)`
- [x] `uiButton(opts)`
- [x] `uiIconButton(opts)`
- [x] `uiActions(buttons)`
- [x] `uiCard(opts)`
- [x] `uiSection(opts)`
- [x] `uiField(opts)`
- [x] `uiSelect(opts)`
- [x] `uiInput(opts)`
- [x] `uiCheckbox(opts)`
- [x] `uiBadge(text, kind)`
- [x] `uiEmpty(text)`
- [x] `uiDetails(opts)`

Acceptance:

- [x] Helpers escape labels and attribute values through `esc()`.
- [x] Helpers support `disabled`, `title`, `class/kind`, `data-action`, and custom dataset values.
- [x] `uiButton()` always emits the base `ui-btn` class while preserving old kind classes.
- [x] `uiCard()` supports title, subtitle, status, actions, footer, kind, className and dataset.
- [x] Existing UI keeps rendering the same after adding the file.
- [x] At least one small area uses `uiButton()` without changing behavior.

## P0 - API Client

Current problem: endpoint strings are spread across action/render files.

Target file:

- [x] Add `components/web_ui/assets/gm_panel/gm_panel_00_api.js`.
- [x] Add it after UI primitives and before state/helpers in `components/web_ui/CMakeLists.txt`.

Helpers:

- [x] `gmGet(url)`
- [x] `gmPost(url)`
- [x] `gmPostJson(url, body)`
- [x] `gmDeleteJson(url, body)` if needed.
- [x] `enc(value)` wrapper for `encodeURIComponent`.

Domain API object:

- [x] `api.gm.state()`
- [x] `api.room.runtime(roomId)`
- [x] `api.room.game(roomId, action)`
- [x] `api.room.timerStart(roomId, durationMs)`
- [x] `api.room.timerAction(roomId, action)`
- [x] `api.room.profileSelect(roomId, profileId)`
- [x] `api.room.scenarioSave(scenario)`
- [x] `api.room.scenarioValidate(scenario)`
- [x] `api.device.list(includeSystem)`
- [x] `api.device.runCommand(deviceId, commandId, params)`
- [x] `api.storage.run(action)`

Acceptance:

- [x] Add parsed-JSON helpers for endpoints whose callers always need JSON.
- [x] Error handling still preserves useful HTTP text/status.
- [x] At least one existing action uses `api.*` instead of raw `gmFetch()`.

## P0 - Action Router

Current problem: `gm_panel_09_events_boot.js` is a large manual dispatcher with
many `closest()`, `if`, dataset parsing, confirm handling and render decisions.

Target:

- [x] Add a shared `GM_ACTIONS` map.
- [x] Add one generic click handler for `[data-action]`.
- [x] Keep legacy handlers working during migration.
- [x] Remove the large legacy `gm_content.onclick` button dispatcher after migration.

Basic shape:

```js
const GM_ACTIONS = {
  'room.open': handleRoomOpen,
  'room.game': handleRoomGame,
  'room.timer': handleRoomTimer,
  'manual.device.command': handleManualDeviceCommand,
};
```

Dataset convention:

- [x] `data-action`: action name.
- [x] `data-op`: operation inside the action group.
- [x] Prefer explicit primary ids such as `data-room-id`, `data-device-id` and
  `data-command-id` instead of a generic `data-id`.
- [x] `data-room-id`: room id.
- [x] `data-device-id`: device id.
- [x] `data-command-id`: command id.
- [x] `data-index`: list index.
- [x] `data-field`: form field.
- [x] `data-param`: command parameter.
- [x] `data-confirm`: confirmation text.

Acceptance:

- [x] Unknown `data-action` reports a visible UI error.
- [x] `data-confirm` is handled once by the router.
- [x] Manual device buttons can use `data-action` with no old custom handler.
- [x] No runtime refresh/render is introduced by manual device commands.

## P1 - Migrate Small Actions First

Start with simple actions before touching scenario editor.

Manual buttons:

- [x] Render manual buttons with `uiButton()`.
- [x] Use `data-action="manual.device.command"`.
- [x] Send command default args through `api.device.runCommand()`.
- [x] Do not refresh room runtime after manual commands.

Room runtime controls:

- [x] Migrate start/stop/reset game buttons.
- [x] Migrate pause/resume/continue buttons.
- [x] Migrate timer buttons.
- [x] Migrate hint send/clear buttons.
- [x] Migrate emergency runtime buttons.
- [x] Remove old room runtime custom click branches after migration.

Admin/editor controls:

- [x] Migrate room create/delete.
- [x] Migrate admin view navigation buttons.
- [x] Migrate profile editor buttons.
- [x] Migrate storage actions.
- [x] Migrate quest device editor/import/command/event buttons.
- [x] Migrate scenario list/editor save/validate buttons.
- [x] Migrate scenario branch tabs/add/delete buttons.
- [x] Migrate scenario step add/edit/move/delete and group/list buttons.
- [x] Migrate Reactive V2 guard/variant/action buttons.
- [x] Remove old custom `data-*` button branches from `gm_content.onclick`.
- [x] Split boot handlers from editor focus/input/change handlers.
- [x] Split scenario-specific `change` handling out of editor event wiring.

Acceptance:

- [x] Running room UI does not redraw from manual device buttons.
- [x] Runtime-only refresh still works for actual room runtime actions.
- [x] Existing confirmation behavior is preserved.

## P1 - Schema Form Helper

Current problem: every editor renders, collects and validates forms differently.

Target file:

- [x] Add `components/web_ui/assets/gm_panel/gm_panel_00_form.js`.

Helpers:

- [x] `renderFormFields(schema, model, scope)`
- [x] `collectFormFields(root, schema, scope)`
- [x] `validateFormFields(model, schema)`

Supported field types:

- [x] `text`
- [x] `number`
- [x] `checkbox`
- [x] `select`
- [x] `textarea`
- [x] `duration_ms`
- [x] `json`

Acceptance:

- [x] Helper supports stable `data-field` attributes.
- [x] Helper preserves numeric values as numbers.
- [x] Helper preserves checkbox values as booleans.
- [x] One non-critical editor block uses schema forms first.

## P1 - Hardware IO UI

New hardware IO UI must use the new patterns from the start.

Screens:

- [x] Hardware IO overview.
- [x] Hardware IO overview uses composed render helpers instead of one dense view function.
- [x] Relay channel test controls.
- [x] MOSFET channel test controls.
- [x] Relay/MOSFET live status read endpoint.
- [x] MOSFET `all_off` control.
- [x] MOSFET pulse/fade active status badges.
- [ ] Relay channel configuration.
- [ ] MOSFET channel configuration.
- [ ] Discrete input configuration.
- [ ] Universal GPIO configuration.

Channel fields:

Common:

- [ ] `enabled`
- [ ] `label`
- [ ] `gpio`
- [ ] `safe_state`

Relay:

- [ ] `active_low`
- [ ] `default_pulse_ms`
- [ ] `max_pulse_ms`
- [ ] `default_blink_on_ms`
- [ ] `default_blink_off_ms`
- [ ] `max_blink_count`

MOSFET:

- [ ] `pwm_freq_hz`
- [ ] `default_value`
- [ ] `default_pulse_value`
- [ ] `default_pulse_ms`
- [ ] `max_pulse_ms`
- [ ] `default_fade_ms`
- [ ] `max_fade_ms`
- [ ] `default_blink_on_ms`
- [ ] `default_blink_off_ms`
- [ ] `default_breathe_ms`
- [ ] `max_effect_count`

Inputs / universal GPIO later:

- [ ] `active_low`
- [ ] `debounce_ms`
- [ ] `mode`

Acceptance:

- [x] Hardware IO UI does not add new custom `closest()` branches.
- [x] Hardware IO UI does not hand-roll repeated input/select HTML.
- [x] Command actions go through `data-action` and `api.*`.
- [ ] Future save/validate actions go through `data-action` and `api.*`.
- [ ] Consider making read-only `GET /api/hardware-io/status` available to operator users when operator diagnostics need it.

## P2 - Scenario Builder Split

Current problem: the scenario builder mixed Reactive V2 rendering, normal branch
management, step rendering/actions, validation, DOM collection and page layout.

Completed split:

- [x] `gm_panel_05c_reactive_v2.js` - Reactive V2 defaults/render/collect/actions.
- [x] `gm_panel_05d_scenario_branches.js` - branch tabs/settings/actions.
- [x] `gm_panel_05e_scenario_steps.js` - step preview/render/labels/actions.
- [x] `gm_panel_05f_scenario_validation.js` - client validation and issue display.
- [x] `gm_panel_05_scenario_builder.js` reduced to catalog helpers, DOM collection and page composition.

Acceptance:

- [x] Scenario builder is no longer the largest GM panel module.
- [x] Reactive V2 code is isolated from normal branch/step code.
- [x] Branch and step actions are isolated from page composition.
- [x] `collectScenarioEditor()` is reduced to orchestration; branch settings
  and normal step collection are split into focused helpers.

## P2 - State Helpers Split

Current problem: `gm_panel_01_state_helpers.js` mixed global state, generic
helpers, scenario runtime progress rendering, sidebars, and dirty/discard
editor guards.

Completed split:

- [x] `gm_panel_01a_scenario_runtime.js` - selected scenario lookup, branch flattening, runtime progress rendering and scenario validation summary helpers.
- [x] `gm_panel_01b_editor_dirty.js` - editable-field dirty tracking, discard confirmations and editor dirty helpers.
- [x] `gm_panel_01c_quest_device_status.js` - quest-device display/status helpers, command policy, manual sidebar rendering.
- [x] `gm_panel_01d_generic_helpers.js` - generic escaping, details attrs, timer/text formatting, cache lookups and option-list helpers.

## P2 - State Namespace

Current problem: `gm_panel_01_state_helpers.js` has many globals.

Target:

- [x] Introduce a `GM` namespace object for new modules.
- [x] Keep old globals during migration.
- [x] Move data caches under `GM.data`.
- [x] Move UI selection state under `GM.ui`.
- [x] Move editors under `GM.editors`.
- [x] Move auth/session state under `GM.session`.

Acceptance:

- [x] New modules can use `GM.*`.
- [x] Old code remains compatible until each area is migrated.
- [x] No large one-shot state rewrite.

## Rules For This Refactor

- [x] Do not rewrite the whole panel in one patch.
- [x] Avoid unintended user-visible behavior changes; intentional UX fixes must
  be called out in the changelog or relevant docs.
- [x] Rebuild `gm_panel.js` after changing source parts.
- [x] Run `node --check` for changed JS parts and generated `gm_panel.js`.
- [x] Do not run ESP-IDF build unless explicitly requested.
- [x] Prefer migrating one small UI area per patch.
