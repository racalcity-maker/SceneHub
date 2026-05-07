# Changelog

All notable project changes are documented in this file.

## Unreleased

### Added

- Added planning docs for local hardware IO, Universal IO Node, and the P2.2 command executor/runtime split.
- Added a `command_executor` component as the first P2.2 extraction step, routing SceneHub-native MQTT and system audio command side effects behind one executor API.
- Added GM room runtime refresh endpoint and audio path metadata warmup/cache for selected profiles.
- Added dedicated command executor backend tests for dispatch metadata, policy checks, pending results, terminal result clearing, and timeout events.
- Added the first `hardware_io` implementation slice with configurable local relay GPIO channels.
- Added built-in `system_relay` Quest Device commands: `set`, `pulse`, and `toggle`.
- Added relay module active-low configuration and defaulted the first relay set to GPIO 15-18 for the current board bring-up.
- Added built-in `system_mosfet` PWM channels with `set`, `fade`, and `pulse` commands routed through `hardware_io`.
- Added `system_mosfet all_off` and MOSFET `pulse_active` / `fade_active` status fields for Hardware IO diagnostics.
- Added the first GM Panel Hardware IO screen for built-in relay and MOSFET testing through the new frontend action/API path.
- Added `/api/hardware-io/status` so the Hardware IO screen can show relay and MOSFET GPIO/state/PWM status.
- Added explicit Hardware IO service availability/fault metadata to `/api/hardware-io/status` and the main status service block.
- Added system-level orchestrator/GM issues for service runtime faults, including `hardware_io` safe-off failures.
- Added HTTP error diagnostics with URI and heap counters for low-memory Web UI failures.
- Added a GM Panel bundle freshness checker so split frontend sources can be verified against the generated `gm_panel.js`.
- Added a memory allocation policy document for internal heap, PSRAM, DMA buffers, runtime-hot paths, and audio buffer cleanup.

### Changed

