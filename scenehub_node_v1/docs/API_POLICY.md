# SceneHub Node API Policy

This policy defines API boundaries for SceneHub Node firmware. It applies to
MQTT commands, local provisioning HTTP routes and future Node v2 rule/config
commands.

## API Layers

Node APIs are split by ownership:

- MQTT device-control API: runtime control and telemetry for SceneHub.
- Local provisioning HTTP API: first setup and local recovery.
- Internal control API: validated command dispatch inside firmware.
- Future Node v2 admin API: rule bundle validate/apply/get/clear.

These APIs may share DTO concepts, but they must not share handler logic that
mixes transport parsing with hardware actions.

## Transport Boundary

Transport handlers only:

- read bounded request payload;
- validate envelope shape;
- call `node_control`, config API or rule API;
- serialize result.

Transport handlers must not:

- call GPIO/hardware drivers directly;
- parse and execute rule actions directly;
- write storage while holding hardware/runtime locks;
- allocate in hot runtime command paths;
- expose secrets such as Wi-Fi passwords.

## MQTT Runtime API

MQTT remains compatible with `device_control_contract_v1.md`.

Required command envelope:

```json
{
  "request_id": "req-123",
  "command": "relay.set",
  "args": {
    "channel": 1,
    "on": true
  },
  "ts_ms": 1713900000000
}
```

Required result envelope:

```json
{
  "request_id": "req-123",
  "command": "relay.set",
  "status": "done",
  "ts_ms": 1713900000100
}
```

MQTT command handlers must use `request_id` as the idempotency key before
executing physical side effects.

## HTTP Provisioning API

HTTP provisioning is local setup/admin only.

Current routes:

- `GET /`
- `GET /api/status`
- `GET /api/config`
- `POST /api/config`
- `POST /api/restart`
- `POST /api/reset-wifi`
- `POST /api/factory-reset`

Rules:

- `GET /api/config` must not return Wi-Fi password or future secrets.
- `POST /api/config` may accept Wi-Fi password and store it.
- Config writes require full validation before save.
- Config changes that affect Wi-Fi, MQTT or GPIO require restart/re-init.
- If `pin_config_locked=true`, pin edits must be ignored or rejected.
- Destructive actions require UI confirmation.

## Internal Control API

`node_control_execute()` is the only command execution boundary.

It accepts:

- command name;
- args JSON or later a typed args DTO;
- request metadata.

It returns:

- `done`, `failed` or `rejected`;
- stable error code;
- optional data JSON.

It must not know whether the command came from MQTT, HTTP test route or future
rule engine.

## Error Codes

Use stable machine-readable error codes:

- `invalid_request`
- `invalid_args`
- `missing_channel`
- `missing_on`
- `invalid_channel`
- `not_configured`
- `not_supported`
- `busy`
- `timeout`
- `internal_error`

Do not use log text as API error text.

## Versioning

- MQTT device-control contract is versioned by topic namespace: `cp/v1`.
- SceneHub Node `device_description` uses `manifest_version`, `format`,
  `node_kind` and `capability_contract`.
- Current compact manifest identity is:
  `manifest_version=2`, `format=compact_resources`,
  `capability_contract=scenehub.node.compact.v1`.
- HTTP provisioning may include schema version later, but should remain simple.
- Node v2 rule bundles use their own `version` field.

Breaking changes require a new explicit version or backward-compatible adapter.

## Payload Limits

Recommended v1 limits:

- MQTT command payload: 4 KB maximum.
- MQTT result/status/event payload: 4 KB maximum.
- HTTP config request: 4 KB maximum.
- `device_description`: 2 KB target for v1, expand only if needed.

Oversize payloads must be rejected cleanly.

## Security Baseline

- Provisioning AP is local recovery/setup, not a long-running admin surface.
- Do not expose stored secrets through any API.
- Do not accept destructive config commands from MQTT broadcast topics.
- Factory reset must keep immutable factory identity unless explicitly erased
  by product policy.

## Future Node v2 API

Rule APIs should be admin/config APIs, not scenario/runtime commands:

- `node.rules.validate`
- `node.rules.apply`
- `node.rules.get`
- `node.rules.clear`
- `node.rules.pause`
- `node.rules.resume`

Rule APIs must validate and compile bundles before activation. Runtime rule
actions must call `node_control_execute()` or typed command handlers, never
hardware drivers directly.
