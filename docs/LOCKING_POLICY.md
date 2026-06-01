# Locking Policy

This document records the first practical locking audit for the current codebase
and defines the immediate guardrails we want to enforce.

It is intentionally concrete: the table below is based on the current code, not
on an idealized future architecture.

## Scope

This first pass focuses on the locks and critical sections that currently matter
most for runtime correctness and deadlock risk:

- `gm_core/session`
- `command_executor`
- `scenehub_read_model`
- `event_bus`
- `mqtt_core`

## Main Rules

1. No external calls under `gm_session_lock`.
2. No external calls under `command_executor_execute_mutex`.
3. `event_bus` handlers are adapter-only and must return quickly.
4. Heavy domain work belongs to the owner task/queue of that subsystem.
5. Lock ordering must be explicit and documented before adding new nested
   locking paths.
6. Synchronous owner-task wrappers must document whether callers may hold locks
   while waiting.

## Current Status

- Closed: `gm_core` no longer calls `event_bus_post*` under `gm_session_lock`.
  Flag-change events are deferred and flushed after
  `gm_room_session_sessions_unlock()`.
- Closed: `gm_core` no longer calls `command_executor_cancel_request()` under
  `gm_session_lock`. Request cancellations are deferred and flushed after
  `gm_room_session_sessions_unlock()`.
- Closed: `command_executor` no longer keeps `s_execute_mutex` across
  `audio_player_*`, `hardware_io_*`, `mqtt_core_publish()`, or
  `command_executor_track_pending()`. The execute mutex now only protects
  lookup/copy of shared scratch state.
- Closed: `scenehub_control` resolves the complete scenario event-ref catalog
  before entering `gm_core` runtime start. `gm_core` copies it into the target
  room snapshot under `gm_session_lock`; wait entry only expands local indices.
- Closed: GM event matching now uses pre-resolved local metadata for both
  wait-event matching and reactive device-event triggers. `quest_device_*`
  lookups are no longer part of the lock-held event-match path under
  `gm_session_lock`.
- Closed: audited `event_bus` consumers are adapter-only in the current scope.
  `gm_core` forwards to its event queue, `mqtt_core` bridges through
  `event_bus_post_job(...)`, `scenehub_read_model` only marks cache invalidate
  pending, and `orchestrator_timeline` / `error_monitor` now stage bounded job
  work through `event_bus_post_job(...)` instead of doing inline handler work.

## Terms

- `external call` means a call outside the lock owner's local state layer.
  Examples:
  - `event_bus_post*`
  - `mqtt_core_publish`
  - `hardware_io_*`
  - `audio_player_*`
  - `quest_device_*`
  - `room_catalog_*`
  - `gm_room_session_*` from another subsystem
- `owner task` means the task or queue that owns mutation of a subsystem state.

## Audited Lock Inventory

