# Desktop App UI Plan

## Goal

This document defines the SceneHub desktop application's product model, user
experience, screen structure and UI architecture.

It is separate from `DESKTOP_CONSOLE_PLAN.md`.

Use this document for:

- desktop application navigation;
- controller discovery and connection UX;
- operator/admin workflows;
- screen composition;
- editor scope and behavior;
- UI architecture and frontend module boundaries.

Use `DESKTOP_CONSOLE_PLAN.md` for:

- firmware/API responsibilities;
- HTTP/WebSocket transport boundaries;
- live event model;
- embedded fallback scope;
- backend migration phases.

## Product Position

The desktop application is the primary operator/admin console for SceneHub.

It should replace the current browser GM panel as the main working surface.

It is not:

- the runtime owner;
- the source of truth for room state;
- a thin browser wrapper around the old web UI.

It should be a desktop-native control application built around the current
SceneHub backend model.

## Design Principles

### 1. Controller-First UX

The application works with one or more `SceneHub Controllers`.

The user should always know:

- which controller is selected;
- whether it is connected;
- whether the session is authenticated;
- whether the displayed data is live or stale.

### 2. Room Operation Comes First

The primary workflow is room operation, not configuration.

The app should open into a GM-first surface, not a settings screen.

### 3. Desktop-Native, Not Browser-Like

The UI should not reproduce the current browser page structure literally.

Use desktop strengths:

- always-visible toolbar and status line;
- controller switcher;
- larger data-dense room and device views;
- faster navigation;
- keyboard shortcuts;
- multi-pane layouts where helpful.

### 4. Runtime And Editors Stay Separate

Live game operation and content editing should be clearly separated.

The operator should not feel like they are inside a form-heavy admin page while
running a room.

### 5. Safe By Default

Destructive or risky actions should be explicit:

- stop/reset confirmations where needed;
- save/discard behavior for editors;
- stale/offline indicators;
- clear auth/connection status.

## Main Product Areas

The desktop app should have five top-level areas:

- `GM`
- `Devices`
- `Issues`
- `Timeline`
- `Settings`

Admin-only content can exist inside these areas without turning the app into a
config-first product.

Inside `GM`, the product should clearly distinguish:

- `Dashboard` as the all-rooms operational overview;
- `Room Control` as the dedicated live operation surface for one room.

## Controller Model

The desktop app should treat the hardware unit as a first-class entity:

- `SceneHub Controller`

A controller record should include:

- `device_id`
- `device_name`
- `hardware_uid`
- `hostname`
- `last_known_ip`
- `firmware_version`
- `api_version`
- `capabilities`
- `last_seen`

## Connection Model

### Startup Behavior

On app start:

1. load `last_used_controller`;
2. attempt quick reconnect to that controller;
3. run controller discovery in parallel;
4. if reconnect succeeds, open the app normally;
5. if reconnect fails, show offline state and available controllers.

### Discovery Sources

Connection flow should support:

- last used controller;
- saved/recent controllers;
- local network discovery;
- manual IP/hostname entry.

Preferred discovery order:

1. saved controllers;
2. mDNS discovery;
3. manual connect.

### Identification

The app must identify a controller as SceneHub through:

- discovery identity such as mDNS service type;
- `GET /api/meta` product identity and API compatibility.

The app must not assume that an arbitrary ESP device on the network is a
SceneHub controller.

## Controller Selection UX

The app should always expose the current controller in the top toolbar.

Toolbar controller section:

- current controller name;
- live/offline/auth state;
- quick reconnect indicator;
- `Switch Controller` action.

`Switch Controller` opens a controller picker with:

- `Current`
- `Recent`
- `Discovered`
- `Add manually`

Each controller card should show:

- device name;
- device id;
- hostname or IP;
- firmware version;
- connection state;
- last seen.

## Authentication UX

Authentication is controller-scoped.

The app should support:

- unauthenticated discovery;
- login after selecting a controller;
- session reuse per controller where possible;
- clear expired-session handling.

The app should display:

- `Connected`
- `Authenticating`
- `Authenticated as admin`
- `Authenticated as operator`
- `Authentication expired`

The user must never be left guessing whether a command failed due to auth or
connection loss.

## Runtime Truth Boundary

The desktop app must not invent runtime truth.

Runtime state comes from controller snapshots and live events.

Local app state may represent only:

- selection;
- layout;
- filters;
- editor drafts;
- optimistic command feedback state.

## Primary Navigation

Recommended top navigation:

- `GM`
- `Devices`
- `Issues`
- `Timeline`
- `Settings`

Secondary navigation should live inside each area instead of exploding the top
level.

## Default Landing Screen

After successful connection and auth, open:

- `GM -> Dashboard`

If the user is an operator and exactly one room is active or pinned, the app may
optionally reopen:

- `GM -> Room`

Do not open into `Settings` by default.

## GM Area

The GM area is the main operational surface.

It should contain:

- `Dashboard`
- `Rooms`
- `Room Control`
- optional room-focused side panels

