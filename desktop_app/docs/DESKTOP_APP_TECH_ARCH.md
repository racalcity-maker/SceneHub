# Desktop App Technical Architecture

## Goal

This document defines the technical architecture of the SceneHub desktop
application.

It translates the product direction from `DESKTOP_APP_UI_PLAN.md` and the
controller/API boundaries from `DESKTOP_CONSOLE_PLAN.md` into a concrete
frontend application structure.

The target is a desktop application that is:

- modular;
- testable;
- explicit about boundaries;
- aligned with the SceneHub backend domain model;
- safe for long-term growth.

## Architectural Position

The desktop app is a client of `SceneHub Controller`.

It is not:

- the runtime owner;
- a local duplicate of controller business logic;
- a thin wrapper around the current browser UI;
- a place where controller truth is recomputed ad hoc in components.

The application should behave as a disciplined client for:

- controller snapshots;
- live controller events;
- explicit controller commands.

## Core Principles

### 1. Mirror Backend Domains, Not Browser Pages

The backend already has good module boundaries. The frontend should align with
those domains instead of centering architecture around page files alone.

Primary desktop domains should map to:

- controller/session;
- room runtime;
- devices;
- issues;
- timeline;
- settings;
- profiles;
- scenarios;
- audio/hardware control where needed.

### 2. One Source Of Runtime Truth

Runtime truth comes from:

- HTTP snapshots;
- WebSocket live events.

Local state may represent only:

- UI selection;
- layout state;
- filters;
- connection/session state;
- command feedback state;
- editor drafts.

Optimistic UI is allowed only for command feedback state.

Do not optimistically mutate room runtime, device state or scenario runtime
unless the controller response explicitly returns the new committed state.

Runtime truth must be confirmed by HTTP snapshot or live event.

### 3. Transport Is A Boundary, Not A Convenience Utility

HTTP and WebSocket logic must not leak directly into UI components.

Components should consume:

- typed queries;
- typed selectors;
- typed mutations;
- typed command hooks.

### 4. Editors Are Not Ordinary Forms

Simple forms can use `React Hook Form`.

Complex editors such as scenario editing must use:

- dedicated model mapping;
- reducer/store logic;
- validation pipeline;
- explicit save/dirty/discard behavior.

### 5. Shared Code Must Stay Small And Boring

`shared/` should contain only:

- UI primitives;
- general-purpose helpers;
- transport primitives;
- cross-cutting validation and utility code.

It must not become a dumping ground for half-domain logic.

## Technology Stack

### Desktop Shell

- `Tauri 2`

### Frontend

- `React`
- `TypeScript`
- `Vite`

### State And Data

- `TanStack Query` for server state and mutations
- `Zustand` for app/session/controller UI state
- dedicated reducers/stores for complex editors
- `React Hook Form` for simple and medium-complexity forms
- `zod` or equivalent runtime validation for transport schemas

### Testing

- `Vitest` for unit and integration tests
- `Testing Library` for component behavior
- `MSW` or equivalent for HTTP mocking
- local WebSocket test harness for event-flow tests
- end-to-end coverage later through Tauri-compatible automation

## Top-Level Architecture

Use a layered modular monolith:

```text
Desktop Shell (Tauri)
  -> App Shell
  -> Platform Services
  -> Domain Modules
  -> Features
  -> Widgets / Pages
  -> Shared UI / Utilities
```

The important rule is directional dependency:

- pages depend on widgets/features/domains;
- widgets depend on features/domains/shared;
- features depend on domains/shared;
- domains depend on shared and platform transport only;
- shared depends on nothing project-specific.

## Layer Model

### 1. Tauri Shell Layer

Responsibilities:

- window lifecycle;
- menu and desktop integration;
- local app storage bridge if needed;
- optional mDNS discovery bridge if browser-only discovery is insufficient;
- future updater and native OS hooks.

Non-responsibilities:

- controller runtime logic;
- domain state derivation;
- editor business logic;
- API contract ownership.

