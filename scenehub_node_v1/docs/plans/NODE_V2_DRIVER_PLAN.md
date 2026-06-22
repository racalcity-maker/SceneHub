# SceneHub Node v2 Driver Plan

This document defines the near-term plan for Node v2 device drivers.

The goal is to support quest-room hardware integrations that are still small,
predictable and firmware-owned.

## Driver Principles

- drivers are compiled into firmware;
- JSON may configure driver instances, but it must not load arbitrary code;
- driver events are routed into the same event system as GPIO-based events;
- driver commands go through validated action routing;
- driver polling/callback work must stay bounded.

## Driver Family Pattern

New driver work should follow the same bounded family split that NFC now uses.

Preferred layering:

- `driver_registry`: bounded registered instance metadata only;
- `<family>_contract`: validates logical config and freezes public ids/events;
- `<family>_config_api`: narrow config/storage facade for provisioning/admin;
- `<family>_api`: narrow runtime/admin status/control facade;
- concrete firmware adapter hidden behind the family owner.

Examples:

- NFC:
  - `node_driver_nfc_contract`
  - `node_driver_nfc_config_api`
  - `node_driver_nfc_api`
  - concrete adapter: `pn532`
- planned audio:
  - `node_driver_audio_contract`
  - `node_driver_audio_config_api`
  - `node_driver_audio_api`
  - first concrete adapter: `dfplayer_mini`
- planned distance sensor:
  - `node_driver_sensor_contract`
  - `node_driver_sensor_config_api`
  - `node_driver_sensor_api`
  - first concrete adapter: `hcsr04`

Do not introduce one giant generic `node_driver_api` or `node_driver_runtime`
that knows every concrete device. Provisioning, MQTT, capability export and
admin flows should depend on narrow family facades, not on chip-specific owner
headers.

## First Driver Goal

The first real driver expansion target should be a minimal RFID/NFC reader.

Current preferred direction:

- `pn532`-class reader support;
- start with one reader use case;
- keep the first implementation intentionally narrow.

This is a better first target than trying to add a broad device framework.

## Minimal RFID/NFC Scope

The first reader implementation should support only what is actually needed for
room logic:

- detect tag/card presence;
- expose a stable tag/card identifier event;
- optionally expose card removed event;
- optionally support a simple identify command for diagnostics.

Not in the first reader scope:

- arbitrary APDU scripting;
- card emulation;
- peer-to-peer features;
- general NFC stack exposure;
- complex write workflows.

## Proposed Logical Driver Contract

Driver type:

- `nfc_reader`

First concrete implementation target:

- `pn532`

Example logical instance:

```json
{
  "id": "reader_1",
  "type": "nfc_reader",
  "driver": "pn532",
  "bus": "i2c_1",
  "config": {
    "poll_interval_ms": 100,
    "debounce_ms": 250
  }
}
```

## Proposed Events

First event set:

- `reader_1_card_seen`
- `reader_1_card_removed`

Suggested event payload fields:

- `source_id`
- `token_id`
- `uid`

The payload must stay bounded and compact.

`token_id` is the preferred engine-facing identity field. Raw `uid` is mainly
for observability and diagnostics.

## Proposed Commands

Initial command set:

- `nfc.identify(driver)`

Possible later commands:

- `nfc.enable(driver)`
- `nfc.disable(driver)`

Do not add write/programming commands in the first scope.

## Driver-State Interaction

The rule engine should not need string-heavy state to use RFID/NFC.

Preferred approach:

- driver keeps a configured allowlist of known card UIDs;
- each known card maps to a bounded `token_id` integer;
- driver event payload may also carry UID text for observability;
- rules in the first version branch on `token_id` or on emitted logical events;
- if later UID matching inside rules is required, add bounded helper
  conditions intentionally instead of turning runtime state into dynamic
  strings everywhere.

This keeps the core engine small while still leaving room for richer reader
features later.

Example allowlist shape:

```json
{
  "known_cards": [
    {
      "uid": "04AABBCCDD",
      "token_id": 1,
      "event": "reader_1_master_card"
    },
    {
      "uid": "11223344",
      "token_id": 2,
      "event": "reader_1_guest_card"
    }
  ]
}
```

This is the preferred way to distinguish cards:

- driver resolves raw UID;
- engine reacts to `token_id` or to the driver-emitted logical event.

Preferred rule-side pattern:

```json
{
  "trigger": {
    "kind": "local_event",
    "event": "reader_1_card_seen"
  },
  "conditions": {
    "kind": "event_field_equals",
    "field": "token_id",
    "value": 2
  }
}
```

This requires the engine/event model to expose bounded current-event fields to
conditions. The first required field for RFID/NFC is `token_id`.

## NFC Hold Via Timer Pattern

The driver should not grow special hold semantics such as:

- `reader_1_card_held_5s`
- `reader_1_card_held_10s`
- `reader_1_card_present_until_removed`

Those behaviors belong in the rule engine.

Preferred pattern:

1. `reader_1_card_seen` starts a named timer.
2. Bundle phase/state marks that a hold session is in progress.
3. `reader_1_card_removed` cancels the timer and resets phase/state.
4. A later `timer` rule performs the success path only if phase/state still
   matches the in-progress hold session.

Why this is the correct split:

- driver stays transport-focused and bounded;
- no extra driver-owned waiting stacks or ad-hoc hold state machines;
- hold behavior remains authorable in JSON;
- the same pattern works for `oneshot`, `repeat` and later `cooldown` use
  cases.

This also matches the memory and locking policy direction:

- the driver only reports compact reader events and health state;
- the rule engine uses compact phase/state/timer slots;
- no raw UID strings need to become long-lived runtime state.

### Current Authoring Rule

Today the schema exposes only `event_field_equals(token_id)`. It does not yet
have `token_id in [...]`, `!=`, or generic "any known card" sugar.

Until such sugar exists, use one of these bounded patterns:

- branch on explicit `token_id` values through `any`;
- use configured known-card logical events such as `reader_1_master_card`;
- duplicate a small rule per known token when that is clearer.

Do not push this gap down into the PN532 driver by inventing many special hold
events there.

### Example Patterns

Ready bundle examples live in:

- [nfc_hold_5s_any_known_card.json](examples/node_v2_bundles/nfc_hold_5s_any_known_card.json)
- [nfc_hold_10s_token_1.json](examples/node_v2_bundles/nfc_hold_10s_token_1.json)
- [nfc_hold_until_removed_reset.json](examples/node_v2_bundles/nfc_hold_until_removed_reset.json)

## Hardware/Bus Direction

The first implementation should keep bus support limited:

- prefer one transport first, likely I2C;
- add UART/SPI only if a real board profile requires it.

Do not design a large generic bus abstraction before the first reader works.

## Next Driver Slices

After NFC, the next useful product slices are:

1. audio player family, first concrete adapter `dfplayer_mini`;
2. sensor family, first concrete adapter `hcsr04`.

These should reuse the NFC family pattern instead of creating special one-off
driver surfaces.

## Planned Audio Family

Logical family:

- `audio_player`

First concrete implementation:

- `dfplayer_mini`

Why this slice is next:

- high quest-room value;
- simple bounded command surface;
- deterministic UART-based owner model;
- useful both from SceneHub and local bundles.

Preferred instance shape:

```json
{
  "id": "player_1",
  "type": "audio_player",
  "driver": "dfplayer_mini",
  "bus": "uart_1",
  "config": {
    "baud": 9600,
    "volume": 22
  }
}
```

Initial outward command set:

- `audio.play`
- `audio.stop`
- `audio.pause`
- `audio.resume`
- `audio.set_volume`

Optional later commands:

- `audio.play_folder_track`
- `audio.set_eq`
- `audio.reinit`

Initial outward events:

- `player_1_started`
- `player_1_finished`
- `player_1_error`

Suggested payload fields:

- `source_id`
- `track`
- `folder` (optional)
- `error_code` (optional)

Runtime and ownership rules:

- UART protocol state belongs to the audio runtime owner;
- command execution must go through a bounded queue, not direct UART writes
  from MQTT/provisioning/admin;
- optional BUSY-pin integration may improve state detection, but it must remain
  adapter-local;
- outward status should use bounded state:
  `health=ok|degraded|error|disabled`,
  `state=idle|playing|paused|offline|disabled`,
  plus compact `error_code`.

The public logical contract should remain audio-oriented. Do not expose raw
DFPlayer packet details or chip-specific transport behavior to SceneHub or to
rule authors.

## Planned Distance-Sensor Family

Logical family:

- `distance_sensor`

First concrete implementation:

- `hcsr04`

This slice should be event-oriented, not stream-oriented.

Preferred instance shape:

```json
{
  "id": "sensor_1",
  "type": "distance_sensor",
  "driver": "hcsr04",
  "bus": "gpio_pair",
  "config": {
    "poll_interval_ms": 100,
    "near_threshold_mm": 400,
    "far_threshold_mm": 700,
    "hysteresis_mm": 40,
    "stable_samples": 3,
    "timeout_ms": 30
  }
}
```

Preferred first outward events:

- `sensor_1_near_enter`
- `sensor_1_near_exit`
- `sensor_1_far_enter`
- `sensor_1_error`

Optional payload fields:

- `source_id`
- `distance_mm`
- `error_code`

Why this event model is preferred:

- quest logic usually cares about threshold/state transitions, not raw sample
  streams;
- MQTT and rule traffic stay bounded;
- the engine does not need high-rate scalar sampling semantics;
- filtering, hysteresis and debouncing stay inside the driver owner.

