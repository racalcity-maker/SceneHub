# Changelog

## 2026-06-17

- Hardened SceneHub Node MQTT overload behavior so terminal command results are
  no longer silently lost when both the command queue and deferred-result queue
  are saturated. The node now attempts a non-blocking overflow rejection publish
  and otherwise schedules MQTT reconnect so the hub sees a transport failure
  instead of hidden command loss.
- Changed MQTT device admission so only `QUEST_DEVICE_MAX_DEVICES` active
  SceneHub Node contract clients are accepted. A 21st new device client now
  gets CONNECT refused instead of forcing control-state eviction, while
  duplicate replacement for an existing client ID remains allowed.
- Stopped `device_control_ingest` from evicting an existing device slot on
  overflow; unexpected telemetry beyond the device limit is now refused without
  rotating the 20 active device states.

## 2026-06-16

- Added the first SceneHub Node v2 runtime-mode slice: config version `10`,
  `scenehub`/`standalone`/reserved `fallback` modes, local provisioning UI/API
  selection, status/manifest reporting, and `standalone` startup without a
  required SceneHub controller host. Rule execution remains disabled.
- Split the SceneHub Node stress test contract data into
  `tests/scenehub_node_stress_test/scenehub_contract.py`, keeping the compact
  manifest and command-case definitions separate from the MQTT runner.
- Added stress coverage for random reconnect churn, optional `boot_id`
  rotation, corrupted `/result` packets, and strict `describe_interface`
  manifest structure checks.
- Added optional fault-injection flags for slow, missing or interrupted command
  results: `--drop-results-rate`, `--delay-results-ms`, and
  `--disconnect-during-command-rate`.
- Added an optional separate controller MQTT command publisher for future ACL
  testing. The default remains `--command-publisher self` because the current
  broker ACL permits each virtual node to publish only inside its own
  `cp/v1/dev/<node>/...` namespace.

## 2026-06-15

- Expanded the SceneHub Node stress test with real-world stability phases:
  input-event scenario traffic, reconnect waves, duplicate-client replacement,
  slow-node isolation and optional soak traffic via `--soak-seconds`.
- Fixed an MQTT session teardown race exposed by reconnect and duplicate-client
  stress phases. A session slot is no longer marked reusable until its TX mutex
  has been released, preventing the accept task from reinitializing the static
  session storage while the old client task is still finishing cleanup.
- Fixed a GM hint HTTP handler lifetime bug: `room_id` and `message` are now
  copied out of the parsed JSON object before `cJSON_Delete()`, preventing
  use-after-free when dispatching the hint command and logging performance.
- Hardened the WebSocket runtime envelope path: string fields are now JSON
  escaped with a bounded writer, websocket sends no longer run while holding the
  runtime state mutex, envelope buffers are local to each send and
  `snapshot_generation` now reflects the monotonic SceneHub websocket state
  generation instead of remaining a constant placeholder.
- Fixed MQTT session slot reuse so allocation no longer clears the embedded
  FreeRTOS static TX mutex storage. Reconnect and duplicate-client waves now
  preserve the per-slot mutex object while resetting only runtime session
  fields, avoiding a rare crash during rapid session replacement.
- Added a short MQTT session slot retirement window after client cleanup so a
  static task stack/TCB slot is not reused while the old client task is still
  returning into `vTaskDelete()`. This hardens duplicate-client reconnect waves
  where old and replacement sessions overlap for the same client ID.
- Changed MQTT duplicate-client replacement so sessions already marked closing
  or retiring no longer claim their `client_id`, and closing a session clears
  its subscriptions immediately. Replacement clients can now become the active
  owner of the client ID without waiting for the old task to finish cleanup.
- Increased the configured MQTT session limit from 24 to 25 so the target
  20-node load can survive a five-node reconnect wave with one replacement slot
  per reconnecting client instead of forcing the last duplicate through the
  full-pool eviction path.
- Updated the SceneHub Node stress reconnect phase to use a fresh MQTT client
  object after each intentional transport disconnect. This matches real device
  reconnect behavior more closely and avoids Paho client state from masking
  broker-side CONNACK/SUBACK handling.
- Fixed broker QoS 1 retry accounting under send-buffer pressure. Zero-byte
  send timeouts now defer the retry without consuming the acknowledgement
  budget, while partial TCP writes close the session immediately because the
  MQTT stream framing can no longer be safely retried from the beginning.