### GM Dashboard

Purpose:

- quick operational overview;
- room health at a glance;
- active session overview;
- fast entry into room control.

Show:

- all rooms;
- session state;
- timer state;
- selected mode/profile;
- current runtime state;
- active waits;
- issue counts;
- key device health summaries.

Actions:

- open room;
- quick emergency actions if explicitly approved in UX design;
- admin quick links where appropriate.

### Rooms View

Purpose:

- room list optimized for scanning and opening rooms quickly.

Desktop can support:

- denser rows than the current browser UI;
- pinned rooms;
- sorting and filtering;
- compact issue indicators.

### Room Control

Purpose:

- the primary live control screen for one room.

This should be the best screen in the application.

Core sections:

- room header;
- mode/profile selection;
- start/stop/reset;
- timer;
- current scenario step;
- wait state;
- operator approval;
- hints;
- room issues;
- room devices;
- manual controls.

Recommended layout:

- left/main column: runtime and control;
- right column: devices, manual actions, issues;
- persistent header with room identity, controller status and hot actions.

Rule:

`Room Control` must avoid editor-style controls unless the user explicitly
enters an admin editing flow outside the live operation surface.

### Room Control Principles

- live state must feel immediate;
- controls must not jump around visually;
- timer must animate locally between backend updates;
- wait/step transitions must be clearly visible;
- destructive actions must be explicit;
- room state must remain usable even during temporary reconnect.

This screen should not become an accidental admin workspace for profile edits,
scenario structure changes or quest-device configuration.

## Command Feedback Model

Operator actions must produce explicit command feedback.

Command feedback states:

- `pending`
- `accepted`
- `rejected`
- `timeout`
- `failed due to auth`
- `failed due to offline controller`
- `failed due to device/runtime condition`

The user must be able to tell:

- the command was sent;
- the controller accepted it;
- the controller rejected it;
- the command could not complete;
- why it could not complete.

This applies to actions such as:

- start/stop/reset game;
- approve/next actions;
- manual device commands;
- audio commands;
- hardware IO commands.

## Stale Data Policy

If data is stale:

- live controls become disabled or require explicit confirmation;
- the last update time is shown;
- room runtime remains visible but clearly marked stale;
- destructive actions are blocked unless the controller is connected and the
  session is authenticated.

Stale data must be obvious enough that an operator does not mistake it for live
runtime truth.

## Devices Area

Purpose:

- monitor devices;
- inspect health and connectivity;
- execute safe manual actions;
- perform admin device setup.

Subsections:

- `Overview`
- `Quest Devices`
- `Observed Clients`
- `Device Setup` (admin)

### Device Overview

Show:

- all quest devices;
- connectivity;
- health;
- runtime state;
- room assignment;
- key flags such as offline, degraded, missing binding.

### Observed Clients

Show:

- physical clients observed from control-contract traffic;
- whether each is already bound to a quest device;
- firmware/build info when available;
- quick path into binding/setup.

### Device Setup

Admin-only.

Use a proper editor layout rather than the current browser flow copied
verbatim.

Suggested desktop layout:

- device list on the left;
- selected device editor on the right;
- discovery/import panel;
- command/event capability tabs.

## Issues Area

Purpose:

- show all room/system issues in one focused place.

This should not be buried inside Dashboard only.

Views:

- active issues;
- grouped by room/system/device;
- severity filters;
- quick open room/device actions.

## Timeline Area

Purpose:

- operator-readable sequence of recent room/system events.

Desktop can support:

- richer filtering;
- room filter;
- event type filter;
- live append behavior;
- freeze/scrollback mode.

## Settings Area

Purpose:

- replace the current heavy admin home page with a settings-oriented area.

The settings area is not the main entry point of the product.

Suggested subsections:

- `Controller`
- `Network`
- `MQTT`
- `Accounts`
- `Audio`
- `OTA`
- `Storage`
- `Hardware IO`

This area can still host editors, but the main UX posture should remain:

- operational app first;
- settings/config second.

## Content Editors Area

System settings and content editors should not collapse into one undifferentiated
admin surface.

The product should visually separate:

- `Settings`
  - controller/system configuration;
- `Content / Editors`
  - profiles;
  - scenarios;
  - quest devices.

Shared admin navigation is fine, but the distinction should stay clear.

## Editor Strategy

### Profiles Editor

Use a clean side-by-side desktop editor:

- profile list;
- selected profile form;
- validation and save state;
- links to related room/scenario.

### Quest Device Editor

Should support:

- capability editing;
- observed-client binding;
- interface import;
- command/event management;
- dirty tracking;
- safe delete/save flows.

### Scenario Editor

This is not just a form.

Treat it as a dedicated desktop editor for a structured scenario model.

Recommended architecture:

- scenario list/browser;
- selected scenario workspace;
- branch/step navigator;
- details inspector;
- validation panel;
- runtime-preview-friendly structure where possible.

Future desktop-only improvements may include:

- simulation;
- richer diffing;
- searchable flags and devices;
- better branch visualization.

