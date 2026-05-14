# Desktop Console Plan

## Goal

SceneHub should move from an embedded browser-first UI to a desktop-first
operator/admin console while keeping a lightweight web fallback on the device.

The firmware should remain responsible for:

- room/session runtime;
- device orchestration;
- audio, hardware IO and MQTT integration;
- auth, settings and OTA;
- a minimal local HMI for setup, fallback control and service access.

The main operator experience should move to a desktop application.

The room runtime must continue running without the desktop app connected.
The desktop console is an operator/admin client, not the runtime owner.

## Why Change The UI Model

The current backend architecture is already split into good module boundaries
and stable HTTP contracts. The main remaining architectural mismatch is that the
frontend has grown into a large application while still living as a browser UI
served directly by the ESP32 firmware.

Current problems with the browser-first model:

- large embedded UI assets increase firmware weight;
- the current GM panel relies on polling-heavy browser behavior;
- the UI is constrained by embedded delivery rather than desktop UX needs;
- rich editors and responsive live views are harder to build and maintain;
- the firmware carries both domain logic and a full operator/admin UI shell.

The goal is not to remove web access completely. The goal is to stop treating
the embedded web UI as the main product surface.

## Product Direction

Target product split:

- `Desktop Console`:
  - primary operator/admin application;
  - full GM panel;
  - room control;
  - device monitoring and setup;
  - scenario/profile editors;
  - timeline/audit;
  - storage/import/export;
  - richer UX and responsive live updates.
- `Embedded Web Fallback / Minimal Local HMI`:
  - setup surface;
  - OTA surface;
  - rescue/service surface;
  - basic operator control surface;
  - available from phone/browser when needed;
  - intentionally lightweight;
  - no heavy editor flows or large front-end bundles.

## Main Decisions

### 1. Keep The Existing Backend Read Model

The current snapshot-based backend should stay in place.

Current snapshot/read-model APIs remain useful for:

- initial sync after client login;
- reconnect and full resync;
- lightweight embedded web fallback;
- diagnostics and testability.

The existing PSRAM-backed snapshots are not a blocker. They are a strength.

### 2. Add A Live Event Layer Instead Of Replacing HTTP

The desktop client should use:

- `HTTP` for login, settings, OTA, CRUD, save/load/import/export and initial
  snapshots;
- `WebSocket` for live runtime/device/timeline/audit updates.

This avoids heavy polling while keeping the system simple and debuggable.

### 3. Do Not Delete The Current Web UI During Desktop Development

The current web UI should remain in the firmware during the transition.

Important note:

- if nobody opens the legacy web UI, it does not create meaningful active
  runtime load on the ESP32;
- it still increases firmware footprint and embedded asset size;
- it can remain in place until the desktop app reaches functional parity.

This makes the migration safer.

## Recommended Desktop Stack

### Desktop Shell

Use `Tauri 2`.

Reasoning:

- smaller and lighter than Electron for this use case;
- a good fit for a local control application;
- frontend can be a standard SPA;
- keeps UI execution on the PC instead of the ESP32.

### Frontend Stack

Use:

- `React`
- `TypeScript`
- `Vite`
- `TanStack Query`
- `Zustand`
- `React Hook Form`

Recommended responsibility split:

- `TanStack Query`:
  - HTTP snapshots;
  - cache invalidation;
  - reconnect resync;
  - command mutation handling.
- `Zustand`:
  - connection state;
  - selected controller;
  - layout state;
  - filters;
  - transient UI state;
  - optional editor stores.
- `React Hook Form`:
  - simple and medium-complexity forms;
  - settings forms;
  - profile editor;
  - quest device editor.

The scenario editor should not be treated as a normal form. It should use a
dedicated reducer/store plus a validation layer around the scenario model.

## Target Firmware Surface

The firmware should expose three UI/API surfaces:

### 1. Desktop API Surface

Primary surface for the desktop application:

- `/api/auth/*`
- `/api/session/info`
- `/api/meta`
- `/api/gm/*`
- `/api/orchestrator/*`
- `/api/settings/*` or existing config endpoints
- `/api/ota/*`
- `/api/ws`

### 2. Embedded Web Fallback / Minimal Local HMI

Minimal browser UI for setup, fallback control and service access:

- login;
- status;
- network/AP/Wi-Fi settings;
- OTA update;
- credential management;
- room start/stop/reset;
- current room runtime summary;
- critical issues;
- device offline visibility;
- audio emergency stop;
- basic room/device state visibility.

### 3. Legacy Full Web UI

Kept temporarily during migration only.

This should eventually be removed or reduced to the new lightweight fallback.

## Embedded Web Fallback Scope

The lightweight on-device web UI should keep only the flows that are genuinely
useful when no desktop client is available.

Keep:

- login;
- status summary;
- Wi-Fi / AP / network setup;
- OTA upload and reboot flow;
- admin/operator credentials;
- audio emergency stop;
- room start/stop/reset;
- current room runtime summary;
- critical issues / device offline visibility.

Remove from the embedded web UI long-term:

- full GM dashboard;
- timeline/audit heavy views;
- quest device setup editor;
- scenario editor;
- game profile editor;
- storage/import/export UI;
- recursive audio browser;
- heavy static bundles and large CSS/JS payloads.

## API Model

### HTTP Responsibilities

Use HTTP for:

- login and logout;
- session info;
- device metadata;
- initial full snapshots;
- settings changes;
- OTA upload/reboot/status;
- CRUD and import/export actions;
- explicit room commands such as start/stop/reset/approve.

The current HTTP handlers should stay available and remain the source of truth
for commands and snapshots.

Commands stay HTTP-only in v1.

WebSocket should be used for notifications and live events only, not for
start/stop/reset/save/import/export commands.

### WebSocket Responsibilities

Use WebSocket only for live events that make the desktop UI feel immediate.

The desktop client should:

1. authenticate via HTTP;
2. fetch `GET /api/meta`;
3. fetch initial state via HTTP;
4. open `/api/ws`;
5. subscribe to live topics;
6. resync via HTTP after reconnect or sequence gap.

### Reconnect And Resync Rule

On reconnect or sequence gap:

1. close stale local WebSocket state;
2. fetch a fresh HTTP snapshot;
3. reopen and resubscribe the WebSocket connection;
4. resume applying live events only after snapshot generation and sequence are
   aligned.

### WebSocket Backpressure Policy

The firmware must not keep an unbounded outgoing event queue per client.

Rules:

- each WebSocket client has a small bounded outgoing queue;
- coalescable events may replace older pending events of the same type/scope;
- timeline/audit append events are normally delivered in order, but if a client
  falls behind far enough to require forced resync, the connection should be
  closed rather than allowing unbounded queue growth;
- if the queue overflows or the client is too slow, close the connection;
- the desktop app must reconnect and fetch a fresh HTTP snapshot.

## `GET /api/meta`

Add a small metadata endpoint for app compatibility and connection UX.

Example response:

```json
{
  "device_id": "scenehub-main",
  "device_name": "SceneHub Control",
  "firmware_version": "1.8.0",
  "api_version": 1,
  "build": {
    "git_sha": "abc1234",
    "build_date": "2026-05-09"
  },
  "capabilities": {
    "gm": true,
    "ota": true,
    "audio": true,
    "hardware_io": true,
    "ws": true
  },
  "limits": {
    "max_rooms": 8,
    "max_devices": 32,
    "max_ws_clients": 2
  }
}
```

The desktop app should use this for:

- device identification;
- compatibility checks;
- feature gating;
- cleaner connect/disconnect UX;
- limits-aware UI behavior.

### Compatibility Policy

The desktop app should explicitly support one or more `api_version` values.

If firmware `api_version` is older or newer than expected:

- show a compatibility warning;
- disable unsupported features based on `capabilities`;
- allow safe fallback actions where possible.

## WebSocket Model

### Principle

Do not stream full snapshots.

Do not send periodic timer ticks.

Do not push every internal state mutation independently if several mutations can
be coalesced into one meaningful UI event.

All live events should use one common envelope.

### Event Envelope

```json
{
  "type": "room.runtime.changed",
  "seq": 1012,
  "schema_version": 1,
  "snapshot_generation": 42,
  "server_time_ms": 24590122,
  "payload": {
    "room_id": "room_a",
    "runtime": {}
  }
}
```

Required envelope fields:

- `type`
- `seq`
- `schema_version`
- `snapshot_generation`
- `server_time_ms`
- `payload`