- Changed incoming QoS 1 PUBLISH handling to send PUBACK as soon as the broker
  accepts the packet, before injecting and forwarding it to subscribers. This
  prevents a same-session publish/subscribe burst from filling the socket with
  outbound QoS 1 traffic while the client's inbound QoS 1 publishes are still
  waiting for acknowledgement.

## 2026-06-13

- Added `tests/scenehub_node_stress_test`, a configurable MQTT stress client
  that emulates 20 SceneHub Node v1 devices named `scenehubnode_1` through
  `scenehubnode_20`. Each virtual node exposes four relays, four MOSFET
  outputs, four inputs, four universal outputs and two addressable LED strips.
- Added stress phases for valid commands, invalid and boundary payloads,
  duplicate request IDs, command-queue pressure, post-stress health checks and
  simultaneous `describe_interface` responses. Missed interface descriptions
  are retried sequentially so the test distinguishes temporary large-message
  congestion from a disconnected or non-responsive node.
- Increased `CONFIG_LWIP_MAX_ACTIVE_TCP` from `24` to `36` so the configured
  20 MQTT node sessions can coexist with the SceneHub web UI and a small number
  of browser connections.
- Increased `CONFIG_SCENEHUB_MQTT_MAX_CLIENTS` from `20` to `24` so a 20-node
  run has spare MQTT session slots for reconnects and duplicate-client
  replacement while old sessions are still closing.
- Added duplicate-client eviction while the MQTT session pool is full. The
  accept loop can now read the incoming CONNECT client ID, close the stale
  session with the same ID and let the reconnect retry claim the freed slot,
  instead of rejecting the reconnect indefinitely with `too many clients`.
- Completed broker-side MQTT QoS 1 delivery for active clean sessions. The
  broker now accepts client `PUBACK` packets, tracks up to 64 unacknowledged
  outgoing messages per client, retains packet bytes in PSRAM while awaiting
  acknowledgement, retries after two seconds with the MQTT `DUP` flag, frees
  acknowledged messages and logs bounded-queue overflow without closing the
  MQTT session.
- Added a 512 KB global bound for pending broker QoS 1 packet storage and
  cleanup of pending packets when an MQTT session closes.
- Added `mqtt_core_publish_qos()` while preserving `mqtt_core_publish()` as the
  QoS 0 compatibility API. SceneHub device commands, including
  `describe_interface`, now use QoS 1; ordinary internal event bridging remains
  QoS 0.
- Changed SceneHub Node v1 command results to publish with QoS 1. Command
  subscriptions remain QoS 1, while heartbeat, status and input-event
  telemetry remain QoS 0.
- Updated the node stress client to use QoS 1 for commands and command results
  by default, including QoS 1 subscriptions in both directions. Added
  `--command-qos 0` for comparison with the previous command path and bounded
  the complete queue-pressure result wait to one shared timeout.
- Added MQTT core unit coverage for matching, malformed and unknown `PUBACK`
  handling. Python syntax and command-line argument checks were run for the
  stress client; firmware and ESP-IDF test builds were intentionally not run.
- Changed the node stress test to keep all emulated clients connected after the
  stress summary. The interactive console logs incoming MQTT commands/results
  and can publish `input.changed` events for any of the four inputs on one node
  or all nodes, including pulse and four-input state helpers. Added
  `interactive_commands.txt` with ready-to-copy command variants and
  `--no-hold` for non-interactive runs.
- Fixed a broker QoS 1 concurrency race found by the 20-node stress log:
  `PUBACK` processing could free a pending packet while another task was still
  sending or retrying it. ACK cleanup and retry inspection now use the session
  TX lock, preventing packet use-after-free and the resulting reconnect storm.
  Late or duplicate `PUBACK` packets are accepted as debug diagnostics instead
  of warnings.
- Made QoS 1 delivery tolerant of temporary send-buffer pressure: initial sends
  and retries now keep the packet pending and defer delivery instead of
  immediately closing the MQTT session on the first send timeout. QoS 1
  backpressure no longer waits for a free slot inside a client session task,
  avoiding self-delivery deadlocks where the same task must process the
  `PUBACK` that frees the slot. Absolute per-client inflight overflow is logged
  and the outbound publish is dropped rather than forcing a reconnect loop.
