# Desktop App Bootstrap Backlog

## Goal

This backlog turns the desktop application plans into an implementation order.

It is intentionally focused on:

- first working controller connection;
- first live GM surfaces;
- first controller/API dependencies;
- avoiding early scope creep into heavy editors.

## Current Priority Order

1. Controller/API bootstrap
2. Desktop shell and connection flow
3. GM live runtime MVP
4. Devices/issues/timeline
5. Settings
6. Editors

## Phase 0 - Working Rules

- [ ] Keep legacy embedded full web UI untouched during bootstrap.
- [ ] Treat desktop app as desktop-first MVP, not feature parity target on day 1.
- [ ] Do not start scenario editor work before live GM room control is stable.
- [ ] Keep commands HTTP-only.
- [ ] Keep WebSocket limited to live events.

## Phase 1 - Controller/API Prerequisites

These items are required on the controller side before the desktop app can feel
correct.

### Metadata And Identity

- [ ] Add `GET /api/meta`.
- [ ] Include `product_id` or equivalent SceneHub identity marker.
- [ ] Include `device_id`, `device_name`, `hostname`, `firmware_version`, `api_version`.
- [ ] Include `capabilities`.
- [ ] Include `limits`.
- [ ] Include build metadata (`git_sha`, `build_date`) if available.

### WebSocket Transport

- [ ] Add `/api/ws`.
- [ ] Implement common WS event envelope.
- [ ] Add `seq`.
- [ ] Add `schema_version`.
- [ ] Add `snapshot_generation`.
- [ ] Add `server_time_ms`.
- [ ] Add subscription model.

### WebSocket Runtime Safety

- [ ] Implement bounded outgoing queue per client.
- [ ] Implement event coalescing where appropriate.
- [ ] Implement slow-client disconnect policy.
- [ ] Implement heartbeat or ping/pong liveness model.
- [ ] Implement reconnect/resync compatibility with HTTP snapshots.

### First Event Set

- [ ] Emit `room.runtime.changed`.
- [ ] Emit `device.state.changed`.
- [ ] Emit `issues.changed`.
- [ ] Emit `timeline.appended`.
- [ ] Emit `audit.appended` if needed for MVP or near-MVP.

### Contract Coverage

- [ ] Add `/api/meta` contract tests.
- [ ] Add WS envelope contract tests.
- [ ] Add room runtime event fixture(s).
- [ ] Add device state event fixture(s).
- [ ] Add reconnect/sequence-gap behavior coverage.

## Phase 2 - Desktop Project Bootstrap

This phase is about making the desktop project runnable and structurally sound.

### Tooling

- [ ] Install desktop app dependencies.
- [ ] Verify `npm run check`.
- [ ] Verify `npm run dev`.
- [ ] Verify `npm run build`.
- [ ] Verify `npm run tauri:dev`.

### App Shell

- [ ] Keep current app shell as the root frame.
- [ ] Add route guards / app-level error boundary.
- [ ] Add top toolbar connection area.
- [ ] Add status bar with connection/stale/auth state.
- [ ] Add shared empty/error/loading state components.

### Mock Mode

- [ ] Add fixture/mock mode for development without hardware.
- [ ] Support mock `/api/meta`.
- [ ] Support mock session.
- [ ] Support mock dashboard/room runtime/devices/issues/timeline.
- [ ] Support mock WS event stream playback.

## Phase 3 - Controller Connection Flow

This is the first meaningful app behavior.

### Local Controller Registry

- [ ] Implement stored controller model.
- [ ] Store last-used controller.
- [ ] Store recent controllers.
- [ ] Add active controller selection state.

### Connection Manager

- [ ] Add controller meta fetch flow.
- [ ] Add session restore flow.
- [ ] Add login flow.
- [ ] Add WebSocket connect/disconnect lifecycle.
- [ ] Track `last_event_time`.
- [ ] Track `last_heartbeat_time`.
- [ ] Track stale/reconnect status.

### Controller Picker UX

- [ ] Add `Switch Controller` entry in toolbar.
- [ ] Add `Current / Recent / Discovered / Add manually` picker structure.
- [ ] Support manual IP/hostname connect.
- [ ] Display unsupported firmware/API mismatch state.

### Discovery

- [ ] Stub discovery service abstraction.
- [ ] Add manual + saved controller flow first.
- [ ] Add mDNS discovery later behind same abstraction.

