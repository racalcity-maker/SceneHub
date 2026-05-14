# Controller Firmware Bootstrap Backlog

## Goal

This backlog defines the firmware-side work required to support the SceneHub
desktop application.

It is the controller implementation companion to:

- `DESKTOP_CONSOLE_PLAN.md`
- `DESKTOP_APP_UI_PLAN.md`
- `DESKTOP_APP_TECH_ARCH.md`
- `DESKTOP_APP_BOOTSTRAP_BACKLOG.md`

This document is firmware-focused.

It should answer:

- what must be added to the controller firmware;
- which modules are responsible;
- what the first desktop-compatible firmware MVP is;
- what order of implementation is safest.

## Scope

This backlog covers:

- controller identity metadata;
- WebSocket transport;
- live event bridge;
- generation/sequence model;
- contract tests;
- desktop support behavior.

This backlog does not cover:

- a full desktop app implementation;
- complete embedded web UI reduction;
- future content-editor UX.

## Guiding Rules

- [ ] The controller remains the runtime owner.
- [ ] Commands remain HTTP-only in v1.
- [ ] WebSocket is live-events-only in v1.
- [ ] Existing snapshot endpoints remain valid and supported.
- [ ] Legacy embedded full web UI stays available during bootstrap.
- [ ] Minimal Local HMI remains a later reduction step, not a bootstrap blocker.

## Current Firmware Baseline

Existing strengths:

- modular backend domains already exist;
- current HTTP/API surface already supports snapshot-based UI;
- runtime and orchestration logic already live on the controller;
- `event_bus` already exists as an internal event transport;
- PSRAM-backed snapshots already exist and should stay.

Main gap:

- no desktop-oriented metadata endpoint;
- no controller live event transport;
- no desktop-facing event envelope and reconnect model.

## Firmware MVP For Desktop Support

The controller firmware is desktop-MVP ready when it provides:

- [ ] `GET /api/meta`
- [ ] stable HTTP snapshot endpoints
- [ ] `/api/ws`
- [ ] common WS envelope
- [ ] sequence + generation + monotonic time fields
- [ ] live room/device/issues/timeline event delivery
- [ ] bounded-queue / slow-client safety
- [ ] reconnect/resync-compatible behavior
- [ ] contract coverage for new metadata and event schemas

## Phase 1 - Metadata And Identity

### Endpoint

- [ ] Add `GET /api/meta`.
- [ ] Make `/api/meta` public and unauthenticated.

`/api/meta` must expose only safe identity and capability data.

It must not expose:

- secrets;
- credentials;
- network passwords;
- sensitive controller configuration.

### Required Fields

- [ ] `product_id`
- [ ] `device_id`
- [ ] `device_name`
- [ ] `hostname`
- [ ] `firmware_version`
- [ ] `api_version`
- [ ] `capabilities`
- [ ] `limits`

### Recommended Fields

- [ ] `build.git_sha`
- [ ] `build.build_date`
- [ ] `hardware_uid` if available and stable

### Likely Implementation Area

- `components/web_ui/`
- likely new handler plus metadata serialization helper

### Contract Test Coverage

- [ ] add `/api/meta` response schema coverage
- [ ] validate missing/optional field behavior
- [ ] verify controller identity marker is always present

### Public Metadata Rule

Desktop connection flow should be:

1. fetch `/api/meta`
2. verify this is a SceneHub controller
3. verify API compatibility
4. then perform login/session flow

## Phase 2 - WebSocket Transport

### Endpoint

- [ ] Add `/api/ws`.

### Transport Responsibilities

- [ ] authenticated desktop client connection
- [ ] message send path
- [ ] close/error handling
- [ ] liveness tracking
- [ ] bounded per-client queue from the start

### Likely Implementation Area

- `components/web_ui/`
- likely new `web_ui_ws*.c` module set

### Required Behaviors

- [ ] one connection path for desktop clients
- [ ] handshake compatible with existing auth/session model
- [ ] explicit disconnect behavior
- [ ] connection-aware cleanup

### WebSocket Auth Model v1

- [ ] client authenticates through existing HTTP login first
- [ ] WS handshake reuses the existing session cookie or token model
- [ ] unauthenticated WS connection is rejected clearly
- [ ] auth expiry closes WS and forces desktop back to auth-required state

Do not introduce a separate WebSocket token flow in v1 unless the existing
session model proves insufficient.

### Subscription Model v1

For v1, avoid a complex subscription protocol.

A connected authenticated desktop client should receive the default desktop
event set:

- `room.runtime.changed`
- `device.state.changed`
- `issues.changed`
- `timeline.appended`
- `audit.appended`

Topic-level subscriptions may be added later only if event volume becomes a
real problem.

## Phase 3 - Common Event Envelope

### Required Fields

- [ ] `type`
- [ ] `seq`
- [ ] `schema_version`
- [ ] `snapshot_generation`
- [ ] `server_time_ms`
- [ ] `payload`

### Rules

- [ ] `server_time_ms` uses monotonic uptime-style milliseconds
- [ ] envelope is validated in tests
- [ ] unknown event types remain safe for clients

### Initial Ready Event

After successful WS connection, server should send:

```json
{
  "type": "connection.ready",
  "seq": 1000,
  "schema_version": 1,
  "snapshot_generation": 42,
  "server_time_ms": 24590122,
  "payload": {
    "device_id": "scenehub-main",
    "api_version": 1
  }
}
```

This is not a snapshot. It is a transport handshake event that tells the
desktop client the current sequence and generation position.

### Likely Implementation Area

- `components/web_ui/`
- event serialization helper / WS writer

## Phase 4 - Sequence And Generation Model

### Sequence

- [ ] introduce monotonic desktop-visible `seq`
- [ ] increment on every published live event
- [ ] guarantee per-connection ordered emission

For v1, `seq` is global per controller, not per client and not per event type.

Each published desktop event increments one global desktop event sequence.

### Snapshot Generation

For v1:

- [ ] introduce one global desktop-visible `snapshot_generation`
- [ ] increment when desktop-visible read model changes materially

Every desktop-relevant HTTP snapshot response should include
`snapshot_generation`.

The generation in a WS event must represent the desktop-visible read model
generation after the change that produced the event.

Later if needed:

- split by scope:
  - `gm_generation`
  - `room_runtime_generation`
  - `device_generation`
  - `timeline_generation`

### Likely Ownership

- shared desktop event publishing layer in `web_ui`
- backed by existing snapshot/update paths

## Phase 5 - Event Bridge

### Goal

Do not recompute desktop state in the WebSocket layer.

Use the existing backend runtime and read model updates, then bridge those
changes into live desktop events.

### Event Bridge Responsibilities

- [ ] subscribe to internal state-change signals
- [ ] coalesce events where appropriate
- [ ] serialize compact payloads
- [ ] publish to connected WS clients

### Preferred Internal Architecture

1. runtime or orchestrator state changes happen in existing modules
2. snapshots/read models update normally
3. significant changes publish internal event_bus messages or equivalent signals
4. WS bridge consumes those signals
5. desktop event is emitted

### Non-Goal

The WS layer does not own persistent event history.

It may keep only small bounded queues for connected clients.

Historical timeline and audit access remains an HTTP snapshot or slice concern.

### Likely Modules

- `components/event_bus/`
- `components/web_ui/`

## Phase 6 - First Desktop Event Set

### `room.runtime.changed`

#### Source Domains

- `components/gm_core/`
- timer/hint/runtime session flows

#### Emit When

- [ ] game start
- [ ] game stop
- [ ] game reset
- [ ] timer pause/resume/reset/add
- [ ] step index changes
- [ ] wait state changes
- [ ] operator approval transitions
- [ ] hint send/clear
- [ ] scenario runtime state changes

#### Contract Work

- [ ] define compact runtime payload
- [ ] do not stream full snapshot
- [ ] keep timer baseline model only

### `device.state.changed`

#### Source Domains

- `components/device_control_ingest/`
- `components/orchestrator_core/`

#### Emit When

- [ ] connectivity changes
- [ ] health changes
- [ ] runtime state changes
- [ ] important result/diagnostic state changes

### `issues.changed`

#### Source Domains

- `components/orchestrator_core/registry/`
- issue aggregation/build logic

#### Emit When

- [ ] issue added
- [ ] issue resolved
- [ ] issue severity changed
- [ ] room/system summary materially changed

### `timeline.appended`

#### Source Domain

- `components/orchestrator_core/timeline/`

#### Emit When

- [ ] new timeline entry appended

### `audit.appended`

#### Source Domain

- `components/orchestrator_core/audit/`

#### Emit When

- [ ] new audit entry appended

## Phase 7 - Timer Baseline Behavior

The controller must not emit one timer message per second.

Required timer behavior:

- [ ] emit timer state only on meaningful change
- [ ] include baseline remaining/duration/state
- [ ] include `server_time_ms`
- [ ] let desktop animate visible countdown locally

This keeps the controller efficient while making the desktop UI feel live.