`server_time_ms` should be monotonic uptime-style milliseconds, not necessarily
wall-clock time. This is important for timer baselines, event ordering,
latency/debug work and reconnect diagnostics.

### Snapshot Generation Scope

For v1, use one global desktop-visible `snapshot_generation`.

It increments when any read model change can affect desktop-visible state.

If this becomes too coarse later, split it into scoped generations such as:

- `gm_generation`
- `room_runtime_generation`
- `device_generation`
- `timeline_generation`

### Event Types

Initial recommended event set:

- `room.runtime.changed`
- `device.state.changed`
- `issues.changed`
- `timeline.appended`
- `audit.appended`

Optional later events:

- `audio.state.changed`
- `hardware_io.state.changed`

### `room.runtime.changed`

Produced when a room runtime-visible state changes:

- game start/stop/reset;
- timer pause/resume/reset/add;
- step index changes;
- wait type changes;
- operator approval transitions;
- hint send/clear;
- scenario runtime state changes.

Example:

```json
{
  "type": "room.runtime.changed",
  "seq": 1012,
  "schema_version": 1,
  "snapshot_generation": 42,
  "server_time_ms": 24590122,
  "payload": {
    "room_id": "room_a",
    "runtime": {
      "session_present": true,
      "session_state": "running",
      "timer_state": "running",
      "timer_duration_ms": 3600000,
      "timer_remaining_ms": 1820000,
      "scenario_runtime_state": "waiting",
      "scenario_current_step_index": 12,
      "scenario_wait_type": "event",
      "scenario_wait_event_type": "door_opened",
      "scenario_wait_source_id": "altar_1"
    }
  }
}
```

### `device.state.changed`

Produced when device connectivity/health/runtime status changes materially.

Example:

```json
{
  "type": "device.state.changed",
  "seq": 1013,
  "schema_version": 1,
  "snapshot_generation": 42,
  "server_time_ms": 24590210,
  "payload": {
    "device_id": "altar_1",
    "state": {
      "connectivity": "online",
      "health": "ok",
      "runtime_state": "armed"
    }
  }
}
```

### WebSocket Client Limit

Initial limit should be explicit and small:

- `1` primary desktop console;
- `1` optional observer/service client.

`max_ws_clients` should be surfaced in `/api/meta`.

### `issues.changed`

Produced when room/system issues are added, removed or change severity.

This may carry:

- room-scoped issue summary;
- changed issue list for a room;
- a compact global summary.

### `timeline.appended`

Produced when a new timeline entry is added.

### `audit.appended`

Produced when a new audit entry is added.

## Timer Strategy

The desktop app should not receive one timer message per second.

Instead, the firmware should send timer baseline fields only when the timer
state changes materially:

- `timer_state`
- `timer_duration_ms`
- `timer_remaining_ms`
- `server_time_ms` and/or equivalent server baseline

The desktop app should animate the visible countdown locally.

This keeps the UI responsive without creating unnecessary firmware traffic.

## Backend Impact

This migration does not require a backend rewrite.

What stays:

- snapshot generation;
- PSRAM-backed read model;
- current HTTP handlers;
- current backend domain boundaries.

What is added:

- one WebSocket endpoint in `web_ui`;
- one event-to-WS bridge layer;
- event coalescing;
- bounded outgoing queues per client;
- slow-client disconnect handling;
- sequence numbers / generation tracking for live updates;
- small metadata endpoint;
- compact UI-facing event schemas.

What is not changed in v1:

- commands remain HTTP-only;
- snapshots remain the base resync model;
- room runtime ownership stays in firmware.

## Recommended Backend Event Flow

Preferred internal model:

1. domain/runtime state changes in existing modules;
2. existing snapshots/read models are updated as they are today;
3. significant changes are published into the in-process `event_bus`;
4. a `web_ui` live-update bridge subscribes to those internal events;
5. the bridge coalesces and serializes compact JSON WebSocket events;
6. desktop clients apply deltas or resync through HTTP when needed.

This keeps the current architecture intact and adds live delivery as a thin
integration layer.

## Candidate Event Producers

Likely event producers:

- `gm_core`
  - room session state;
  - timer state;
  - runtime step changes;
  - operator approval changes;
  - hints.
