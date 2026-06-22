# Memory Allocation Policy And Audit

SceneHub runs on ESP32-S3 in a control role. Runtime behavior must be
predictable, so memory allocation policy is part of the architecture, not just
an optimization.

## Policy

- No dynamic allocation in hot runtime paths:
  - scenario tick;
  - event matching;
  - command execution;
  - local hardware IO actions;
  - audio output/mixer/write loops.
- Prefer static storage, fixed pools or one-time startup allocation for data
  that has a known maximum size.
- Prefer PSRAM for large non-DMA buffers.
- Keep small DMA/internal buffers in DMA-capable internal RAM when hardware
  requires it.
- HTTP, JSON import/export, OTA and admin storage operations may allocate, but
  they must fail cleanly and should prefer PSRAM.
- Any remaining allocation in a user-facing runtime path must be documented
  here before it is accepted.

## Memory Classes

| Class | Allocation Rule | Examples |
| --- | --- | --- |
| `startup-static` | Allocate once during init or use static storage | event bus pool, GM session table |
| `runtime-hot` | No malloc/free/cJSON allocation | scenario tick, event match, hardware commands |
| `game-control` | Avoid allocation; fixed scratch/pool preferred | start/stop/reset game, scenario start |
| `audio-critical` | No allocation while streaming/mixing/outputting | mixer, I2S output, decode loop |
| `audio-start` | One-time per track allocation should be removed where practical | reader context, decode buffers |
| `http-admin` | Allocation allowed, PSRAM-first, clean 4xx/5xx failure | GM panel JSON, scenario save/validate |
| `storage` | Allocation allowed, PSRAM-first, clean failure | import/export/load/save |
| `ota` | Allocation allowed but should become static/fixed chunk buffer | OTA upload chunk |

## Current State

Already improved:

- `event_bus` uses a PSRAM-backed fixed message pool.
- `gm_room_session` stores room sessions and prepared scenario event-ref
  snapshots in static PSRAM.
- `command_executor_execute()` reuses PSRAM-backed `quest_device_t` and
  `quest_device_command_t` scratch instead of allocating per command.
- `command_executor_execute()` reads audio/hardware params with bounded
  no-allocation scanners.
- `command_executor_mqtt.c` builds MQTT command envelopes in a static PSRAM
  payload buffer instead of cJSON merge/print.
- `mqtt_core` uses one reusable RX packet buffer per client slot and a fixed
  bridge job pool instead of allocating per incoming PUBLISH or outgoing event.
- `mqtt_core` owns broker session tables, retained table, client RX/TX buffers,
  accept/client task stacks and the broker lock as fixed static storage.
- `orchestrator_timeline` and `error_monitor` keep event-bus handlers
  adapter-only and defer follow-up work through bounded fixed job staging
  instead of dedicated per-service queues and task stacks.
- Core runtime/service locks in GM session, command executor, hardware IO,
  service status and orchestrator registry/audit/timeline use static semaphore
  storage instead of heap-backed mutex allocation.
- `gm_room_session_runtime_process_pending_work()` uses static PSRAM timeout
  event scratch.
- `scenehub_control` resolves the complete scenario event-ref catalog before
  runtime start with reusable static PSRAM scratch. `gm_core` copies the
  prepared catalog into the target room slot under the session lock. Runtime
  wait entry and Reactive V2 trigger matching only read that local snapshot.
- GM game-control paths use static PSRAM session/scenario/validation scratch
  protected by a mutex instead of allocating for start/reset/select actions.
- Scenario start uses static PSRAM scenario/validation scratch and reads the
  selected scenario id under the session lock instead of allocating temporary
  session/scenario/report objects.
- GM API room-state paths use static PSRAM session scratch protected by a
  mutex instead of per-call heap allocations.
- `scenehub_state` invalidation coalescing and `ws_runtime` websocket envelope
  broadcast paths are allocation-free and use bounded fixed-size payload
  buffers.
- `device_control_ingest` no longer allocates a transient full device snapshot
  per MQTT message just to publish event-bus updates; it now captures a small
  local event DTO and uses static mutex storage.
- `device_control_ingest` handles `heartbeat/status/diag` with a bounded JSON
  scanner, and `result/event` now use the same bounded parse path while still
  preserving raw `data/args` JSON slices for downstream consumers.
- Profile selection is reference-only in HTTP: it checks room/scenario linkage
  and copies the scenario name without loading or validating the full scenario.
  Audio path warmup is allowed, but it must run as a coalesced background job
  outside `/api/gm/room/profile/select`, not synchronously on the HTTP task.
- `/api/gm/room/runtime` is read-only; scenario advancement is owned by the
  runtime task, not HTTP GET handlers.

## P0 - Runtime And Game-Control Allocations