## Phase 8 - Backpressure And Slow Client Policy

### Requirements

- [ ] per-client bounded outgoing queue
- [ ] no unbounded event buffering
- [ ] coalescable events may replace older pending equivalents
- [ ] slow or blocked clients are disconnected
- [ ] reconnect requires HTTP resync on the client side

### Examples

Coalescable:

- room runtime changed
- device state changed
- issue summary changed

Do not try to buffer infinite history for:

- timeline
- audit

If the client cannot keep up, disconnect and force resync.

If timeline or audit events are dropped for a client, the client must be
disconnected with a resync-required reason. Do not silently skip append-only
events.

### Likely Implementation Area

- `components/web_ui/`

## Phase 9 - Heartbeat And Liveness

### Required

- [ ] track client liveness through ping/pong or heartbeat
- [ ] expose enough behavior for desktop stale detection
- [ ] close dead connections cleanly

### Desktop-Relevant Effects

- no false long-lived "connected" state
- cleaner stale-data detection
- cleaner reconnect policy

## Phase 10 - Auth And Session Compatibility

### Required

- [ ] reuse existing auth/session model for HTTP + WS
- [ ] ensure `/api/meta` and `/api/ws` align with role/session rules
- [ ] ensure desktop app can detect auth-expired state cleanly

### Verify

- [ ] admin session behavior
- [ ] operator session behavior
- [ ] unauthenticated behavior
- [ ] auth-expired behavior

## Phase 11 - Contract Tests And Fixtures

### Add Backend Coverage For

- [ ] `/api/meta`
- [ ] WS event envelope
- [ ] `room.runtime.changed`
- [ ] `device.state.changed`
- [ ] timer baseline behavior
- [ ] reconnect/sequence-gap semantics where testable

### Fixtures

- [ ] store JSON fixtures near backend/web contract tests
- [ ] make them reusable by the desktop mock/fixture mode

Fixtures are source-controlled contract artifacts.

Desktop mock mode should consume the same fixture examples used by firmware
tests where practical.

## Phase 12 - Minimal Local HMI Follow-Up

This is not required for the first desktop bootstrap, but should be planned.

Later reduction work:

- [ ] keep login/status/network/OTA/accounts
- [ ] keep room start/stop/reset
- [ ] keep current room runtime summary
- [ ] keep critical issues/device offline visibility
- [ ] keep audio emergency stop
- [ ] remove heavy embedded editor/admin surfaces

## Suggested Module-Level Work Breakdown

### `components/web_ui/`

- [ ] `/api/meta`
- [ ] `/api/ws`
- [ ] WS connection/session handling
- [ ] event envelope serialization
- [ ] queueing/backpressure logic
- [ ] heartbeat/liveness

### `components/gm_core/`

- [ ] identify room runtime change emission points
- [ ] expose meaningful runtime change notifications
- [ ] avoid noisy timer tick event spam

### `components/device_control_ingest/`

- [ ] identify connectivity/telemetry state change points

### `components/orchestrator_core/`

- [ ] identify issue change points
- [ ] identify device read-model change points
- [ ] identify timeline append points
- [ ] identify audit append points

### `components/event_bus/`

- [ ] confirm reuse path for desktop-facing bridge events
- [ ] add event types only if needed, without polluting unrelated flows

## First Safe Implementation Order

1. `/api/meta`
2. WS transport skeleton with bounded per-client queue
3. common event envelope
4. sequence + generation + monotonic time
5. slow client policy and disconnect semantics
6. `room.runtime.changed`
7. `device.state.changed`
8. `issues.changed`
9. `timeline.appended`
10. contract tests and fixtures
11. auth/liveness hardening

This order prioritizes:

- identity;
- transport safety;
- live room correctness;
- compact value early.

## Explicit Non-Goals For Firmware Bootstrap

- [ ] full embedded web UI rewrite
- [ ] removal of legacy full web UI on day 1
- [ ] desktop-driven runtime ownership
- [ ] WebSocket commands in v1
- [ ] full editor-oriented desktop support before runtime support
- [ ] persistent event history inside the WebSocket layer

## Acceptance Criteria

The controller firmware bootstrap is complete when:

- [ ] the desktop app can identify a controller through `/api/meta`
- [ ] a desktop client can connect through `/api/ws`
- [ ] live room runtime changes are delivered without polling
- [ ] device/issue/timeline updates are delivered as compact events
- [ ] slow clients cannot exhaust controller memory
- [ ] disconnect/reconnect behavior remains safe
- [ ] current snapshot APIs still work for resync and fallback