Rust should stay thin. The application should remain mostly a TypeScript
product unless a native integration clearly requires Rust.

### 2. App Shell Layer

Responsibilities:

- application bootstrap;
- providers;
- router;
- theme/global layout;
- global connection/auth banners;
- controller selection shell;
- top-level error boundaries.

Files typically live under:

```text
src/app/
```

### 3. Platform Services Layer

Responsibilities:

- HTTP client;
- WebSocket client;
- heartbeat or liveness tracking primitives;
- local persistence for recent controllers and preferences;
- environment/platform adapters.

This layer knows how to talk to the outside world, but not what a room, device
or scenario means.

### 4. Domain Layer

Responsibilities:

- typed transport contracts for one domain;
- domain mappers between API payloads and frontend model shapes;
- query definitions;
- command/mutation definitions;
- live event application logic;
- selectors and derived read models for UI.

This is the main architectural center of the frontend.

### 5. Feature Layer

Responsibilities:

- combine one or more domain operations into user-facing workflows;
- encapsulate command sequences and local interaction logic;
- expose reusable behavior to pages/widgets.

Examples:

- controller connection flow;
- login flow;
- room command execution;
- profile save flow;
- scenario validation-and-save flow.

### 6. Widget Layer

Responsibilities:

- substantial reusable UI blocks composed from features and domain hooks.

Examples:

- room runtime panel;
- controller picker;
- issue list;
- timeline feed;
- profile editor pane;
- scenario editor workspace.

### 7. Page Layer

Responsibilities:

- route-level composition only.

Pages should orchestrate widgets and route-level layout, not own transport or
domain mutation details directly.

## Recommended Source Structure

Use this as the initial project layout:

```text
desktop_app/
  src-tauri/
  src/
    app/
      main.tsx
      router.tsx
      providers.tsx
      shell/
        AppFrame.tsx
        TopToolbar.tsx
        StatusBar.tsx
        RouteGuards.tsx
    shared/
      ui/
      lib/
      styles/
      validation/
      testing/
    platform/
      http/
        client.ts
        errors.ts
      ws/
        client.ts
        envelope.ts
        subscriptions.ts
      persistence/
        app_storage.ts
        controller_storage.ts
      discovery/
        discovery_service.ts
    domains/
      controller/
      session/
      rooms/
      devices/
      issues/
      timeline/
      settings/
      profiles/
      scenarios/
      audio/
    features/
      controller-connection/
      auth-login/
      room-command/
      hint-control/
      device-command/
      backup-restore/
    widgets/
      controller-picker/
      controller-status/
      room-runtime-panel/
      room-sidebar/
      manual-controls/
      issue-list/
      timeline-feed/
      profile-editor/
      quest-device-editor/
      scenario-editor/
    pages/
      gm-dashboard/
      gm-rooms/
      gm-room/
      devices/
      issues/
      timeline/
      settings/
```

## Domain Module Contract

Each domain folder should follow a predictable internal structure.

Example:

```text
domains/rooms/
  api/
    room_http.ts
    room_ws.ts
    room_schemas.ts
  model/
    room_types.ts
    room_mappers.ts
    room_selectors.ts
  queries/
    room_queries.ts
    room_query_keys.ts
  commands/
    room_commands.ts
  events/
    room_event_apply.ts
  state/
    room_ui_store.ts
  tests/
```

Not every domain needs every subfolder, but the pattern should stay consistent.

### Domain Public API Rule

Each domain exposes a public `index.ts`.

Other modules may import only from the domain public API, not from internal
`api/`, `model/`, `events/` or `state/` folders directly.

Good:

```ts
import { useRoomRuntime, roomSelectors } from "@/domains/rooms";
```

Bad:

```ts
import { applyRoomRuntimeEvent } from "@/domains/rooms/events/room_event_apply";
```

## State Architecture

### Server State

Use `TanStack Query` for:

- controller metadata;
- session info;
- GM dashboard snapshots;
- room runtime snapshots;
- device lists;
- issue lists;
- timeline/audit slices;
- settings reads;
- save/load/import/export command results.

Server state should live in the query cache, not in ad hoc Zustand mirrors.

### Stale Cache Policy

Runtime queries become stale when:

- WebSocket disconnects;
- no live event or heartbeat is received within the configured timeout;
- a sequence gap is detected;
- controller identity changes;
- auth/session expires.

When runtime data is stale:

- visible state remains readable;
- risky commands are disabled or require reconnect;
- a stale banner is shown globally and in affected room views.

### Live Event State

WebSocket events should not update UI components directly.

Use a pipeline:

1. receive and validate envelope;
2. route by event type;
3. map payload into domain event;
4. apply event to query cache or scoped live store;
5. notify UI through normal query/store subscription.

This keeps live updates composable and testable.

### App/Connection State

Use `Zustand` for cross-cutting app state such as:

- selected controller;
- connection status;
- current auth/session display state;
- last event timestamp;
- last heartbeat timestamp;
- stale-data flags;
- layout mode;
- user UI preferences;
- command feedback map.

Do not store large domain snapshots in Zustand if they already live in query
cache.

The connection manager should own:

- `last_event_time`;
- `last_heartbeat_time`;
- stale timeout;
- reconnect timeout.

### Editor State

Use dedicated reducer/store logic for:

- scenario editor;
- quest device editor if it grows beyond simple forms;
- complex backup/restore preview flows if needed.

Editor state should explicitly track:

- original loaded version;
- current draft;
- dirty state;
- validation state;
- save state;
- conflict/stale warning state.

## Transport Architecture

### HTTP Boundary

HTTP remains the command and snapshot boundary.

Rules:

- commands go through typed mutation functions;
- snapshots go through typed query functions;
- raw `fetch` must not appear in components;
- mutation side effects must be centralized.

### WebSocket Boundary

WebSocket is live-events-only in v1.

Rules:

- UI never sends room/device/scenario commands over WebSocket;
- WebSocket clients emit validated envelopes only;
- event application is domain-owned;
- reconnect/resync is orchestrated centrally, not piecemeal by pages.

The WebSocket layer should track liveness through ping/pong or heartbeat events.

### Validation

All transport payloads should be validated at runtime at the boundary.

Validate:

- `/api/meta`
- session info
- key HTTP snapshots
- WebSocket envelopes
- live event payloads for known event types

On invalid payload:

- mark transport error;
- log enough diagnostic information;
- prefer safe degradation and resync over undefined partial state.

### Capability-Gated Features

Feature modules must check controller capabilities before enabling screens,
queries or commands that depend on optional firmware features.

Examples:

- `audio=false` -> hide or disable audio controls;
- `hardware_io=false` -> hide hardware IO setup;
- `ws=false` -> fall back to polling mode or show unsupported firmware warning.

## Query Key Strategy

Use explicit stable query keys.

Examples:

- `['controller', controllerId, 'meta']`
- `['controller', controllerId, 'session']`
- `['controller', controllerId, 'gm', 'dashboard']`
- `['controller', controllerId, 'room', roomId, 'runtime']`
- `['controller', controllerId, 'devices']`
- `['controller', controllerId, 'issues']`
- `['controller', controllerId, 'timeline']`
- `['controller', controllerId, 'settings', section]`

Always include controller identity in keys so multi-controller support stays
clean.

## Live Event Application Strategy

### General Rule

Prefer patching existing query cache entries over broad invalidation when:

- the event is small;
- the affected query scope is known;
- applying the delta is deterministic.

Prefer invalidation or resync when:

- event ordering is uncertain;
- state gap is detected;
- event semantics are too coarse;
- the local cache entry is absent or incompatible.

### Domain Ownership

Each domain owns application of its own events.

Examples:

- `domains/rooms/events/room_event_apply.ts`
- `domains/devices/events/device_event_apply.ts`
- `domains/issues/events/issues_event_apply.ts`