- Bounded QoS 1 send deferral. A session now closes after repeated deferred
  retry sends so a dead or non-draining socket cannot stay alive indefinitely
  and keep retrying the same packet dozens of times.
- Limited each QoS 1 retry pump pass to one due packet so an MQTT session task
  does not spend a long burst inside retry sends and starve the receive path
  that must process incoming `PUBACK` packets.
- Throttled broker QoS 1 backpressure warnings so overload diagnostics report
  suppressed repeats instead of flooding the serial log.
- Reduced interactive stress-test log volume. Hold mode now logs incoming
  commands only by default, truncates payload text to 240 characters, and
  exposes `--log-results` plus `--log-payload-bytes` for deeper diagnostics.
- Made the node stress client's receive callback tolerant of non-UTF8 MQTT
  topic bytes, recording the malformed packet instead of letting the Paho
  network thread die.
- Changed the node stress client to stop subscribing each emulated node to its
  own `/result` topic by default. Results are now considered observed when the
  broker PUBACKs the QoS 1 result publish, which matches real node behavior
  more closely and removes a large artificial broker-to-node echo load. Added
  `--subscribe-results` to reproduce the older heavier mode.
- Moved SceneHub Node v1 overflow results onto a bounded result queue. When the
  four-command queue is full, the MQTT callback now enqueues a terminal
  `rejected/busy` result and returns quickly; the command worker publishes that
  result with bounded retries. This keeps the real queue limit while preventing
  queue-pressure tests from losing terminal command results silently. The
  Python stress node mirrors the same result-queue behavior.
## 2026-06-03

- Fixed Wi-Fi config migration after the RAM-only network recovery change:
  `config_store` now migrates v1 NVS config to v2 instead of falling back to
  empty defaults, and the hub performs a one-time import of legacy ESP-IDF
  driver STA credentials into `config_store` before switching the driver to
  RAM-only storage.
- Made the maximum room slot count a SceneHub build-time setting:
  `CONFIG_SCENEHUB_MAX_ROOMS` now defaults to `1`. The room catalog,
  GM session slots, prepared event-ref snapshots and room read-model limits
  inherit this value, reducing default static PSRAM use for the current
  single-room product shape while keeping multi-room builds configurable.
- Added the active audio pipeline refactor plan for pop-free background/effect
  playback and tracked the issue as an active P1 runtime defect.
- Added Phase 0 audio diagnostics for output state transitions, source
  start/primed/finish/stop/fade events, stream starvation, partial stream reads,
  I2S enable/disable/reset, and MP3 first-frame/first-PCM timing.
- Simplified WAV decode ownership: WAV readers now only parse/convert PCM and
  no longer apply their own edge fades, leaving source fade behavior to the
  mixer/source lifecycle.
- Made audio output start explicit: low-level I2S writes no longer enable
  output implicitly, and the mixer now performs `idle -> priming -> running`
  with bounded DMA preload before enabling output on idle start.
- Added dual background mixer slots for gapless background switching: the next
  background track is decoded into an inactive fixed PSRAM stream slot and must
  reach the primed PCM window before the old background is faded/stopped. The
  inactive slot stays muted while priming and becomes audible only after the
  old background fade-out reaches zero.
- Fixed mixer source EOF handling so an audible source with buffered tail PCM
  stays under mixer source ownership. Long buffered tails now drain normally
  and receive the bounded tail fade only at the final fade window instead of
  being faded immediately at decoder EOF.
- Tightened background switch routing so the runtime treats active mixer
  background slots as live sources, not only reader task handles. This prevents
  a live background from being misrouted as a cold start if runtime task state
  and mixer source state drift apart.
- Reduced audio diagnostics pressure on the live mixer path: source-primed
  logging is now emitted only on the actual unprimed -> primed transition, and
  per-reader slot route details are debug-only.
- Fixed frame alignment for audio stream-buffer writes. Mixer writers now send
  only chunks that fit in frame-aligned free space, preventing a full muted
  priming buffer from accepting 1-3 stray bytes and shifting subsequent PCM
  into digital noise.
- Made source fade-in signal-aware so MP3 encoder delay or leading near-silence
  does not consume the whole fade budget before the first audible transient.
- Made normal audio stop paths graceful: `Stop game`, background stop and effect
  stop now request mixer fade-out and no longer perform an I2S/MAX98357A reset
  during ordinary playback shutdown.