Goal: no dynamic allocation in scenario/event/command hot paths, and no large
alloc/free churn in common game control actions.

- [x] Remove per-command `quest_device_t` / `quest_device_command_t` allocation
  from `components/command_executor/command_executor.c`.
- [x] Remove runtime tick stack timeout buffer from
  `components/gm_core/session/gm_room_session_runtime.c`.
- [x] Remove wait-event `quest_device_t` allocation from
  `components/gm_core/session/gm_room_session_runtime_wait.c`.
- [x] Remove Reactive V2 trigger-match `quest_device_t` allocation from
  `components/gm_core/session/gm_room_session_reactive_v2.c`.
- [x] Audit and reduce `gm_room_session_game.c` allocations for start/stop/reset
  game paths.
- [x] Audit and reduce `gm_room_session_runtime.c` scenario start allocations
  for scenario/report/session scratch.
- [x] Remove the legacy `gm_api.c` facade after active GM operations moved to
  `scenehub_control`.
- [x] Replace `command_executor.c` parameter cJSON parsing with a bounded
  parser or typed command args for system/audio/hardware commands.
- [x] Decide whether `command_executor_mqtt.c` may keep cJSON envelope building
  or should move to fixed-format/streamed JSON for command dispatch.
- [x] Remove per-message MQTT PUBLISH payload allocation from
  `components/mqtt_core/mqtt_core_protocol.c`.
- [x] Replace MQTT bridge event-copy allocation with a fixed pool in
  `components/mqtt_core/mqtt_core_bridge.c`.
- [x] Replace MQTT broker long-lived heap storage for sessions, retained
  messages, per-client packet buffers and static task stacks with fixed
  storage.
- [x] Replace core runtime/service heap-backed mutex creation with static
  semaphore storage in the GM runtime, command executor, hardware IO,
  service status and orchestrator runtime views.

Notes:

- Scenario start is not a 100 ms tick path, but it is a live operator action.
  It should still avoid large heap churn where practical.
- Prepared event-ref storage is bounded static PSRAM, not heap allocation:
  `GM_ROOM_SESSION_PREPARED_EVENT_REF_MAX` is currently `320`. One catalog is
  approximately `104 KB`. `CONFIG_SCENEHUB_MAX_ROOMS` now defaults to `1`, so
  the default build reserves one room-slot catalog plus one reusable
  control-layer build scratch, approximately `208 KB`. Raising the room limit
  adds approximately one catalog-sized PSRAM reservation per extra room slot.
  Catalog bytes are zeroed on scenario stop/reset, full session reset and fresh
  room-slot allocation. There is no runtime alloc/free lifecycle.
- Command dispatch is a control path and should remain allocation-light even
  when multiple devices are triggered.
- `command_executor.c` reads flat params with a bounded scanner instead of
  cJSON.
- `command_executor_mqtt.c` builds command envelopes into a static PSRAM payload
  buffer and merges flat default/request args without cJSON. Request args
  override same-name default args.
- Incoming MQTT PUBLISH payloads are parsed in the reusable per-client RX
  packet buffer. The buffer has one extra byte for NUL termination, so the
  ingest/event path does not allocate for every device message.
- MQTT event-bus publishing uses a fixed PSRAM bridge job pool. If the pool is
  exhausted, the outgoing bridge job is dropped cleanly instead of fragmenting
  the heap.
- MQTT retained messages store payload bytes directly inside fixed retained
  table slots in PSRAM. Retained publish/clear no longer allocates or frees a
  payload buffer per retained topic update.
- GM room session event/runtime tasks still use `xTaskCreate()`. Do not move
  them to static stacks until the stack unit/placement is audited, because a
  naive `StackType_t[stack_size]` conversion can reserve much more internal RAM
  than the dynamic task path.
- Audio output/tone/silence buffers are static DMA-capable storage.
- WAV decode uses fixed PSRAM buffers per mixer channel. The mixer keeps two
  fixed background slots plus one effect slot so background switches can prime
  the next track before stopping the current one.
- Audio reader contexts use fixed background A/background B/effect slots.
- MP3 wrapper buffers are static PSRAM storage, and Helix decoder allocation is
  served from a bounded 96 KB PSRAM pool.
- MP3 effect playback budget is bounded by the 96 KB Helix pool plus static
  MP3 input/main/output buffers in `helix_mp3_wrapper.c`.
- Web UI auth/request helper buffers use shared PSRAM-first allocation helpers.
- OTA upload uses one static PSRAM chunk buffer protected by an upload mutex
  instead of allocating a chunk per request.
- `/api/gm/room/runtime` uses static PSRAM scratch for the session snapshot and
  only computes asset summary from the running scenario snapshot. It no longer
  loads the full selected scenario inside the HTTP task before scenario start.
