# Node Config Reference

This is the release-facing configuration reference for `scenehub_node_v1`.

## Persisted Node Config

Stored through `node_config`.

| Field | Default | Notes |
| --- | --- | --- |
| `node_id` | `scenehub_node_s3` | Physical MQTT topic namespace. Must be unique. |
| `node_name` | `SceneHub Node S3` | Human-readable label. |
| `wifi_ssid` | empty | Missing value triggers provisioning when Wi-Fi is required. |
| `wifi_password` | empty | Do not expose in logs or screenshots. |
| `controller_host` | empty | Hub/MQTT host. Optional in pure standalone mode. |
| `mqtt_port` | `1883` | Controller/broker port. |
| `mqtt_client_id` | `dcc-scenehub-node-s3` | MQTT connection id; may differ from `node_id`. |
| `reset_gpio` | `-1` | Disabled unless configured or board profile supplies default. |
| `pin_config_locked` | board/profile dependent | Locks pin editing when factory profile owns pins. |
| `operation_mode` | `scenehub` | `scenehub`, `standalone`, alpha `fallback`. |
| `standalone_mqtt_enabled` | `false` | Allows MQTT in standalone mode when enabled. |
| `fallback_timeout_ms` | `0` | Fallback entry timeout; `0` disables timeout entry. |
| `fallback_return_delay_ms` | `3000` | Delay before returning after stable MQTT. |
| `fallback_return_policy` | `auto_on_stable_mqtt` | Or `manual_stay_active`. |

## Hardware Arrays

| Area | Capacity | Default |
| --- | --- | --- |
| Relay outputs | 8 | disabled GPIO `-1`, active settings per channel. |
| MOSFET outputs | 8 | disabled GPIO `-1`, PWM/effect settings per channel. |
| Universal IO | 8 | disabled role, GPIO `-1`. |
| LED strips | 2 | GPIO `-1`, 30 pixels, WS2812, GRB. |

## PN532 Factory Config

Compile-time PN532 options are under `SceneHub Node NFC reader` Kconfig.

| Option | Notes |
| --- | --- |
| `SCENEHUB_NODE_DRIVER_PN532_ENABLED` | Enables PN532 runtime and factory config export. |
| `SCENEHUB_NODE_DRIVER_PN532_I2C_SDA_GPIO` | SDA GPIO. |
| `SCENEHUB_NODE_DRIVER_PN532_I2C_SCL_GPIO` | SCL GPIO. |
| `SCENEHUB_NODE_DRIVER_PN532_RESET_GPIO` | Optional reset GPIO, `-1` when unused. |
| `SCENEHUB_NODE_DRIVER_PN532_I2C_ADDRESS` | 7-bit I2C address, current working default `0x24`. |
| `SCENEHUB_NODE_DRIVER_PN532_POLL_INTERVAL_MS` | Poll interval. Driver enforces a minimum poll interval. |
| `SCENEHUB_NODE_DRIVER_PN532_DEBOUNCE_MS` | Presence debounce. |

## Board Profile

Factory pin profile options live under `SceneHub Node board profile`.

Release baseline:

- supported board: ESP32-S3 N16R8;
- verified board count: 4 identical ESP32-S3 N16R8 units;
- configured pins are treated as the release pin profile;
- pin changes are allowed, but each changed pin profile must be recorded and
  retested before being treated as release-supported.

Important release choices:

- whether factory pin profile is enabled;
- whether pin configuration is locked;
- relay/MOSFET/IO/LED GPIO mapping;
- active-low settings;
- reset/config GPIO.

Record the actual board profile in every release sign-off report.

## Known Cards

Known cards must contain only real UIDs. Empty placeholder cards must not be
exported into runtime config or release examples.

For PN532 presence scenarios:

- `card_seen` starts token-specific behavior;
- repeated no-card scans are required so debounce can commit removal;
- `card_removed` turns off presence effects and resets local wait state.

## Release Rules

- Record `node_id`, `mqtt_client_id`, board profile and PN532 pins.
- Do not ship duplicate `node_id` values.
- Do not expose Wi-Fi password.
- Re-test provisioning after changing config schema or factory profile.
