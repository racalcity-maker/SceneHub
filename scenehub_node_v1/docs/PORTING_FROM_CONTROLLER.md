# Porting From SceneHub Controller

The node should be built against the MQTT contract, not against controller
components.

## Safe To Port

- Protocol constants:
  - `cp/v1/dev/{node_id}/heartbeat`
  - `cp/v1/dev/{node_id}/status`
  - `cp/v1/dev/{node_id}/diag`
  - `cp/v1/dev/{node_id}/result`
  - `cp/v1/dev/{node_id}/event`
  - `cp/v1/dev/{node_id}/control/command`
- JSON envelope field names:
  - `request_id`
  - `command`
  - `args`
  - `status`
  - `error`
  - `data`
  - `device_description`
- Result status names:
  - `accepted`
  - `done`
  - `failed`
  - `rejected`
- Error code names from the contract.
- Idempotency behavior from the Python reference client.
- Reconnect order from the checklist.

## Do Not Port

- `mqtt_core`: SceneHub is the broker; a node is an MQTT client.
- `command_executor`: SceneHub decides when to dispatch commands; a node only
  executes commands addressed to itself.
- `device_control_ingest`: SceneHub consumes node telemetry; a node produces it.
- `quest_device`: SceneHub stores device metadata; a node only exposes
  `device_description`.
- `scenehub_read_model`: SceneHub projection layer only.
- `gm_core` and room scenario runtime: controller-only gameplay state.
- Web UI request/response DTOs.

## Possible Future Shared Code

If duplication becomes painful, extract only a tiny protocol helper package:

- topic builder;
- bounded JSON field helpers;
- enum/string constants;
- command/result envelope validation.

Do not share runtime ownership, storage, locks, MQTT broker state or UI models.