- `/api/gm/room/scenarios` builds the scenario editor payload one scenario at
  a time with shared scratch instead of allocating a full
  `room_scenario_t[ROOM_SCENARIO_MAX_SCENARIOS]` response buffer.
- `/api/gm/room/scenarios` does not re-run full scenario validation while
  listing saved scenarios. Explicit scenario validate/save/start paths own
  deep validation.
- `/api/gm/room/profiles` and `/api/gm/room/profile/save` validate game-mode
  references only. Full scenario validation remains on scenario save/start
  paths and is not run inside profile HTTP handlers.
- cJSON is configured with PSRAM-first hooks in Web UI init. Any buffer returned
  by `cJSON_Print*()` must be released with `cJSON_free()` or the same
  heap-caps path, never plain `free()`.
- `config_store` uses one static PSRAM `app_config_t` scratch protected by a
  static mutex instead of allocating a fixed-size config snapshot per save.
- `orchestrator_core/registry` uses a shared static PSRAM scratch pool for
  snapshot device lists, ingest lookups, one room session, one room scenario
  and validation reports.
- Orchestrator room-runtime asset summary uses a bounded `"file"` JSON field
  scanner for `audio.play` command args instead of `cJSON_Parse()` per command
  while building `/api/gm/room/runtime`.
- GM audio-prepare warmup scans `audio.play` args with the same bounded
  no-allocation approach instead of parsing transient cJSON documents.
- Orchestrator room-scenario views iterate scenario storage one scenario at a
  time; they do not reserve a second full `room_scenario_t[]` catalog.
- `ROOM_SCENARIO_MAX_SCENARIOS` is limited to 12 for the current product
  shape. Full scenario structs are still large and remain the biggest static
  PSRAM user.

## P0 - Audio Allocation Plan

Goal: no allocation in streaming/output loops, predictable allocation before
playback starts, and no heap fragmentation from repeated tracks.

Recommended audio memory model:

- Small I2S/DMA buffers:
  - static or one-time allocated;
  - internal RAM;
  - DMA-capable;
  - never freed during normal operation.
- Large decode/read buffers:
  - static or fixed pool;
  - PSRAM where DMA is not required;
  - allocated once at audio service init.
- MP3 decoder state:
  - fixed pool if possible;
  - if Helix requires allocation through the shim, route it to a bounded
    allocator and document max usage.
- Reader context:
  - use static slots for background A, background B and effect, or a small
    fixed pool.

Current allocation points:

- [x] `components/audio_player/audio_player_output.c`
  - `heap_caps_malloc(... MALLOC_CAP_DMA)` for the I2S output buffer.
  - Replace with static DMA-capable storage or one-time init allocation.
- [x] `components/audio_player/audio_player_decode.c`
  - allocates `in_buf` and `out_buf` per decode/read path.
  - Replace with PSRAM-backed static/fixed buffers where DMA is not required.
- [x] `components/audio_player/audio_player_runtime.c`
  - allocates `audio_reader_ctx_t` per playback.
  - Replace with fixed background/effect reader slots or a small pool.
- [x] `components/audio_player/helix_shim.c`
  - Helix allocation shim uses heap.
  - Measure and constrain with a fixed MP3 decoder pool if feasible.

Acceptance:

- [ ] Starting/stopping/changing tracks repeatedly does not reduce largest free
  internal heap over time.
- [x] Background and effect playback do not allocate in the mixer/output loop.
- [x] MP3 effect playback has a documented max memory budget.
- [x] DMA buffers are internal/DMA-capable; large non-DMA decode buffers live in
  PSRAM.

## P1 - HTTP And JSON Allocation Reduction

Goal: HTTP/admin paths may allocate, but should not flood internal RAM or fail
because one large response needs contiguous DRAM.

Current allocation points:

- `components/web_ui/*`
  - many `cJSON_Create*`, `cJSON_Parse*`, `heap_caps_malloc` request/response
    buffers.
- `components/web_ui/web_ui_ota.c`
  - OTA upload chunk allocated per upload handler.
- `components/web_ui/gm/web_ui_gm_scenario.c`
  - runtime response is now streamed through a bounded chunked writer backed by
    static scratch instead of building a transient cJSON tree.

Tasks:

- [x] Keep all Web UI request body buffers PSRAM-first.
- [x] Convert frequent runtime JSON endpoints to preallocated response writers
  or bounded chunked writers where practical.
- [x] Convert OTA upload chunk to static/one-time allocation.
- [x] Avoid full `/api/gm/state` polling during active room control.
- [x] Keep `/api/gm/room/runtime` lightweight and bounded.

Notes:

- `web_ui_send_json()` still uses cJSON for most admin responses, but cJSON
  hooks are PSRAM-first and responses are sent in chunks.
