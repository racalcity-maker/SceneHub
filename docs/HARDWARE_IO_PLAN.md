# Hardware IO Plan

This plan tracks the next SceneHub hardware expansion: local relay, MOSFET and
universal IO outputs/inputs exposed as built-in Quest Devices.

## Implementation Status

- First implementation slice added: `components/hardware_io`.
- `system_relay` is exposed as a built-in Quest Device with `set`, `pulse`,
  and `toggle` commands.
- Relay GPIOs are configured through menuconfig and default to `-1`
  (disabled). The current project defaults are GPIO 15-18 for the first board
  bring-up.
- Relay module supports active-high and active-low control. Active-low is the
  default for the first SSR module.
- Relay init drives the configured safe/off level before and after GPIO output
  configuration, so boot does not briefly energize active-low relay boards.
- Manual relay `set` and `toggle` cancel any active pulse timer before changing
  state, so an older pulse cannot later override the manual command.
- `system_mosfet` is exposed as a built-in Quest Device with `set`, `fade`,
  `pulse`, and `all_off` commands.
- MOSFET status now exposes `pulse_active` and `fade_active` for GM diagnostics.
- Command execution rejects overlong relay pulse, MOSFET pulse and MOSFET fade
  durations using Kconfig limits. Defaults are 60000 ms.
- `system_input` is exposed as a built-in Quest Device with 4 debounced digital
  inputs. Events are channel-specific so scenario waits can match the current
  `device_id + event_id` runtime model.
- `system_gpio` is exposed as a built-in Quest Device with 4 universal channels.
  Each channel is configured as disabled, digital input, or digital output.
- `command_executor` routes built-in `system_relay`, `system_mosfet`,
  `system_input`, and `system_gpio`
  commands to local GPIO/PWM. External Quest Devices with commands such as
  `relay.pulse` or `mosfet.fade` still use the MQTT backend.
- GM Panel has a first `Hardware IO` admin screen for relay/MOSFET channel
  testing and reads live channel status through `/api/hardware-io/status`.
- Hardware IO init is optional. A bad GPIO/configuration marks
  `hardware_io` service init as failed, but SceneHub keeps Web UI and runtime
  online; local hardware commands return `hardware_io_unavailable`.
- `/api/hardware-io/status` exposes explicit service availability/fault
  metadata. If `hardware_io` is unavailable, the GM Panel shows the reason and
  disables Hardware IO controls instead of treating empty channel arrays as a
  healthy state.
- `Stop game` and `Reset game` force relay, MOSFET and GPIO output channels to
  safe/off after the GM session lock is released. Safe-off failures are recorded
  in `service_status`. `END_GAME` does not force safe-off.
- Hardware IO service faults are promoted into orchestrator/GM system issues,
  so the operator sees a visible `hardware_io fault` instead of only a disabled
  Hardware IO screen.

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
- `toggle`: invert one channel. Manual-only; scenarios should use idempotent
  `set` or bounded `pulse`.
- No public `relay.all_off` command yet. `Stop game` and `Reset game` force
  every relay channel to safe off through the internal hardware safe-off path.

Parameters:

- `channel`: 1..4.
- `on`: boolean for `set`.
- `duration_ms`: required for `pulse`.

Events:

- Optional `changed` event when a relay output changes.

### `system_mosfet`

Four MOSFET low-side PWM output channels.

Commands:

- `set`: set one MOSFET PWM value immediately.
- `fade`: linearly fade one channel to `target` over `duration_ms`.
- `pulse`: set one channel to `value` for `duration_ms`, then restore the
  previous value.
- `all_off`: force every MOSFET channel to safe off.

Later commands:

- `effect`: named local effects after the base state machine is stable.

Parameters:

- `channel`: 1..4.
- `value`: 0..255 for `set` and `pulse`.
- `target`: 0..255 for `fade`.
- `duration_ms`: required for `fade` and `pulse`.

Status fields:

- `value`: current PWM value.
- `pulse_active`: pulse timer is active.
- `fade_active`: fade timer is active.
- `pwm_freq_hz`: configured LEDC PWM frequency.

### `system_input`

Four debounced digital inputs.

Commands:

- `get_state`: manual diagnostics for one input channel.

Events:

- `ch1_changed` .. `ch4_changed`
- `ch1_high` .. `ch4_high`
- `ch1_low` .. `ch4_low`
- `ch1_pressed` .. `ch4_pressed`
- `ch1_released` .. `ch4_released`

Input behavior:

- `high` and `low` are physical pin level events.
- `pressed` and `released` use the configured active-low/active-high logical
  mapping.
- Debounce is polling-based, not ISR-based, to avoid doing timer work inside
  GPIO interrupts.

### `system_gpio`

Four universal IO channels.

Modes:

- `input`: emit events when state changes.
- `output`: accept simple digital output commands.
- `disabled`: ignore the pin.

Output commands:

- `set`
- `pulse`
- `toggle` manual-only.
- `get_state` manual diagnostics.

Input events:

- `ch1_changed` .. `ch4_changed`
- `ch1_high` .. `ch4_high`
- `ch1_low` .. `ch4_low`
- `ch1_active` .. `ch4_active`
- `ch1_inactive` .. `ch4_inactive`

Input behavior:

- Debounce should be configurable.
- Initial state should be sampled after boot and exposed in status.
- Event IDs include the channel because the current scenario wait matcher uses
  `device_id + event_id`.

## Component Design

Add a new component:

```text
components/hardware_io/
  include/hardware_io.h
  hardware_io.c
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

- `CONFIG_SCENEHUB_RELAY1_GPIO`
- repeat for channels 2..4

- `CONFIG_SCENEHUB_RELAY_ACTIVE_LOW`

MOSFET:

- `CONFIG_SCENEHUB_MOSFET1_GPIO`
- repeat for channels 2..4
- `CONFIG_SCENEHUB_MOSFET_PWM_FREQ_HZ`

System inputs:

- `CONFIG_SCENEHUB_INPUT1_GPIO`
- repeat for channels 2..4
- `CONFIG_SCENEHUB_INPUT_ACTIVE_LOW`
- `CONFIG_SCENEHUB_INPUT_PULLUP`
- `CONFIG_SCENEHUB_INPUT_PULLDOWN`
- `CONFIG_SCENEHUB_INPUT_DEBOUNCE_MS`

Universal GPIO:

- `CONFIG_SCENEHUB_GPIO1_GPIO`
- `CONFIG_SCENEHUB_GPIO1_MODE`
- repeat for channels 2..4
- `CONFIG_SCENEHUB_GPIO_ACTIVE_LOW`
- `CONFIG_SCENEHUB_GPIO_PULLUP`
- `CONFIG_SCENEHUB_GPIO_PULLDOWN`
- `CONFIG_SCENEHUB_GPIO_DEBOUNCE_MS`
- `CONFIG_SCENEHUB_GPIO_MAX_PULSE_MS`

Safe-state rule:

- Every output must start in off/safe state before any scenario or manual
  command can run.
- Invalid/unconfigured pins should disable the corresponding channel.
- `Stop game` and `Reset game` force built-in relay/MOSFET/GPIO output
  channels off.
- Safe-off runs after the GM session lock has been released, so a slow or
  failing hardware driver does not hold the room runtime lock.
- Safe-off failures are surfaced through `service_status` and the Hardware IO
  status contract.
- `END_GAME` does not force safe-off. Finale cleanup must be explicit scenario
  behavior, matching the system-audio policy.

## Quest Device Integration

`quest_device` should expose built-in hardware devices the same way it exposes
`system_audio`.

Expected saved/generated capabilities:

- `system_relay` commands are available for scenario `DEVICE_COMMAND`.
- `system_mosfet` commands are available for scenario `DEVICE_COMMAND`.
- `system_input` events are available for scenario waits.
- `system_gpio` output commands are available when a channel is configured as
  output.
- `system_gpio` input events are available for `WAIT_DEVICE_EVENT`,
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
- Hardware IO admin screen can test relay/MOSFET/input/GPIO channels and call
  `mosfet.all_off`.
- Hardware IO status endpoint includes relay, MOSFET, input and universal GPIO
  channel status.

Later UI polish:

- Status snapshots should expose `last_change_ms` and `last_error` for better
  GM diagnostics.
- Per-channel labels.
- Input event history.

## Deferred Output Effects

The first hardware IO version only provides one-shot output commands:
`set`, `pulse`, `fade` and `all_off`. Repeating blink/breathe behavior should
be added as explicit bounded effects, not by chaining scenario steps forever.

Relay effects:

- `relay.blink(channel, on_ms, off_ms, count)`

MOSFET effects:

- `mosfet.blink(channel, value, on_ms, off_ms, count)`
- `mosfet.breathe(channel, min, max, fade_ms, hold_ms, count)`
- Later generic form: `mosfet.effect(channel, mode, args)`

Rules:

- Scenario effects should be bounded. `count = 0` should either be rejected for
  scenarios or allowed only for manual/debug control.
- `relay.set`, `relay.pulse`, `relay.toggle`, `Stop game` and `Reset game`
  must cancel active relay blink/effect on the affected channel.
- `mosfet.set`, `mosfet.fade`, `mosfet.pulse`, `mosfet.all_off`, `Stop game`
  and `Reset game` must cancel active MOSFET blink/breathe/effect on the
  affected channel.
- Status should expose effect state, for example `effect_active`, `effect_mode`,
  `effect_remaining_count` and `last_change_ms`.
- UI should offer compact controls for common frequencies rather than forcing
  users to hand-enter every timing field.

## Tests

Use a backend adapter so tests do not require real GPIO hardware.

Unit tests:

- Channel range validation.
- Active-low mapping.
- Safe default state.
- `set`, `toggle`, `pulse`, plus `Stop game` / `Reset game` safe-off.
- Pulse expiry returns to safe state.
- Input debounce.
- Input event generation.
- Disabled/unconfigured channel behavior.

Integration tests:

- Built-in Quest Device export includes hardware capabilities.
- Room Scenario can call `system_relay set`.
- Room Scenario can call `system_mosfet pulse`.
- Room Scenario can wait for `system_input chN_high/chN_low/chN_pressed`.
- Room Scenario can wait for `system_gpio chN_high/chN_low/chN_active`.
- Orchestrator snapshot includes hardware state.
- GM API command path can execute a hardware command.

## Remaining Implementation Order

Completed:

- `hardware_io` component skeleton and Kconfig.
- Relay and MOSFET output state machine.
- Built-in `system_relay` and `system_mosfet` Quest Devices.
- Built-in `system_input` and `system_gpio` Quest Devices.
- Command executor routing for local relay/MOSFET/input/GPIO.
- Hardware IO status endpoint and GM Panel relay/MOSFET test screen.
- Polling debounce for system input and universal GPIO input channels.

Next:

1. Add focused hardware IO state-machine tests with a mockable backend.
2. Add scenario/GM API integration tests for local hardware commands/events.
3. Add orchestrator snapshot/status visibility for hardware IO details.
4. Extend GM Panel Hardware IO view to show input/GPIO channels.
5. Add relay/MOSFET/GPIO effect commands after base outputs are stable.

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