## Phase 4 - Authentication And Session UX

- [ ] Add session query module.
- [ ] Add login screen or modal flow.
- [ ] Add controller-scoped auth state.
- [ ] Add auth-expired handling.
- [ ] Add operator/admin role visibility rules.
- [ ] Add observer/read-only role path later if backend supports it.

## Phase 5 - GM MVP

This is the first real value milestone.

### Dashboard

- [ ] Add GM dashboard route.
- [ ] Load dashboard snapshot.
- [ ] Show room cards or rows.
- [ ] Show top-level room/session/issue summaries.
- [ ] Add open-room action.

### Rooms View

- [ ] Add rooms list view.
- [ ] Add room filtering/sorting scaffold.
- [ ] Add quick open-room flow.

### Room Control

- [ ] Add room runtime query.
- [ ] Add live room runtime event application.
- [ ] Render timer, step, wait state and room summary.
- [ ] Render safe room commands: start/stop/reset/approve.
- [ ] Add command feedback UI.
- [ ] Add stale data behavior in room view.
- [ ] Keep editor-style controls out of live room surface.

### Runtime Safety

- [ ] Disable risky controls when controller state is stale/offline.
- [ ] Show global stale banner.
- [ ] Show room-local stale indicator.
- [ ] Confirm runtime truth only from snapshot/live event.

## Phase 6 - Devices / Issues / Timeline

### Devices

- [ ] Add device overview route.
- [ ] Load device snapshot.
- [ ] Apply live device state events.
- [ ] Add safe manual device command flow.

### Issues

- [ ] Add issues route.
- [ ] Load issues snapshot.
- [ ] Apply issue change events.
- [ ] Add room/device filtering.

### Timeline

- [ ] Add timeline route.
- [ ] Load initial timeline snapshot.
- [ ] Apply timeline append events.
- [ ] Add freeze/live-scroll behavior later.

## Phase 7 - Settings MVP

Only after GM runtime surfaces are stable.

- [ ] Add settings route shell.
- [ ] Add controller/network sections.
- [ ] Add accounts section.
- [ ] Add OTA section.
- [ ] Add audio/settings sections as capability-gated features.
- [ ] Add storage shell.
- [ ] Respect capabilities when hiding/disabling sections.

## Phase 8 - Backup / Restore

- [ ] Add export configuration flow.
- [ ] Add import file select flow.
- [ ] Add validation preview.
- [ ] Add compatibility warning.
- [ ] Add explicit overwrite confirmation.
- [ ] Add pre-import backup behavior where feasible.

## Phase 9 - Editors

Do not start this phase early.

### Profiles Editor

- [ ] Add profile list/load/save/delete flows.
- [ ] Add dirty tracking.
- [ ] Add validation and save feedback.

### Quest Device Editor

- [ ] Add quest device list/editor split layout.
- [ ] Add observed-client binding flow.
- [ ] Add capability import flow.
- [ ] Add save/delete/dirty handling.

### Scenario Editor

- [ ] Add dedicated editor subsystem.
- [ ] Add scenario browser + workspace.
- [ ] Add reducer/store for structural edits.
- [ ] Add validation panel.
- [ ] Add save/discard/conflict handling.
- [ ] Keep it lazy-loaded.

## Phase 10 - Desktop-Only Enhancements

- [ ] Observer mode
- [ ] Operator locked mode
- [ ] Keyboard shortcuts
- [ ] Pinned rooms
- [ ] Better reports/diagnostics
- [ ] Offline project mode
- [ ] Simulation/preview tools

## First MVP Definition

The first desktop MVP is complete when:

- [ ] app starts and connects to a controller;
- [ ] app can identify a valid SceneHub controller through `/api/meta`;
- [ ] login/session flow works;
- [ ] dashboard works;
- [ ] room control works with live updates;
- [ ] stale/offline states are explicit;
- [ ] command feedback is visible and reliable;
- [ ] devices/issues/timeline basic routes work;
- [ ] heavy editors are still deferred.

## Explicit Non-Goals For MVP

- [ ] full scenario editor parity
- [ ] full settings parity with old web UI
- [ ] multi-controller simultaneous operation
- [ ] offline project editing
- [ ] simulation/reporting suite
- [ ] visual perfection before runtime correctness