- Made effect replacement graceful: starting a new effect now gives the current
  effect stream a short fade-out before the stream is stopped.
- Added one-shot mixer diagnostics for the first audible MP3 effect block,
  including raw first samples, peak level, fade state and reader timing; added
  a second one-shot diagnostic for the first non-silent effect block where
  fade-in actually starts consuming its budget.
- Reduced live-mixer logging after field diagnostics showed identical MP3
  signal-start PCM for popping and non-popping gong runs. Source-primed,
  partial-read and first-effect-block diagnostics are now debug-level so INFO
  logging does not block the audio-critical mixer loop.
- Moved all remaining `audio_player` informational logs to debug level,
  including output lifecycle, source lifecycle, WAV/MP3 open/first-frame timing
  and volume-load messages. Audio warnings and errors remain visible.

## 2026-06-01

- Completed the `gm_core` decomposition baseline. Runtime ownership is now
  limited to room-session state, timer/progression, waits, flags, reactive
  state and command plans; profile/scenario/sidebar persistence, game
  orchestration, safe-off policy and command execution are owned outside
  `gm_core`.
- Removed the retired `gm_api` / `gm_control` write facades, unsupported
  compatibility wrappers, `gm_legacy_compat`, and the old active
  `GM_CORE_DECOMPOSITION_PLAN.md` after all slices were completed and the
  durable architecture docs were updated.
- Added prepared runtime inputs for scenario start. `scenehub_control` now
  resolves the complete event-ref catalog before entering the session runtime,
  and `gm_core` copies that catalog into bounded PSRAM snapshots for normal
  waits, wait-any/wait-all, and reactive triggers. Runtime matching no longer
  performs Quest Device catalog lookups or accepts accidental compact id
  variants such as `@1`.
- Centralized build-time defaults in the header-only `scenehub_config`
  component, removed its placeholder anchor source file, added explicit
  consumer dependencies, and kept mutable runtime settings in `config_store`.
  The web-auth bootstrap password now reads the actual
  `CONFIG_SCENEHUB_WEB_AUTH_DEFAULT_PASS` Kconfig symbol.
- Tightened hub network recovery policy: setup AP now starts as pure AP,
  STA connection is enabled only for the saved-config/apply-config paths, and
  ESP-IDF Wi-Fi storage is RAM-only so stale driver-owned credentials cannot
  make setup mode auto-connect STA and then schedule AP shutdown. The old
  web-auth-only reset GPIO monitor was removed; `system_reset_policy` is the
  single owner of reset/setup button behavior.
- Retired the stale standalone
  `GM_REACTIVE_BRANCH_FRONTEND_CLEANUP_PLAN.md`. The remaining actionable
  reactive-editor cleanup is tracked in `KNOWN_ISSUES.md` instead of a long
  outdated phase plan.
- Removed stale `SCENEHUB_CONTROL_DISPATCH_PLAN.md` and `gm_api_contract.md`.
  The dispatch-owner baseline is complete, HTTP method/error policy remains in
  `API_HTTP_POLICY.md`, and the remaining work is to generate a compact current
  route reference from Web UI handlers instead of preserving an outdated
  hand-written GM API snapshot.
- Documented `scenehub_control` as the next architecture risk after the
  `gm_core` cleanup: it remains the write-side application facade, but future
  API growth should split by family instead of turning the umbrella header into
  a new god-module.
- Documented `gm_room_session.h` as the remaining broad `gm_core` public
  boundary. Future work should split control entrypoints, view DTOs,
  command-plan port types and shared enums when there is real caller pressure,
  rather than expanding one runtime umbrella header.
- Documented the `scenehub_control` dispatch owner invariant in code and lock
  policy: synchronous queue/notification wrappers must not be called while
  holding GM session locks, from event-bus hot paths, or from the owner task
  itself.
- Clarified the GM profile naming boundary in docs: the component is
  `gm_profile_store`, while the public persisted model remains
  `gm_game_profile_t`.
- Audited docs for stale completed plans. Reactive Branch v2 runtime behavior
  and hub network recovery policy now live in durable reference docs; the old
  standalone plan files were removed.

## 2026-05-26

- Hardened local web auth without adding heavy hashing: admin bootstrap now
  uses salted `SHA-256(salt || password || device_id)` storage, tracks
  `password_initialized`, forces the first password change after bootstrap
  login, keeps `GM` / normal admin surfaces blocked until the password is
  replaced, and sends normal admin login back to `/gm` instead of the legacy
  admin panel.
