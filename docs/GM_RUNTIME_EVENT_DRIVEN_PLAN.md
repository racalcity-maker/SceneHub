# GM Runtime Event-Driven Plan

This temporary plan tracks a focused refactor of the GM runtime loop away from
fixed periodic polling and away from hybrid "wake or next tick" scheduling.

The target is a pure cause-driven runtime:

- external scenario/runtime events wake the runtime explicitly
- time-based continuation is driven by one-shot timers that enqueue runtime work

The goal is not to make the runtime interrupt-heavy. The goal is to make every
runtime advance happen for a concrete reason instead of because `100 ms` passed.

## Current State

- [x] The runtime task now blocks on a bounded runtime-cause queue in
      [gm_room_session_runtime.c](/d:/Projects/SceneHub/components/gm_core/session/gm_room_session_runtime.c:938)
      instead of sleeping on a fixed `vTaskDelay(...)` cadence.
- [x] `scenehub_event_t` traffic reaches scenario progression through the
      runtime inbox rather than the event worker directly executing runtime
      semantics.
- [x] A one-shot runtime deadline timer now wakes the runtime for the nearest
      session/branch wait deadline, reactive cooldown, or pending command
      timeout.
- [x] Some transitional pieces still exist:
      - the runtime now calls
        `gm_room_session_runtime_process_pending_work()`
      - the runtime still does a broad pending-work pass after each cause;
        narrower event/deadline/control entrypoints can still be split out

## Goal

- [ ] Remove unconditional periodic runtime polling.
- [ ] Remove "nearest deadline sleep loop" as the final scheduler model.
- [ ] Advance runtime only from explicit causes:
      - incoming scenario/runtime events
      - explicit operator/runtime control actions
      - one-shot timer expirations for time-based waits/cooldowns/timeouts
- [ ] Keep time-based behavior deterministic and bounded.

## Non-Goals

- [x] Do not redesign scenario semantics.
- [x] Do not move scenario ownership out of `gm_core`.
- [x] Do not move runtime scheduling logic into `event_bus`.
- [x] Do not mix this with read-model/UI refresh refactors.

## Direction

- [x] Introduce a GM runtime inbox / wake path.
- [x] Route meaningful external changes into that inbox instead of relying on
      the next global tick.
- [x] Replace tick-based time advancement with explicit one-shot timers for:
      - `WAIT_TIME`
      - reactive cooldown expiry
      - command-result timeout expiry
- [ ] Keep heavy scenario work on the runtime task, not in timer callbacks.

## Candidate Shape

- [x] Introduce an explicit wake primitive:
      `gm_room_session_runtime_wake()`
- [x] Introduce a runtime event/inbox contract, for example:
      `gm_room_runtime_event_t`
- [x] Make timer callbacks post runtime wake causes instead of advancing
      scenario state directly.
- [x] Make the runtime task block on queue/notification indefinitely and wake
      only when:
      - a runtime event is queued, or
      - a timer callback posts a runtime event

## Boundaries

- [x] `gm_core` owns runtime wake decisions, timer ownership, and scenario
      progression.
- [x] `event_bus` still only delivers SceneHub events and remains transport.
- [x] `command_executor` still owns command timeout/result bookkeeping; the GM
      runtime only consumes results/timeouts through an explicit runtime cause.
- [x] `room_scenario` stays a bounded model and validation layer, not a runtime
      scheduler.

## Main Risks

- [ ] Lost wakeups causing stuck waits or delayed progression.
- [ ] Stale timer events after stop/reset causing double-advance or ghost
      resumes.
- [ ] More complicated locking around session mutation, runtime queueing, and
      timer cancellation.
- [x] Tests that assumed fixed tick-delay progression are being rewritten
      toward cause-driven waiting instead of sleeping for
      `CONFIG_SCENEHUB_GM_RUNTIME_TICK_MS`.

## Rollout

- [x] P0. Record the target and boundaries in this plan.
- [x] P1. Introduce a runtime wake primitive and use it from obvious session
      mutation paths.
      This is still transitional, but it creates the wake abstraction that the
      later event/timer-driven runtime will use. Obvious
      start/stop/reset/scenario
      control paths call `gm_room_session_runtime_wake()`.
- [x] P2. Introduce a runtime inbox / cause contract for GM runtime work.
      `gm_core` now has a bounded runtime-cause queue, and the event worker
      bridges `scenehub_event_t` traffic into the runtime inbox instead of
      executing runtime semantics directly on the event task.
- [x] P3. Route event-originated progression through the runtime inbox instead
      of directly executing all runtime semantics on the event task.
      `scenehub_event_t` traffic now reaches scenario progression through the
      runtime-cause queue instead of `gm_room_session_event_task()` calling
      `gm_room_session_scenario_on_event(...)` directly.
- [x] P4. Introduce explicit one-shot timer sources for `WAIT_TIME`, reactive
      cooldown, and command-result timeout continuation.
      `gm_core` now maintains a runtime deadline timer that tracks the nearest
      session/branch wait deadline, reactive cooldown, or pending command
      timeout and wakes the runtime explicitly when that deadline is reached.
- [x] P5. Remove unconditional periodic polling from the runtime task.
      The runtime task now blocks on the runtime-cause queue and wakes only
      for explicit runtime causes or deadline-timer wake events.
- [ ] P6. Remove transitional tick-only helpers and update tests to assert
      cause-driven runtime progression.
      Progress so far:
      - [x] remove `CONFIG_SCENEHUB_GM_RUNTIME_TICK_MS`
      - [x] stop Web UI handler runtime tests from sleeping against a fixed
            tick interval
      - [x] split the old `gm_room_session_scenario_tick()` body into
            command-timeout, wait/deadline, and branch-advance helpers
      - [x] add `gm_room_session_runtime_process_pending_work()` as the
            runtime-owned entrypoint
      - [x] remove the legacy `gm_room_session_scenario_tick()`
            compatibility wrapper from tests/public surface

## Acceptance

- [ ] Idle systems no longer wake the GM runtime just because a fixed poll
      interval elapsed.
- [ ] Device-event and operator-driven progression do not wait for a periodic
      tick.
- [ ] `WAIT_TIME`, cooldowns, and command-result timeouts remain correct
      without a fixed global poll loop.
- [ ] No regressions in start/stop/reset, wait/result handling, or reactive
      branch behavior.
