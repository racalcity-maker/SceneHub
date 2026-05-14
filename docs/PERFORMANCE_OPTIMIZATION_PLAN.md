# Performance Optimization Plan

This is a temporary working checklist.

Use it while SceneHub moves from "already acceptable" performance to more
intentional low-latency, low-churn runtime behavior.

When this plan is complete:

- move the durable outcomes into `CHANGELOG.md`;
- keep permanent rules in `MEMORY_ALLOCATION_POLICY.md` or architecture docs;
- remove this file from the active tree.

## Why This Exists

The recent runtime refresh and invalidation work already improved a lot:

- browser and desktop app now consume targeted `gm.invalidate` events;
- explicit recovery now uses `gm.resync.required`;
- common GM write-side actions no longer fall back to broad state invalidation;
- routine browser refresh paths no longer hide broad `/api/gm/state` reloads
  behind runtime/profile/scenario refresh helpers;
- common runtime/control helper paths no longer parse transient cJSON just to
  read `audio.play.file`.

That means the next bottlenecks are no longer broad UI refresh semantics.
They are now more likely to be:

- MQTT/device ingest parsing;
- GM runtime wake/pass cost per active cause;
- broad snapshot JSON building;
- heavy validation/import/admin paths.

## Optimization Goals

- Keep runtime-hot paths allocation-free.
- Reduce avoidable JSON parse/build work in high-frequency paths.
- Prefer addressable invalidation and narrow fetch over broad snapshot rebuild.
- Keep admin/full-snapshot paths correct first, then optimize only when they
  show up in real usage.
- Add lightweight measurement before deep refactors when the next hotspot is
  unclear.

## Current Priority Order

1. `device_control_ingest`
2. `gm_room_session_runtime`
3. full snapshot/read-model JSON building
4. validation and editor-side heavy paths
5. websocket/control-plane polish

## P0 - Guardrails

- [ ] Do not optimize by reintroducing broad invalidation or broad refresh.
- [ ] Do not add generic diff engines.
- [ ] Do not add new repeated allocation in runtime-hot paths.
- [ ] Keep recovery/full refresh semantics explicit and separate from routine
  invalidation.
- [ ] Prefer bounded scanners, static scratch, fixed pools, and reuse.

## P1 - Device Ingest Hot Path

Goal: reduce CPU and heap churn in device MQTT ingest, because this is the most
likely next runtime hotspot.

Current baseline:

- topic parsing is lightweight;
- `heartbeat/status/diag` now use a bounded scanner;
- `result/event` now use the same bounded scanner path;
- ingest now publishes a compact local event DTO instead of copying the full
  device state;
- ingest now tracks `last_changed_device_id`, which helps invalidation but does
  not reduce parse cost.

Targets:

- [x] Audit which incoming topics actually need full cJSON parse.
- [x] Split ingest by responsibility so parse/apply/event work does not keep
  accumulating in one file.
- [x] Remove transient full-state heap snapshot allocation from the common
  ingest path.
- [x] Replace repeated scalar field extraction with a bounded scanner where the
  payload contract is stable enough.
- [x] Avoid copying the full device snapshot when only a smaller event DTO is
  needed.
- [ ] Keep PSRAM-first behavior for large helper buffers if any are needed.
- [x] Preserve compatibility with current device contract and error handling.

Acceptance:

- [x] common `heartbeat/status/diag` ingest paths avoid full document parse.
- [x] `result/event` avoid full document parse where practical.
- [x] ingest does not allocate transient document trees in the common path if a
  bounded scanner is sufficient;
- [ ] invalidation and event-bus behavior stay unchanged externally.

Relevant files:

- `components/device_control_ingest/device_control_ingest.c`
- `components/mqtt_core/mqtt_core.c`

## P2 - GM Runtime Follow-Up

Goal: keep the new event-driven runtime cheap and deterministic after the fixed
poll loop removal.

Current baseline:

- the fixed `100 ms` runtime polling loop is gone;
- runtime progression now flows through the GM runtime inbox and deadline timer;
- the broader scheduler migration now lives in
  [GM_RUNTIME_EVENT_DRIVEN_PLAN.md](/d:/Projects/SceneHub/docs/GM_RUNTIME_EVENT_DRIVEN_PLAN.md:1);
- some runtime work is still concentrated in
  `gm_room_session_runtime_process_pending_work()`
  as a legacy-shaped pass function.

Targets:

- [ ] Reduce redundant `gm_room_session_mark_session_changed_locked()` calls
      where multiple state updates happen in one logical step.
- [ ] Review lock hold time inside scenario advancement and branch execution.
- [ ] Split `gm_room_session_runtime_process_pending_work()` further if smaller
      cause-oriented runtime passes make the hot path easier to reason about.
- [ ] Narrow event routing with indexed dispatch follow-ups from
      [GM_RUNTIME_INDEXED_DISPATCH_PLAN.md](/d:/Projects/SceneHub/docs/GM_RUNTIME_INDEXED_DISPATCH_PLAN.md:1).

