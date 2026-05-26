# Node Implementation Checklist

This checklist is the practical bring-up guide for a physical SceneHub MQTT
node. The authoritative protocol reference is `device_control_contract_v1.md`.

## Identity

- Choose one stable node topic id, for example `relay_room_2`.
- Use the same id in all node topics:
  `cp/v1/dev/relay_room_2/...`.
- Store the same id in SceneHub Quest Device metadata as the physical node id
  field currently named `client_id`.
- The MQTT connection client id may be different, for example
  `dcc-relay-room-2`.
- Do not publish telemetry under the MQTT connection id unless it is also the
  physical node topic id.
- Do not reuse the same physical node topic id for two different nodes.

## Required MQTT Topics

Node publishes:

- `cp/v1/dev/{node_id}/heartbeat`
- `cp/v1/dev/{node_id}/status`
- `cp/v1/dev/{node_id}/result`
- `cp/v1/dev/{node_id}/event`
- `cp/v1/dev/{node_id}/diag` when diagnostics are useful

Node subscribes:

- `cp/v1/dev/{node_id}/control/command`

Broadcast subscription is optional:

- `cp/v1/dev/all/control/command`

Only safe node-level commands should be accepted from broadcast topics.

## Boot And Reconnect

After every MQTT connect or reconnect, publish in this order:

1. `heartbeat`
2. `status`
3. queued terminal command results completed while disconnected

Heartbeat target:

- Publish heartbeat about every 2 seconds.
- SceneHub treats missing fresh heartbeat/status/result as offline/fault.

Status target:

- Publish status on boot.
- Publish status after meaningful output/input/runtime state changes.
- Keep status small and bounded.

## Required Command Handling

Every command payload contains:

- `request_id`
- `command`
- `args`
- `ts_ms` when available

Node rules:

- Always publish a result with the same `request_id`.
- Treat `request_id` as the idempotency key.
- Never repeat unsafe physical side effects for a duplicate `request_id`.
- If duplicate work is still running, republish `accepted` or `started`.
- If duplicate work is already complete, republish the cached terminal result.
- Reject invalid JSON, unsupported commands and invalid args with
  `status=rejected`.

Terminal statuses:

- `done`
- `failed`
- `rejected`

Non-terminal status:

- `accepted` or `started`, only if a later terminal result will be published.

Scenario behavior:

- Only terminal `done` advances a `result_required` scenario step.
- `failed`, `rejected` or timeout fail the step.
- `accepted` alone does not advance the step.
- `started` alone does not advance the step.

## Required Node Commands

Implement these node-level commands first:

- `describe_interface`
- `node.get_status`
- `node.identify`

Optional later:

- `node.reboot`
- `node.reset_config`

`describe_interface` result must include `device_description` with stable
commands and events. SceneHub imports this only after admin confirmation.

## Hardware Safety

- Validate channel numbers before touching hardware.
- Reject commands for unavailable outputs with `invalid_channel`.
- Reject unsafe concurrent operations with `busy`.
- Put outputs into a known safe state on boot if the hardware requires it.
- Make effects cancellable by explicit `set`, stop/reset commands or local
  safety policy.
- Keep dangerous commands target-only; do not accept them from broadcast.

## Payload Limits

- Keep normal command, event, heartbeat, status and result payloads under 4 KB.
- Keep `describe_interface` compact. Current SceneHub target is at least 4 KB
  payload and 6 KB MQTT packet.
- Reject unsupported oversize requests with `invalid_request` or
  `invalid_args`.

## Error Codes

Use stable error codes:

- `invalid_request`
- `invalid_args`
- `invalid_channel`
- `not_supported`
- `busy`
- `timeout`
- `internal_error`
- `unauthorized`

Prefer short human-readable `error.message` text for UI diagnostics.

## Bring-Up Smoke Test

Minimum manual test before using the node in a room:

1. Connect the node to SceneHub MQTT.
2. Confirm heartbeat/status make the device `online` and `ok`.
3. Run `describe_interface` from Device Setup and import commands/events.
4. Run one safe manual command and confirm a `done` result.
5. Run one invalid command/arg and confirm `rejected` with `error.code`.
6. Start a room scenario that sends one node command.
7. Confirm result-required command steps advance only after `done`.
8. Disconnect the node and confirm SceneHub marks it fault/offline.
9. Reconnect the node and confirm heartbeat/status restore health.
10. Send a duplicate `request_id` and confirm the physical action is not
    repeated.

## Common Failure Modes

- Node connects as `dcc-relay-room-2` but publishes to
  `cp/v1/dev/dcc-relay-room-2/...` while SceneHub expects
  `cp/v1/dev/relay_room_2/...`.
- Node publishes heartbeat but never status, leaving UI diagnostics incomplete.
- Node returns `accepted` or `started` and never sends a terminal result.
- Node executes duplicate `request_id` commands as fresh physical actions.
- Node uses retained command/result/event messages.
- Node imports a large `device_description` that exceeds MQTT packet limits.
- Two saved Quest Devices point to the same physical node topic id.