- Renamed product-facing project/configuration identifiers to `SceneHub`.
- Replaced broad project settings with `CONFIG_SCENEHUB_*` settings while keeping MQTT broker terminology for the MQTT module itself.
- Updated setup AP, mDNS, Web UI titles, default hostname, and tooling documentation to use SceneHub naming.
- Removed archived project cleanup references and old transition-plan documentation from the active project tree.
- Kept MQTT broker terminology for `mqtt_core` and kept the Helix third-party MP3 decoder as active audio functionality.
- Made the setup AP password configurable through `CONFIG_SCENEHUB_SETUP_AP_PASSWORD`.
- Completed the Quest Device command model transition from legacy `topic + payload` commands to SceneHub-native `command + args + request_id + result` envelopes.
- Split the command executor implementation into core, MQTT, audio, and result/timeout modules.
- Split Quest Device JSON codec, storage/file IO, and system-device definitions into dedicated internal modules, cutting down the `quest_device.c` god file.
- Split GM scenario Web UI handlers into runtime, editor catalog, and scenario store modules, cutting down the `web_ui_gm_scenario.c` god file.
- Split the GM panel scenario builder model/schema/default-step helpers and step payload renderers into separate source parts, cutting down `gm_panel_05_scenario_builder.js`.
- Split orchestrator Web UI API activity, control-device, and room-scenario JSON views into dedicated modules, cutting down `orchestrator_api_view.c`.
- Split GM room scenario runtime wait-state helpers into a dedicated internal module, cutting down `gm_room_session_runtime.c`.
- Routed GM session audio cleanup through the executor-backed `system_audio` stop command.
- Routed `system_relay` commands through `command_executor` while keeping external Quest Device relay commands on the MQTT backend.
- Routed `system_mosfet` commands through `command_executor` while keeping external Quest Device MOSFET commands on the MQTT backend.
- Moved Web UI cJSON allocations to PSRAM-first allocation to preserve internal heap for larger GM JSON responses.
- Updated Quest Device discovery to use `device_description` with `command`, `capability`, and `policy` metadata.
- Normalized command result handling around `accepted`, `done`, `failed`, `rejected`, and timeout semantics.
- Added shared command-result status helpers in `quest_common` for executor, ingest, runtime, and orchestrator read-model code.
- Normalized device-control result ingest before scenario runtime consumes command results, including `ok -> done` and `error -> failed` compatibility at the ingest boundary.
- Moved `result_required` command timeout tracking into `command_executor`, with runtime consuming generated `timeout` events through the same result path as device failures.
- Added explicit command executor pending-request cancellation and wired scenario wait cleanup to cancel stale result waits on stop/reset/restart/branch cleanup.
- Preserved per-command params inside `DEVICE_COMMAND_GROUP` JSON and scenario execution so grouped pulses/fades can carry arguments.
- Updated the GM Device Setup UI and the reference device-control client to use the new SceneHub-native device contract.
- Changed GM runtime action POST handlers to return lightweight accepted JSON and let the GM panel refresh only room runtime state after hot-path actions.
- Hardened command groups so `result_required` commands are rejected until batch result policy is implemented.
- Captured the Reactive Branch v2 runtime decisions and started backend support for reactive policy metadata, reentry mode, max fire count, fire counters, and trigger-conflict suppression.
- Added the first clean Reactive Branch v2 data model and JSON contract for `trigger`, `guard_flags`, `variants`, sequential `actions`, `result_policy`, and `on_complete` without requiring legacy step-chain branches.
- Added the first Reactive Branch v2 runtime path for device-event trigger matching, guard checks, variant selection, sequential action execution, wait-time continuation, command-result continuation, cooldown/listening transitions, and max-fire completion.
- Added `flag_changed` emission from scenario flag updates and wired Reactive Branch v2 result policy handling for command `failed`, `rejected`, and `timeout` statuses.
- Expanded Reactive Branch v2 runtime coverage for `queue_one`, cooldown-from-start, `on_done` result policy, fail-scenario cleanup, operator/runtime triggers, `rotate`/`escalate`, `on_complete`, and sequential command groups.
- Added the first GM scenario builder editor for creating Reactive Branch v2 branches with trigger, guards, policy, reentry, variants/actions, and result policy while keeping legacy reactive branches editable.
- Reworked the Reactive Branch v2 editor UX around `Reaction type`, `When`, `If`, and `Then`, with empty-by-default actions, escalation levels, quick action buttons, and advanced settings kept out of the main flow.
- Added collapsible Reactive Branch v2 actions and explicit `Can repeat` / `Run once` behavior for `Same actions`.
- Made `Can repeat` clear hidden max-fire limits, while `Run once` saves a one-fire limit.
- Added hardware safe-off on `Stop game` and `Reset game` for built-in relay/MOSFET/GPIO outputs. `END_GAME` intentionally keeps audio and hardware cleanup explicit.
- Moved `Stop game` / `Reset game` hardware safe-off out of the GM session lock and surface safe-off failures through `service_status`.
- Made the Hardware IO GM Panel controls disable themselves when the `hardware_io` service is unavailable or faulted instead of showing an apparently healthy empty channel list.
- Updated Scenario setup, architecture, Hardware IO, API and Known Issues documentation for Reactive Branch v2, local hardware IO and resolved OTA-audio noise behavior.
- Reworked GM Panel refresh behavior toward selective rendering: room runtime polling patches only the runtime panel, visible clocks update locally, and the manual-button sidebar skips DOM replacement unless its render key changes.
- Reworked GM Panel static-data polling to use `/api/gm/versions` for changed devices, observed clients, scenarios, and profiles instead of forcing broad GM snapshot reloads.
- Kept full GM Panel rendering as the fallback for navigation, editor save/delete, room structural changes, and unknown state transitions.
- Split GM Panel scenario editor collection so branch settings and normal step collection are handled by focused helpers instead of one large `collectScenarioEditor()` body.
- Added `json` field support to the shared GM Panel schema-form helper.
- Moved hot-path GM runtime wait/reactive scratch storage toward static PSRAM-backed buffers to reduce repeated heap allocation/free churn.
- Moved GM game-control, scenario-start and GM API session/scenario scratch storage to static PSRAM-backed buffers protected by mutexes.
- Removed the unused command-executor heap allocation helper from the internal executor API.
- Replaced command-executor flat parameter cJSON parsing with a bounded no-allocation scanner for audio and local hardware commands.
- Replaced MQTT command-envelope cJSON building with a static PSRAM payload writer that merges default/request args without heap allocation.
- Moved audio output/tone/silence buffers, WAV decode buffers, reader contexts, MP3 wrapper buffers, and Helix decoder scratch to static or bounded PSRAM/DMA storage.
- Moved Web UI auth/request buffers to shared PSRAM-first allocation helpers and replaced OTA upload chunk allocation with a static PSRAM chunk buffer.
- Moved `/api/gm/room/runtime` session scratch storage to a static PSRAM-backed buffer and stopped loading the full selected scenario inside the HTTP task before scenario start.
- Made `/api/gm/room/profile/select` lightweight by using reference validation and scenario-name lookup instead of full scenario validation/loading, and moved selected-profile audio warmup to a coalesced background event-bus job outside the HTTP task.
- Fixed cJSON hook ownership after moving Web UI JSON allocations to PSRAM: printed JSON buffers are now released through the cJSON/heap-caps allocator path.
- Changed game-mode profile HTTP handlers to use lightweight room/scenario reference validation instead of running full scenario validation inside the HTTP task.
- Changed `/api/gm/room/scenarios` to return saved scenario metadata without re-running full scenario validation for every scenario in the HTTP task.
- Replaced fixed-size `config_store` snapshot allocations with a static PSRAM scratch buffer protected by a static mutex.
- Fixed a static audio reader context race where a completed background reader could mark the channel done before releasing its fixed context, causing the next background track to fail with `reader ctx unavailable`.
- Replaced orchestrator registry snapshot scratch allocations with a shared static PSRAM scratch pool for device lists, ingest lookups, room sessions, scenario lists, and validation reports.
- Reworked orchestrator room-scenario scratch to iterate one scenario at a time instead of reserving a second full `room_scenario_t[ROOM_SCENARIO_MAX_SCENARIOS]` array in PSRAM.
- Replaced MQTT retained-message per-publish payload allocation/free with fixed retained payload storage inside the PSRAM retained table.
- Replaced MQTT broker session tables, retained table, client RX/TX buffers, accept/client task stacks, and broker mutex storage with fixed static storage.
- Moved core service/runtime mutexes in GM, command executor, Hardware IO, service status, MQTT and orchestrator modules to static semaphore storage.
- Reworked `/api/gm/room/scenarios` to build scenario editor responses one scenario at a time instead of allocating a full scenario array per request.
- Reduced the in-memory room scenario catalog limit from 24 to 12 scenarios to return about 1.3 MB of PSRAM on the current firmware shape.
- Removed per-message MQTT PUBLISH payload allocation by parsing incoming payloads in reusable per-client RX packet buffers.
- Replaced MQTT event bridge `malloc` copies with a fixed PSRAM job pool for outgoing event-to-MQTT publishing.