- Documented and implemented the hub write-side dispatch-envelope model for
  manual device commands. Successful async remote dispatch now returns
  `status=accepted` plus `request_id`, and audit/timeline correlation uses that
  same key instead of pretending immediate terminal success.
- Added one shared `scenehub_control` dispatch owner for async write-side
  transport work. `describe_interface` and manual GM device-command execution
  now share the same owner task and owner-held scratch instead of per-endpoint
  workers or dedicated stacks.
- Added `COMMAND_RESULT_SEMANTICS.md` as the durable baseline for
  `done/started/accepted/failed/rejected/timeout`, request-id correlation, and
  append-only audit/timeline expectations.
- Added `API_HTTP_POLICY.md` and moved mutable controller endpoints away from
  state-changing `GET`. Wi-Fi, MQTT and logging config now use `POST` request
  bodies instead of query-string mutation payloads.
- Reworked hub network recovery policy: setup AP is now requested only by
  holding the reset/setup pin during boot, runtime long hold restores defaults
  and reboots, failed STA startup no longer auto-enables AP after retry
  exhaustion, and an empty Wi-Fi config still opens setup AP for default-state
  recovery.
- Hardened SceneHub Node provisioning exposure: normal provisioned boots now
  auto-close the provisioning HTTP surface after five minutes unless the
  current boot explicitly requests `Keep setup open`; first-time
  `provisioning_required` boots stay open.
- Hardened SceneHub Node setup AP posture: provisioning now uses WPA2-PSK
  credentials derived from the device MAC instead of an open AP.
- Continued the Node v1 architecture cleanup by splitting the god files
  `node_control`, `node_hw_led`, and `node_config`, and by routing provisioning
  write/reset/apply/restart through `node_admin_control`.
- Switched embedded GM live refresh further toward WS-first behavior:
  background `versions` and active-room runtime polling are now suppressed
  while the WebSocket is healthy, and `/api/gm/room/runtime` defaults to
  `include_assets=0` unless a caller explicitly asks for asset readiness
  detail.
- Enforced `requires_confirmation` on the manual HTTP/device-control path:
  commands that carry `requires_confirmation=true` now require
  `"confirmed": true` in `POST /api/gm/device/command/run`, while
  `danger_level` remains UI/log metadata.

## 2026-05-18

- Split the overloaded embedded GM Panel source part
  `gm_panel_07_loaders_and_runtime_actions.js` into narrower families for
  static/view loading, room runtime refresh, version/invalidation refresh, and
  runtime/manual actions. The behavior is unchanged, but the split sources are
  easier to maintain and now align better with the intended ownership seams.
- Reworked the embedded GM Panel operator flow around `Rooms`: removed the
  `Dashboard` navigation entry, made `Rooms` the default view, and kept device
  quick actions in the right sidebar instead of a broad all-device overview.
- Turned the embedded `Devices` area into an admin-side `Device Controls`
  screen with a mini wizard for sidebar quick actions. Admins now choose a
  saved device, a concrete resource/channel, an action/effect, optional fixed
  params, and a human-readable operator label.
- Replaced the old auto-generated sidebar manual-button dump with curated
  quick-action presets. The live sidebar now shows only configured operator
  presets, grouped by device, while danger/confirmation behavior still follows
  the saved Quest Device command policy.
- Reused the existing compact/system device normalization path so built-in
  relay/MOSFET/IO devices and compact SceneHub Node manifests expand into
  concrete resources instead of collapsing to one template-level button.
- Added browser-local persistence for embedded GM sidebar presets through
  `localStorage` on the controller host, avoiding any firmware API or SD-card
  storage change in this slice.
- Fixed the embedded quick-action wizard so resource/action dropdowns populate
  correctly for compact and system devices, and tightened the admin preset UX
  into a denser step-card layout with live preview updates and better default
  selection flow.
- Hardened embedded GM live refresh against invalid `/api/gm/state` snapshots:
  the panel now keeps the last usable room state instead of replacing it with
  an incomplete payload, and websocket invalidation refresh now logs and
  exposes the underlying error message instead of a generic ws failure badge.
