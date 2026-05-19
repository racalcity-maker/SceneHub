# Device Control Contract v1

This contract defines the SceneHub-native MQTT control plane for smart nodes.

SceneHub-native devices use command envelopes. The old `topic + static payload`
command shape is not part of the forward contract because no production devices
depend on it yet.

## Namespace

Base namespace:

```text
cp/v1/dev/{device_id}/...
```

In this namespace `{device_id}` means the physical MQTT namespace id, also
stored in SceneHub Quest Device metadata as `client_id`. A Quest Device `id` is
the saved game/config object id and may differ from the physical MQTT namespace
id. The MQTT connection `client_id` may be different and is used only for broker
session/auth identity.

Recommended node id format:

- Use stable lowercase ids with `[a-z0-9_-]` only.
- Prefix field/debug MQTT connection ids with `dcc-`, for example
  `dcc-relay-room-2`, but keep the topic namespace id stable, for example
  `relay_room_2`.
- Do not rely on prefix ACL matching. SceneHub product ACLs should allow exact
  known node ids or explicitly configured id patterns.

Device publishes:

- `cp/v1/dev/{device_id}/heartbeat`
- `cp/v1/dev/{device_id}/status`
- `cp/v1/dev/{device_id}/diag`
- `cp/v1/dev/{device_id}/result`
- `cp/v1/dev/{device_id}/event`

Device subscribes:

- `cp/v1/dev/{device_id}/control/command`

Optional broadcast command topic:

- `cp/v1/dev/all/control/command`

The broadcast topic is optional and should only be used for safe node-level
commands such as `node.identify`. Dangerous commands must target a single node.

## Transport Policy

MQTT version:

- SceneHub targets MQTT 3.1.1 semantics.
- QoS 0 and QoS 1 are supported. QoS 2 is not supported and may cause the
  connection to be closed.

QoS:

- Commands may be published as QoS 0 or QoS 1.
- Heartbeat, status, diagnostics, runtime events and results should be QoS 0 by
  default.
- A node may use QoS 1 for command results when the command has physical side
  effects, but it must still handle duplicates by `request_id`.

Retain:

- Commands must not be retained.
- Events and results must not be retained.
- Heartbeat should not be retained.
- Status may be retained only if the node explicitly republishes fresh status on
  every reconnect. SceneHub does not require retained status for correctness.

Packet limits:

- Normal command, event, heartbeat, status and result payloads should stay under
  4 KB.
- `describe_interface` results may be larger. Current SceneHub target is at
  least 4 KB payload and 6 KB MQTT packet.
- Nodes should reject unsupported oversize commands with `status=rejected` and
  `error.code=invalid_request` or `invalid_args`.

## Command Envelope

SceneHub sends commands to:

```text
cp/v1/dev/{device_id}/control/command
```

Payload:

```json
{
  "request_id": "req-123",
  "command": "relay.pulse",
  "args": {
    "channel": 1,
    "duration_ms": 1000
  },
  "ts_ms": 1713900000000
}
```

Rules:

- `request_id` is required for every command.
- `command` is a stable dotted command name such as `relay.pulse` or
  `node.identify`.
- `args` is an object. Empty args must be `{}`.
- `ts_ms` is sender timestamp when available.
- A device must publish a result with the same `request_id`.
- Nodes must treat `request_id` as the idempotency key. If the same command with
  the same `request_id` is received again, the node must not repeat unsafe
  physical side effects. It should republish the last known result, or publish
  `accepted` if the original work is still running.
- Nodes may ignore stale duplicate commands whose terminal result was already
  published and whose local duplicate cache expired, but they must not execute
  them as fresh commands with the same `request_id`.

## Result Envelope

Device publishes command results to:

```text
cp/v1/dev/{device_id}/result
```

Successful result:

```json
{
  "request_id": "req-123",
  "command": "relay.pulse",
  "status": "done",
  "ts_ms": 1713900001000
}
```

Failed result:

```json
{
  "request_id": "req-123",
  "command": "relay.pulse",
  "status": "failed",
  "error": {
    "code": "invalid_channel",
    "message": "Relay channel out of range"
  },
  "ts_ms": 1713900000100
}
```

Rejected result for commands the device refuses before execution:

```json
{
  "request_id": "req-123",
  "command": "relay.pulse",
  "status": "rejected",
  "error": {
    "code": "busy",
    "message": "Relay controller is busy"
  },
  "ts_ms": 1713900000100
}
```

Accepted result for long-running work:

```json
{
  "request_id": "req-123",
  "command": "led.pulse",
  "status": "accepted",
  "ts_ms": 1713900000050
}
```

Rules:

- `status` is `done`, `failed`, `rejected`, or `accepted`.
- `accepted` is only for long-running commands that will publish a later
  terminal `done`, `failed`, or `rejected`.
- `accepted` never advances a `result_required` scenario step.
- Only terminal `done` advances a `result_required` scenario step.
- `failed`, `rejected`, or timeout fail the step.
- `failed` and `rejected` must include `error.code`.
- SceneHub may time out a request if no terminal result arrives.
- SceneHub creates pending result tracking before publishing the command. This
  allows very fast nodes to return a result immediately without racing pending
  registration.

Recommended error codes:

- `invalid_request`
- `invalid_args`
- `invalid_channel`
- `not_supported`
- `busy`
- `timeout`
- `internal_error`
- `unauthorized`

Runtime events and status may include an optional monotonically increasing
`seq` field. SceneHub treats it as diagnostic metadata in v1.

## Reconnect And Boot Behavior

After MQTT connect or reconnect, a node should publish in this order:

1. `heartbeat`
2. `status`
3. Any queued terminal command results that were completed while disconnected

Heartbeat interval:

- Default target interval is 2 seconds.
- SceneHub currently considers a device offline after no fresh
  heartbeat/status/result for the configured online timeout. The current default
  target is 5 seconds.

Boot identity:

- `boot_id` should change after every MCU reboot.
- `uptime_ms` should reset after reboot.
- `status_seq` or `seq` should be monotonically increasing per boot when used.

Inflight commands:

- If a node reconnects while executing a command, it should publish `accepted`
  for the existing `request_id` if execution is still active, then publish a
  terminal result when complete.
- If a node rebooted and lost command state, it should publish `failed` with
  `error.code=internal_error` when it can identify the lost request. If it cannot
  identify the request, SceneHub will time out the command.

## Event Envelope

Device publishes events to:

```text
cp/v1/dev/{device_id}/event
```

Payload:

```json
{
  "event": "input.pressed",
  "args": {
    "channel": 1
  },
  "ts_ms": 1713900001200
}
```

Rules:

- `event` is a stable dotted event name such as `input.pressed`.
- `args` is an object. Empty args must be `{}`.
- SceneHub matches saved event capabilities by event name plus configured args.

## Heartbeat

Device publishes heartbeat to:

```text
cp/v1/dev/{device_id}/heartbeat
```

Payload:

```json
{
  "ts_ms": 1713900000000,
  "boot_id": "5f8d-2a11",
  "uptime_ms": 1234567,
  "status_seq": 42
}
```

## Status

Device publishes status to:

```text
cp/v1/dev/{device_id}/status
```

Payload:

```json
{
  "ts_ms": 1713900000000,
  "boot_id": "5f8d-2a11",
  "fw_version": "0.1.0",
  "mode": "normal",
  "state": "idle",
  "health": "ok",
  "capabilities": ["heartbeat", "status", "diag", "describe_interface", "node.identify"],
  "runtime": {
    "active": false
  }
}
```

Rules:

- `health` is `ok`, `warning`, or `fault`.
- `capabilities` should include service-level capabilities.
- `status` should not include the full device description. Use
  `describe_interface` for that.

## Diagnostics

Device publishes diagnostics to:

```text
cp/v1/dev/{device_id}/diag
```

Payload:

```json
{
  "ts_ms": 1713900000000,
  "level": "warn",
  "code": "input_noise",
  "message": "Input 2 generated too many edges",
  "details": {
    "channel": 2
  }
}
```

Rules:

- `level` is `info`, `warn`, or `error`.
- Diagnostics affect SceneHub health aggregation.

## Device Description Discovery

`describe_interface` is an on-demand discovery command. Devices that support it
should include `describe_interface` in `status.capabilities`.

SceneHub sends:

```json
{
  "request_id": "req-8c9f1d",
  "command": "describe_interface",
  "args": {},
  "ts_ms": 1713900000000
}
```

