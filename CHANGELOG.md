# Changelog

All notable project changes are documented in this file.

## Unreleased

### Added

- Added a temporary checked migration plan for moving SceneHub device import,
  scenario editing and runtime dispatch to compact Node manifest v2 without
  expanding node resources into per-channel commands.
- Added dedicated GM session runtime-chaos tests for noisy wait/event flows,
  shuffled `WAIT_ALL_DEVICE_EVENTS`, duplicate command results, and late
  timeout/result ordering, plus a Python stress harness for noisy runtime
  polling and MQTT event survival checks.
- Added the first compact Node manifest v2 intake slice: Quest Device storage
  preserves `device_description`, interface discovery returns v2 manifests
  directly, Device Setup renders compact summaries instead of expanded command
  rows, and the scenario editor can render resource/effect dropdowns from
  compact templates.
- Tightened compact Node manifest identity and validation: node manifests now
  use `manifest_version`, `node_kind` and `capability_contract`, while
  SceneHub rejects expanded legacy node manifests and raw GPIO metadata at the
  device import boundary.
- Added planning docs for local hardware IO, Universal IO Node, and the P2.2 command executor/runtime split.
- Added a `command_executor` component as the first P2.2 extraction step, routing SceneHub-native MQTT and system audio command side effects behind one executor API.
- Added a `scenehub_scenario_validation` component so product/runtime-aware scenario checks can live outside the `room_scenario` model layer.
- Added GM room runtime refresh endpoint and audio path metadata warmup/cache for selected profiles.
- Added dedicated command executor backend tests for dispatch metadata, policy checks, pending results, terminal result clearing, and timeout events.
- Added backend tests that verify planned command dispatch is dropped safely after session reset/stop or branch/action desync for both normal and Reactive Branch v2 paths.
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
- Added a dedicated `scenehub_events` implementation layer with event builders, validation helpers, string helpers, and semantic match helpers.
- Added event-bus-side invalid-event rejection by default, with an explicit `CONFIG_SCENEHUB_EVENT_BUS_SKIP_EVENT_VALIDATION` escape hatch.
- Added a dedicated GM runtime event-driven migration plan and runtime inbox/deadline-timer infrastructure in `gm_core`.

### Changed

- Renamed product-facing project/configuration identifiers to `SceneHub`.
- Moved GM room runtime HTTP refresh paths off shared web scratch buffers:
  runtime summary/detail DTOs and JSON chunk buffers are now request-local, so
  slow HTTP chunked sends no longer hold shared runtime-read state during
  socket I/O.
- Hardened read-side/runtime concurrency around scratch and event flow:
  `scenehub_state` now broadcasts websocket invalidations after unlocking,
  lazy static mutex creation paths use critical sections, and
  `scenehub_read_model` scratch access now enforces lock ownership in debug
  builds.
