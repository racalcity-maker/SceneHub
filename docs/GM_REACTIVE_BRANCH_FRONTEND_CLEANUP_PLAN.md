# GM Reactive Branch Frontend Cleanup Plan

## Purpose

Clean up the GM Panel scenario editor for Reactive Branch v2 without rewriting
or destabilizing normal runtime branches.

Reactive branches are side reactions, not a second scenario flow. The frontend
must stop treating them as legacy branch `steps[]` with a special first trigger
step. New and editable reactive branches must use the Reactive Branch v2 shape:

```text
trigger -> guard_flags -> policy -> selected variant -> actions -> result_policy
```

This plan is intentionally scoped to the embedded GM Panel frontend. Backend
runtime semantics, normal branch execution and normal branch editing should stay
stable during this pass.

## References

- `ARCHITECTURE.md` - branch roles, Reactive Branch v2 runtime semantics and
  completion rules.
- `reactive_branch_v_2_design.md` - current Reactive Branch v2 JSON contract,
  editor rules and non-goals.
- `gm_api_contract.md` - saved scenario JSON and GM Panel API contract.
- `GM_SCENARIO_FRONTEND_MODEL_REFACTOR_CHECKLIST.md` - existing draft-first
  direction and remaining verification items.
- `TESTING.md` - GM Panel bundle and syntax checks.
- `KNOWN_ISSUES.md` - active backlog and broader UI/runtime risks.

## Current Problems

- Reactive branches still have two frontend concepts:
  - legacy reactive branch: `steps[]`, where the first step acts as a trigger;
  - Reactive Branch v2: `trigger`, `guard_flags`, `policy`, `variants[]`,
    `actions[]` and `result_policy`.
- Normalization currently makes reactive branches look like v2 branches even
  when legacy `steps[]` data exists. This can hide old steps and makes the
  editor behavior hard to reason about.
- Reactive action editing mixes draft mutations with DOM collection. Some
  controls patch `scenarioEditor.draft`; other paths reconstruct values from
  rendered fields.
- Event selectors for reactive fields are duplicated across dirty tracking,
  input routing, change routing and action handling.
- Normal step payload rendering and reactive action payload rendering share
  behavior but not a clean model boundary, so fixes drift between the two.
- Generated `gm_panel.js` can become stale when source parts change unless the
  bundle check is run consistently.

## Observed Reactive Editor Bugs

These are the concrete failures that motivated this cleanup and must be covered
by the implementation checks.

- Selecting a reactive action device can appear to do nothing. The device
  select is matched by the generic reactive `input` route in
  `gm_panel_09_scenario_change_events.js`, which calls
  `gmHandleReactiveV2Change(..., true)`. That deferred path commits the draft
  but skips render, so the dependent command dropdown is not rebuilt
  immediately.
- The corresponding `change` path has a handler for
  `select[data-step-field="device_id"]`, but the behavior depends on browser
  event ordering because `input` and `change` are routed differently for the
  same select control.
- Reactive action editing mixes two models:
  - device and command select handlers patch the draft directly;
  - other fields rebuild the action from DOM through
    `collectReactiveV2ActionFromDom`.
  This can overwrite a just-patched action with stale rendered fields.
- Reactive action fields can mutate a throwaway draft clone. The top-level
  `gmHandleReactiveV2Change()` creates one working draft, then the action
  branch used to call `reactiveV2DraftActionContext()`, which created a second
  working draft. The action handler changed the second draft, but the outer
  function committed the first draft. Trigger fields did not hit this because
  they mutated the outer context directly. This explains why choosing a trigger
  device worked while choosing an action device such as `UID Gate 1` reverted to
  the previous `Relay channels` command UI.
- Scenario id edits can detach the active draft. `scenarioEditorSource()` only
  uses `scenarioEditor.draft` when `draft.id` matches
  `scenarioEditor.scenario_id`, but the `#scenario_id` change handler updates
  only `draft.id`. A later reactive edit can therefore fall back to the
  original/detail object and make the UI look like it reverted.
- Reactive add/delete/toggle action code updates `scenarioEditor.draft`,
  `dirty` and `validation_report` directly instead of using the shared
  `scenarioCommitDraft()` path. This bypasses draft revision tracking and makes
  validation freshness inconsistent.
- Saving a reaction can appear successful and then immediately reset the
  selected trigger device/event. The frontend save path validates and posts the
  current draft, then invalidates and reloads the scenario detail through the
  layout endpoint. The current layout writer emits reactive `variants` but does
  not emit the v2 `trigger`, `guard_flags`, `policy`, `reentry` or
  `result_policy` fields. The editor normalizes that partial layout back into a
  default empty trigger, so client validation reports
  `REACTIVE_TRIGGER_INCOMPLETE` after a successful save. This is a read/detail
  projection bug, not an offline-device status issue.

## Non-Goals