### Fixed

- Fixed GM game start stack pressure by avoiding duplicate full profile validation in the HTTP task and moving room-scenario validation Quest Device scratch storage to PSRAM.

## 2026-05-03

### Added

- Expanded `quest_backend` test coverage across room catalog, room scenarios, GM API, GM room sessions, orchestrator snapshots/timeline/audit/registry, event bus, service status, config store utilities, OTA manager, audio player state, web UI contracts, web UI handlers, and integration quest flows.
- Added a `quest_backend` custom partition table and default test sdkconfig so the enlarged backend test binary fits the configured flash layout.
- Added test adapters for web UI HTTP handling so request/response behavior can be tested without a live HTTP server.
- Added OTA manager backend injection hooks so OTA state transitions can be tested without real flash OTA operations.

### Changed

- Increased room scenario validation code storage so long validation codes such as `REACTIVE_BRANCH_TRIGGER_REQUIRED` are preserved without truncation.
- Tightened reactive branch validation to check the first physical branch step rather than skipping disabled steps.
- Updated system audio command validation so unsupported background audio formats are rejected before file lookup.
- Updated backend test registration and component dependencies for the broader test suite.
- Updated testing documentation paths for the current project location.

### Fixed

- Fixed the truncated validation code regression where `REACTIVE_BRANCH_TRIGGER_REQUIRED` could appear as `REACTIVE_BRANCH_TRIGGER_REQUIRE`.
- Fixed test project flash/partition configuration issues caused by the expanded test binary size.


## 2026-05-04 Audio player and Scenario Builder fixes

### Scenario Builder / GM Panel

