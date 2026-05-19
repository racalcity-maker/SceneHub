# SceneHub Node Architecture Policy

This policy defines the intended module boundaries for SceneHub Node firmware.
The node should keep the good architecture rules from SceneHub while staying a
small MQTT device, not a controller clone.

## Direction

Target dependency direction:

```text
node_app / config_api / provisioning_ui
  -> node_control / node_read_model
  -> node_runtime / rule_engine / capability_registry / driver_registry
  -> command_handlers / hardware_io / mqtt_client / storage
```

Practical rules:

- Commands go down through `node_control`.
- MQTT/device events go up through a small event queue.
- `node_read_model` only projects local state for status/diagnostics.
- Hardware modules never call MQTT directly.
- MQTT modules never own hardware decisions.
- JSON/rule execution never bypasses command validation.
- Do not create god files. Split files by ownership before a module mixes
  lifecycle, protocol parsing, rendering, storage and hardware behavior.

## V1 Module Shape

Minimum v1 modules:

- `node_app`: boot, configuration loading and service lifecycle.
- `provisioning_ui`: AP first setup and small local web configuration UI.
- `mqtt_transport`: MQTT connect/reconnect, subscribe, publish.
- `protocol`: topic builder, command/result/status JSON envelopes.
- `node_control`: command dispatch entry point and idempotency.
- `capability_registry`: static device_description commands/events.
- `hardware_io`: relay/MOSFET/input/audio-facing hardware adapters.
- `node_read_model`: heartbeat/status/diag snapshots.
- `storage`: persistent config only.

Provisioning/config code is an admin path. It may allocate in bounded ways, but
it must not block runtime command execution or touch hardware directly.

V1 must stay simple:

- no scenario engine;
- no broad dynamic scripting;
- no UI DTOs;
- no controller-side runtime/session concepts.

## V2 Module Shape

Node v2 may add runtime-configurable behavior, but it must not turn hardware
drivers into script-owned global state.

Additional v2 modules:

- `rule_engine`: validates and runs persisted rule graphs.
- `rule_store`: stores versioned custom JSON logic.
- `rule_api`: accepts admin-provided rule bundles through MQTT commands.
- `action_router`: maps rule actions to validated local commands.
- `event_router`: maps hardware/input/timer events into rule triggers.
- `driver_registry`: exposes firmware-built device drivers as named
  capabilities.
- `sandbox`: enforces limits, budgets and allowed capabilities.

V2 rule execution must call the same local command handlers as MQTT commands.
No separate "fast path" may touch hardware directly.

Device drivers remain firmware modules. JSON may enable configured driver
instances and bind them to logical names, but it must not load arbitrary driver
code.

## Ownership

- MQTT owns transport connection state only.
- `node_control` owns command lifecycle and idempotency cache.
- `rule_engine` owns active rule runtime state.
- Hardware drivers own hardware registers/peripheral state.
- Driver instances own their device protocol state.
- Storage owns durable config and rule bundles.
- Read model owns exported status/diag snapshots.

If two modules need the same information, prefer a copied DTO or event over a
shared mutable struct.

## Events

Events flow upward:

```text
hardware/input/timer/mqtt result
  -> node event queue
  -> rule_engine / read_model / MQTT event publisher
```

Handlers must be adapter-only:

- validate event;
- copy bounded data into a queue item;
- return quickly.

Heavy work runs in the owner task.

## JSON Boundaries

JSON is a boundary format, not the internal runtime model.

- Parse MQTT/admin JSON into bounded structs.
- Validate before storing or executing.
- Store rule bundles as versioned JSON, but compile them into fixed runtime
  structs before execution.
- Never keep cJSON object pointers in hot runtime state.
- Reject unknown rule schema versions unless explicit migration exists.

## File Ownership Rule

A source file should have one primary reason to change. It is time to split a
file when it mixes two or more of these responsibilities:

- service lifecycle/init;
- HTTP route registration;
- HTML/static UI rendering;
- request body parsing;
- config validation/storage;
- MQTT transport;
- command dispatch;
- hardware IO;
- rule execution;
- diagnostics/read-model projection.

Preferred split examples:

- `node_provisioning_lifecycle.c`: Wi-Fi/AP/STA and web server lifecycle.
- `node_provisioning_http.c`: HTTP route registration and route handlers.
- `node_provisioning_ui.c`: static HTML/UI payloads.
- `node_provisioning_config_api.c`: config GET/POST parsing and response JSON.
- `node_capability_writer.c`: `device_description` writer only.

If a file grows because a feature is being staged, split it before adding the
next feature slice. Temporary consolidation is acceptable only for a very small
first scaffold and should be paid down immediately.

## Compatibility

Node v2 must remain compatible with the v1 device-control contract:

- same heartbeat/status/result/event topics;
- same command envelope;
- same result envelope;
- same `describe_interface` discovery;
- same idempotency rule by `request_id`.

Dynamic v2 logic is an extension behind the same transport contract, not a new
control plane.
