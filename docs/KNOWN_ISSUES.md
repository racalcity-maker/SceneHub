добавить к нодам возможность кастомного джейсон, и при этом возможность работать автономно без хаба
выставив настройку в интерфейсе либо кастомный джейсон на автомате либо через сценхаб
# Known Issues


This file is the single active backlog for open product, runtime and
architecture issues.

Policies and durable rules live in dedicated policy/reference documents:

- `ARCHITECTURE.md`
- `LOCKING_POLICY.md`
- `MEMORY_ALLOCATION_POLICY.md`
- `API_HTTP_POLICY.md`
- `device_control_contract_v1.md`
- `reactive_branch_v_2_design.md`

Temporary plan files were consolidated here. Closed work from those plans is
not repeated; only unresolved risks and deferred work are tracked below.

## Active Backlog

### P0 - Correctness And Runtime Safety

No active P0 defects are currently tracked.

Closed P0 baseline lives in `docs/HUB_AUDIT_P0_P1_PLAN.md`,
`docs/LOCKING_POLICY.md`, `docs/COMMAND_RESULT_SEMANTICS.md`, and
`docs/device_control_contract_v1.md`. Reopen P0 here only for a concrete
runtime correctness bug, unsafe lock/IO regression, or broken command-result
ordering.

### P1 - Runtime Performance

Active P1 defects:

- Audio playback can pop/click when starting background audio, switching
  background tracks, or starting MP3 effects over active background audio. This
  is tracked in `docs/AUDIO_PIPELINE_REFACTOR_PLAN.md`. The target model is a
  mixer-owned output lifecycle, continuous I2S writer behavior while running,
  and source priming before a track/effect becomes audible.

Remaining verification work:

- Run larger device-count / heartbeat-noise testing against the current
  WS-first runtime refresh, narrowed invalidation, and `include_assets=0`
  default runtime detail.
- After that test, decide whether command-result waiters, flag waiters or
  device-event waiters need indexes. Do not add index structures without a
  measured broad-scan problem.
- Recheck `/api/gm/state` after frontend/runtime stabilization. It should stay
  bootstrap, structural refresh or recovery only, not the normal live-runtime
  path.

### P2 - Architecture Boundaries

Open architecture work:

- Measure prepared event-ref snapshot copy time during scenario start on the
  target board. The current bounded static PSRAM model avoids heap churn and
  runtime lookup, but copies approximately `104 KB` under `gm_session_lock`
  when committing a scenario start.
- Replace the transitional scenario-layout path with a compact read-model DTO.
  The public lookup path is now read-model based, but the isolated layout
  writer still serializes from `room_scenario_t`.
- Narrow `device_control_ingest` consumers where operational paths only need
  focused questions such as presence, result summary or command availability.
  The full ingest snapshot should remain diagnostic/read-side data.
- Watch `scenehub_control` as the next center of gravity after the `gm_core`
  cleanup. It is acceptable as the write-side facade, but future growth should
  split public APIs by family instead of expanding one broad umbrella header.
- Watch `gm_room_session.h` as a remaining broad public runtime header. It
  still groups control entrypoints, view DTOs, command-plan ports,
  prepared-start DTOs and runtime-shaped structs; future work should split it
  before adding more public surface.
- Add isolated domain tests for GM wait/reaction/state transitions, command
  planning without transport/hardware, `scenehub_events`, and
  `room_scenario` validation.
- Introduce fakeable ports where they reduce test cost: time, event post,
  command dispatch, and prepared runtime inputs.
- Regenerate a compact current HTTP API reference from the Web UI route table
  and handler contracts. The old broad `gm_api_contract.md` was removed because
  it lagged behind the implementation and still described retired `gm_control`
  paths.

### P3 - UI And Application Behavior

No active P3 UI defect is currently tracked.

Deferred UI hygiene:

- Remove remaining legacy reactive first-step-trigger UI assumptions from the
  GM Panel editor. Live code should treat reactive branches as Reactive Branch
  v2 only (`trigger`, `guard_flags`, `policy`, `variants[]`,
  `result_policy`) and keep any old `steps[]` compatibility at explicit
  import/normalization boundaries.
- Continue splitting GM panel JavaScript only when ownership is clear:
  static-data loaders, room runtime refresh/render, scenario/profile/device
  refresh, quest-device editor actions, runtime actions, and editor
  save/validate flows.
- Add read-only operator hardware diagnostics only if an operator workflow
  actually needs it.

### P4 - Hardware IO

Remaining hardware IO work:

- Add focused hardware IO state-machine tests with a mockable backend.
- Add scenario/GM API integration tests for local relay, MOSFET and IO commands
  plus input events.
- Expose more hardware IO detail through read-model/operator views only if a
  real operator workflow needs it. The current GM view already has channel
  labels, basic status, effect-active state and bounded operator commands.

### P5 - Command Executor

Remaining command-executor work:

- Define batch result policy before enabling result-required command groups:
  all-success vs any-success, first-failure behavior, per-command timeout and
  aggregate result shape.
- Keep retry policy out until command execution metrics and failure modes are
  clearer.

### P6 - Component Layout And God Files

- `scenehub_config` is now the shared build-time config component. Keep it
  limited to compile-time defaults; do not let it grow into a runtime settings
  owner or service locator.
- Split the broad `scenehub_control.h` public surface into family headers when
  the next real caller pressure appears: GM/session, scenarios, devices,
  profiles, sidebar presets and hardware IO. Avoid churn until there is a
  concrete consumer or dependency problem.
- Split the broad `gm_room_session.h` public surface when useful into control
  entrypoints, view DTOs, command-plan port types and shared runtime enums.
  Avoid a mechanical split until a caller or dependency cleanup benefits from
  it.
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

### P8 - Node Describe Interface Size Budget

Large `describe_interface` handling was partially refactored: the large
metadata response no longer lives in ordinary steady-state ingest result
storage. Remaining work is tracked in
`docs/NODE_DESCRIBE_INTERFACE_REFACTOR_PLAN.md`:

- Review whether current MQTT transport ceilings are still appropriate after
  the metadata split.
- Keep `quest_device.device_description_json` as the only durable manifest
  owner after save/import flows.
- Decide whether the transient metadata cache should stay inside ingest or move
  further toward a narrower control-owned pending-response path.
- Design compact runtime manifest vs rich UI metadata only if node channel
  counts or LED/effect catalogs keep growing.

### P9 - Deferred Product Work

- Optional `system_led` support remains deferred.
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
