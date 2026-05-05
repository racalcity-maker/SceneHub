# Hardware IO Plan

This plan tracks the next SceneHub hardware expansion: local relay, MOSFET and
universal IO outputs/inputs exposed as built-in Quest Devices.

The goal is to make the SceneHub box useful even without external MQTT clients:
room scenarios should be able to switch local outputs, pulse actuators and wait
for local input events through the same Quest Device / Room Scenario model.

## Product Direction

- Keep local hardware as System Devices, not a separate quest runtime path.
- Room Scenarios continue to use `DEVICE_COMMAND` and `WAIT_DEVICE_EVENT`.
- GM Panel manual buttons should work from the normal Quest Device capability
  model.
- Hardware state should be visible through orchestrator/device snapshots.
- RS485/MAX485 transport is a later optional transport layer, not part of the
  first local GPIO implementation.

## Built-In System Devices

### `system_relay`

Four relay channels.

Commands:

- `set`: set one relay channel on/off.
- `pulse`: turn one channel on for `duration_ms`, then return to safe off.
- `toggle`: invert one channel.
- `all_off`: force every relay channel to safe off.

Parameters:

- `channel`: 1..4.
- `state`: `on` or `off` for `set`.
- `duration_ms`: required for `pulse`.

Events:

- Optional `changed` event when a relay output changes.

### `system_mosfet`

Two MOSFET output channels.

Initial commands:

- `set`: set one MOSFET channel on/off.
- `pulse`: turn one channel on for `duration_ms`, then return to safe off.
- `toggle`: invert one channel.
- `all_off`: force every MOSFET channel to safe off.

Later commands:

- `pwm`: set duty cycle if the hardware design and timer ownership are settled.

Parameters:

- `channel`: 1..2.
- `state`: `on` or `off` for `set`.
- `duration_ms`: required for `pulse`.
- `duty`: later 0..100 or 0..1000 for PWM.

### `system_io`

Four universal IO channels.

Modes:

- `input`: emit events when state changes.
- `output`: accept the same simple output commands as relay channels.
- `disabled`: ignore the pin.

Output commands:

- `set`
- `pulse`
- `toggle`

Input events:

- `changed`
- `high`
- `low`

Input behavior:

- Debounce should be configurable.
- Initial state should be sampled after boot and exposed in status.
- Event payloads should include channel and state.

## Component Design

Add a new component:

```text
components/hardware_io/
  include/hardware_io.h
  hardware_io.c
  hardware_io_gpio.c
  hardware_io_runtime.c
  hardware_io_system_device.c
  CMakeLists.txt
```

Responsibilities:

- GPIO init and safe default output state.
- Active-high / active-low mapping.
- Pulse timers.
- Input debounce.
- Event posting into `event_bus`.
- Snapshot/status API for orchestrator and Web UI.
- Command execution API used by built-in Quest Device handlers.

Do not put GPIO handling inside `gm_core`, `quest_device`, or `web_ui`.

## Configuration

Add Kconfig settings under `SceneHub Configuration`.

Relay:

- `CONFIG_SCENEHUB_RELAY1_PIN`
- `CONFIG_SCENEHUB_RELAY1_ACTIVE_LOW`
- repeat for channels 2..4

MOSFET:

- `CONFIG_SCENEHUB_MOSFET1_PIN`
- `CONFIG_SCENEHUB_MOSFET1_ACTIVE_LOW`
- repeat for channel 2

Universal IO:

- `CONFIG_SCENEHUB_IO1_PIN`
- `CONFIG_SCENEHUB_IO1_MODE`
- `CONFIG_SCENEHUB_IO1_ACTIVE_LOW`
- repeat for channels 2..4

Shared:

- `CONFIG_SCENEHUB_HARDWARE_IO_ENABLED`
- `CONFIG_SCENEHUB_HARDWARE_IO_DEFAULT_PULSE_MS`
- `CONFIG_SCENEHUB_HARDWARE_IO_DEBOUNCE_MS`

Safe-state rule:

- Every output must start in off/safe state before any scenario or manual
  command can run.
- Invalid/unconfigured pins should disable the corresponding channel.

## Quest Device Integration

`quest_device` should expose built-in hardware devices the same way it exposes
`system_audio`.

Expected saved/generated capabilities:

- `system_relay` commands are available for scenario `DEVICE_COMMAND`.
- `system_mosfet` commands are available for scenario `DEVICE_COMMAND`.
- `system_io` output commands are available when a channel is configured as
  output.
- `system_io` input events are available for `WAIT_DEVICE_EVENT`,
  `WAIT_ANY_DEVICE_EVENT`, and `WAIT_ALL_DEVICE_EVENTS`.

Command result behavior:

- Unknown channel -> `ESP_ERR_NOT_FOUND` or validation error.
- Disabled channel -> `ESP_ERR_INVALID_STATE`.
- Invalid parameter -> `ESP_ERR_INVALID_ARG`.
- GPIO/timer failure -> propagated ESP-IDF error.

## Orchestrator and Web UI

Minimum first pass:

- Hardware System Devices appear in device snapshots.
- Manual buttons are available through normal command capabilities.
- Current hardware channel states are exposed in orchestrator/device detail.
- Timeline/audit records command execution and failures.

Later UI polish:

- Hardware panel with compact relay/MOSFET/IO state indicators.
- Per-channel labels.
- Per-channel manual controls.
- Input event history.

## Tests

Use a backend adapter so tests do not require real GPIO hardware.

Unit tests:

- Channel range validation.
- Active-low mapping.
- Safe default state.
- `set`, `toggle`, `pulse`, `all_off`.
- Pulse expiry returns to safe state.
- Input debounce.
- Input event generation.
- Disabled/unconfigured channel behavior.

Integration tests:

- Built-in Quest Device export includes hardware capabilities.
- Room Scenario can call `system_relay set`.
- Room Scenario can call `system_mosfet pulse`.
- Room Scenario can wait for `system_io high`.
- Orchestrator snapshot includes hardware state.
- GM API command path can execute a hardware command.

## Implementation Order

1. Add `hardware_io` component skeleton and Kconfig.
2. Implement GPIO adapter and mockable backend.
3. Implement relay and MOSFET output state machine.
4. Add universal IO input/output mode support.
5. Register built-in System Devices and capabilities.
6. Add scenario and GM API integration tests.
7. Add orchestrator snapshot/status visibility.
8. Add Web UI polish only after backend behavior is stable.

## Deferred RS485 / MAX485 Transport

RS485 should be treated as a separate transport project.

Possible component:

```text
components/rs485_transport/
```

Responsibilities:

- UART configuration.
- MAX485 DE/RE direction control pin.
- Frame encoding/decoding.
- Timeouts/retries.
- Bus device discovery.
- Mapping remote devices into the same Quest Device capability model.

Do this only after local GPIO System Devices are stable. RS485 may not be needed
for the first product version.