Do not centralize all event application in one giant switch file with hidden
domain coupling.

### Cross-Domain Mutation Rule

A domain may read other domain public types or selectors when needed, but it
must not mutate another domain's query cache directly.

Cross-domain workflows belong in features.

## Command Execution Model

All commands should go through feature-level or domain-level command hooks.

Command pipeline:

1. UI triggers a typed command;
2. command feedback entry becomes `pending`;
3. HTTP mutation executes;
4. response/error maps into command feedback state;
5. affected queries are patched or invalidated;
6. later live events confirm the resulting runtime state.

This keeps the UI responsive without inventing runtime truth.

Do not optimistically mutate room runtime, device state or scenario runtime
unless the controller response explicitly returns the committed state change.

## Command Feedback Store

Maintain a small scoped command feedback store keyed by:

- controller id;
- room id or device id where applicable;
- command instance id.

Feedback entries should include:

- status;
- started time;
- completion time;
- short user-facing message;
- optional structured error code.

Do not overload domain snapshot stores with ephemeral command feedback.

## Typed Error Model

Use a typed app error model instead of ad hoc message passing.

Example shape:

```ts
type AppError =
  | { kind: "connection"; message: string; retryable: boolean }
  | { kind: "auth"; message: string; action: "login" | "logout" }
  | { kind: "schema"; message: string; endpoint?: string }
  | { kind: "command"; code?: string; message: string }
  | { kind: "capability"; feature: string; message: string }
  | { kind: "validation"; issues: ValidationIssue[] };
```

This should drive:

- user-facing error presentation;
- retry behavior;
- logging classification;
- feature fallback behavior.

## Connection Architecture

### Controller Registry

Maintain a local controller registry model:

- saved controllers;
- discovered controllers;
- last used controller;
- active controller.

This should be a dedicated module, not hidden in generic local storage helpers.

### Connection Manager

Create one connection manager module that owns:

- active controller lifecycle;
- initial meta fetch;
- login/session restore;
- WebSocket connect/disconnect;
- heartbeat tracking;
- stale-data timers;
- reconnect policy;
- resync triggers.

The rest of the app should consume connection state, not implement its own.

### Discovery

Discovery should be abstracted behind one service interface so the app can start
with manual + saved controllers and later add mDNS without restructuring every
page.

## Navigation Architecture

Use route-level pages for the main areas:

- GM dashboard
- GM rooms
- GM room
- Devices
- Issues
- Timeline
- Settings

Do not create separate top-level routes for every tiny subsection too early.
Use nested route composition inside product areas where necessary.

Pages may own route params and layout composition only.

If a page grows command logic, connection logic or editor logic, extract it into
a feature, widget or domain module.

## Settings And Editors Separation

Keep system settings and content editors separate at architecture level too.

Recommended split:

- `domains/settings/`
- `domains/profiles/`
- `domains/scenarios/`
- `domains/devices/`

Even if the UI later groups them under shared admin navigation, the code should
not merge them into one giant admin module.

## Scenario Editor Architecture

The scenario editor deserves its own bounded subsystem.

Recommended internal split:

- transport layer for scenario load/save/validate;
- mapper between API payload and editor draft model;
- reducer for structural edits;
- validation layer;
- workspace widgets;
- save/discard/conflict flow.

Suggested editor sub-areas:

- scenario browser;
- branch navigator;
- step list;
- inspector panel;
- validation panel;
- preview/debug helpers later.

The scenario editor should not depend on DOM reads or implicit form extraction.

## Error Handling Strategy

Classify errors into:

- connection errors;
- auth/session errors;
- transport schema errors;
- controller capability mismatch;
- command execution errors;
- editor validation errors.

Each class should have:

- logging policy;
- user-facing message policy;
- fallback behavior.

Not every error deserves a global modal.

## Fixture-Driven Development

The desktop app should support fixture or mock mode for development without
hardware.

