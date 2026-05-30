# Hub Audit P0/P1 Plan

Audit date: 2026-05-25

## Scope

Audit and follow-up fixes covered:

- P0.1 write HTTP paths -> `scenehub_control`
- P0.2 command policy enforcement
- P0.3 scenario command result lifecycle
- P0.4 no execution or send under critical locks

## Confirmed OK

### P0.1 write boundary

Audited write handlers in `web_ui` route writes through `scenehub_control`.
No direct write bypasses were found in the checked paths for:

- scenario save/delete/import/load/save-store
- profile save/delete/import/load/save-store
- quest device save/delete/load/manual command
- room action
- hardware/manual action

Read and export paths still read from store/read-model directly in places, but
that was not a write-boundary issue in the audited handlers.

### P0.2 enforced backend policy

Backend enforcement is confirmed for:

- `manual_allowed`
- `scenario_allowed`
- `result_required`
- `timeout_ms`

Main enforcement points:

- `components/command_executor/command_executor.c`
- `components/gm_core/api/gm_api.c`
- `components/gm_core/session/gm_room_session_commands.c`
- `components/command_executor/command_executor_result.c`

### P0.4 lock safety

Confirmed by code inspection:

- `gm_core` drops session lock before real command/audio/hardware execution
- `mqtt_core` does not hold publish-send under the core lock
- `command_executor` does not hold device lookup mutex during actual execute/publish

### P1 runtime noise follow-ups are now narrowed

Two inspected `P1` paths were tightened:

- `event_bus` still updates its internal counters on every post/dispatch/drop,
  but the mirrored `service_status` snapshot is now throttled instead of taking
  the diagnostic mutex on every hot-path transition
- `device_control_ingest` no longer bumps the shared runtime generation for
  heartbeat-only presence refresh

Conclusion:

- these were valid performance hygiene issues
- the current implementation is good enough for the next larger device-count
  heartbeat/status-noise test without introducing heavier generation-model
  refactors first

### P1 snapshot rebuild amplification is now narrowed

The broad `orchestrator_registry` cache invalidation path was narrowed:

- `orchestrator_registry` no longer invalidates its shared cache on ordinary
  device status/runtime/control event noise
- `scenehub_state_notify_invalidation(...)` now invalidates the shared registry
  cache only for structural slices such as device/room catalog, scenarios,
  profiles, sidebar presets, and full-snapshot recovery paths

Conclusion:

- status/presence churn now stays in lightweight runtime invalidation paths
- broader snapshot rebuilds are reserved for structure/config-style changes
- this is a better baseline before larger device-count polling/noise tests

### P1 GM runtime refresh is now WS-first

The GM panel runtime refresh path was narrowed:

- periodic `/api/gm/versions` polling is now suppressed while the WebSocket is
  healthy
- active-room runtime fallback polling is also suppressed while the WebSocket
  is healthy
- `/api/gm/room/runtime` now defaults to `include_assets=0`; asset-readiness
  summary is opt-in instead of being on the default live path

Conclusion:

- WebSocket invalidation is now the primary live-refresh transport
- HTTP polling remains only as recovery/fallback behavior
- asset-rich room-runtime reads are no longer the default background path

## Fixed In Code

### P0.3 result lifecycle: `started` is now explicit

Hub-side contract now treats both `accepted` and `started` as pending states.

Files changed:

- `components/quest_common/include/scenehub_command_result.h`
- `tests/command_backend/main/test_command_executor.c`
- `tests/command_backend/main/test_device_control_ingest.c`
- `tests/gm_session/main/test_gm_room_session.c`
- `docs/COMMAND_RESULT_SEMANTICS.md`
- `docs/device_control_contract_v1.md`
- `docs/NODE_IMPLEMENTATION_CHECKLIST.md`

Semantics now documented and tested:

- `started` does not advance a `result_required` scenario step
- `accepted` does not advance a `result_required` scenario step
- only terminal `done` advances the step
- `failed`, `rejected`, and `timeout` fail the step

Write-side semantics are now also documented in:

- `docs/COMMAND_RESULT_SEMANTICS.md`

That document is the current baseline for:

- terminal vs non-terminal status meaning
- scenario wait behavior
- manual HTTP dispatch-envelope semantics
- `request_id` correlation
- append-only audit/timeline expectations

### P0.4 HTTP send under `web_ui` scratch mutex

Single-scenario orchestrator path no longer performs HTTP send while holding
`s_room_scenario_detail_scratch_mutex`.

File changed:

- `components/web_ui/orchestrator/web_ui_orchestrator.c`

Current behavior:

- scratch data is filled under lock
- response-owned copy is allocated
- mutex is released
- JSON/layout send happens after unlock

## Still Open

### P0.2 confirmation policy is now explicit for manual HTTP actions

The semantic gap around dangerous manual actions is now narrowed:

- `requires_confirmation` is enforced on the manual HTTP/device-control path
- callers must send `confirmed=true` for commands whose policy has
  `requires_confirmation=true`
- `danger_level` remains UI/log metadata and is not a backend gate

Conclusion:

- operator/admin confirmation is no longer only a frontend courtesy
- direct API callers cannot bypass `requires_confirmation` by skipping the UI
- scenario/runtime execution remains separate and is not gated by this manual
  HTTP flag

### P0.3 write-side envelope remains intentionally dispatch-only

Manual HTTP device-command execution no longer reports async remote dispatch as
immediate `done`.

Current behavior:

- synchronous local hub-owned actions may return `done`
- successful async remote dispatch returns:
  - `status = accepted`
  - `request_id` when available
- the initial HTTP response is not a terminal remote lifecycle mirror

Conclusion:

- this is the intended model
- exact remote lifecycle should continue through command result/status/event
  flow, audit, and timeline correlation by `request_id`
- a future early `remote_status` field is optional, but not required for the
  baseline contract

### P0.4 shared dispatch-owner cleanup is now in place

The async transport-facing request-thread coupling that remained around:

- `describe_interface`
- manual GM device-command execution

is now handled through one shared `scenehub_control` dispatch owner.

Conclusion:

- no per-endpoint worker was introduced
- `describe_interface` and manual device-command dispatch now reuse the same
  owner task and owner-held scratch
- the policy remains the same for future work: do not solve write-side
  transport cleanup with one-off workers and large dedicated stacks

## Next Actions

1. Decide whether initial manual HTTP responses should ever expose early `remote_status` in addition to `status = accepted`.
2. If richer manual lifecycle is required later, keep it additive and do not collapse write-side and remote statuses back into one field.
5. Run targeted builds/tests for `tests/command_backend`, `tests/gm_session`, and `tests/web_ui_backend` when verification is allowed.
6. Before larger device-count testing, verify that current throttling and
   heartbeat-generation narrowing are sufficient before introducing more
   complex generation-family splits or additional invalidation classes.
7. Before larger device-count polling/noise testing, verify that the narrowed
   registry invalidation model is sufficient before introducing additional
   cache layers or generation-family splits.

## Verification Status

No build or test run was performed after these fixes in this pass.
