# GM Scenario Frontend Model Refactor Checklist

Goal: replace the current mixed `summary/detail/draft/DOM` scenario flow with a single clear frontend model that does not lose `params`, does not collapse editor state after refresh/save, and keeps validation tied to the actual editable draft.

## Context

- Current GM panel scenario UI mixes:
  - scenario list summaries
  - scenario detail/layout objects
  - in-memory editor draft
  - DOM-collected state
  - runtime/read-only room scenario projections
- This causes:
  - audio/file params disappearing after refresh/save
  - editor opening on summary-only data
  - steps/branches disappearing until `Edit` is clicked again
  - validation being applied to objects that are not the real saved draft
  - stale labels and mixed old/new step metadata

## Target Model

- [x] Define `ScenarioSummary` as list-only data.
  - Required fields: `id`, `name`, `room_id`, `step_count`, `branch_count`, `valid`, `validation_issue_count`.
- [x] Define `ScenarioDetail` as editor/runtime-readable full scenario layout.
  - Required fields: full `branches`, `steps`, `variants`, `actions`, `params`.
- [x] Define `ScenarioDraft` as the only editable source of truth in the frontend.
- [x] Define `ValidationReport` as a separate object bound to a specific draft revision, not to DOM or list cache.

## Invariants

- [x] Scenario list cache must never be treated as editor detail.
- [x] Refreshing scenario summaries must never destroy or downgrade an open editor draft.
- [x] Saving a scenario must not replace editor state with a summary object.
- [x] DOM must be a render target, not the primary source of truth for scenario state.
- [x] Validation must always run against the current `ScenarioDraft`.
- [x] Room/runtime views must stay read-only and must not mutate editor state.

## State Split

- [x] Split current room scenario frontend state into explicit stores:
  - `scenarioSummariesByRoom`
  - `scenarioDetailsByRoomScenario`
  - `scenarioEditorDraft`
  - `scenarioEditorSession`
  - `scenarioValidationReport`
- [x] Stop overloading `gmRoomScenarios[roomId]` for both list and editor/detail use.
- [x] Audit all helpers that currently read from `roomScenarios(roomId)` and classify them:
  - summary-only
  - detail-only
  - runtime-only

## Load Flow

- [x] Make `Scenarios` screen left panel load summaries only.
- [x] Make `Edit` explicitly load detail for the selected scenario.
- [x] On successful detail load, initialize `ScenarioDraft = clone(detail)`.
- [x] Keep detail cache and draft cache separate.
- [x] Ensure `Refresh` updates summaries without downgrading open detail/draft state.

## Save Flow

- [x] Change `Save` flow so the editor keeps using the saved detail-shaped draft after success.
- [x] After save:
  - update summary list
  - update detail cache for that scenario
  - keep editor bound to full detail/draft, not summary
- [x] Remove any path where post-save refresh replaces editor state with a summary object.

## Draft Mutation Model

- [x] Stop relying on `collectScenarioEditor()` as the main state assembly mechanism.
- [x] Introduce reducer-style draft mutations for scenario editing:
  - `setScenarioName`
  - `setBranchField`
  - `setStepDevice`
  - `setStepCommand`
  - `setStepParam`
  - `addStep`
  - `deleteStep`
  - `moveStep`
  - `addBranch`
  - `deleteBranch`
  - `setReactiveTrigger`
  - `setReactiveActionParam`
- [x] Make DOM change handlers dispatch these mutations instead of rebuilding truth from the DOM.
- [x] Reactive V2 input/change path must patch only the active reaction branch instead of rebuilding the whole scenario from the DOM.
- [x] Keep `collectScenarioEditor()` only as transitional/fallback code until the reducer flow fully replaces it.

## Audio-Specific Rules

- [x] Centralize scenario audio draft rules in one place instead of spreading them across render/change/save code.
- [x] Keep explicit channel semantics:
  - `effect` may allow effect-playable files
  - `background` requires WAV only
- [x] Ensure invalid file/channel combinations are surfaced as draft validation errors, not silent data loss.
- [x] Do not silently drop `params.file` during unrelated refresh/save/editor transitions.
- [x] If channel changes force invalidation of a file, make that a visible editor-side mutation with a clear validation/error state.

## Validation Model

- [x] Split validation into layers that all work on the same draft:
  - field-level validation
  - draft/domain validation
  - backend authoritative validation
- [x] Make frontend validation reference exact draft nodes:
  - branch id
  - step id
  - reactive action id / variant index / action index
- [x] Ensure validation UI does not attach issues to summary-only or stale editor objects.
- [x] Confirm audio validation uses the same actual draft payload that `Save` sends.

## Rendering Cleanup

- [x] Make scenario step labels deterministic and derived from current draft state when auto-generated.
- [x] Decide whether labels are:
  - partially generated with explicit refresh/update rules
  - user-authored labels remain untouched once they diverge from the last auto label
- [x] Remove mixed old/new label states such as audio command rows showing stale relay/UID labels.
- [x] Make collapsed step summaries render from current draft params, not stale cached view state.

## Runtime / Read-Only Views

- [x] Keep room control scenario layout/running view on read-only runtime/detail projections.
- [x] Ensure runtime summary text like `Play sfx` / `Play bg` reads from actual stored params.
- [x] Verify runtime views do not accidentally mask missing params by using generic labels.

## Migration / Transitional Work

- [x] Identify code paths that currently depend on summary data pretending to be detail data.
- [x] Mark temporary bridge helpers explicitly as transitional.
- [x] Remove transitional fallback once detail/draft separation is complete.

## Files To Review First

- [x] `components/web_ui/assets/gm_panel/gm_panel_05_scenario_builder.js`
- [x] `components/web_ui/assets/gm_panel/gm_panel_05a_scenario_model.js`
- [x] `components/web_ui/assets/gm_panel/gm_panel_05f_scenario_validation.js`
- [x] `components/web_ui/assets/gm_panel/gm_panel_05c_reactive_v2.js`
- [x] `components/web_ui/assets/gm_panel/gm_panel_08_editor_actions.js`
- [x] `components/web_ui/assets/gm_panel/gm_panel_07_loaders_and_runtime_actions.js`
- [x] `components/web_ui/assets/gm_panel/gm_panel_09_scenario_change_events.js`
- [x] `components/web_ui/assets/gm_panel/gm_panel_00_actions.js`

## Execution Order

- [x] Phase 1: stop data loss
  - separate summary vs detail use
  - stop post-save/post-refresh downgrade of editor state
- [x] Phase 2: make draft authoritative
  - reducer-style updates
  - minimize DOM-derived truth
- [x] Phase 3: unify validation/reporting with draft identity
- [x] Phase 4: clean step labeling, audio rules, and runtime summaries
- [x] Phase 5: remove transitional code

## Verification

- [ ] Saving a scenario with audio params preserves `params.file` after:
  - `Save`
  - `Refresh`
  - reopen via `Edit`
  - page reload
- [ ] `Scenarios` list stays fast and summary-only.
- [ ] `Edit` always shows full branches/steps, never an empty `Main` because of summary fallback.
- [ ] `Validate` and `Start game` report errors on the correct steps/reactions.
- [ ] Internal `system_audio/system_relay/system_mosfet/system_io` commands remain functional.
- [ ] Room runtime view and scenario editor show consistent step names and audio summaries.

## Closeout

- [ ] Update `docs/CHANGELOG.md` with the completed scenario frontend model refactor.
- [ ] Delete this checklist file after the work is fully complete.