Mock mode should use contract fixtures for:

- `/api/meta`
- session
- dashboard
- room runtime
- devices
- issues
- timeline
- WebSocket event streams

This allows UI and interaction work to progress without a permanently connected
ESP32 controller.

## Logging And Diagnostics

Add a structured app-side logger abstraction early.

Useful categories:

- connection lifecycle;
- HTTP request failures;
- WebSocket lifecycle;
- event validation failures;
- command execution outcomes;
- editor save/validation flows.

The logger should support:

- development console output;
- optional file export later;
- redaction of secrets.

## Testing Strategy

### Unit Tests

Focus on:

- mappers;
- selectors;
- reducers;
- event application functions;
- command feedback state transitions;
- validation helpers.

### Contract Tests

Use JSON fixtures aligned with backend contract documents and tests.

Validate:

- `/api/meta`
- WebSocket envelope
- room runtime event fixtures
- device event fixtures
- reconnect/resync handling

### Integration Tests

Test:

- query + event interaction;
- reconnect flow;
- stale-data behavior;
- command feedback flow;
- editor save/validate flows.

### Component Tests

Test:

- room control command states;
- stale banners;
- controller picker;
- issue list filtering;
- scenario editor structural interactions.

### End-To-End Tests

Later, automate:

- connect to controller;
- login;
- open room;
- execute safe commands;
- verify live updates;
- switch controller;
- offline/reconnect behavior.

## Build And Release Architecture

### App Packaging

Use Tauri packaging per platform.

The frontend should remain buildable independently from firmware builds.

### Bundle And Asset Discipline

Heavy editors should be lazy-loaded by route or feature boundary.

Initial app load should include only:

- app shell;
- controller connection flow;
- GM dashboard and room-control basics.

The following may be code-split:

- scenario editor;
- backup/restore tools;
- diagnostics-heavy views;
- optional advanced admin tooling.

### Configuration

Keep app configuration separate from controller data.

Local app config examples:

- saved controllers;
- recent controllers;
- UI preferences;
- window state;
- last selected route/room where appropriate.

Do not persist controller runtime truth as local app configuration.

## Coding Rules

These rules should be treated as architecture constraints:

- no raw transport calls in components;
- no global monolithic store for all state;
- no domain logic inside shared UI primitives;
- no cross-domain imports that skip intended boundaries;
- no "utils" folder that quietly owns business rules;
- no scenario editor state hidden in DOM extraction;
- no untyped event handling path.

## Runtime-Connected And Offline Project Modes

Long-term, the architecture should support two distinct operating modes.

### Runtime-Connected Mode

Controller is the source of truth.

This mode allows:

- live runtime visibility;
- controller commands;
- live device and issue monitoring.

### Offline Project Mode

Local project or config file is the source of editable draft truth.

This mode does not allow runtime commands.

Upload or publish back to a controller must be explicit.

## Recommended First Implementation Order

1. app shell and controller connection manager
2. typed `/api/meta` and session flows
3. dashboard snapshot queries
4. room runtime snapshot queries
5. WebSocket envelope validation and event bus
6. room/device/issues live event application
7. room command feedback model
8. devices and timeline views
9. settings forms
10. editors

This order keeps the first delivered architecture centered on live operation,
not on admin tooling.

## Long-Term Growth Path

The architecture should leave room for:

- observer mode;
- operator locked mode;
- offline project/config inspection;
- richer simulation tools;
- reports and diagnostics;
- desktop-only scenario design features.

Those should grow as separate features and bounded modules, not by collapsing
more behavior into one central app store.

## Acceptance Criteria

The architecture is healthy when:

- the app can add a new controller-facing domain without touching unrelated
  pages heavily;
- live runtime flows do not depend on page-level polling hacks;
- connection logic is centralized;
- editor complexity is isolated from live operation screens;
- transport, domain logic and presentation remain visibly separate;
- the code structure still makes sense after scenario editor and settings land.