Acceptance:

- [ ] active runtime passes do not do obviously redundant state writes;
- [ ] runtime cause handling stays bounded and predictable;
- [ ] no behavior regressions in wait/result/operator flows.

Relevant files:

- `components/gm_core/gm_room_session_runtime.c`
- `components/gm_core/gm_room_session_runtime_wait.c`
- `components/gm_core/gm_room_session_reactive_v2.c`
- `components/gm_core/gm_room_session_events.c`

## P3 - Snapshot And Read-Model Response Cost

Goal: keep broad snapshot and room runtime endpoints cheap enough, and avoid
letting admin/full-snapshot work leak into active control flows.

Current baseline:

- `/api/gm/room/runtime` is already much lighter than before;
- active room control no longer uses broad `/api/gm/state` polling;
- broad snapshot refresh is explicit `loadGMFullSnapshot()` use for bootstrap,
  structural refresh and recovery;
- `system.summary` and rooms-runtime refresh cover non-room-control narrow
  refresh paths without rebuilding the full UI state;
- `orchestrator_registry` uses cached static snapshot storage;
- room-runtime asset summary no longer uses transient cJSON parse for
  `audio.play.file`;
- `/api/gm/state` still builds a broad cJSON tree.

Targets:

- [ ] Audit whether `/api/gm/state` is still called often enough to matter in
  real operator flows.
- [ ] If needed, split the broad DTO into smaller stable writers or chunked
  sections.
- [x] Keep `room.runtime` as the dominant active-room endpoint.
- [ ] Review whether `system.summary` can be served without building the whole
  GM snapshot tree.

Acceptance:

- [x] active room control does not depend on broad snapshot JSON generation;
- [x] broad snapshot cost is isolated to navigation/admin/recovery;
- [ ] no duplicate heavy work appears between registry build and JSON view
  formatting.

Relevant files:

- `components/scenehub_read_model/orchestrator_registry.c`
- `components/web_ui/orchestrator_api_view.c`
- `components/web_ui/web_ui_orchestrator.c`

## P4 - Validation And Heavy Admin Paths

Goal: improve perceived speed of save/validate/import actions without treating
admin-only work as runtime-hot.

Current baseline:

- validation is not on the steady-state runtime path;
- some validation helpers still use cJSON for params inspection;
- import/export/store paths are allowed to allocate and use cJSON.

Targets:

- [ ] Audit `room_scenario_validation.c` for avoidable transient parse work in
  repeated command checks.
- [ ] Decide whether command-param validation can reuse bounded scanners like
  runtime/control paths do.
- [ ] Keep admin/import paths correct first; optimize only repeated expensive
  patterns.

Acceptance:

- [ ] save/validate latency avoids obviously repeated full JSON parse where a
  bounded read is enough;
- [ ] admin paths remain cleanly separated from runtime-hot rules.

Relevant files:

- `components/room_scenario/room_scenario_validation.c`
- `components/web_ui/web_ui_gm_scenario_store.c`
- `components/quest_device/quest_device_json.c`

## P5 - WebSocket And Control Plane Polish

Goal: keep the transport layer cheap and deterministic, without over-optimizing
rare control-plane messages.

Current baseline:

- websocket output is bounded and lightweight;
- browser and desktop already share one invalidation contract;
- input control messages like `subscribe` and `ping` still use cJSON parse.

Targets:

- [ ] Keep websocket payloads bounded as event fields evolve.
- [ ] Consider replacing `cJSON_Parse(text)` for tiny control-plane messages if
  this path ever becomes measurable.
- [ ] Keep `gm.invalidate` and `gm.resync.required` stable and minimal.

Acceptance:

- [ ] websocket message handling stays simple and bounded;
- [ ] no unnecessary payload growth appears in routine invalidation events.

Relevant files:

- `components/ws_runtime/ws_runtime.c`
- `desktop_app/src/platform/ws/envelope.ts`
- `desktop_app/src/domains/gm/queries/useGmVersionsWs.ts`
- `components/web_ui/assets/gm_panel/gm_panel_09_events_boot.js`

## P6 - Measurement And Exit

Goal: stop guessing once the easy wins are done.

Targets:

- [ ] Add lightweight timing/log probes only where the next bottleneck is not
  obvious.
- [ ] Record measured wins in `CHANGELOG.md`, not just structural refactors.
- [ ] Remove this file after the durable results are moved elsewhere.

Good measurement candidates:

- ingest parse time by topic kind;
- runtime wake count by cause and active-room count;
- runtime pass duration after event or deadline wake;
- `/api/gm/state` build + send time;
- `/api/gm/room/runtime` build + send time;
- validation duration for large scenarios.

## Notes

- This plan is intentionally temporary.
- The current desktop transport scope is `ws_runtime`, not UART.
- Future UART/serial transport work should reuse the same invalidation/resync
  semantics, but it is not part of this plan.
