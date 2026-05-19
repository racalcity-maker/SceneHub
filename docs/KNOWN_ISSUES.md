# Known Issues

This file is the single active backlog for open product, runtime and
architecture issues.

Policies and durable rules live in dedicated policy/reference documents:

- `ARCHITECTURE.md`
- `LOCKING_POLICY.md`
- `MEMORY_ALLOCATION_POLICY.md`
- `gm_api_contract.md`
- `device_control_contract_v1.md`
- `reactive_branch_v_2_design.md`

Temporary plan files were consolidated here. Closed work from those plans is
not repeated; only unresolved risks and deferred work are tracked below.

## Active Backlog

### P0 - Correctness And Runtime Safety

- Keep command execution outside GM session locks. Scenario planning may inspect
  lightweight metadata, but external calls, hardware IO, MQTT publish, audio
  control and storage must happen after the owner lock is released.
- Maintain the MQTT command-result ordering rule: pending result tracking must
  be created before publishing result-required commands, and cleared if publish
  fails.
- Keep MQTT bridge direction explicit. Inbound MQTT messages must not be
  reflected back into MQTT through generic event bridging.
- Add focused regression coverage for MQTT ACL/QoS edge cases, request-id
  uniqueness, retained delivery QoS handling and duplicate subscription
  updates.

### P1 - Runtime Performance

- Keep routine active-room runtime updates event-driven through `ws_runtime`.
  Fixed polling should only be fallback/recovery behavior, not the normal live
  path.
- Finish compact runtime read DTOs so routine runtime detail does not pass
  through broader domain/session structs than needed.
- Keep stable scenario layout, live runtime counters and asset readiness as
  separate paths. Default runtime detail must not trigger storage or asset
  scans.
- Ensure runtime HTTP chunk sending does not hold shared scratch/cache mutexes.
- Audit `/api/gm/state` usage. It should be bootstrap, structural refresh or
  recovery only, not a frequent live-runtime refresh path.
- Review runtime advancement for redundant state writes and excessive
  `gm_room_session_mark_session_changed_locked()` calls.
- Add indexed dispatch follow-ups only where broad scans still show measurable
  runtime cost: command-result waiters, flag waiters and device-event waiters.
- Keep WebSocket invalidation payloads bounded and avoid generic diff engines
  or repeated allocation in hot paths.

### P2 - Architecture Boundaries

- Finish moving Web UI write handlers behind narrow control APIs. HTTP handlers
  should parse, authorize, call one service function and serialize the result.
- Complete the deferred `gm_core` decomposition so the component stops acting as
  the default owner of runtime state, command dispatch glue, profile/sidebar
  storage helpers, and legacy application facades at the same time.
- Rework manual GM device-command execution so HTTP handlers do not synchronously
  run transport and device-control work inside the `httpd` task. Preferred
  direction: accept the command through a narrow control API, enqueue execution
  in a dedicated control worker/context, return an accepted/request-id style
  result, and let UI wait through runtime/WS updates instead of coupling live
  command execution to HTTP task stack limits.
- Replace the transitional scenario layout path with a compact read-model DTO.
  Today the HTTP handler calls the read model for lookup, but the isolated
  layout writer still serializes `room_scenario_t`.
- Audit `device_control_ingest` boundary width. The full
  `device_control_ingest_device_t` snapshot is valid for diagnostics/read-side
  consumers, but narrow operational questions should keep moving to focused
  accessors such as presence or result-summary instead of copying the whole
  ingest DTO through control paths.
- Revisit GM sidebar preset boundary ownership if the feature grows. Today
  `gm_sidebar_preset_t` is acceptable as the controller-backed storage owner,
  but if control/web payload needs diverge, split a narrower control/web shape
  instead of turning the persisted store struct into a catch-all view model.
- Keep read-model APIs split by family. Do not grow the compatibility
  `orchestrator_registry.h` facade with new unrelated DTOs.
- Add isolated domain unit tests for `scenehub_events`, GM wait/reaction/state
  transitions, command planning without transport/hardware, and
  `room_scenario` validation.
- Introduce small fakeable ports where they reduce test cost: time, event post,
  command dispatch and optional scenario/load helpers for `gm_core`.
- Re-evaluate event-bus topology only after boundaries and tests are stable.
  Possible future work: typed lanes, multiple queues or stronger backpressure.

Deferred execution order for the `gm_core` decomposition:

1. Stop routing new write-side behavior through `gm_api` and `gm_control`.
   `scenehub_control` should become the real application facade, with the old
   `gm_core/api` and `gm_core/control` entrypoints reduced to thin wrappers or
   removed.
2. Keep `gm_core` focused on room session state, scenario runtime progression,
   waits, flags, timer/hint state, operator approval, and command plans.
   External command execution should not stay inside `gm_core/session`.
3. Extract planned-command execution out of `gm_core/session`. The runtime
   should produce and validate `gm_room_session_command_plan_t`, while a narrow
   control/dispatch layer resolves Quest Device metadata and calls
   `command_executor`.
4. Keep `command_executor` transport- and side-effect-focused. Do not move
   branch/step/reactive runtime semantics into the executor while doing the
   extraction.
