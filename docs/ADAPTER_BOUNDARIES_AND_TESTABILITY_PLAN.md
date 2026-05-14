# Adapter Boundaries And Testability Plan

This temporary plan tracks the next architecture cleanup after the GM runtime
and SceneHub events refactors.

The main goal is to tighten component boundaries so the event bus remains a
transport layer, domain logic becomes easier to test in isolation, and broad
public contracts get smaller over time.

## Current State

- [x] `event_bus` is transport-only in terms of type ownership: domain event
      semantics live in `scenehub_events`.
- [x] `gm_core` already forwards bus events into its own runtime queue instead
      of doing all runtime work on the bus dispatch task.
- [x] Audited bus consumers now keep handlers adapter-only: `gm_core` forwards
      to its runtime queue, `mqtt_core` bridges through `event_bus_post_job(...)`,
      `scenehub_read_model` only marks invalidate-pending, and
      `orchestrator_timeline` / `error_monitor` stage bounded follow-up jobs.
- [x] Backend test coverage already exists for `scenehub_events` consumers,
      `gm_core`, command execution, `room_scenario`, Web UI handlers, and the
      read model, but much of it still runs through the shared `quest_backend`
      harness rather than through narrow fake-port unit seams.
- [ ] Several components still rely on integration-style tests more than on
      cheap domain unit tests.
- [ ] Some domains still call global services directly, which makes isolated
      testing harder than it should be.
- [ ] `scenehub_read_model` still exposes a broad public contract through
      `orchestrator_registry.h`.

## Rule

- [x] `event_bus` handlers must be adapter-only boundaries.
      Meaning:
      - receive an event
      - optionally do lightweight validation/filtering
      - enqueue/post work into the component that owns the domain logic
      - return quickly
- [x] Heavy work must not run on the bus dispatch task.
      Examples of heavy work:
      - broad runtime scans
      - snapshot assembly
      - large JSON parsing
      - hardware side effects
      - scenario progression

## Non-Goals

- [x] Do not introduce a heavyweight DI framework.
- [x] Do not redesign the event bus into multiple buses right now.
- [x] Do not mix this plan with UI styling or unrelated performance work.

## Priority

1. Keep event-bus handlers as fast adapters
2. Add isolated domain unit tests
3. Introduce targeted ports/DI around domain logic
4. Shrink broad read-model public contracts
5. Re-evaluate multi-queue or typed-bus evolution only after the above

## Rollout

- [x] P0. Record the rule and backlog in docs.
- [x] P1. Audit event-bus consumers and enforce adapter-only handlers.
      Scope:
      - identify handlers that still do more than enqueue/adapt
      - move heavy follow-up work into component-owned queues/tasks/jobs
      Acceptance:
      - bus handlers return quickly
      - no handler performs broad domain scans or hardware side effects inline
- [ ] P2. Add isolated domain unit-test coverage.
      First candidates:
      - `scenehub_events`
      - `gm_core` wait/reaction/state-transition logic
      - command planning without hardware/transport
      - `room_scenario` validation
      Acceptance:
      - core domain transitions are testable without full device/integration
        setup
- [ ] P3. Introduce targeted ports/DI around domain logic.
      Suggested first target:
      - `gm_core`
      Suggested port surface:
      - `now_ms`
      - `post_event`
      - `dispatch_command`
      - optional scenario/load helpers where needed
      Acceptance:
      - domain tests can replace time/event/command backends with fakes
- [ ] P4. Split broad `scenehub_read_model` public contracts.
      First target:
      - `components/scenehub_read_model/include/orchestrator_registry.h`
      Suggested shape:
      - `orch_registry_types.h`
      - `orch_registry_api.h`
      - family-specific view/type headers
      Acceptance:
      - consumers include smaller contracts
      - read-model public types stop accumulating unrelated concerns in one
        header
- [ ] P5. Re-evaluate bus topology only after boundaries and testability are in
      better shape.
      Possible future work:
      - multiple internal queues
      - typed subscriber lanes
      - stronger backpressure isolation

## Acceptance

- [x] Event-bus handlers are consistently adapter-only.
- [ ] Core domain logic has meaningful isolated unit tests.
- [ ] `gm_core` and similar domains can be tested with fake ports instead of
      full integration setup.
- [ ] `scenehub_read_model` no longer exposes one overly broad public
      contract.