- `device_control_ingest`
  - telemetry/connectivity changes.
- `orchestrator_core`
  - device health/read model changes;
  - issues;
  - room summary changes.
- `orchestrator_timeline`
  - new timeline entries.
- `orchestrator_audit`
  - new audit entries.
- `audio_player`
  - optional audio playback status updates.
- `hardware_io`
  - optional channel state updates.

## Desktop App Structure

Suggested top-level structure:

```text
frontend/
  src/
    app/
    shared/
    domains/
      session/
      rooms/
      devices/
      profiles/
      scenarios/
      settings/
      timeline/
      audit/
      audio/
    pages/
    widgets/
```

Suggested domain split:

- `session`
- `rooms`
- `devices`
- `profiles`
- `scenarios`
- `settings`
- `timeline`
- `audit`
- `audio`

## Desktop Connection Flow

Phase 1 desktop connection flow should support:

- manual IP or hostname entry;
- recent controllers list;
- `GET /api/meta` compatibility check;
- visible connection status;
- reconnect action;
- later optional mDNS discovery.

mDNS discovery is useful, but should not block the first implementation.

## Contract Tests And Fixtures

Before the desktop app becomes the primary client, add contract coverage for:

- `/api/meta` schema;
- WebSocket event envelope schema;
- `room.runtime.changed` fixture;
- `device.state.changed` fixture;
- timer baseline behavior;
- reconnect and sequence-gap behavior.

JSON fixtures should live near backend/web contract tests so desktop and
firmware work can validate against the same examples.

## Delivery Phases

### Phase 0 - Preserve The Current System

- keep the current web UI in firmware;
- do not remove existing HTTP handlers;
- continue shipping the current product;
- treat the current browser UI as the operational fallback.

### Phase 1 - Prepare The Firmware

- add `GET /api/meta`;
- define `api_version`;
- define WebSocket event schemas;
- add `/api/ws`;
- implement WebSocket transport;
- implement event bridge;
- implement event coalescing;
- implement bounded outgoing queues;
- implement slow-client disconnect policy;
- add sequence numbers and reconnect/resync rules.

### Phase 2 - Build Desktop App Foundation

- create Tauri app shell;
- implement login/session handling;
- implement connection flow;
- implement device discovery/connect flow;
- implement initial HTTP client and query layer;
- implement WebSocket client and reconnect behavior.

### Phase 3 - Move Live Operator Surfaces First

Implement first:

- dashboard;
- room control;
- device monitoring;
- issues;
- timeline;
- audit.

These screens benefit most from WebSocket responsiveness and require fewer
editor-heavy flows.

### Phase 4 - Move Admin Editors

Implement after the live runtime surfaces are stable:

- profile editor;
- quest device setup;
- scenario editor;
- storage/import/export tooling.

### Phase 5 - Introduce Lightweight Embedded Fallback

- replace the heavy browser-first UI with a small rescue/service page;
- position it as the `Minimal Local HMI`;
- keep only setup, OTA and emergency controls;
- remove large embedded GM/editor bundles from firmware.

## Long-Term Guardrail

Long-term, heavy scenario design, simulation, reports and asset libraries may
remain desktop-only.

The firmware should store and run validated configured scenarios, not become the
owner of heavyweight editor UX again.

## Risks And Guardrails

### Risks

- designing an oversized WebSocket protocol;
- sending too many live events from the firmware;
- trying to migrate editors before room/device live control is stable;
- coupling the desktop app too tightly to one firmware build without explicit
  versioning.

### Guardrails

- keep HTTP as the command and snapshot boundary;
- use WebSocket for live deltas only;
- coalesce rapid room/device changes before push;
- limit event types in the first implementation;
- keep legacy web UI until desktop reaches practical parity;
- leave a minimal browser fallback even after desktop becomes primary.

## Acceptance Criteria

The migration is successful when:

- the desktop app becomes the main operator/admin interface;
- room runtime feels immediate without polling lag;
- the firmware no longer needs to serve a heavy full-featured browser UI;
- the embedded web surface remains usable as a Minimal Local HMI for setup, OTA,
  rescue and basic operator control;
- reconnect/resync is reliable through snapshot + live event layering;
- current backend modularity and API clarity are preserved.
