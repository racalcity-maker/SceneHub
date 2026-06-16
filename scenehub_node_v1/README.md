# SceneHub Node v1

This folder is the starting point for a physical SceneHub MQTT node firmware.
It is intentionally separate from the controller firmware in `components/`.

Protocol references:

- `../docs/device_control_contract_v1.md`
- `../docs/NODE_IMPLEMENTATION_CHECKLIST.md`
- `docs/ARCHITECTURE_POLICY.md`
- `docs/MEMORY_POLICY.md`
- `docs/LOCKING_POLICY.md`
- `docs/PROVISIONING_AND_CONFIG.md`
- `docs/BOARD_PROFILES.md`
- `docs/API_POLICY.md`
- `docs/NODE_V2_DESIGN.md`
- `docs/NODE_V2_RULE_SCHEMA_DRAFT.md`
- `docs/NODE_V2_TRANSITION_PLAN.md`

Reference emulator:

- `../tools/device_control_client/`

## Scope

SceneHub Node v1 is responsible for:

- connecting to SceneHub MQTT;
- publishing heartbeat/status/diag/result/event messages;
- subscribing to `cp/v1/dev/{node_id}/control/command`;
- implementing idempotent command handling by `request_id`;
- exposing `describe_interface`, `node.get_status` and `node.identify`;
- controlling local hardware safely.

SceneHub Node v1 must not depend on SceneHub controller internals such as
`gm_core`, `quest_device`, `device_control_ingest`, `command_executor`,
`mqtt_core` or Web UI DTOs.

Node v2 is tracked as a future extension in `docs/NODE_V2_DESIGN.md` and the
implementation sequence is tracked in `docs/NODE_V2_TRANSITION_PLAN.md`. The
v1 architecture should already leave room for v2 by keeping protocol, runtime,
hardware, storage and rule execution boundaries separate.

## Identity Rules

- `node_id` is the physical topic namespace id, for example `relay_room_2`.
- MQTT connection client id may differ, for example `dcc-relay-room-2`.
- All telemetry and command topics use `node_id`, not the MQTT connection id.
- Two physical nodes must not share the same `node_id`.

## Minimum Bring-Up

1. Configure Wi-Fi and MQTT host.
2. Configure stable `node_id`.
3. Connect using MQTT 3.1.1.
4. Subscribe to `cp/v1/dev/{node_id}/control/command`.
5. Publish heartbeat.
6. Publish status with capabilities.
7. Handle `describe_interface`.
8. Handle one safe hardware command.
9. Publish terminal command results.
10. Verify duplicate `request_id` does not repeat physical side effects.

Provisioning requirements are tracked in
`docs/PROVISIONING_AND_CONFIG.md`. The first firmware slice should include AP
first setup, a small local web UI and reset-pin recovery before field use.

Current provisioning/security baseline:

- setup AP uses WPA2-PSK with device-derived credentials
- the local provisioning UI auto-closes after 5 minutes on normal provisioned
  boots
- first-time `provisioning_required` boots stay open until setup is complete
- a boot-local `Keep setup open` override exists in the provisioning UI

## What Can Be Reused From SceneHub

Reuse as implementation guidance:

- topic names and JSON envelopes from `device_control_contract_v1.md`;
- behavior from `tools/device_control_client/client.py`;
- command/event descriptions from reference `device_description` examples;
- smoke checks from `NODE_IMPLEMENTATION_CHECKLIST.md`.

Do not copy into node firmware:

- controller MQTT broker code;
- command executor code;
- device ingest/read-model code;
- GM session/scenario runtime code;
- Web UI DTOs or handlers.

Those modules are controller-side and would couple the node to SceneHub
internals instead of the stable MQTT contract.

## Folder Layout

- `examples/` contains protocol payload examples for firmware tests.
- `docs/` contains node-local notes that are too implementation-specific for
  the global SceneHub docs.

Firmware source lives under target-specific folders:

- `esp_idf/`

Keep target-specific build systems isolated so this folder does not affect the
SceneHub controller build.

## ESP-IDF Bring-Up

The first firmware target is ESP32-S3.

```powershell
cd scenehub_node_v1\esp_idf
idf.py set-target esp32s3
idf.py -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.esp32s3 build
```

The v1 capacity is intentionally fixed and small:

- 8 relay outputs;
- 8 MOSFET outputs;
- 8 universal IO pins;
- 2 WS2812/LED strip outputs;
- 1 reset/config pin.

Small boards such as ESP32-C3 mini use the same firmware model with fewer
enabled pins in config. Large ESP32/ESP32-S3 boards can enable more pins up to
the fixed limits.

For boxed hardware, enable the factory pin profile in `menuconfig`:

```text
Component config
  -> SceneHub Node board profile
     -> Enable factory pin profile
```

When enabled, the firmware applies configured factory pins at boot. Any GPIO set
to `-1` stays disabled and does not appear in `device_description`. If
`Lock pin configuration in UI/API` is enabled, provisioning should treat pin
configuration as factory-owned and hide or disable pin editing. For dev boards,
leave the factory pin profile disabled and configure pins through provisioning.