- Do not rewrite normal branch runtime or normal branch editor behavior in this
  phase.
- Do not turn Reactive Branch v2 into a generic graph editor or nested scenario
  engine.
- Do not add Reactive Branch v2 support for normal-only step types such as
  `WAIT_EVENT`, `WAIT_FLAGS`, `OPERATOR_APPROVAL`, `GOTO` or `END_GAME`.
- Do not change backend result policy semantics unless a frontend bug exposes a
  contract mismatch that must be fixed at the boundary.
- Do not preserve the legacy first-step-trigger model as a product feature.

## Invariants

- `branch.type === "reactive"` means Reactive Branch v2 in the editor.
- Reactive branches are rendered separately from normal flow branches.
- Reactive actions are not normal scenario steps. They may reuse compatible
  payload types, but they keep reactive semantics.
- Normal branches continue to use `steps[]`.
- Reactive branches use `variants[].actions[]`.
- The editor draft is the only editable source of truth. DOM is a render target
  and event source, not the canonical data model.
- All reactive draft mutations go through the same commit path used for dirty
  state, validation invalidation and draft revision updates.
- Compatibility for old saved shapes happens at import/normalization or explicit
  migration boundaries, not throughout the render/event code.

## Target Frontend Model

Reactive branch draft:

```json
{
  "id": "wrong_card_reaction",
  "name": "Wrong card reaction",
  "type": "reactive",
  "enabled": true,
  "trigger": {
    "kind": "device_event",
    "device_id": "uid_gate",
    "event_id": "sequence_invalid"
  },
  "guard_flags": [],
  "policy": {
    "mode": "single",
    "cooldown_ms": 0,
    "max_fire_count": 0
  },
  "reentry": {
    "mode": "ignore"
  },
  "variants": [
    {
      "id": "variant_1",
      "label": "Actions",
      "actions": []
    }
  ],
  "result_policy": {
    "on_done": "continue",
    "on_fail": "fail_reaction",
    "on_timeout": "fail_reaction"
  }
}
```

Allowed reactive action types:

- `DEVICE_COMMAND`
- `DEVICE_COMMAND_GROUP`
- `WAIT_TIME`
- `SET_FLAG`
- `SHOW_OPERATOR_MESSAGE`

Normal-only step types stay normal-only until the backend contract explicitly
adds them to Reactive Branch v2.

## Execution Plan

### Phase 1 - Freeze Normal Branch Surface

- Identify the normal branch render/edit/save path and avoid behavior changes
  except for small shared helpers that are covered by checks.
- Keep `steps[]` editing for normal branches exactly where it is until reactive
  cleanup is complete.
- Add temporary comments only where they prevent accidental reactive/normal
  coupling during this cleanup.

Acceptance:

- Existing normal branch editor still opens, edits and saves `steps[]`.
- Normal-only step types remain available only in normal branches.

### Phase 2 - Make Reactive v2 Detection Explicit

- Replace heuristic checks such as "has `variants` or `trigger`" with a direct
  editor rule: `branch.type === "reactive"` renders Reactive Branch v2.
- Rename or remove helpers whose names imply optional v2 mode for reactive
  branches.
- Ensure new reactive branches are created with the full v2 skeleton and no
  default action.

Acceptance:

- Reactive branch tabs always open the v2 editor.
- Normal branch tabs always open the normal step editor.
- New reactions have `trigger`, `guard_flags`, `policy`, `reentry`,
  `variants[0].actions`, and `result_policy`.

### Phase 3 - Handle Legacy Reactive Shapes At One Boundary

- Add a single normalization/migration function for old reactive `steps[]`
  shapes.
- Best-effort migration:
  - map a legacy trigger step to `trigger`;
  - map compatible post-trigger steps to `variants[0].actions`;
  - reject or flag unsupported steps instead of silently dropping them.
- Remove scattered legacy checks from render and event code after the boundary is
  in place.

Acceptance:

- A legacy reactive fixture either migrates into v2 deterministically or reports
  an explicit editor/validation error.
- No render path treats `branch.steps[]` as the live reactive editor model.

### Phase 4 - Centralize Reactive Draft Mutations

- Introduce focused operations for reactive editing:
  - `patchReactiveTrigger`
  - `patchReactiveGuard`
  - `patchReactivePolicy`
  - `patchReactiveReentry`
  - `patchReactiveResultPolicy`
  - `addReactiveVariant`
  - `deleteReactiveVariant`
  - `patchReactiveVariant`
  - `addReactiveAction`
  - `deleteReactiveAction`
  - `moveReactiveAction`
  - `patchReactiveAction`
- Route all reactive controls through these operations.
- Remove direct assignments to `scenarioEditor.draft`, `dirty` and
  `validation_report` from reactive action handlers.
- Keep one commit helper responsible for draft revision, dirty state and
  validation invalidation.

Acceptance:

- Every reactive field edit updates `scenarioEditor.draft`.
- Every reactive add/delete/move action invalidates stale validation.
- No reactive save depends on collecting the action back from DOM.

### Phase 5 - Add A Scoped Action Payload Registry

- Create a small action/step payload registry shared by normal steps and
  reactive actions only at the payload level.
- Each entry should define:
  - allowed scopes: `normal`, `reactive`;
  - default payload;
  - normalize function;
  - render fields;
  - field patching rules.
- Start with the shared reactive-safe types:
  - `DEVICE_COMMAND`
  - `DEVICE_COMMAND_GROUP`
  - `WAIT_TIME`
  - `SET_FLAG`
  - `SHOW_OPERATOR_MESSAGE`
- Keep normal-only types out of reactive UI through `allowedScopes`.

Acceptance:

- Reactive action type dropdown cannot select normal-only step types.
- `DEVICE_COMMAND` and `DEVICE_COMMAND_GROUP` use the same payload rules in
  normal and reactive contexts.
- Audio/system command parameter rules do not fork between normal and reactive
  editors.

### Phase 6 - Collapse Selector And Event Routing Duplication

- Move repeated scenario field selectors into one named constant or small helper
  module inside the GM Panel source parts.
- Split event routing by intent:
  - scenario identity fields;
  - branch settings;
  - normal step fields;
  - reactive branch fields;
  - reactive action fields.
- Keep dirty tracking and change routing based on the same selector source.

Acceptance:

- Adding a new reactive field requires one registry/routing change, not edits in
  several unrelated selector strings.
- Dirty tracking fires for all reactive trigger, policy, variant and action
  fields.

### Phase 7 - Delete Legacy Reactive UI Code

Remove reactive-only legacy behavior from the normal step editor:

- "Add trigger first" hints for reactive branches.
- Allowed step ordering where the first reactive step must be a trigger.
- Branch-step action restrictions that depend on legacy reactive `steps[]`.
- Render branches that choose between legacy reactive editor and v2 editor.
- Compatibility handlers for `operator_event` and `runtime_event` as separate
  frontend field names, after `event_id` is canonical in the editor.

Keep compatibility only where the backend JSON import/export contract still
requires it.

Acceptance:

- Searching the GM Panel source for legacy trigger-step behavior finds no live
  reactive editor path.
- `branch.steps[]` remains used by normal branches only.

### Phase 8 - Verification And Bundle Discipline

Required checks:

```powershell
python components\web_ui\assets\check_gm_panel_bundle.py
node --check components\web_ui\assets\gm_panel.js
```

When available in the environment:

```powershell
cmake --build build --target gm_panel_bundle
```

Manual/editor checks:

- Create a normal branch, add a normal step and save.
- Create a reaction, choose a device-event trigger and save.
- Create operator-event and runtime-event triggers and confirm saved JSON uses
  the expected event id.
- Add, edit, delete and reorder reactive actions.
- Add a reactive `DEVICE_COMMAND_GROUP` and verify nested commands/params save.
- Switch between normal and reactive tabs without losing unsaved draft state.
- Validate a scenario and confirm issues attach to the correct reactive action
  or trigger.
- Reopen a saved reaction and confirm all trigger, guard, policy, variant,
  action and result-policy fields are preserved.

## Files To Review First

- `components/web_ui/assets/gm_panel/gm_panel_05a_scenario_model.js`
- `components/web_ui/assets/gm_panel/gm_panel_05b_scenario_payloads.js`
- `components/web_ui/assets/gm_panel/gm_panel_05c_reactive_v2.js`
- `components/web_ui/assets/gm_panel/gm_panel_05d_scenario_branches.js`
- `components/web_ui/assets/gm_panel/gm_panel_05e_scenario_steps.js`
- `components/web_ui/assets/gm_panel/gm_panel_05f_scenario_validation.js`
- `components/web_ui/assets/gm_panel/gm_panel_05_scenario_builder.js`
- `components/web_ui/assets/gm_panel/gm_panel_08_editor_actions.js`
- `components/web_ui/assets/gm_panel/gm_panel_09_scenario_change_events.js`
- `components/web_ui/assets/gm_panel/gm_panel_09_editor_events.js`
- `components/web_ui/orchestrator/orchestrator_scenario_layout_writer.c`
- `components/web_ui/orchestrator/orchestrator_api_view_room_scenarios.c`

## Cleanup Exit Criteria

- Reactive branches have one editable model in the frontend.
- Legacy reactive `steps[]` support is not present in live editor code.
- Normal branches still edit and save through their existing `steps[]` path.
- Reactive action payload handling is shared where payloads are truly shared,
  but reactive branch semantics remain separate.
- Generated bundle checks pass.
- This plan can be replaced by completed changelog notes and any remaining
  unresolved risks moved to `KNOWN_ISSUES.md`.