| Lock / Section | Owner | Acquired In | What Happens While Held | Nested Lock / Callback Risk | Hold Time |
|---|---|---|---|---|---|
| `s_sessions_mutex` | `gm_core/session` | `gm_room_session_sessions_lock()` in [gm_room_session.c](/d:/Projects/SceneHub/components/gm_core/session/gm_room_session.c:197) | Session mutation, prepared catalog copy/clear, branch progression, wait transitions, view reads | Reduced. Flag-event posts and command-result cancellations are deferred until unlock; wait-entry and reactive trigger matching read the room-local prepared snapshot | Medium |
| `s_tick_mutex` | `gm_core/session` runtime pass | [gm_room_session_runtime_process_pending_work()](/d:/Projects/SceneHub/components/gm_core/session/gm_room_session_runtime.c:982) | Serializes runtime pass and timeout polling | Yes. It nests `s_sessions_mutex` | Medium |
| `s_game_scratch_mutex` | `gm_core/session` prepared game-start report scratch | [gm_room_session_game.c](/d:/Projects/SceneHub/components/gm_core/session/gm_room_session_game.c:25) | Serializes game-start validation report use | May nest `s_start_scratch_mutex`, then `s_sessions_mutex` through prepared scenario start | Medium |
| `s_start_scratch_mutex` | `gm_core/session` prepared scenario-start report scratch | [gm_room_session_runtime.c](/d:/Projects/SceneHub/components/gm_core/session/gm_room_session_runtime.c:149) | Serializes scenario-start validation report use | May nest `s_sessions_mutex` during local runtime snapshot commit | Medium |
| `s_prepared_scenario_mutex` | `scenehub_control` prepared event-ref build scratch | [scenehub_control.c](/d:/Projects/SceneHub/components/scenehub_control/scenehub_control.c:61) | Serializes one reusable PSRAM prepared-catalog scratch through build and core copy | May nest `s_event_resolver_mutex`, then core start locks while passing the completed scratch into `gm_core` | Medium |
| `s_event_resolver_mutex` | `scenehub_control` Quest Device event-ref lookup scratch | [scenehub_control.c](/d:/Projects/SceneHub/components/scenehub_control/scenehub_control.c:39) | Protects reusable `quest_device_t` lookup scratch while building the prepared catalog before runtime start | Nested under `s_prepared_scenario_mutex`; never acquired under `gm_session_lock` | Low |
| `sh_ctrl_disp` queue/task | `scenehub_control` dispatch owner | [scenehub_control_dispatch.c](/d:/Projects/SceneHub/components/scenehub_control/scenehub_control_dispatch.c:455) | Serializes describe-interface and device-command dispatch using owner-held scratch; callers enqueue and wait for task notification | Callers must not hold `gm_session_lock`, must not run from `event_bus` dispatch hot path, and must not be the `sh_ctrl_disp` task itself. Queue length 2 is intentional backpressure | Medium |
| `s_execute_mutex` | `command_executor` execute path | [command_executor_execute()](/d:/Projects/SceneHub/components/command_executor/command_executor.c:467) | Resolves device metadata into local snapshots and releases the lock before side effects | No current external-call nesting in the audited execute path | Low |
| `s_pending_lock` | `command_executor` pending-result table | [command_executor_result.c](/d:/Projects/SceneHub/components/command_executor/command_executor_result.c:41) | Tracks/clears pending requests and builds timeout events | No longer nested under `s_execute_mutex` in the MQTT dispatch path | Low |
| `s_cache_mutex` | `scenehub_read_model` snapshot publish/copy | [orchestrator_registry.c](/d:/Projects/SceneHub/components/scenehub_read_model/registry/orchestrator_registry.c:43) | Publishes and copies cached snapshot metadata/blob only | No. Full uncached snapshot build is serialized by a separate build mutex outside `s_cache_mutex` | Medium |
| `s_scratch_mutex` | `scenehub_read_model` scratch buffers | [orch_common.c](/d:/Projects/SceneHub/components/scenehub_read_model/orch_common.c:37) | Shared scratch arrays for snapshot and room view assembly | Can be nested under `s_cache_mutex` through uncached snapshot build | Medium |
| `event_bus` critical sections (`s_pool_lock`, `s_handler_lock`, `s_stats_lock`) | `event_bus` internals | [event_bus.c](/d:/Projects/SceneHub/components/event_bus/event_bus.c:100) | Pool bookkeeping, stats, handler list copy | No handler callbacks run while these critical sections are held | Very short |
| `mqtt_core` session lock `s_lock` | `mqtt_core` connection/session bookkeeping | [mqtt_core.c](/d:/Projects/SceneHub/components/mqtt_core/mqtt_core.c:119) | Session count and session bookkeeping | Not fully audited in this pass; `mqtt_core_publish()` itself does not take `s_lock` directly in the shown path | Low to medium |

## Remaining P0 Violations

No open P0 boundary violations remain in the currently audited scope.

Recent closure worth calling out:

- sequential command-group and reactive-group planning no longer resolve
  `quest_device_get_command()` under `gm_session_lock`
- the metadata check still happens, but only in planned dispatch after the
  session lock is released

## Important But Not P0

### `event_bus`

`event_bus` itself is in better shape than the subsystems above:

- handler list is copied under `s_handler_lock`
- handlers run outside bus internal critical sections in
  [dispatch_message()](/d:/Projects/SceneHub/components/event_bus/event_bus.c:189)
- the real risk is sequential handler cost, not lock inversion inside the bus

Policy:

- handlers stay adapter-only
- heavy work goes to component queues or `event_bus_post_job(...)`

Audited status in the current scope:

- `gm_core` event handling is queue-backed
- `mqtt_core` bridge work is deferred through `event_bus_post_job(...)`
- `scenehub_read_model` registry handler only flips invalidate-pending state
- `orchestrator_timeline` and `error_monitor` handlers now stage bounded
  follow-up work through `event_bus_post_job(...)`

### `scenehub_read_model` cache lock

`s_cache_mutex` is narrower now, but the overall read-model path still deserves
ongoing lock-order review because snapshot build and room runtime projection
walk several subsystems.

Current snapshot path:

- uncached snapshot build is serialized outside `s_cache_mutex`
- [orch_snapshot_builder_build_uncached()](/d:/Projects/SceneHub/components/scenehub_read_model/orch_snapshot_builder.c:30)
  still walks devices, rooms, issues, and session projections
- `s_cache_mutex` is now limited to publish/copy of the built snapshot

### `scenehub_control` dispatch owner

`scenehub_control_dispatch.c` uses one owner task, `sh_ctrl_disp`, for
transport-facing write-side operations that need shared scratch:

- `describe_interface`
- manual/synchronous device-command resolution and dispatch
- scenario command dispatch after the GM session lock is released

The wrapper pattern is synchronous:

```text
caller -> queue request -> sh_ctrl_disp executes -> caller waits notification
```

This is allowed only under these invariants:

- caller does not hold `gm_session_lock` or other owner locks while waiting;
- caller is not an `event_bus` dispatch hot-path handler;
- caller is not `sh_ctrl_disp` itself;
- queued work is bounded and has its own operation timeout where it can block on
  transport/device response;
- queue length 2 is intentional backpressure, not a bulk work queue.

The owner task exists to serialize scratch buffers and command/device
resolution, not to become a generic application runtime executor.

Why it matters:

- snapshot build still touches many subsystems in one pass
- build latency still scales with snapshot size
- lock ordering against `gm_session_lock` and catalog/device paths is not yet
  explicitly documented

Target follow-up:

- document ordering before adding more nested locks
- keep the publish/copy lock narrow and avoid re-expanding it around uncached
  build work

## Observed Lock Ordering

Observed in the audited scope:

1. `s_tick_mutex -> s_sessions_mutex`
2. `s_prepared_scenario_mutex -> s_event_resolver_mutex`
3. `s_prepared_scenario_mutex -> s_game_scratch_mutex -> s_start_scratch_mutex -> s_sessions_mutex`
4. `s_prepared_scenario_mutex -> s_start_scratch_mutex -> s_sessions_mutex`
5. snapshot build serialization -> `s_scratch_mutex`
6. snapshot/view builders -> gm/session reads

What must not be introduced without review:

- reverse `s_sessions_mutex -> s_tick_mutex`
- reverse `s_event_resolver_mutex -> s_prepared_scenario_mutex`
- reverse `s_sessions_mutex -> s_start_scratch_mutex`
- any callback/event path that can re-enter the owner subsystem while a lock is
  held

## Immediate Policy

These are the rules to apply before larger refactors:

- `gm_session_lock` protects local GM session state only.
- `command_executor_execute_mutex` protects local executor planning/scratch
  state only.
- No transport, hardware, audio, or cross-subsystem calls under those two
  locks.
- Event-bus handlers must not mutate broad shared state inline.
- New nested lock paths require updating this document.

## Next Steps

### P0

- [x] Remove `event_bus_post_priority(...)` from under `gm_session_lock`.
- [x] Remove `command_executor_cancel_request(...)` from under
      `gm_session_lock`.
- [x] Remove audio/hardware/MQTT side effects from under
      `command_executor_execute_mutex`.
- [ ] Document and enforce current allowed lock order.
- [x] Remove reactive-event `quest_device_*` matching from under
      `gm_session_lock`.

### P1

- [x] Audit remaining event-bus consumers for adapter-only behavior.
- [x] Rework `gm_core` wait-entry helpers to separate external metadata resolve
      from local session-state commit.
- [ ] Revisit `scenehub_read_model` cache lock scope.

### P2

- [ ] Add debug assertions for forbidden calls while critical owner locks are
      held.
- [ ] Move more heavy shared-state mutation toward owner-task / message-passing
      boundaries.
