# Node Board Profiles

Node firmware uses one codebase across ESP-IDF targets. Board profiles define
safe defaults and GPIO constraints; runtime config decides which pins are
actually enabled.

## Current First Target

- Target: `esp32s3`
- Default reset/config GPIO: `0`
- Relay capacity: `8`
- MOSFET capacity: `8`
- Universal IO capacity: `8`
- LED strip capacity: `2`

All functional pins are disabled by default. Provisioning/config enables only
the pins used by the physical build.

## Factory Pin Profile

The build can enable `SCENEHUB_NODE_FACTORY_PIN_PROFILE` in menuconfig for boxed
hardware. In that mode, selected relay/IO/LED pins are compiled into the
firmware and applied on boot.

Driver factory profiles may use the same pattern. For example, a boxed build
may enable a compile-time `pn532` NFC reader stub with fixed I2C SDA/SCL pins
through the component's own menuconfig section, instead of exposing those pins
through provisioning first.

The factory profile exposes the full v1 capacity:

- 8 relay GPIOs;
- 8 MOSFET GPIOs;
- 8 universal IO GPIOs with role `disabled`, `input` or `output`;
- 2 LED strip GPIOs with pixel count.

Any factory GPIO left at `-1` is disabled. Disabled pins do not appear in the
generated `device_description`.

If `SCENEHUB_NODE_FACTORY_PIN_LOCKED` is enabled, the local provisioning UI
should hide or disable pin editing. This is intended for product boxes where
pinout is fixed by the PCB.

For dev boards and one-off builds, keep the factory pin profile disabled. The
node will boot with no functional pins enabled, and provisioning/runtime config
can enable only the pins needed for that physical build.

## Small Boards

Small boards such as ESP32-C3 mini use the same capacity model but enable fewer
pins. For example, a two-pin node may enable:

- one relay output;
- one universal input;
- one reset/config pin.

The fixed capacity is a firmware table size, not a requirement that the board
has that many usable GPIOs.

## Large Boards

Larger ESP32/ESP32-S3 boards may enable more pins up to the fixed limits. The
web config UI should only offer GPIOs allowed by the current board profile.

## Reserved Pins

Board profiles must hide pins that are unsafe for normal configuration:

- boot/strapping pins unless explicitly used as reset/config;
- native USB pins;
- flash/PSRAM pins;
- pins unavailable on the selected module/package.

The first ESP32-S3 profile is intentionally conservative and can be refined per
actual dev board/module after hardware testing.

Board profile implementation should also follow the no-god-file rule. If
target-specific defaults grow, split them by target or product board instead of
placing every board's pins in one source file.
