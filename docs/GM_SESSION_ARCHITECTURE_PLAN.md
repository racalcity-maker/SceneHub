# GM Session Architecture Plan

This temporary plan tracks the incremental cleanup of `components/gm_core/session`
so the runtime can keep evolving without turning into one large shared internal
surface.

## Goal

- Keep current runtime behavior intact.
- Narrow the internal API inside `gm_core/session`.
- Separate session state, scenario execution, waits, reactive branches,
  command dispatch, and read/projection helpers into clearer sublayers.
- Remove long-running external command execution from the session lock in a
  later phase without doing a risky rewrite first.

## Phase Order

### Phase 1 - Narrow Internal Boundaries

Do not change behavior yet.

- Split `gm_room_session_internal.h` into smaller internal headers:
  - `gm_room_session_commands_internal.h`
  - `gm_room_session_projection_internal.h`
  - `gm_room_session_wait_internal.h`
  - `gm_room_session_reactive_internal.h`
  - `gm_room_session_runner_internal.h`
- Keep the remaining shared lock/store/runtime-task declarations in
  `gm_room_session_internal.h`.
- Update session source files to include only the internal slices they use.

Acceptance:

- Runtime behavior stays unchanged.
- Internal dependencies are narrower and easier to reason about.
- Future refactors no longer need to touch one giant shared header first.

### Phase 2 - Add Compact Read APIs

- Add small read-side getters instead of copying full `gm_room_session_t`
  broadly:
  - `gm_room_session_get_runtime_summary(...)`
  - `gm_room_session_get_timer_view(...)`
  - `gm_room_session_get_branch_runtime_view(...)`
  - `gm_room_session_get_selected_view(...)`
- Add a single-lock helper for callers that need several compact views from the
  same snapshot:
  - `gm_room_session_get_read_views(...)`
- Add a projection helper for room/read-model callers that need runtime branch
  state and scenario device references without copying the full session:
  - `gm_room_session_get_projection_view(...)`
- Start moving read-model/API callers away from full session copies where
  practical.

Acceptance:

- Read-side layers stop depending on the full session aggregate unless they
  truly need it.

### Phase 3 - Split Plan From Execution

- Keep the scenario runner as the state machine owner.
- Under the session lock, decide what command should run next and record a
  small command job/intention.
- Execute the external command after unlock.
- Keep result handling asynchronous through the existing event/result path.

Acceptance:

- `command_executor_execute(...)` no longer runs from inside the session lock.
- Session lock hold time is shorter and easier to reason about.

### Phase 4 - Separate Static And Environment Validation

- Keep `room_scenario` responsible for static scenario-model validation only.
- Move SceneHub-specific environment validation into an application layer such
  as `scenehub_control`.

Acceptance:

- `room_scenario` no longer needs direct knowledge of `hardware_io` or
  `quest_device` runtime environment rules.

### Phase 5 - Split Read-Model Runtime Projection

- Break `orchestrator_registry.c` into:
  - public facade
  - snapshot cache
  - system summary
  - room runtime projection
  - asset-summary helpers/cache

Acceptance:

- Runtime read-model logic remains the same, but the file/module boundaries are
  maintainable again.

### Phase 6 - Frontend Runtime Loader Cleanup

- Split GM panel runtime/frontend state work into:
  - runtime fetch helpers
  - invalidation router
  - runtime render helpers

Acceptance:

- Frontend runtime refresh logic becomes easier to audit independently from
  render and routing policy.

## Current Status

- Phase 1 is done: internal session headers are split into narrower slices.
- Phase 2 is done except for explicitly allowed full-session snapshot
  entrypoints.
  - compact `timer`, `selected`, and `runtime summary` read APIs are added
  - `gm_room_session_get_read_views(...)` is added for single-lock multi-view
    reads
  - `gm_room_session_get_projection_view(...)` and
    `gm_room_session_build_projection_view(...)` are added for room/read-model
    projection
  - `gm_api_get_room_state()` now reads compact session views instead of
    copying the whole aggregate
  - `orchestrator_registry_get_system_summary()` now uses the timer/session
    view instead of a full session copy
  - `gm_room_session_game.c` now uses compact read views for selected scenario,
    game start prechecks, and reset duration/profile lookups
  - `gm_room_session_projection_view_t` and nested branch runtime view are
    added for room/read-model projection needs
  - `orch_room_view.c` now reads projection views instead of copying the whole
    session aggregate
- Remaining full-session readers are narrow and explicit:
  - `gm_api_room_session_get()` still exposes the full `gm_room_session_t`
    snapshot as a deliberate debug/runtime API
  - `orchestrator_registry_get_room_runtime_view()` still takes one scratch
    full-session snapshot because it needs branch step/runtime detail and asset
    counting from the running scenario
- Phase 3 is functionally implemented and now needs hardening, not a fresh
  architectural split:
  - scenario and reactive command steps now produce a small command plan under
    the session lock instead of calling the executor directly from the runner
    path
  - the external command is executed synchronously after unlock, and the
    runtime re-enters the lock only to apply success/error/wait state
  - sequential command groups and reactive action groups reuse the same
    planned-dispatch boundary
- Phase 5 is effectively done enough for this plan:
  - room/runtime projection helpers now live in `scenehub_read_model`
    (`orch_room_view.c`, `orch_room_profile_view.c`) instead of all staying in
    one oversized registry path
  - any future file-size cleanup inside `scenehub_read_model` belongs in the
    component layout/performance plans, not in this session-architecture plan
- Phase 4 is done:
  - `room_scenario` no longer depends on `quest_device` or `hardware_io`
  - SceneHub-specific environment/device validation now lives outside the
    scenario-model component, via `scenehub_scenario_validation`
  - `scenehub_control`, `gm_core`, and `scenehub_read_model` use the
    SceneHub-side validator when they need product/runtime-aware scenario
    validation
- Phase 6 is no longer tracked here.
  - GM Panel runtime fetch/invalidation/render cleanup moved to
    `docs/GM_PANEL_REFACTOR_PLAN.md` and most of that work is already closed

## Remaining Work

- Phase 3 hardening:
  - validate dispatch/apply behavior across reset/stop/desync races
  - keep/add focused tests around planned dispatch cancellation when branch
    state changes between `plan` and `apply`
  - keep `command_executor_execute(...)` out of lock-held runner paths; the
    remaining direct callsites should stay limited to the post-unlock dispatch
    helper and explicit manual/session command entrypoints
- Decide whether the two remaining full-session snapshot readers are permanent
  exceptions or worth narrowing later:
  - `gm_api_room_session_get()`
  - `orchestrator_registry_get_room_runtime_view()`

## Out Of Scope

- Frontend runtime loader/polling/render cleanup now lives in
  `docs/GM_PANEL_REFACTOR_PLAN.md`.
- Broad `scenehub_read_model` file regrouping now belongs in
  `docs/COMPONENT_LAYOUT_CLEANUP_PLAN.md` and related component cleanup docs.

## Current Decision

Keep the low-RAM command-plan boundary, treat Phase 2 and Phase 4 as closed,
and spend the next pass on Phase 3 race hardening.