- Fixed the embedded GM bundle manifest after splitting `gm_panel_07`: the
  actual CMake-side `GM_PANEL_PARTS` list now includes `07b/07c/07d`, so the
  firmware-embedded `gm_panel.js` matches the locally rebuilt bundle and no
  longer drops runtime refresh functions such as
  `refreshGMByInvalidationSlices`.
- Restored embedded GM runtime helper compatibility after the panel split:
  room/scenario runtime text paths still referenced the legacy
  `questDeviceCommandName` / `questDeviceEventName` helpers, so opening a room
  after scenario selection could fail during render. The compatibility wrappers
  now map those calls to the current scenario helper names.
- Moved embedded GM sidebar quick actions out of browser-local `localStorage`
  into controller-backed SD storage at `/sdcard/quest/gm_sidebar_presets.json`.
  The GM panel now loads presets through dedicated backend APIs, supports
  export/import/load flows, and offers one-time import of legacy browser
  presets into controller storage.
- Added controller-side validation for sidebar presets and tightened shared
  command param validation so flat/system commands are checked alongside
  compact manifest commands. This closes the gap where presets or scenarios
  could save `system_audio.play` without `params.file` or other required
  command arguments and only fail later at runtime.
- Hardened scenario save on the backend: scenario payload saves now run through
  scenario validation before persistence, so invalid command params are
  rejected on save instead of being stored and discovered only after start.
- Split `components/gm_core/sidebar/gm_sidebar_presets.c` into smaller sidebar
  preset source parts for core state/validation, JSON import-export, and SD
  storage persistence. This keeps the new controller-backed quick-action store
  out of god-file territory and removes an unused helper warning from
  `scenehub_control_sidebar_presets.c`.
- Moved the long-lived GM sidebar preset table out of internal RAM into PSRAM
  and removed `GM_SIDEBAR_PRESET_MAX_ITEMS` stack snapshots from the
  list/export/import JSON path. This recovers roughly 21 KB of internal RAM
  and removes a likely `httpd` panic source when the GM panel loads quick
  actions.
- Audited DTO/buffer ownership boundaries: the inventory now covers persistent
  store entities such as `gm_sidebar_preset_t`, explicitly flags the wide
  `device_control_ingest_device_t` snapshot as a watch item, and records
  follow-up backlog work for narrow accessor APIs and admin/storage stack
  discipline.
- `Start game` room-action failures now preserve and surface scenario
  validation causes instead of collapsing to a generic `invalid_request`.
  Failed game start validation writes `scenario_last_error`, `scenehub_control`
  maps it to `scenario_invalid`, and the web JSON error response now includes a
  human-readable `message` field.
- Fixed an embedded GM scenario editor regression where the scenarios view
  could open a summary-only scenario entry as if it were full detail,
  collapsing the editor to an empty `Main` branch. The panel now treats
  `Scenarios` as a full-detail cache requirement and fetches layout detail for
  a scenario before opening the editor when needed.
- Moved `gm_control` into `gm_core/control` and folded its public header into
  `gm_core/include`. The old standalone `gm_control` component is gone; the
  `room_unhealthy` start-game preflight now lives in `scenehub_control`, which
  avoids creating a new `gm_core <-> scenehub_read_model` dependency cycle.

## 2026-05-16

- Completed the compact SceneHub Node manifest v2 migration. SceneHub now
  treats Node devices as compact resources plus command/event templates,
  preserves `device_description.manifest_version=2` without per-channel command
  expansion, and keeps scenario `command_id` equal to the command template id.
- Updated Device Setup, scenario editor rendering, runtime command resolution,
  read-model catalog DTOs and the reference Python device client for compact
  Node manifests. Flat `commands[]` / `events[]` remain only as the custom
  Quest Device path, not as a SceneHub Node compatibility layer.
- Started the compact Node manifest v2 migration: SceneHub now preserves raw
  `device_description` data and the GM panel shows compact node summaries
  instead of expanding node resources into command rows.
- Added strict compact Node manifest identity validation and rejected GPIO
  metadata at the SceneHub import boundary.
- Split scenario editor layout streaming out of
  `components/web_ui/orchestrator/web_ui_orchestrator.c` into
  `orchestrator_scenario_layout_writer.*`. The endpoint contract is unchanged,
  but the HTTP handler no longer embeds the large layout JSON writer.
- Started consolidating temporary planning documents into `KNOWN_ISSUES.md` so
  active work is tracked in one place and completed plan history does not keep
  accumulating.
