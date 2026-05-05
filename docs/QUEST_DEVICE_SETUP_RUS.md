# Quest Device setup

Quest Device is the saved SceneHub-side description of one physical client or
one built-in system service. It stores commands and events that Room Scenarios
can use through `DEVICE_COMMAND` and `WAIT_DEVICE_EVENT`.

## Model

Physical clients talk through the Device Control Contract:

- telemetry: `heartbeat`, `status`, `diag`;
- command result: `result`;
- runtime event: `event`;
- command receive topic: `cp/v1/dev/{client_id}/control/command`.

Quest Device stores only SceneHub-native command/event capabilities. It does not
store arbitrary `topic + payload` commands.

## Command

Minimal command:

```json
{
  "id": "relay_1_pulse",
  "label": "Relay 1 pulse",
  "capability": "relay",
  "command": "relay.pulse",
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
  },
  "args_schema": [
    {
      "key": "channel",
      "label": "Channel",
      "type": "number",
      "optional": false
    }
  ]
}
```

Fields:

- `id`: stable command id inside this Quest Device.
- `label`: operator-facing label.
- `capability`: UI/grouping capability, for example `relay`.
- `command`: command name sent in the command envelope, for example
  `relay.pulse`.
- `default_args`: optional default args object merged with scenario params.
- `policy.manual_allowed`: show this command as a manual GM button.
- `policy.scenario_allowed`: allow this command from Room Scenarios.
- `policy.requires_confirmation`: require confirmation in UI.
- `policy.result_required`: command should produce a terminal `result`.
- `policy.timeout_ms`: expected result timeout.
- `policy.danger_level`: operator risk marker, usually `normal`.
- `args_schema`: editable argument definitions for UI and scenarios.

## Event

Minimal event:

```json
{
  "id": "input_1_pressed",
  "label": "Input 1 pressed",
  "capability": "input",
  "event": "input.pressed",
  "match": {
    "channel": 1
  }
}
```

Fields:

- `id`: stable event id inside this Quest Device.
- `label`: operator-facing label.
- `capability`: UI/grouping capability, for example `input`.
- `event`: event name received from the device.
- `match`: optional argument filter reserved by the contract.

At runtime, `WAIT_DEVICE_EVENT` currently waits on the event name and the
Quest Device `client_id`. Argument-level `match` is stored for the contract and
UI, and should be used by later matching logic when needed.

## System Audio

`system_audio` is a built-in Quest Device. It uses the same command/event model:

- commands: `audio.play`, `audio.stop`, `audio.pause`, `audio.resume`,
  `audio.set_volume`;
- events: `audio_finished`, `playback_failed`.

## Storage

Quest Devices are stored at:

```text
/sdcard/quest/quest_devices.json
```

Import rejects old command/event shapes that contain `kind`, `topic`, `payload`,
`action`, `button_enabled`, `dangerous`, `result_required`, `timeout_ms`,
`params_schema`, or `event_type`.