- Lowered expected high-volume device-event matching logs from
  `INFO`/`WARN` to `DEBUG`, added critical-event fast-path routing plus drop
  counters for GM runtime queues, and fixed the scenario editor so
  `allow_operator_skip` persists as a real boolean instead of causing
  `invalid request` on save/validate.
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
- Split `scenehub_control` into focused room/profile/scenario/device modules and removed the remaining generic success path that could fall back to broad GM state invalidation.
- Completed the changed-only GM refresh transition for profile/scenario/device editor and storage/import flows. Browser GM Panel and desktop app now consume the same `gm.invalidate` websocket contract with targeted slice refresh, while explicit recovery uses `gm.resync.required` instead of overloading routine invalidation.
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
- Replaced transient `cJSON_Parse()` use in orchestrator room-runtime asset scanning and GM audio-prepare warmup with bounded `"file"` field scanners, keeping those common runtime/control helper paths allocation-free.
- Completed the P0 transport-independent backend extraction: `scenehub_control` now owns GM write-side commands, `scenehub_state` owns GM state-change notifications, public orchestrator room DTOs no longer expose `gm_room_session`, and Web UI room runtime/profile/scenario read paths now act as thin adapters over `scenehub_control` and `orchestrator_registry`.
- Completed the main P1 layer-cleanup items: `room_scenario` now exposes separate static/runtime validation entry points, `gm_core` declares its explicit `service_status` dependency, public orchestrator registry DTOs no longer expose `gm_room_session`, and event schema types are split into `scenehub_events` while `event_bus` stays transport infrastructure. The remaining P1 structural follow-up is the full read-model extraction out of `orchestrator_core`.
- Split scenario validation along the intended boundary: `room_scenario` now owns only model/static/runtime-semantic checks, while SceneHub-specific quest-device and local hardware environment validation moved to `scenehub_scenario_validation` and is consumed by `scenehub_control`, `gm_core`, and `scenehub_read_model`.
- Changed `room_scenario_validate_by_id(...)` to reuse the shared scenario scratch buffer instead of heap-allocating a full bounded scenario object.
- Moved scenario progress ownership out of browser/desktop heuristics: `gm_core` and `scenehub_read_model` now publish explicit branch counters and `steps[].state` runtime data, while Web UI and the desktop app render backend-owned `done/current/waiting/error` state instead of deriving it from step indexes.
- Closed the temporary Web UI thin-facade rollout: `web_ui` now acts as a transport/serialization layer over `scenehub_control`, `gm_core`, and `scenehub_read_model`, runtime refresh paths use narrow backend-owned semantics instead of broad GM snapshot fallbacks, and the temporary thin-facade plan was retired.
- Completed the `scenehub_events` contract migration: runtime code, tests, and `event_bus` now use SceneHub-native event types directly instead of transport-era aliases.
- Reworked GM runtime scheduling away from fixed `100 ms` polling: event progression now flows through a runtime inbox, and time-based continuation is driven by a one-shot runtime deadline timer instead of an unconditional sleep loop.
- Reorganized `room_scenario` into `json/` and `storage/` source families while keeping model/domain validation in the component root.
- Moved GM wait-event and Reactive Branch v2 device-event metadata resolution fully outside `gm_session_lock`, using pre-resolved local match data during lock-held event matching.
- Reworked `orchestrator_timeline` and `error_monitor` event-bus consumers to stay adapter-only through bounded `event_bus_post_job(...)` staging instead of dedicated service queues and task stacks.
- Replaced `/api/gm/room/runtime` cJSON response building with a bounded chunked JSON serializer that streams directly from static runtime-view scratch.
- Added a lighter `/api/gm/room/runtime?detail=summary` mode and switched multi-room GM refresh paths to it so dashboard/rooms polling no longer pulls full branch/asset runtime detail for every room.
- Trimmed duplicate scalar fields from the remaining `/api/gm/room/runtime` detail payload, removing unused wait/count metadata and other redundant runtime fields from the hot room-control response.
- Updated locking and memory policy docs to reflect the current GM scratch-lock topology, adapter-only event-bus handlers, and static scratch use in scenario-start/reactive resolve paths.

### Fixed

- Fixed GM game start stack pressure by avoiding duplicate full profile validation in the HTTP task and moving room-scenario validation Quest Device scratch storage to PSRAM.
- Fixed `FLAG_CHANGED` event emission so scenario flag updates no longer publish invalid pseudo-`DEVICE_CONTROL` events.
- Fixed MQTT text-event construction so injected MQTT events no longer rely on an implicit zeroed `payload_type`.
- Fixed GM runtime/operator UI wait labels for `WAIT_TIME`: Room Control and branch progress now show a useful time summary such as `30 sec` instead of the generic `time`.
- Fixed scenario-start `httpd` stack overflows by moving large start-path scratch and Reactive Branch v2 trigger-resolve `quest_device_t` storage off the HTTP task stack and into static PSRAM-backed scratch protected by mutexes.

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


### Added

