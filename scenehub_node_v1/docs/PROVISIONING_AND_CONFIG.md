# Node Provisioning And Configuration

SceneHub nodes need a simple local setup path before they can connect to the
controller. Provisioning is part of the product architecture, not a debug-only
feature.

## Goals

- First setup must work without an existing Wi-Fi connection.
- A user can set node name, Wi-Fi credentials and SceneHub controller address.
- A physical reset pin can recover the node without reflashing.
- The same safe config actions are available from the local web UI.
- Runtime/hardware logic must keep running predictably and not depend on the
  web UI after setup.

## Provisioning Mode

The node enters provisioning mode when:

- no valid Wi-Fi credentials are stored;
- Wi-Fi cannot connect after a bounded retry window;
- reset pin requests Wi-Fi settings reset;
- local web UI requests reconfigure mode;
- factory reset clears all config.

Provisioning mode starts a local access point:

- SSID: `SceneNode-XXXX`;
- password: `setup-XXXXXX`, derived from the AP MAC;
- local address: implementation-defined, for example `192.168.4.1`;
- web UI route: `/`.

Provisioning availability policy:

- first-time `provisioning_required` boot stays open until setup completes;
- already provisioned normal boots auto-close the provisioning HTTP surface
  after 5 minutes;
- the local UI may disable that timer for the current boot only with
  `Keep setup open`;
- after reboot, the timeout is active again by default.

The AP should be stopped after successful STA connection unless explicitly
configured for service/debug mode.

## Local Web UI

The local node web UI should be intentionally small:

- node name;
- physical `node_id`;
- Wi-Fi SSID/password;
- SceneHub controller host/IP and MQTT port;
- MQTT connection client id;
- firmware version and build id;
- current connection status;
- current driver/capability status;
- reset Wi-Fi settings action;
- factory reset action;
- reboot action;
- export diagnostics action.

Do not implement a full SceneHub GM/editor UI inside the node.

## Reset Pin

One physical reset/config pin should support staged recovery:

- hold 5 seconds: reset Wi-Fi settings only and enter provisioning AP;
- hold 30 seconds: factory reset all node config and enter provisioning AP.

Recommended behavior:

- sample/debounce the pin in a low-cost task;
- provide LED/status feedback at threshold transitions;
- require release after threshold before executing destructive reset;
- ignore short accidental presses unless explicitly used for identify mode;
- keep hardware outputs in safe state during factory reset.

Factory reset clears:

- Wi-Fi credentials;
- controller/MQTT settings;
- node name if configured by user;
- custom v2 rule bundles;
- driver instance config;
- idempotency cache;
- diagnostics history if stored.

Factory reset should not erase:

- firmware image;
- immutable factory identity;
- calibration data unless explicitly included in the reset policy.

## Config Storage

Store config as versioned data:

```json
{
  "version": 1,
  "node_id": "relay_room_2",
  "node_name": "Relay room 2",
  "wifi": {
    "ssid": "QuestWiFi"
  },
  "controller": {
    "host": "192.168.43.203",
    "mqtt_port": 1883
  },
  "mqtt": {
    "client_id": "dcc-relay-room-2"
  }
}
```

Passwords/secrets should be stored through the target platform's secure storage
when available. Do not expose secrets in status, diagnostics or
`describe_interface`.

Config writes must be atomic:

- validate new config fully;
- write candidate config;
- commit/swap only after successful write;
- keep the previous valid config if write fails.

## Config API Commands

These commands may be exposed later through MQTT/admin paths:

- `node.config.get`
- `node.config.set`
- `node.config.reset_wifi`
- `node.config.factory_reset`
- `node.reboot`

They must require admin/operator confirmation in SceneHub UI. Dangerous config
commands must not be accepted from broadcast topics.

## Security Baseline

- Provisioning AP should not run indefinitely after successful setup.
- Provisioning AP should use WPA2-PSK rather than open auth.
- Local web UI must not expose stored Wi-Fi password.
- Factory reset action should require confirmation.
- If auth is added, keep it simple and local-only.
- MQTT credentials, when introduced, belong to config storage and must not be
  returned in diagnostic payloads.

## Runtime Boundary

Provisioning/config code is not a runtime-hot path:

- it may use bounded JSON/admin allocation;
- it must not run under hardware locks;
- it must not block command execution for long operations;
- it should request state changes through `node_control` or owner-task messages.

## Implementation Boundary

Do not keep provisioning as one large file. Keep these concerns split:

- Wi-Fi/AP/STA lifecycle;
- HTTP route registration;
- HTML UI payload;
- config JSON read/write handlers;
- reset/restart actions.

The first scaffold may temporarily keep them together, but it must be split
before adding more provisioning behavior.