Device responds on `result`:

```json
{
  "ts_ms": 1713900000100,
  "request_id": "req-8c9f1d",
  "command": "describe_interface",
  "status": "done",
  "data": {
    "device_description": {
      "version": 1,
      "commands": [
        {
          "id": "relay_pulse_1",
          "label": "Relay 1 pulse",
          "capability": "relay",
          "command": "relay.pulse",
          "args_schema": [
            { "key": "channel", "type": "number", "required": true, "min": 1, "max": 4 },
            { "key": "duration_ms", "type": "number", "required": true, "min": 1, "max": 60000 }
          ],
          "default_args": {
            "channel": 1,
            "duration_ms": 1000
          },
          "policy": {
            "manual_allowed": true,
            "scenario_allowed": true,
            "requires_confirmation": false,
            "result_required": true,
            "timeout_ms": 3000,
            "danger_level": "normal"
          }
        },
        {
          "id": "identify",
          "label": "Identify node",
          "capability": "node",
          "command": "node.identify",
          "args_schema": [],
          "default_args": {},
          "policy": {
            "manual_allowed": true,
            "scenario_allowed": true,
            "requires_confirmation": false,
            "result_required": true,
            "timeout_ms": 3000,
            "danger_level": "normal"
          }
        }
      ],
      "events": [
        {
          "id": "input_1_pressed",
          "label": "Input 1 pressed",
          "capability": "input",
          "event": "input.pressed",
          "args_schema": [
            { "key": "channel", "type": "number", "required": true, "min": 1, "max": 4 }
          ],
          "match": {
            "channel": 1
          }
        }
      ]
    }
  }
}
```

Discovery rules for SceneHub Node compact manifests:

- `device_description.manifest_version` is required and currently equals `2`.
- `device_description.format` must be `compact_resources`.
- `device_description.node_kind` identifies the node class.
- `device_description.capability_contract` must be
  `scenehub.node.compact.v1`.
- Node resources are described under `resources`; raw GPIO numbers are node
  local config and must not be exposed in the manifest.
- `command_templates[].id` is the scenario `command_id`.
- Channel/effect/resource selection lives in scenario `params`, not in
  generated per-channel command ids.
- `command_templates[].args_schema_ref` and
  `event_templates[].args_schema_ref` must reference entries in `schemas`.
- Flat `commands[]` and `events[]` are a separate custom-device path, not a
  SceneHub Node compatibility format.
- SceneHub imports this metadata only after admin confirmation.
- `device_description` is discovery/config metadata. It must be requested on
  demand, not sent in every heartbeat/status.

## Required Node Actions

Every SceneHub-native smart node should support:

- `describe_interface`
- `node.identify`
- `node.get_status`

Recommended optional commands:

- `node.reboot`
- `node.reset_config`

## SceneHub Semantics

- Physical clients appear in GM `Observed`.
- Quest Devices store a `client_id` that points to the physical
  control-contract topic namespace id.
- A physical client is considered registered when referenced by a saved Quest
  Device `client_id`, even if Quest Device id/name or MQTT connection client id
  is different.
- If a previously observed registered device stops sending fresh telemetry, GM
  treats it as `offline`.
- `offline` registered quest devices are critical faults for room/system health.
- `not observed` during setup is warning/degraded until first valid telemetry.
- Saved Quest Device capabilities describe available commands/events.
- Room Scenarios remain the authoritative quest-flow configuration.
- Health, connectivity, firmware and diagnostics come from
  `heartbeat/status/diag/result`, not from `device_description`.

## Room Scenario Mapping

`DEVICE_COMMAND`:

- resolves a saved Quest Device command capability;
- builds a command envelope using `command` and merged args;
- sends to `cp/v1/dev/{client_id}/control/command`;
- waits for terminal result when `policy.result_required=true`;
- ignores `accepted` for advancement;
- advances only on terminal `done`;
- fails the scenario step on `failed`, `rejected`, or timeout.

`WAIT_DEVICE_EVENT`:

- resolves a saved Quest Device event capability;
- waits for `cp/v1/dev/{client_id}/event`;
- matches by `event` plus configured `match` args.

## Compatibility Policy

No long-term `topic + payload` compatibility path is required for production
devices. There are no production nodes yet, so the public contract should stay
SceneHub-native from the start.
