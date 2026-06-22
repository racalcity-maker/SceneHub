# Known Issues

This file is the single active backlog for open alpha-phase product, runtime,
support and architecture issues.

Do not duplicate closed work here. Durable rules and completed baseline fixes
belong in the contract and policy documents instead:

- `ARCHITECTURE.md`
- `LOCKING_POLICY.md`
- `MEMORY_ALLOCATION_POLICY.md`
- `API_HTTP_POLICY.md`
- `COMMAND_RESULT_SEMANTICS.md`
- `device_control_contract_v1.md`
- `reactive_branch_v_2_design.md`

## Active Backlog

### P0 - Correctness And Runtime Safety

No active P0 defect is currently tracked.

Reopen P0 only for a concrete crash, data corruption, broken command-result
ordering, unsafe lock/IO regression, or a node/hub flow that silently applies
the wrong physical effect.

### P1 - Alpha Runtime And Support Risk

- Audio playback can still pop/click when starting background audio, switching
  background tracks, or starting MP3 effects over active background audio.
  Tracking doc: `AUDIO_PIPELINE_REFACTOR_PLAN.md`.
- Re-run the GM node-admin persistence path after the latest modal/import/admin
  changes:
  - `Get config`
  - `Import`
  - `Save device`
  - reopen modal and confirm imported counts persisted
  - `Load stored bundle`
  - `Validate bundle`
  - `Apply bundle`
  - reboot if required
- Re-check that the current node-admin HTTP responses stay visible in GM after
  the latest backend honesty fixes:
  - `rules.apply` should surface `applied=true`
  - `rules.apply` should surface `restart_required=true`
  - `rules.clear` should surface `cleared=true`
  - GM modal status should make those results visible without guessing from
    button state alone
- Re-run wide runtime-noise verification on the current WS-first refresh model
  before adding any waiter indexes or other broad runtime structures.
- Keep watching memory pressure on heavy admin/UI paths. `httpd` 500
  `no memory` events should be treated as active alpha regressions if they
  reappear during device-edit or bundle-admin flows.

### P2 - Node V2 Product Slice Gaps

- `fallback` mode is now implemented as an alpha first slice, not a deferred
  placeholder. It still needs wider hardware/fault testing before support can
  treat it as field-stable or production-ready.
- Centralized NFC known-card CRUD in SceneHub GM is intentionally deferred.
  Current owner surface is node-local provisioning.
- PN532 runtime recovery is implemented in layers, but the transport still
  needs more real-hardware confidence before being treated as field-stable
  under cable, power or intermittent-reader faults.
- Node standalone bundle support is still on the shipped `8 KB` alpha contract.
  Honest `32 KB` support now has its own rollout plan:
  `scenehub_node_v1/docs/NODE_V2_LARGE_BUNDLE_32KB_PLAN.md`.
  Until that plan lands end to end, support should treat `bundle_too_large`
  as expected behavior for oversized bundles rather than a storage bug.
- Re-check that degraded NFC reader state never blocks unrelated node
  operations or hides otherwise valid exported commands/events in GM.
- Keep exported bundle-facing names as the preferred scenario contract. Any
  regression where GM falls back to raw channel names instead of exported
  command/event labels is an active product bug.

### P3 - Hub Admin UX And Visibility

- Keep polishing inline feedback in the quest-device modal so import/save/admin
  failures are visible without guessing from button color or modal close
  behavior.
- Re-check that modal rendering does not aggressively rerender or poll in ways
  that obscure in-flight admin results or interfere with editing.
- Add a normal `Refresh interface` workflow for node manifest refresh so new
  effect names or exported bundle capabilities do not depend on manual reimport
  habits.
- NFC-specific GM admin surfaces are deferred. If later added, they must stay
  in `Devices -> Edit device`, not in scenario builders or quick operator
  actions.

### P4 - Node LED Follow-Up

- Add clearer preview-state indication in provisioning UI.
- Tighten effect-control rendering so each effect shows only the controls it
  actually uses.