- Added `ws_runtime` component for lightweight WebSocket runtime notifications.
- Added WebSocket endpoint `/api/ws`.
- Added WebSocket client subscription flow.
- Added common WebSocket envelope format with:
  - `type`
  - `seq`
  - `schema_version`
  - `snapshot_generation`
  - `server_time_ms`
  - `payload`
- Added `connection.ready` event after successful WebSocket subscription.
- Added `subscription.ready` event after successful subscribe request.
- Added `pong` response for WebSocket `ping` messages.
- Added `gm.versions.changed` WebSocket event for notifying clients about GM state/version changes.
- Added monotonic WebSocket versions notification generation counter.
- Added `gm_orchestrator_notify_state_changed()` as a centralized notification bridge for GM API state mutations.

### Changed

- Changed WebSocket endpoint from temporary `/ws/gm` to documented `/api/ws`.
- Changed WebSocket messages to follow the desktop app envelope contract.
- Replaced temporary `hello`/`subscribed` plain messages with envelope-based events:
  - `connection.ready`
  - `subscription.ready`
- Replaced scattered `orchestrator_registry_invalidate()` + `gm_versions_notify_if_changed()` calls with centralized `gm_orchestrator_notify_state_changed()`.
- Updated GM API mutation handlers to trigger centralized state-change notifications.

### Fixed

- Fixed WebSocket message format mismatch with desktop app expectations.
- Fixed non-monotonic `gm.versions.changed.payload.generation` values by introducing a separate increasing notification counter.
- Fixed missing WebSocket notifications for GM actions such as `Start game` by routing API state mutations through the centralized notify helper.

### Notes

- WebSocket is currently used only as a lightweight invalidation/notification channel.
- Full runtime, scenarios, devices, profiles, and other heavy data are still fetched through HTTP.
- `gm.versions.changed` does not carry full runtime data. Clients should use it as a signal to call `/api/gm/versions` and then refresh only the changed data.
Если хочешь на русском

Можно так:

# Журнал изменений

## Unreleased

### Добавлено

- Добавлен компонент `ws_runtime` для лёгких WebSocket-уведомлений.
- Добавлен WebSocket endpoint `/api/ws`.
- Добавлена подписка клиента на WebSocket-события.
- Добавлен общий формат WebSocket envelope:
  - `type`
  - `seq`
  - `schema_version`
  - `snapshot_generation`
  - `server_time_ms`
  - `payload`
- Добавлено событие `connection.ready`.
- Добавлено событие `subscription.ready`.
- Добавлен ответ `pong` на WebSocket-сообщение `ping`.
- Добавлено событие `gm.versions.changed` для уведомления клиентов об изменениях GM-состояния.
- Добавлен монотонный счётчик generation для WebSocket-уведомлений.
- Добавлена функция `gm_orchestrator_notify_state_changed()` как единая точка уведомления об изменении GM-состояния.

### Изменено

- Временный endpoint `/ws/gm` заменён на документированный `/api/ws`.
- WebSocket-сообщения приведены к контракту desktop-приложения.
- Временные сообщения `hello` и `subscribed` заменены на envelope-события:
  - `connection.ready`
  - `subscription.ready`
- Повторяющиеся вызовы `orchestrator_registry_invalidate()` и `gm_versions_notify_if_changed()` заменены на единый вызов `gm_orchestrator_notify_state_changed()`.
- GM API handlers, изменяющие состояние, теперь вызывают централизованное уведомление об изменении состояния.

### Исправлено

- Исправлено несовпадение формата WebSocket-сообщений с ожиданиями desktop-приложения.
- Исправлена немонотонность `gm.versions.changed.payload.generation`.
- Исправлено отсутствие WebSocket-уведомлений после действий вроде `Start game`.

### Примечания

- WebSocket пока используется только как лёгкий канал уведомлений.
- Тяжёлые данные — runtime, сценарии, устройства, профили — по-прежнему загружаются через HTTP.
- `gm.versions.changed` не содержит полный runtime. Клиент должен воспринимать его как сигнал выполнить `/api/gm/v
