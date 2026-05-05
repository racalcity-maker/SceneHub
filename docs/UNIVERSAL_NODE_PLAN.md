# Universal Node Plan

This plan defines the future SceneHub universal node: an ESP32-based local IO
controller that owns physical peripherals and reports capabilities/events to
SceneHub through the normal device control contract.

The node must be a reliable IO controller first. It must not become a second
SceneHub. SceneHub owns quest scenarios; the node owns local hardware safety,
fast IO and peripheral timing.

## v0.1 Goal

Ship a useful MQTT-controlled room IO node with:

- 4 relay outputs.
- 4 MOSFET low-side PWM outputs.
- 4 discrete digital inputs.
- 4 universal GPIO lines.
- 1 WS2812/WS2815 DATA output.
- 1 UART header, physically exposed.
- 1 I2C header, physically exposed.
- CONFIG/BOOT button.
- RESET button.
- status LED.
- identify LED, or identify behavior on the status LED.

ESP-NOW, RS485 and other transports are deferred until the universal node is
stable.

## Controller

Supported MCU targets:

- ESP32-S3
- ESP32-C3

Required controls:

- BOOT/CONFIG button.
- RESET button.
- Status LED.
- Separate identify LED or shared status LED identify pattern.

Button behavior:

- Short CONFIG press: identify blink.
- Long CONFIG press: enter setup/provisioning mode.
- Very long CONFIG press: reset node config, if enabled.

LED behavior:

- Booting.
- Setup mode.
- MQTT connected.
- SceneHub linked.
- Fault.
- Identify blink.

## Required Firmware Contract

v0.1 firmware must include:

- Wi-Fi config.
- MQTT connect/reconnect.
- Device announce.
- Capabilities publish / `describe_interface`.
- Heartbeat.
- Status snapshot.
- Action receive.
- Action result `done` / `failed`.
- Diagnostics publish.
- OTA.
- Config save/load.
- Identify action.

MQTT callbacks must enqueue work. They must not directly toggle hardware.

## Relay Outputs

Four relay channels.

Primary use cases:

- Electromagnetic locks.
- Dry contacts.
- Lamps.
- Power for external modules.
- Smoke machine / strobe trigger.
- External PSU/device enable.

v0.1 actions:

- `relay.set(channel, state)`
- `relay.pulse(channel, duration_ms)`

Later actions:

- `relay.toggle(channel)`
- `relay.all_off()`

Requirements:

- Safe off state at boot.
- Per-channel GPIO pin config.
- Per-channel active low/high config.
- Per-channel label.
- `pulse` is mandatory because quest rooms often need short lock/trigger
  activation.

## MOSFET PWM Outputs

Four low-side MOSFET PWM channels.

Primary use cases:

- 12/24 V LED strips.
- UV strips.
- Decoration lighting.
- Careful solenoid drive.
- Small DC loads.

v0.1 actions:

- `mosfet.set(channel, value)`
- `mosfet.fade(channel, target, duration_ms)`
- `mosfet.pulse(channel, value, duration_ms)`

Later actions:

- `mosfet.effect(channel, mode)`
- `mosfet.all_off()`

Requirements:

- Safe off state at boot.
- Per-channel GPIO pin config.
- Per-channel PWM frequency config.
- Per-channel duty range.
- Per-channel label.

## Discrete Inputs

Four digital input channels.

Primary use cases:

- Buttons.
- Reed switches.
- Limit switches.
- Door sensors.
- Drawer sensors.
- Object placed sensors.
- Coin acceptors.
- Dry contacts.
- Output from another controller.

v0.1 action:

- `input.get_state(channel)`

v0.1 events:

- `input.changed(channel, state)`
- `input.pressed(channel)`
- `input.released(channel)`

Later events:

- `input.pulse_detected(channel)`

v0.1 per-input settings:

- `active_low` / `active_high`.
- `debounce_ms`.

Later per-input settings:

- pull-up / pull-down / external.
- event_on_change.
- event_on_press.
- event_on_release.

## Universal GPIO

Four configurable GPIO lines reserved for flexible installs.

v0.1 modes:

- `disabled`
- `digital_input`
- `digital_output`
- `pwm_output`
- `pulse_output`

v0.1 actions:

- `gpio.set(channel, state)`
- `gpio.pulse(channel, duration_ms)`
- `gpio.pwm_set(channel, value)`

v0.1 events:

- `gpio.changed(channel, state)`

Use cases:

- Small LED.
- Trigger for an external module.
- Extra button.
- External driver enable.
- Simple PWM for small lighting.
- Signal to another device.

Do not add UART/I2C/servo/encoder modes to universal GPIO in v0.1. The physical
pins can exist, but firmware should keep these modes simple.

## Addressable LED Data

One dedicated DATA output for WS2812/WS2815.

v0.1 actions:

- `led.fill(color)`
- `led.off()`
- `led.blink(color, count)`
- `led.pulse(color, duration_ms)`

Later actions:

- `led.set(color)`
- `led.pixel(index, color)`
- `led.effect(mode)`

Use cases:

- Status lighting.
- Magic effects.
- Direction indicators.
- Progress indication.

Complex effects such as fire, rainbow and chase are deferred.

## UART Header

Expose one UART header physically in v0.1.

Potential future uses:

- UART RFID reader.
- DFPlayer, if needed.
- Another MCU.
- RS485 module through MAX485.
- Scanner/service module.

Suggested header pins:

- GND.
- 3V3.
- TX.
- RX.
- 5V if available.
- Optional EN/DE pin for future MAX485.

Full UART peripheral support can be deferred unless a concrete v0.1 peripheral
is selected.