- Re-run full LED persistence verification after the latest config/runtime
  splits:
  - save base LED wiring
  - save presets
  - reboot
  - reload provisioning UI
  - run command from hub
- Re-check that strip wiring and LED presets never overwrite each other.
- Add explicit logs for LED preset save/apply paths, not only effect runtime
  start/done/cancel/fail.

### P5 - Node V1 / Transport Cleanup Debt

- Move duplicate-request idempotency/cache ownership out of
  `mqtt_transport` into an explicit runtime/control owner.
- Reconcile old documented payload targets with the current compact manifest
  and provisioning/admin buffer sizes.
- Add focused node-side regression coverage for duplicate `request_id`, result
  delivery, input-event publishing, reset paths, config validation, and the
  remaining owner-runtime test gaps described in
  `scenehub_node_v1/docs/scenehub_node_owner_runtime_plan.md`.
- Add stress coverage for terminal-result overflow so overload remains explicit
  rejection/reconnect behavior, not silent loss.

### P6 - Architecture And Test Debt

- Measure prepared event-ref snapshot copy time during scenario start on target
  hardware. The current static PSRAM model is acceptable, but the bounded
  commit copy under `gm_session_lock` is still broad enough to keep watching.
- The `node_provisioning` god-file reduction is in progress but not fully
  complete yet. Keep the remaining core config file narrow and continue moving
  mixed parser/serializer/admin helpers out if it starts growing again.
- Node v2 dependency cleanup is now in durable policy/docs. Keep the remaining
  watch item narrow:
  - if more hardware slices appear, revisit whether a single NFC status facade
    should expand into a compact multi-driver read-model rather than teaching
    transport/admin code per-driver details.
- Replace the transitional scenario-layout path with a compact read-model DTO.
- Narrow `device_control_ingest` consumers where they only need focused
  questions instead of the whole ingest snapshot.
- Add isolated domain tests for GM wait/reaction/state transitions, command
  planning without hardware, `scenehub_events`, and `room_scenario`
  validation.
- Introduce fakeable ports only where they materially reduce test cost:
  time, event post, command dispatch, prepared runtime inputs.

### P7 - Manifest / Admin Payload Budget

- Keep `quest_device.device_description_json` as the only durable manifest
  owner after save/import flows.
- Revisit current MQTT transport ceilings only if compact-manifest growth or
  richer node metadata starts pushing the current budget again.
- Decide later whether transient node-admin metadata should stay in ingest or
  move into a narrower control-owned pending-response path.
- Design a two-layer compact runtime manifest vs richer UI metadata split only
  if channel counts, LED schemas or bundle-export metadata grow enough to force
  it.

### P8 - Deferred Product Work

- Optional `system_led` support remains deferred.
- Universal Node remains future work and must stay on the same Quest
  Device/device-control contract rather than fork a second scenario model.
- ESP-NOW and RS485/MAX485 remain deferred transports.

## Verification Hotspots

When touching current alpha surfaces, re-check these end-to-end flows:

- GM `Get config` -> `Import` -> `Save device`
- node `Validate bundle` -> `Apply bundle` -> reboot -> `Load stored bundle`
- PN532 enabled but absent: node remains usable, GM shows degraded reader state
- exported bundle commands/events appear in GM with the intended names
- LED wiring + preset persistence after reboot

## Retired Into Durable Docs

The following are no longer tracked here because the baseline already shipped
and the behavior is documented elsewhere:

- command-result terminal/pending semantics
- provisioning auto-close baseline
- compact `describe_interface` manifest baseline
- node admin split between GM modal workflow and quick admin actions
- PSRAM-first owner scratch migration for the large node admin/runtime objects
- `runtime_snapshot` borrowed-pointer lifetime handling
- fallback runtime diagnostic status lock discipline
- local-rule re-entry guard for exported MQTT rule-command dispatch
- shared node JSON escaping baseline for manifest/status/admin payloads
- technical identifier whitelist for rule/schema/export/driver ids
