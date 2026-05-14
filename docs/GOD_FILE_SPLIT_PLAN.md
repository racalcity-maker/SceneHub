# God File Split Plan

This temporary plan tracks large source files that already exceed a reasonable
single-file responsibility boundary and should be split incrementally.

It is intentionally narrower than general architecture work. The goal here is
to reduce file-level coupling and improve navigability without mixing in broad
behavioral rewrites.

## Scope

- [x] Focus on first-party source files in `components/`.
- [x] Treat bundled `components/web_ui/assets/gm_panel.js` as out of scope.
- [x] Treat `third_party/` code as out of scope unless we later decide to wrap
      it differently.
- [x] Prefer splitting by responsibility boundary, not by arbitrary line count.
- [x] Keep public include contracts and endpoint contracts stable during file
      splits unless a separate migration is planned.

## Guardrails

- [x] Split one file family at a time.
- [x] Prefer moving cohesive helper clusters together instead of scattering
      tiny helpers across many files.
- [x] Keep private headers next to the new source family that owns them.
- [x] Avoid semantic rewrites during the same pass as the file split.
- [x] Update `CMakeLists.txt`, local includes, and this plan together.
- [x] If a large file is still cohesive, defer it behind files with clearly
      mixed responsibilities.

## Priority Backlog

- [ ] `components/scenehub_read_model/orchestrator_registry.c`
      - [x] move the registry core under `components/scenehub_read_model/registry/`
      - [x] split cache/snapshot lifecycle from view-building logic
      - [x] split asset counting/loading helpers from runtime room view shaping
      - [x] isolate local JSON scanning helpers if they stay internal to this
            component
      - [x] keep the registry-facing public contract easy to discover
- [ ] `components/web_ui/web_ui_utils.c`
      - [x] split low-level HTTP adapter wrappers from app-specific JSON
            serialization helpers
      - [x] split test adapter plumbing from production response helpers
      - [x] keep shared web helpers discoverable from one small root surface
- [ ] `components/room_catalog/room_catalog.c`
      - [x] split persistence/path helpers from in-memory catalog operations
      - [x] split JSON import/export helpers from CRUD logic
      - [x] keep room-catalog model ownership and validation local to the
            component
- [ ] `components/hardware_io/hardware_io_io.c`
      - [x] split timer/effect runtime from channel configuration/status logic
      - [x] keep the public IO control entry points easy to find
      - [x] avoid mixing this split with broader hardware-IO behavior changes
- [ ] `components/web_ui/web_ui_auth.c`
      - [x] split session storage/validation from HTTP auth handlers
      - [x] split password-reset monitor/runtime helpers from request handlers
      - [x] keep route-level auth checks easy to trace
- [ ] `components/web_ui/assets/gm_panel/gm_panel_07_loaders_and_runtime_actions.js`
      - [ ] split static-data loaders from room runtime refresh/render logic
      - [ ] split scenario/profile/device refresh helpers from full-snapshot
            orchestration
      - [ ] keep the user-visible refresh flow unchanged while reducing module
            breadth
- [ ] `components/web_ui/assets/gm_panel/gm_panel_08_editor_actions.js`
      - [ ] split quest-device editor actions from profile/scenario/storage
            actions
      - [ ] split room-scenario runtime actions from editor save/validate flows
      - [ ] keep action routing stable while narrowing module ownership

## Lower-Priority Candidates

- [ ] `components/room_scenario/room_scenario_validation.c`
      - [ ] revisit after higher-priority mixed-responsibility files are
            reduced
      - [ ] if split later, separate structural checks from report-building and
            runtime-specific validation paths without moving validation out of
            the `room_scenario` model component
- [ ] `components/quest_device/quest_device_json.c`
      - [ ] revisit only if JSON codec maintenance starts to slow feature work
      - [ ] keep it as one codec file unless import/export paths diverge enough
            to justify a split
- [ ] `components/web_ui/assets/gm_panel/gm_panel_05a_scenario_model.js`
      - [ ] revisit only if schema/catalog helpers keep growing after the
            loader/action modules are reduced

## Rollout

- [x] P0. Record the current split backlog and guardrails.
- [x] P1. Split `orchestrator_registry.c`.
      Cache/snapshot core now stays in `registry/orchestrator_registry.c`,
      runtime-view shaping in `registry/orch_registry_runtime_view.c`,
      runtime asset/cache helpers in `registry/orch_registry_runtime_assets.c`,
      and step-schema listing in `registry/orch_registry_step_schemas.c`.
- [x] P2. Split `web_ui_utils.c`.
      HTTP adapter transport now lives in `web_ui_http_adapter.c`,
      SceneHub-control response mapping in `web_ui_scenehub_control_responses.c`,
      generic JSON response builders in `web_ui_json_responses.c`,
      and `web_ui_utils.c` is reduced to generic memory/string/origin helpers.
- [x] P3. Split `room_catalog.c`.
      Core/state logic now stays in `room_catalog.c`, JSON codec in
      `json/room_catalog_json.c`, persistence in
      `storage/room_catalog_persistence.c`, with a shared private contract in
      `room_catalog_internal.h`.
- [x] P4. Split `hardware_io_io.c`.
      Core/config/status logic now stays in `hardware_io_io.c`, runtime timer
      and event-flow logic in `hardware_io_io_runtime.c`, with shared IO state
      in `hardware_io_io_internal.h`.
- [x] P5. Split `web_ui_auth.c`.
      Shared auth helpers and state now live in `web_ui_auth.c` plus
      `web_ui_auth_internal.h`, session storage/validation in
      `web_ui_auth_sessions.c`, route-level auth gating in
      `web_ui_auth_gate.c`, HTTP auth handlers in `web_ui_auth_handlers.c`,
      and reset-monitor runtime logic in `web_ui_auth_reset.c`.
- [ ] P6. Split `gm_panel_07_loaders_and_runtime_actions.js`.
- [ ] P7. Split `gm_panel_08_editor_actions.js`.
- [ ] P8. Re-evaluate the lower-priority candidates after the earlier passes.

## Exit

- [ ] Delete this temporary plan after the high-priority backlog is either
      completed or explicitly accepted as-is, and after any lasting file-family
      conventions are captured in permanent docs.