- `/api/gm/state` is intentionally still a broad/admin snapshot endpoint. It
  reuses static registry snapshot storage, but its JSON response tree remains
  cJSON-based because it is not the active room-control hot path.
- Full `/api/gm/state` refresh is allowed only for bootstrap, structural
  resync, or explicit recovery. Runtime refresh must prefer narrow endpoints
  such as `/api/gm/room/runtime`, rooms-runtime refresh, and
  `/api/gm/system/summary`.
- `/api/gm/room/runtime` no longer allocates its `gm_room_session_t` snapshot
  per poll and no longer loads selected-scenario asset scratch before scenario
  start.
- `/api/gm/room/runtime` now streams its JSON contract directly from the
  static runtime-view scratch through a bounded chunked writer, avoiding a
  transient cJSON tree for a frequent endpoint.
- `/api/gm/room/runtime?detail=summary` omits heavy arrays and asset-detail
  fields for dashboard/rooms refresh, so the common multi-room polling path
  no longer ships full branch-step runtime payloads for every room.
- `/api/gm/room/runtime` no longer parses transient cJSON documents while
  scanning `audio.play` file args for asset readiness.
- The remaining frequent runtime endpoint improvement, if needed later, is a
  smaller stable runtime DTO that further cuts payload width and lock-held send
  time.

## P1 - Storage And Import/Export

Goal: storage operations can allocate but should use PSRAM and should not run
inside realtime flows.

Current allocation points:

- `components/room_scenario/storage/room_scenario_persistence.c`
- `components/quest_device/quest_device_storage.c`
- `components/gm_profile_store/gm_game_profile.c`
- `components/config_store/config_store.c`

Tasks:

- [x] Prefer PSRAM-first buffers for file read/write operations.
- [x] Keep import/export behind admin/manual actions only.
- [x] Consider fixed maximum document buffers for known JSON file sizes.
- [x] Ensure failed allocation returns a clear error and never partially
  corrupts persisted state.

Notes:

- Room scenario, Quest Device and GM profile persistence use PSRAM-first file
  buffers and tmp-file/rename saves.
- Room scenario and Quest Device files have explicit maximum JSON sizes; GM
  profiles have an explicit storage read limit.
- Config store no longer allocates fixed-size `app_config_t` snapshots for NVS
  writes.
- JSON import/export still uses cJSON document trees. That is acceptable for
  manual admin/storage paths, but fixed-size document buffers can be revisited
  if these paths start showing fragmentation under stress.

## P1 - Orchestrator And Registry Scratch

Goal: snapshots and registry views should reuse fixed scratch where possible
because they are called by the GM UI.

Current allocation points:

- `components/orchestrator_core/registry/*`
  - no heap allocation remains for common snapshot scratch; the remaining
    snapshot cache is static PSRAM storage.

Tasks:

- [x] Move common registry scratch arrays to static PSRAM storage protected by
  the registry lock.
- [x] Keep snapshot cache invalidation explicit.
- [x] Avoid rebuilding large snapshots for active room runtime refresh.

Notes:

- Snapshot build and room scenario detail views share one registry scratch pool.
  Public detail reads take the scratch lock directly because they do not go
  through the snapshot cache lock.
- Runtime room views and websocket invalidation now avoid transient JSON parse
  churn in their common helper paths. The remaining broad JSON work is in
  admin/full-snapshot response building, not active room-runtime refresh.
- The registry scratch pool intentionally keeps only one `room_scenario_t`
  scratch object. A full duplicate `room_scenario_t[ROOM_SCENARIO_MAX_SCENARIOS]`
  would reserve multiple megabytes of PSRAM before the heap is available.
- Active room control uses the dedicated runtime endpoint and selective GM
  Panel refresh path, so broad registry snapshots are not the hot runtime poll.

## Audit Commands

Useful local checks:

```powershell
rg -n "\b(malloc|calloc|realloc|free)\b|heap_caps_(malloc|calloc|realloc|free)|cJSON_(Create|Parse|Print|Delete)" components main -g "*.c" -g "*.h"
```

```powershell
rg -n "heap_caps_(malloc|calloc|free)|cJSON_Parse|cJSON_Create|cJSON_Delete" components\gm_core components\command_executor components\hardware_io components\audio_player components\event_bus components\quest_device components\room_scenario -g "*.c"
```

## Definition Of Done

- [x] No runtime-hot allocation remains in scenario tick/event/command paths.
- [x] Audio output and mixer paths have no runtime allocation.
- [x] Track start uses bounded static/fixed-pool buffers.
- [x] Frequent GM runtime endpoints avoid large transient allocations.
- [x] Remaining allocation sites are documented by class and have clear failure
  behavior.
