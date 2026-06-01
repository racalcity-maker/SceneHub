# Architecture Layer Risk Map

This document records the practical layer map for SceneHub and the dependency
shapes that need extra scrutiny during refactors.

The durable rules live in `ARCHITECTURE.md`. This file exists to make the risk
areas explicit, so future changes do not slowly turn the runtime into a
cross-coupled system where every layer knows too much about every other layer.

## Target Direction

The intended dependency direction is:

```text
web_ui
  -> scenehub_control / scenehub_read_model
  -> gm_core / room_scenario / quest_device / device_control_ingest
  -> event_bus / mqtt_core / hardware_io / audio_player / storage
```

The practical law is:

- Commands go down through control/application services.
- Events go up through `event_bus` or component-owned queues.
- Read models only read and project state.
- Web UI only calls services and serializes/deserializes HTTP/WebSocket data.

## Acceptable Dependencies

These dependency shapes are expected and acceptable:

- `web_ui -> scenehub_control`
- `web_ui -> scenehub_read_model`
- `scenehub_control -> gm_core / room_scenario / quest_device`
- `gm_core -> registered command-plan dispatch hook`
- `scenehub_control -> command_executor`
- `command_executor -> mqtt_core / hardware_io / audio_player`
- `device_control_ingest -> event_bus`
- `mqtt_core -> event_bus`
- `scenehub_read_model -> gm_core / quest_device / device_control_ingest`

The key condition is direction. Write-side actions should enter through control
services. Read-side views should enter through read-model APIs. Lower layers
should not call back into UI or application handlers.

## Current Risk Areas

### Web UI Knowledge Of Low-Level DTOs

Risk:

- HTTP handlers can become application logic instead of thin adapters.
- UI-specific decisions can leak into domain/runtime structures.
- Runtime endpoints can accidentally pull broad DTOs into hot paths.

Target cleanup:

- Move write actions behind narrow control APIs.
- Keep handlers limited to parse, authorize, call service, serialize result.
- Use compact read-model DTOs for runtime/detail/summary paths.

Tracked by:

- `KNOWN_ISSUES.md` P2

### Broad Read-Model Surface

Risk:

- `scenehub_read_model` becomes a facade that knows too much about sessions,
  scenario layout, assets, device state, and storage.
- Public headers accumulate unrelated types.
- Small runtime reads can keep depending on large intermediate structures.

Target cleanup:

- Split broad public contracts into family-specific headers.
- Separate stable layout views from live runtime counters.
- Keep asset readiness and storage-heavy scans out of default runtime detail.

Tracked by:

- `KNOWN_ISSUES.md` P1, P2 and P6

### GM Core Access To Product Metadata

Risk:

- `gm_core/session` can start knowing too much about Quest Device metadata,
  storage layout, validation details, and product configuration.
- Scenario execution becomes harder to test without full SceneHub setup.
- Planned command execution can regress into lock-held external calls.

Target cleanup:

- Keep `gm_core` focused on session state, scenario runtime progression, waits,
  flags, timers, and command plans.
- Use command dispatch boundaries for external side effects.
- Use targeted ports/fakes for time, events, and command dispatch in tests.

Tracked by:

- `KNOWN_ISSUES.md` P2
- `ARCHITECTURE.md` GM Core Boundary

### SceneHub Control As New Heavy Facade

Risk:

- `scenehub_control` can become the new center where storage, runtime,
  hardware, device metadata and HTTP-shaped response concerns accumulate.
- The public umbrella header can force unrelated consumers to see broad DTO
  families such as profiles, scenarios, devices, hardware IO and GM runtime.
- Control orchestration can drift from "application facade" into a second
  runtime engine or domain owner.

Target cleanup:

- Keep `scenehub_control` as an application/write-side facade, not a domain
  subsystem.
- Split public API headers by family when the next expansion needs it:
  GM/session, scenarios, devices, profiles, sidebar presets and hardware IO.
- Keep internal implementation files family-oriented and avoid adding unrelated
  logic to the main facade file.
- Use narrow DTO/result envelopes for control responses; do not let HTTP JSON
  shapes define domain storage/runtime structs.

Tracked by:

- `KNOWN_ISSUES.md` P2 and P6
- `ARCHITECTURE.md` SceneHub Control Boundary

### Broad GM Room Session Header

Risk:

- `gm_room_session.h` exposes session control APIs, view DTOs, command-plan
  ports, prepared-start DTOs and runtime-shaped structs through one public
  include.
- Read/control/web-adjacent components can accidentally depend on internal
  runtime shape instead of narrow views or command ports.
- Future additions can make the header a compatibility anchor that blocks
  further `gm_core` isolation.

Target cleanup:

- Split the public surface when a real caller/dependency problem appears:
  control entrypoints, view DTOs, command-plan port and shared enums/types.
- Keep `gm_room_session_t` and branch runtime internals out of new external
  consumers where a projection or prepared DTO is enough.
- Prefer `scenehub_read_model` DTOs for UI/API serialization.

Tracked by:

- `KNOWN_ISSUES.md` P2 and P6
- `ARCHITECTURE.md` GM Core Boundary

