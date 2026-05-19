# Device Control Reference Client

Reference Python client that emulates smart devices for `Device Control Contract v1`.

Path:

- `tools/device_control_client/client.py`

## What It Does

For each configured device:

- publishes:
  - `cp/v1/dev/{node_id}/heartbeat`
  - `cp/v1/dev/{node_id}/status`
  - `cp/v1/dev/{node_id}/diag`
  - `cp/v1/dev/{node_id}/result`
- subscribes:
  - `cp/v1/dev/{node_id}/control/command`
  - `cp/v1/dev/all/control/command`

`device_id` is the local config/UI target name. `node_id` is the physical MQTT
namespace id used in topics. By default `node_id` is copied from `device_id`.
`client_id` is only the MQTT connection/auth identity and may be prefixed, for
example `dcc-relay-room-2`.

Supported commands:

- `node.get_status`
- `node.identify`
- `node.reboot` (fake reboot)
- `node.reset_runtime`
- `node.apply_preset` (uses local profile preset map; returns `not_supported` if preset missing)
- `describe_interface` when `device_description` is configured for the device
- flat custom commands listed in `device_description.commands`
- compact node command templates listed in `device_description.command_templates`

## Device Description Discovery

Each configured device may include a `device_description` block. This is used by GM Panel Device Setup to import device capabilities.

SceneHub Node devices should use compact manifest v2:

- `manifest_version: 2`
- `format: "compact_resources"`
- `node_kind`
- `capability_contract: "scenehub.node.compact.v1"`
- `resources`, `command_templates`, `event_templates`, `schemas`

The client returns compact v2 unchanged from `describe_interface` and builds
its local command lookup from `command_templates`. Flat `commands[]` /
`events[]` remain supported only for hand-written custom device tests, not as a
SceneHub Node compatibility format.

When the broker sends:

```json
{
  "request_id": "req-1002",
  "command": "describe_interface",
  "args": {},
  "ts_ms": 1713900000000
}
```

the client returns:

```json
{
  "ts_ms": 1713900000100,
  "request_id": "req-1002",
  "command": "describe_interface",
  "status": "done",
  "error_code": "",
  "message": "",
  "data": {
    "device_description": {
      "manifest_version": 2,
      "format": "compact_resources",
      "node_kind": "virtual_relay_node",
      "capability_contract": "scenehub.node.compact.v1",
      "resources": {},
      "command_templates": [],
      "event_templates": [],
      "schemas": {}
    }
  }
}
```

If a configured command template has `emit_event_id`, the client publishes the
matching native event to `cp/v1/dev/{node_id}/event` after optional
`emit_delay_ms`. Test templates may also use `emit_event_id_by_channel` to map a
resource channel to an event id.

Command results use the current terminal statuses:

- `done`
- `failed`
- `rejected`
- `accepted` followed by terminal `done` for long-running commands such as `node.reboot`

Duplicate commands with the same `request_id` are idempotent: the client
re-publishes the cached terminal result, or `accepted` while the original command
is still running.

## Internal Runtime State

Each emulated device keeps:

- `boot_id`
- `uptime_ms`
- `health`
- `online`
- `last_error`
- `preset`

## Test Features

Per device (config):

- `simulate_fault` - forces `health=fault`
- `simulate_degraded` - forces `health=degraded`
- `response_delay_ms` - artificial delay before command handling
- `silent_mode` - suppresses all outbound MQTT telemetry
- `fake_reboot_ms` - reboot downtime for `node.reboot`
- `result_qos` - result publish QoS, `0` by default and `1` when configured

## Requirements

- Python 3.9+
- `paho-mqtt`

Install:

```bash
pip install paho-mqtt
```

## Run

From the SceneHub repository root:

```bash
python tools/device_control_client/client.py --config tools/device_control_client/config_example.json
```

Run only selected device(s):

```bash
python tools/device_control_client/client.py --config tools/device_control_client/config_example.json --device uid_gate_1
```

Run without interactive console:

```bash
python tools/device_control_client/client.py --config tools/device_control_client/config_example.json --no-console
```

## Interactive Console

By default client starts interactive shell (`dcc>`).

Commands:

- `help`
- `list`
- `show <device_id|all>`
- `fault <device_id|all> <on|off|toggle>`
- `degraded <device_id|all> <on|off|toggle>`
- `silent <device_id|all> <on|off|toggle>`
- `delay <device_id|all> <ms>`
- `reboot <device_id|all>`
- `preset <device_id|all> <preset_id>`
- `publish <device_id|all> <hb|status|diag>`
- `event <device_id|all> <event_id> [payload]`
- `quit` / `exit`

## Command Payload Example

```json
{
  "request_id": "req-1001",
  "command": "node.apply_preset",
  "args": {
    "preset_id": "maintenance"
  },
  "ts_ms": 1713900000000
}
```

## Files

- `client.py` - runtime and MQTT logic
- `profiles.py` - profile templates and preset maps
- `config_example.json` - sample config for multi-device run
