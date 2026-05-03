# Device Control Reference Client

Reference Python client that emulates smart devices for `Device Control Contract v1`.

Path:

- `tools/device_control_client/client.py`

## What It Does

For each configured device:

- publishes:
  - `cp/v1/dev/{device_id}/heartbeat`
  - `cp/v1/dev/{device_id}/status`
  - `cp/v1/dev/{device_id}/diag`
  - `cp/v1/dev/{device_id}/result`
- subscribes:
  - `cp/v1/dev/{device_id}/control/command`
  - `cp/v1/dev/{device_id}/cmd` (alias for tests)
  - `cp/v1/dev/all/control/command`
  - `cp/v1/dev/all/cmd`
  - quest command topics listed in `quest_interface.commands`

Supported commands:

- `refresh_status`
- `reboot` (fake reboot)
- `reset_runtime`
- `apply_preset` (uses local profile preset map; returns `not_supported` if preset missing)
- `describe_interface` when `quest_interface` is configured for the device

## Quest Interface Discovery

Each configured device may include a `quest_interface` block. This is used by GM Panel Device Setup to import quest device capabilities.

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
  "status": "ok",
  "error_code": "",
  "message": "",
  "data": {
    "quest_interface": {
      "version": 1,
      "commands": [],
      "events": []
    }
  }
}
```

The client also subscribes to every `commands[].topic`. If a command has `emit_event_id`, the client publishes the matching event after optional `emit_delay_ms`.

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
- `fake_reboot_ms` - reboot downtime for `reboot` command

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
  "command": "apply_preset",
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