#### Fixed branch editor DOM synchronization
- Fixed a bug where scenario branch data could be overwritten during UI re-render.
- `collectScenarioEditor()` now collects data from the branch that is actually rendered in DOM, instead of relying only on `scenarioEditor.active_branch`.
- Added `data-scenario-editor` and `data-active-branch-index` to the rendered scenario editor container.
- Scoped scenario editor DOM queries to the active editor root instead of using global `document.querySelector(...)`.
- Scoped step collection to `.scenario-steps-panel` to prevent accidental collection of unrelated `[data-scenario-step]` elements.

#### Fixed scenario branch progress rendering
- Fixed incorrect branch runtime binding when several branches had duplicate IDs.
- Branch runtime progress now prefers matching by branch index first, then falls back to branch ID.
- Fixed local/global step index conversion for branch progress display.
- Resolved visual issue where a branch appeared to advance even though runtime had not actually passed its waiting step.

#### Fixed duplicate branch IDs
- Added unique branch ID generation for new normal and reactive branches.
- New branch IDs are now generated from existing branch IDs instead of using `branches.length`.
- Added normalization protection for already-saved scenarios containing duplicate branch IDs.
- Prevented old branches from being visually overwritten or confused after adding/deleting branches.

---

### Audio Player

#### Fixed background switching fade-out architecture
- Reworked background fade-out to follow correct responsibility boundaries:
  - `runtime` controls command order and background switching.
  - `reader` continues reading PCM while fade-out is happening.
  - `mixer` only applies fade-in/fade-out and mixes PCM.
  - `output` safely writes aligned PCM to I2S.
- Removed fade-out side effects from `stop_reader()`.
- Added graceful background switching:
  - when a new background track is requested, the current background fades out first;
  - only after fade-out completes, the old reader is stopped;
  - then the new background stream is started.
- Added fade-out support for `STOP_BACKGROUND`.
- Kept global `STOP` behavior as an immediate hard stop/reset.

#### Added mixer fade-out state
- Added fade-out tracking state:
  - `s_fade_out_remaining`
  - `s_fade_out_total`
  - `s_fade_out_muted`
- Added `audio_player_mixer_fade_out_stream(...)`.
- Added `audio_player_mixer_fade_out_active(...)`.
- Updated fade-out logic so the mixer no longer stops streams by itself.
- Mixer now fades background PCM to zero and keeps muted output until runtime performs the actual stop.
- Fade-out state is now reset correctly on stream start, stream stop, and stop all.

#### Fixed fade-out hanging on empty buffers
- Adjusted mixer processing so active background fade-out can continue even when no fresh background PCM bytes are currently available.
- Prevented fade-out waits from hanging until timeout when the stream buffer becomes empty during fade-out.

#### Fixed repeated “last audio frame” issue
- Mixer now writes silence when there is no output audio data instead of leaving I2S without fresh PCM.
- This prevents the output from holding or repeating the last audio frame during fade-out, stop, or underflow-like situations.

#### Improved I2S/output stability
- Hardened audio output against partial or unaligned writes.
- Output writes are now expected to stay frame-aligned for 16-bit stereo PCM.
- Added protection against I2S partial-write situations that could leave the audio output in a bad state.
- Mixer now resets the audio output if `audio_player_output_write(...)` fails or writes an unexpected number of bytes.

#### Fixed OTA-related gray noise issue
- Fixed a failure mode where OTA confirmation or similar system activity could disturb audio output timing and leave playback in gray noise until Stop/Start.
- The player now better survives timing interruptions by:
  - maintaining frame-aligned audio writes;
  - handling partial output writes;
  - writing silence when needed;
  - resetting output on detected bad I2S write state.

#### Increased mixer buffering
- Increased background/effect stream buffer size to improve playback resilience.
- Increased mixer preroll frames to reduce risk of starvation during SD/flash/system activity.

---

### WAV edge fade / repeat playback

#### Fixed click on repeated background WAV playback
- Reworked WAV edge fade logic so fade-in/fade-out is based on absolute output frame position instead of restarting per decoded chunk.
- Fade-in is now applied only across the first edge-fade frames of the file.
- Fade-out is now applied across the final edge-fade frames of the file.
- This prevents clicks when a background WAV loops/repeats.

---

### Result

- Scenario branches no longer disappear or mix steps after UI re-render.
- New branches no longer receive duplicate IDs.
- Branch progress display now matches actual runtime state.
- Background switching now fades out correctly before starting the next background.
- Stop background now fades out smoothly.
- OTA confirmation no longer causes persistent gray noise.
- WAV background repeat no longer clicks at loop start.