## Backup / Restore UX

Desktop should provide a first-class backup and restore flow.

Capabilities:

- export full controller configuration;
- import with validation preview;
- show firmware/API compatibility warnings;
- never overwrite important config without confirmation;
- create a pre-import backup where possible.

This should be treated as a real product feature, not as a hidden storage tool.

## Role Model

### Operator

Primary access:

- GM;
- room control;
- devices visibility;
- issues;
- timeline;
- hints;
- safe room actions.

No access to:

- destructive config changes;
- scenario editing;
- device definition editing;
- storage import/export;
- controller-critical settings.

### Admin

Has operator capabilities plus:

- full settings;
- profile/device/scenario editing;
- storage actions;
- OTA and system setup;
- hardware IO setup tools.

### Observer

Observer mode is read-only.

Observer access may:

- connect to a controller;
- watch dashboard, rooms, timeline and issues;
- inspect live state.

Observer access may not:

- execute commands;
- change settings;
- save editor changes.

This is useful for:

- service access;
- monitoring;
- second-screen display;
- debugging and support.

## Operator Locked Mode

The product should support a future `Operator Locked Mode`.

In this mode:

- admin navigation is hidden;
- the app opens directly into a pinned room or dashboard;
- settings and editors are not casually reachable;
- exiting restricted mode may require admin authentication.

This is important for real installations where operators need a safe, focused
working surface instead of the full product.

## Visual Direction

The desktop app should not look like a browser page stretched into a window.

High-level direction:

- dark control-room UI is acceptable if intentionally designed;
- strong hierarchy between live runtime, warnings and configuration;
- dense but readable data presentation;
- avoid oversized cards when table/list layouts are better;
- status color language must stay consistent across rooms/devices/issues;
- offline/stale states must be visually obvious.

## Connection And Runtime Status Language

The app should have one consistent status language.

Recommended states:

- `Connected`
- `Connecting`
- `Reconnecting`
- `Offline`
- `Auth required`
- `Session expired`
- `Stale data`

These states should be visible globally, not hidden inside one view.

## Recommended Frontend Module Structure

Suggested top-level structure:

```text
frontend/
  src/
    app/
      main.tsx
      router.tsx
      providers.tsx
      shell/
    shared/
      ui/
      lib/
      styles/
      http/
      ws/
    domains/
      controllers/
      session/
      rooms/
      devices/
      issues/
      timeline/
      settings/
      profiles/
      scenarios/
      audio/
    pages/
      gm-dashboard/
      gm-rooms/
      gm-room/
      devices/
      issues/
      timeline/
      settings/
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
```

## Local State Guidance

Use:

- `TanStack Query` for server state;
- `Zustand` for controller/session UI state, layout, selection and transient app
  state;
- dedicated reducer/store layers for complex editors;
- `React Hook Form` for simpler forms.

Do not put all editor logic into one global store.

The chosen top-level frontend structure is less important than preserving clean
boundaries. The app may keep the proposed `domains/` structure or evolve later
toward a more feature-sliced split, as long as room control, device monitoring
and editor logic do not collapse into one shared blob.

## Migration Relationship To Current Web UI

The desktop app should be built from scratch, but it should reuse the existing
product workflow knowledge from the current web UI.

Reuse from the current UI:

- room control concepts;
- device categories and actions;
- scenario/profile domain language;
- role boundaries;
- proven backend contracts.

Do not reuse blindly:

- page structure;
- manual DOM-era state shape;
- browser-specific render assumptions;
- polling-oriented interaction patterns.

## Phase Plan

### Phase A - App Foundation

- app shell;
- controller model;
- saved/recent controllers;
- manual connect;
- connection status;
- login/session UX;
- toolbar and navigation shell.

### Phase B - Live GM Surfaces

- dashboard;
- rooms view;
- room control;
- issues;
- timeline.

This should be the first high-value working milestone.

### Phase C - Devices

- device overview;
- observed clients;
- manual device actions;
- admin device setup.

### Phase D - Settings

- network;
- MQTT;
- accounts;
- OTA;
- audio;
- storage;
- hardware IO.

### Phase E - Editors

- profiles editor;
- quest device editor refinement;
- scenario editor.

### Phase F - Desktop-Only Enhancements

- keyboard shortcuts;
- pin/favorite rooms;
- richer simulation or preview tools;
- better reports and diagnostics;
- layout customization.

Longer-term optional direction:

- offline project mode;
- open exported config without a controller;
- inspect rooms/devices/scenarios offline;
- validate and prepare edits before upload;
- later upload prepared config back to a controller.

## Acceptance Criteria

The desktop app direction is correct when:

- users think in terms of selecting a SceneHub controller, not opening a web
  page;
- the app reconnects cleanly to the last-used controller;
- GM room control becomes the strongest and fastest workflow in the product;
- settings stop being the product home screen;
- the app is usable for both operator and admin roles;
- complex editors fit naturally into a desktop workspace;
- the product no longer depends on a heavy embedded browser UI for its main UX.