5. Move game start/stop/reset orchestration out of `gm_core/session` and into
   `scenehub_control`: profile/scenario loading, SceneHub-specific scenario
   validation, safe-off policy, and other application workflow concerns belong
   there.
6. Replace store-reading session mutators with snapshot/prepared-data APIs where
   practical. `scenehub_control` should load `gm_game_profile_t`,
   `room_scenario_t`, and related metadata; `gm_core` should consume prepared
   data instead of reading storage during runtime use cases.
7. Split non-runtime storage owners out of `gm_core` when the write-side path is
   ready, especially `gm_game_profile` and GM sidebar presets, so `gm_core`
   can drop `sd_storage` and other storage-heavy dependencies.
8. After each extraction step, reduce `components/gm_core/CMakeLists.txt`
   dependencies and add regression coverage that proves command planning still
   works without transport, hardware, or storage side effects inside the core
   runtime.

### P3 - UI And Application Behavior

- Fix any remaining desktop app live-view jitter. Runtime updates should patch
  local state instead of replacing large view models on every event.
- Keep web runtime rendering incremental. Avoid returning to whole-panel
  `innerHTML` refresh for live state.
- Continue splitting large GM panel JavaScript modules only when ownership is
  clear:
  - static-data loaders vs room runtime refresh/render logic;
  - scenario/profile/device refresh helpers vs full snapshot recovery;
  - quest-device editor actions vs profile/scenario/storage actions;
  - runtime actions vs editor save/validate flows.
- Future save/validate UI actions should continue through `data-action` and
  shared API helpers.
- Consider read-only operator access to hardware diagnostics only if operator
  diagnostics need it.

### P4 - Hardware IO

- Add focused hardware IO state-machine tests with a mockable backend.
- Add scenario/GM API integration tests for local hardware commands and input
  events.
- Improve orchestrator/device snapshots with hardware IO detail fields such as
  `last_change_ms`, `last_error` and active effect state.
- Refine the Hardware IO GM view around channel labels `IO 1..4`,
  `Relay 1..4` and `MOSFET 1..4`, hiding raw board pins from normal UI.
- Add bounded relay/MOSFET/IO effect commands only after base outputs remain
  stable. Effects must be cancellable by `set`, `pulse`, `toggle`, `all_off`,
  `Stop game` and `Reset game` as appropriate.

### P5 - Command Executor

- Optional `system_led` support remains deferred.
- Define batch result policy before enabling result-required command groups:
  all-success vs any-success, first-failure behavior, per-command timeout and
  aggregate result shape.
- Do not add retry policy until command execution metrics and failure modes are
  clearer.
- Keep RS485 and ESP-NOW transports deferred until local hardware and MQTT
  command paths are stable.

### P6 - Component Layout And God Files

- Reorganize `scenehub_read_model` source layout if it continues to grow:
  views, builders/cache, and a small shared helper area.
- Reorganize `audio_player` internals only if maintenance cost justifies it;
  do not churn the Helix third-party code.
- Split large files when there is a clear ownership seam:
  `orchestrator_registry.c`, `web_ui_utils.c`, `room_catalog.c`,
  `hardware_io_io.c`, `web_ui_auth.c` and `gm_panel_08_editor_actions.js`.
- Lower-priority codec/validation splits should wait until they block feature
  work: `room_scenario_validation.c`, `quest_device_json.c` and
  `gm_panel_05a_scenario_model.js`.
- Do not move component directories into umbrella folders unless it becomes a
  dedicated migration with include path, CMake, tests and docs updated in one
  pass.

### P7 - Scenario Validation And Storage/Admin Paths

- Audit `scenehub_scenario_validation.c` for avoidable transient parse work and
  broad command lookup scans.
- Consider bounded scanners or command lookup caches for repeated
  save/validate paths, while keeping admin/import correctness first.
- Keep admin/storage handlers free of large stack-local snapshots. Prefer
  owner-held scratch or PSRAM-first temporary buffers for import/export/list
  paths instead of repeating wide fixed arrays in `httpd` call stacks.
- Keep admin/storage-heavy paths clearly separated from runtime-hot rules.

### P8 - Deferred Product Work

- Universal Node remains future work. Keep it aligned with the same Quest
  Device/device-control contract instead of creating a separate scenario model.
- ESP-NOW and RS485/MAX485 remain deferred transports. Revisit only after the
  base SceneHub controller, local hardware and MQTT paths are stable.

## Resolved

### Audio output could turn into loud noise around OTA confirmation

Observed on 2026-05-06: playback could become harsh noise/scrape around the
log line `ota_manager: OTA image confirmed`. Restarting the device cleared the
issue.

Status: resolved/mitigated. OTA confirmation now waits for `system_ready`, then
confirms after a short stabilization delay instead of waiting a fixed 30 seconds
after boot. The audio player was also hardened against timing interruptions by
keeping output writes frame-aligned, handling partial writes, writing silence
when needed, and resetting output on detected bad I2S write state.

A direct DAC/I2S reset around OTA confirmation was intentionally not used as the
primary fix.
