# SceneHub Node Docs

This index points to the active documentation for the Node v2 rollout.

## Start Here

- `NODE_V2_TRANSITION_PLAN.md` - rollout scope, modes, and shipped direction.
- `scenehub_node_owner_runtime_plan.md` - owner-driven runtime refactor plan and remaining phase map.
- `PROVISIONING_AND_CONFIG.md` - provisioning UI behavior and config workflow.
- `NODE_PROVISIONING_QUICKSTART_RUS.md` - short bring-up path for first node setup.
- `NODE_V2_ENGINE_CONTRACT.md` - authoritative rule-engine semantics.
- `NODE_V2_LARGE_BUNDLE_32KB_PLAN.md` - rollout plan for honest `32 KB`
  standalone bundle support without widening ordinary runtime DTOs.
- `NODE_V2_DRIVER_PLAN.md` - driver rollout plan, especially NFC/PN532 scope.

## Contracts

- `NODE_V2_RULE_SCHEMA_DRAFT.md` - bundle schema shape and authoring model.
- `NODE_V2_ENGINE_CONTRACT.md` - compile/runtime contract for standalone logic.
- `NODE_V2_LARGE_BUNDLE_32KB_PLAN.md` - staged plan for lifting the current
  `8 KB` alpha bundle contract to a bounded `32 KB` HTTP-first admin path.
- `PROVISIONING_AND_CONFIG.md` - node-side admin and configuration surface.

## Policies

- `ARCHITECTURE_POLICY.md` - layering and ownership rules.
- `MEMORY_POLICY.md` - allocation discipline, scratch usage, DTO boundaries.
- `LOCKING_POLICY.md` - lock ordering and external-call restrictions.
- `API_POLICY.md` - node HTTP/API surface rules.

## Hardware And Drivers

- `BOARD_PROFILES.md` - board-specific profiles and pin assumptions.
- `NODE_V2_DRIVER_PLAN.md` - current and planned driver slices.
- `NFC_PN532_SETUP_AND_DIAGNOSTICS_RUS.md` - practical PN532 setup and degraded-state diagnostics.
- `LED_REMAINING.md` - remaining LED-specific work.

## Migration And Audit

- `NODE_V2_TRANSITION_PLAN.md` - active transition checklist and remaining work.
- `scenehub_node_owner_runtime_plan.md` - current owner-runtime cleanup phases after the main Node v2 slice.
- `NODE_V1_RUNTIME_AUDIT_PLAN.md` - older runtime audit context.
- `V1_AUDIT_BACKLOG.md` - legacy backlog still worth preserving.
- `PORTING_FROM_CONTROLLER.md` - notes for code moved from controller lineage.

## Tests

- `../esp_idf/tests/node_runtime/README.md` - node-side owner-runtime test app, now with initial live coverage for control source-policy and schema identifier guards.

## Example Bundles

Reference bundles live in `examples/node_v2_bundles/`.

- `nfc_hold_5s_any_known_card.json`
- `nfc_hold_10s_token_1.json`
- `nfc_hold_until_removed_reset.json`
- `secret_door_girkon_1_bundle.json`
- `export_open_room_2.json`

## Operator Context

Hub-facing admin/import behavior is documented in:

- `../../docs/NODE_V2_HUB_ADMIN_UI_PLAN.md`
- `../../docs/SUPPORT_RUNBOOK.md`
- `../../docs/VERSION_COMPATIBILITY_MATRIX.md`