## I2C Header

Expose one I2C header physically in v0.1.

Potential future uses:

- PN532 I2C.
- PCF8574/MCP23017 expander.
- OLED display.
- Sensors.
- RTC.

Suggested header pins:

- GND.
- 3V3.
- SDA.
- SCL.

Full I2C peripheral support can be deferred unless a concrete v0.1 peripheral
is selected.

## Minimal v0.1 Actions

Node:

- `node.identify`
- `node.reboot`
- `node.get_status`

Relay:

- `relay.set`
- `relay.pulse`

MOSFET:

- `mosfet.set`
- `mosfet.fade`
- `mosfet.pulse`

Input:

- `input.get_state`

Universal GPIO:

- `gpio.set`
- `gpio.pulse`
- `gpio.pwm_set`

LED:

- `led.fill`
- `led.off`
- `led.blink`

## Minimal v0.1 Events

Node:

- `node.boot`
- `node.online`
- `node.offline`
- `node.error`

Input:

- `input.changed`
- `input.pressed`
- `input.released`

Universal GPIO:

- `gpio.changed`

Action results:

- `action.done`
- `action.failed`

## Identify Action

`node.identify` is mandatory.

SceneHub sends `node.identify`; the node should visibly identify itself by:

- blinking the status/identify LED;
- optionally blinking the addressable strip;
- optionally pulsing a configured safe relay channel if explicitly enabled.

This is important during installation because several identical nodes may be
online at the same time.

## Firmware Architecture

Suggested modules:

```text
node_firmware/
  app/
  drivers/
    relay_driver
    mosfet_pwm_driver
    digital_input_driver
    universal_gpio_driver
    led_strip_driver
    uart_peripheral_driver
    i2c_peripheral_driver
  control_contract/
  mqtt_transport/
  config_store/
  local_runtime/
  status_led/
  ota/
```

Runtime rules:

- Hardware commands run through a local command executor.
- Every output has a safe-state fallback.
- Local pulse/fade timers keep working even if MQTT disconnects.
- Node publishes heartbeat/status/diag/result.
- Node responds to `describe_interface`.
- Config changes are persisted.
- Watchdog or fatal restart returns outputs to safe state.

## SceneHub Contract

The node should speak the existing control contract:

- `heartbeat`
- `status`
- `diag`
- `result`
- `describe_interface`
- command topics
- event topics

SceneHub should see the node capabilities as:

- relay commands;
- MOSFET commands;
- input events;
- universal GPIO commands/events;
- LED strip commands;
- optional UART/I2C peripheral capabilities later.

The first implementation should prefer one physical node with grouped
commands/events because it is easier to configure and diagnose.

## Explicitly Out of v0.1

Do not include these in the first firmware release:

- ESP-NOW.
- RS485 network.
- CAN.
- Display UI.
- Encoder/menu UI.
- Local scenario engine.
- RFID integration.
- Audio.
- SD card.
- Complex LED effects.
- Node-hosted Web UI.
- BLE provisioning.

These can be added later. Adding them to v0.1 would turn the node into a second
SceneHub and slow down the useful IO controller release.

## ESP-NOW Deferred Plan

ESP-NOW should be added only after the base universal node is stable.

Possible topology:

```text
SceneHub
  -> MQTT
Universal Node
  -> ESP-NOW
Small wireless subdevices
```

Rules:

- MQTT remains the SceneHub-facing control channel.
- ESP-NOW is local to the node and its subdevices.
- Wi-Fi channel must be fixed/compatible with the node STA connection.
- Subdevices should not own quest scenarios.
- Subdevice capabilities are surfaced through the node's `describe_interface`.

ESP-NOW use cases:

- Wireless buttons.
- Small battery sensors.
- Remote readers.
- Small LED/relay satellite boards.

Deferred implementation tasks:

1. Add ESP-NOW peer registry.
2. Add pairing/provisioning flow.
3. Add encrypted peer support if needed.
4. Map ESP-NOW subdevice state into node status.
5. Map subdevice capabilities into `describe_interface`.
6. Forward subdevice events to SceneHub through MQTT.

## RS485 / MAX485 Deferred Plan

RS485 should also be deferred.

Future uses:

- Wired satellite IO modules.
- Long cable runs in noisy rooms.
- Reader/terminal modules.

Future tasks:

1. UART mode selection for MAX485.
2. DE/RE direction control pin.
3. Framing and addressing.
4. Timeouts/retries.
5. Mapping remote devices into `describe_interface`.

## Hardware Bring-Up Checklist

- Confirm boot strap pins are not assigned to unsafe outputs.
- Confirm relay and MOSFET channels boot off.
- Confirm input pull configuration is stable.
- Confirm PWM channels do not glitch on boot.
- Confirm LED strip output does not conflict with boot strapping.
- Confirm UART/I2C headers have usable power and ground references.
- Confirm CONFIG button cannot accidentally trigger destructive reset.
- Confirm watchdog returns outputs to safe state on firmware fault/reboot.

## Test Plan

Unit tests:

- Command parser and validation.
- Relay state machine.
- MOSFET PWM validation and safe state.
- Digital input debounce.
- Universal GPIO mode switching.
- LED strip command validation.
- Config persistence.
- `describe_interface` shape.

Hardware smoke tests:

- Relay channel 1..4 set/pulse.
- MOSFET channel 1..4 set/fade/pulse.
- Input channel 1..4 events.
- Universal GPIO input/output/PWM modes.
- WS2812/WS2815 clear/fill/blink.
- UART header electrical sanity.
- I2C header electrical sanity.
- MQTT reconnect and safe-state behavior.