### Event Bus Bridge Cycles

Risk:

- Transport bridges can reflect inbound messages back to the same transport.
- Generic events can become a hidden control path.
- Heavy work can creep back onto the bus dispatch task.

Target cleanup:

- Keep bus handlers adapter-only.
- Preserve explicit direction/origin policy for transport bridges.
- Post bounded follow-up work into component-owned queues/tasks/jobs.

Tracked by:

- `KNOWN_ISSUES.md` P0 regression coverage and P2

## Anti-Patterns

Avoid these shapes:

- `web_ui` mutates `gm_core` session internals directly.
- `web_ui` imports domain/session DTOs when a narrow view/control DTO would do.
- `read_model` dispatches commands, posts control events, or repairs domain
  state.
- `gm_core` reads storage or device files during runtime progression.
- `event_bus` handlers perform broad scans, JSON assembly, hardware side
  effects, or scenario progression inline.
- MQTT inbound messages are bridged back into MQTT without an explicit
  non-MQTT origin.
- Runtime detail endpoints rebuild stable layout/assets on every live refresh.

## Refactor Priority

When improving the architecture, prefer this order:

1. Split compact read DTOs from domain/session DTOs.
2. Split broad read-model public contracts.
3. Move Web UI write actions behind narrow control APIs.
4. Add isolated domain tests and fake ports around `gm_core`.
5. Re-evaluate bus topology only after the boundaries above are stable.

This order reduces immediate runtime cost and coupling before introducing more
abstract infrastructure.

## Progress Notes

- 2026-05-15: Runtime summary projection started moving off broad
  `orch_room_entry_t` intermediates. Room runtime summary now uses compact
  `gm_room_session_get_read_views(...)` data directly; detail projection still
  has broader branch/wait/asset needs and should be narrowed separately.
- 2026-05-15: Runtime detail live projection also moved off the
  `orch_room_entry_t` scratch path. Detail now fills summary, branches, waits,
  flags, and scenario device ids from `gm_room_session_get_projection_view(...)`;
  asset readiness remains explicitly gated by `include_assets`.
- 2026-05-15: Runtime read-model public DTOs were split into
  `orch_runtime_view.h`. `orchestrator_registry.h` remains a compatibility
  facade, while hot-path runtime HTTP code can include the narrower runtime
  contract directly.
- 2026-05-15: Scenario layout/detail DTOs were split into
  `orch_scenario_view.h`. The broad registry header remains compatible, while
  scenario schema/catalog code can include the narrower scenario contract.
- 2026-05-15: Device/control-device DTOs and device read APIs were split into
  `orch_device_view.h`. Issue DTOs still live in the registry facade; the device
  header only forward-declares them for pointer-based issue listing.
- 2026-05-15: Issue DTOs were split into `orch_issue_view.h`, removing the
  issue forward declaration from the device view contract.
- 2026-05-15: Room profile DTOs and profile list API were split into
  `orch_profile_view.h`.
- 2026-05-15: Room DTOs and room list/get APIs were split into
  `orch_room_view.h`. Simple GM room/profile handlers now include room/profile
  read contracts directly instead of the registry facade.
- 2026-05-15: Full snapshot/system summary DTOs and core registry lifecycle APIs
  were split into `orch_registry_snapshot.h`. `orchestrator_registry.h` is now
  only a compatibility facade over the narrower read-model family headers.
  Several web/state callsites were moved to the narrower headers directly.
- 2026-05-15: The remaining public Web UI orchestrator callsite was moved off
  the broad registry facade and now includes only scenario, device, and snapshot
  read-model contracts. The compatibility facade is now reserved for internal
  aggregation and legacy callers.
- 2026-05-15: Quest Device save started moving behind a narrower control
  boundary. The HTTP handler now passes the JSON payload to
  `scenehub_control_save_device_payload(...)`; domain parsing and save
  invalidation are handled in `scenehub_control`.
- 2026-05-15: Room profile save was moved through the same control-boundary
  pattern with `scenehub_control_save_profile_payload(...)`, reducing Web UI
  knowledge of profile domain parsing.
- 2026-05-15: Room scenario validate/save were moved behind payload-oriented
  control APIs. Scenario scratch ownership and `room_scenario_t` parsing now
  live in `scenehub_control` instead of HTTP handlers.
- 2026-05-15: Hardware IO mode changes were moved behind
  `scenehub_control_hardware_io_set_mode(...)`. The Web UI handler now only
  parses the request and serializes the response; scenario conflict scanning
  and hardware mutation live in the control layer.
- 2026-05-15: Scenario editor layout lookup started moving behind the
  read-model boundary with `orchestrator_registry_get_room_scenario_layout(...)`.
  The HTTP handler no longer calls `room_scenario_get(...)` directly, but the
  streaming layout writer still consumes the domain layout struct until a
  compact layout DTO replaces it.
- 2026-05-15: Scenario editor layout streaming was extracted from
  `web_ui_orchestrator.c` into `orchestrator_scenario_layout_writer.*`. The
  remaining domain-layout dependency is now isolated in one serializer instead
  of being embedded in the HTTP handler.