Runtime and ownership rules:

- raw sampling, filtering and hysteresis belong to the sensor runtime owner;
- SceneHub and rules should react to logical state transitions first;
- do not publish continuous distance streams as the main contract;
- a later admin/diag sample command is acceptable, but it must stay outside
  the normal scenario/event path.

## Driver Rollout Phases

### Phase A: Driver Contract Freeze

- freeze `nfc_reader` logical model;
- freeze event names and payload shape;
- freeze minimal command set.

### Phase B: Config and Capability Projection

- expose configured reader instance in `device_description`;
- include supported commands and events;
- include AI context guidance.

Implementation status, 2026-06-18:

- `node_driver_registry` now owns bounded registered driver-instance metadata.
- `node_driver_nfc_reader` already validates the first `pn532` / `i2c_1`
  config shape, known-card allowlist, `token_id` mapping and optional
  logical event names.
- Factory `menuconfig` settings can already register one bounded PN532 stub
  instance at boot.
- Rule schema and AI context already understand `drivers[]`, `local_event`
  reader flows and `event_field_equals(token_id)`.
- `device_description` now projects the configured factory reader instance and
  driver local-event templates when the factory config is valid.
- AI context now also lists the configured driver instance and known-card
  mappings when the factory config is valid.

### Phase C: Runtime Driver Module

- add bounded polling/interrupt handling;
- route `card_seen` and `card_removed` into `node_event_router`;
- keep callback path short and allocation-free.

Implementation status, 2026-06-18:

- Added `node_driver_nfc_reader_runtime` as a separate owner module.
- Runtime owner now has a bounded queue/task, debounce-aware stable/pending
  card state and a future-facing `submit_scan()` boundary.
- Runtime owner already routes debounced `reader_id_card_seen`,
  `reader_id_card_removed` and optional known-card logical events into
  `node_rule_engine_dispatch_local_event()`.
- The same debounced driver events are mirrored outward by the rule-engine
  event owner through the normal MQTT event path, so SceneHub can observe
  reader events directly without the driver runtime owning MQTT publication.
- Added a first separate `pn532` I2C adapter owner that initializes the chip,
  polls `InListPassiveTarget` and feeds raw UID observations into
  `submit_scan()`.
- NFC runtime active config, reload config, overlay storage scratch and driver
  task stacks now follow the memory policy direction: PSRAM-first owner storage
  where safe, with internal fallback, instead of holding every large owner
  object permanently in internal `.bss`.
- The PN532 adapter now uses bounded fast retries plus a quiet offline /
  recovery cooldown instead of endlessly hammering init when the reader is
  absent or briefly disconnected.
- Recovery is now layered: protocol/session retry first, then bus reinstall,
  then optional hardware reset when a PN532 reset pin is configured.
- Manual NFC recovery is exposed through the admin-owner path as
  `node.nfc.reinit`, not by letting provisioning or MQTT callbacks touch the
  adapter directly.
- The current adapter is still first-pass bring-up code and must be verified on
  real hardware before calling the runtime transport stable.
- Runtime status now also exposes bounded reader health snapshots for
  provisioning and MQTT status:
  `health=ok|degraded|error|disabled`,
  `state=online|recovery|offline|disabled`,
  plus a compact `error_code` string when the reader is enabled but not ready.

### Phase D: Rule Integration

- allow rule triggers from routed reader events;
- allow `emit_event` and normal rule flow to react to reader events;
- avoid special-case engine hacks for the reader.

Implementation status, 2026-06-20:

- Routed reader events already participate in the normal local-event rule path.
- The current closed product baseline is:
  - local provisioning can configure PN532 reader wiring and known cards;
  - runtime publishes bounded reader health/state/error;
  - SceneHub can observe reader degradation through node status;
  - `describe_interface` exports reader resources, known-card metadata and
    NFC-derived events.
- This is sufficient for the current Node v2 slice because the engine reacts
  to `token_id` / logical events, not to Hub-owned NFC card state.
- Centralized card CRUD in SceneHub GM is intentionally deferred. For now, the
  node-local provisioning surface remains the owner of known-card editing.

### Phase E: Optional Enhancements

- richer diagnostics;
- configurable poll/debounce settings;
- explicit enable/disable command;
- bounded UID matching helpers if truly needed.
- optional Hub-side NFC card admin workflow (`node.nfc.cards.get/set`) in
  `Devices -> Edit device`, if product flow later proves it is worth the extra
  transport/admin surface.

## Constraints

- a bad driver config must not brick active node behavior;
- driver runtime must have fixed capacity;
- driver code must not bypass engine/control boundaries;
- driver events must remain observable even when rules reject or ignore them.

## Non-Goals

- not a generic plugin marketplace for hardware;
- not a full NFC framework;
- not an arbitrary external-device scripting platform.
