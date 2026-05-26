# SceneHub Docs

## Core References

- `ARCHITECTURE.md` - high-level firmware architecture.
- `gm_api_contract.md` - GM panel HTTP/API contract.
- `device_control_contract_v1.md` - SceneHub-native physical device MQTT contract.
- `COMMAND_RESULT_SEMANTICS.md` - terminal vs pending command status rules.
- `API_HTTP_POLICY.md` - HTTP method/payload hygiene policy for hub and node surfaces.
- `reactive_branch_v_2_design.md` - current Reactive Branch v2 contract and runtime rules.
- `TESTING.md` - test layout and local test guidance.
- `KNOWN_ISSUES.md` - single active backlog for open product/runtime/architecture issues.
- `CHANGELOG.md` - durable record of completed changes worth preserving.

## Setup And Operations

- `ROOM_SCENARIO_SETUP_RUS.md` - room scenario setup guide.
- `QUEST_DEVICE_SETUP_RUS.md` - Quest Device setup guide.

## Policies And Architecture

- `ARCHITECTURE_LAYER_RISK_MAP.md` - practical dependency risk map for the SceneHub layering contract.
- `MEMORY_ALLOCATION_POLICY.md` - runtime/audio allocation policy, audit and cleanup checklist.
- `LOCKING_POLICY.md` - lock ownership, ordering and external-call rules.
- `HUB_AUDIT_P0_P1_PLAN.md` - current hub-side audit status for write boundaries, policy enforcement and result lifecycle.
- `NODE_DESCRIBE_INTERFACE_REFACTOR_PLAN.md` - deferred node manifest size/budget refactor plan.

## Completed Plans Retired Into Durable Docs

The following work is no longer tracked in standalone plan files because the
baseline has already shipped and the durable contract/docs were updated:

- hub write-side manual-command status semantics
- web auth bootstrap hardening
- node provisioning auto-close
- node LED editor rollout baseline

## Desktop App

- `../desktop_app/README.md` - desktop application workspace entry point.
- `../desktop_app/docs/` - desktop-specific documentation.

## Assets

- `Pics/` - screenshots used by README and GM panel docs.
