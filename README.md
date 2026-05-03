# Quest Orchestrator Firmware

Firmware for the `Quest Orchestrator`: an ESP32-S3 based quest control hub with:

- local MQTT broker module
- Web UI with authentication
- Quest Device capability model
- schema-driven Room Scenario runtime
- Game Mode/profile selection
- GM room/session control model
- audio playback and background/effect mixing
- OTA firmware update

The firmware is designed for stand-alone escape room and interactive exhibit setups where the controller acts as both the game control plane and the local integration point for field devices. MQTT broker functionality is one module inside the product, not the product identity.

## Main Capabilities

- Embedded MQTT broker for local devices over Wi-Fi
- Web UI for status, settings, audio, firmware update and GM entry
- Role-aware GM panel for operator workflow and admin setup
- Quest Devices capability model with imported or manually entered commands/events
- Built-in System Devices such as `system_audio`, exposed through the same command/event model
- Schema-driven Room Scenario runtime with validation, device commands, device-event waits, wait-time and operator gates
- Game Modes that select room scenario, duration and future content packs
- Room-level and device-level action facades with in-memory audit trail
- Device control contract ingest (`heartbeat/status/diag/result`)
- Observed control device visibility with quest-device binding
- Admin `MQTT Interface` setup with on-demand `describe_interface` discovery
- OTA update flow with rollback-aware status
- Local status LED and fault monitoring
- Registered offline quest devices treated as critical room/system faults
- System audio can be used from room scenarios through `DEVICE_COMMAND system_audio play/stop/pause/resume/set_volume`; audio has separate background/effect channels with one I2S mixer owner.

## Current Architecture

The codebase is now split into clear modules:

- `quest_common` - shared current-model limits and safe string helpers
- `quest_device` - capability-device store for physical quest devices and built-in system devices
- `room_scenario` - room-level scenario definitions, schema-backed JSON, validation and runtime storage
- `game_profile` - game mode/profile storage, validation and persistence
- `gm_core` - room session/timer/hint state
- `room_catalog` - canonical room list from active config
- `gm_control` - canonical room action model and execution facade
- `orchestrator_core` - orchestrator read model, device control facade, audit and event timeline
- `device_control_ingest` - parses `cp/v1/dev/{id}/{heartbeat|status|diag|result}` into orchestrator-side device control state
- `audio_player` - playback service
- `sd_storage` - SD card ownership
- `web_ui` - HTTP/API layer, including stable `orchestrator_api_view` JSON mapping
- `mqtt_core` - MQTT broker
- `event_bus` - internal typed event transport with PSRAM-backed message pool and job queue

Detailed architecture notes are in:

- `docs/ARCHITECTURE.md`
- `docs/gm_panel_ui_plan.md`
- `docs/gm_api_contract.md`
- `docs/QUEST_DEVICE_SETUP_RUS.md`
- `docs/ROOM_SCENARIO_SETUP_RUS.md`

## Startup Policy

The firmware uses three practical boot classes:

- `BOOT_FATAL` - platform infrastructure that must succeed before normal boot continues
- `BOOT_DEFERRED_FATAL` - product services that may start in a later bootstrap stage but are still mandatory for a usable device
- `BOOT_OPTIONAL` - services that may fail without blocking the rest of the product

Current mapping:

- `BOOT_FATAL`
  - `nvs_flash`
  - `ota_manager`
  - `config_store`
  - `service_status`
  - `event_bus`
  - `device_control_ingest`
  - `error_monitor`
- `BOOT_DEFERRED_FATAL`
  - `network`
  - `mqtt_core`
  - `web_ui`
  - `room_catalog`
  - `quest_device`
  - `room_scenario`
  - `game_profile`
  - `gm_core`
- `BOOT_OPTIONAL`
  - `audio_player`

Current implementation note:

- the policy is explicit and documented
- deferred-fatal startup still runs through a dedicated bootstrap task in `main/main.c`
- this is a working startup scheme, but not yet a fully unified startup orchestrator

## Requirements

- ESP32-S3
- PSRAM enabled
- SD card connected to configured SPI pins
- I2S audio output connected if audio playback is used
- ESP-IDF 5.3.x

## Build

```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

## Tests

The project has automated tests at multiple levels:

- `tests/mqtt_core` - local broker unit/regression tests
- `tests/stress_chaos_tests` - external protocol/stress scripts against a running Quest Orchestrator

Key documented coverage includes:

- Quest Device validation/import/export
- Room Scenario validation/runtime/storage
- Game Mode validation/storage
- GM room session game start/stop/reset
- MQTT broker semantics:
  - retained messages
  - wildcard routing
  - max subscriptions
  - soak/stability checks

Detailed test notes and run commands are in:

- `docs/TESTING.md`

## Important Runtime Storage

### NVS

Stored in `config_store`:

- Wi-Fi config
- MQTT config and credentials
- Web credentials
- time/NTP config
- logging flags

### SD Card

Stored through `sd_storage` and used by higher-level services:

- room catalog in `/sdcard/quest/rooms.json`
- quest devices in `/sdcard/quest/quest_devices.json`
- room scenarios in `/sdcard/quest/room_scenarios.json`
- game profiles in `/sdcard/quest/game_profiles.json`
- audio files

## Web UI

The Web UI is served directly by the firmware.

Main functional areas:

- Status
- GM (operator/admin quest panel)
- Audio
- Settings
- Update

The `/gm` panel is the primary quest console:

- operators can select profiles, start/stop/reset games, watch timers, scenario progress, waits, issues, devices, audit and timeline
- admins get additional sections for Profiles, Scenarios, Device Setup and Storage
- device setup and scenario/profile editing are admin-only
- the main admin web surface should stay a lightweight system entry point and utility area

Authentication:

- cookie-based session auth
- admin account
- operator/user account with GM-first routing
- credential reset supported by hardware reset flow

GM/orchestrator API highlights:

- `GET /api/gm/state` returns GM dashboard and room state
- `GET /api/gm/room/profiles` lists selectable profiles for a room
- `POST /api/gm/room/profile/select` selects a profile for a room session
- `POST /api/gm/room/game/start` starts timer and room scenario from the selected profile
- `POST /api/gm/room/game/stop` stops the current game
- `POST /api/gm/room/game/reset` resets the current game/session
- `GET /api/orchestrator/control/devices` returns observed control-contract devices
- `POST /api/gm/device/describe-interface` requests a physical client quest interface
- `GET /api/orchestrator/audit/recent` returns recent action audit entries

## OTA Update

OTA is supported through the Web UI.

Flow:

1. open the firmware update page
2. upload the built firmware binary
3. device writes image into OTA partition
4. device reports `phase=reboot_required`
5. operator explicitly requests reboot
6. device boots into new image
7. healthy boot confirms the update

The OTA status view shows:

- running partition
- boot partition
- current app version
- transfer progress
- lifecycle phase
- rollback-related state

Main OTA phases exposed by the API:

- `idle`
- `uploading`
- `reboot_required`
- `rebooting`
- `verify_wait_ready`
- `verify_pending`

## Quest Runtime Model

The current product model is intentionally simple:

- Quest Devices describe capabilities: commands, events, physical client id and operator-visible name.
- Devices do not own quest flow and do not require room assignment.
- Physical clients report health through `cp/v1/dev/{client_id}/{heartbeat|status|diag|result}`.
- Admin Device Setup can import capabilities through on-demand `describe_interface` or enter commands/events manually.
- Room Scenarios are the only place where quest flow is assembled.
- Game Modes select a room scenario and duration/settings.
- Built-in orchestrator services, starting with audio, are exposed as System Devices.
- Gameplay runs through Quest Devices, Room Scenarios, Game Modes and GM Sessions.

`MQTT Interface` is now a capability-import workflow for locally smart/custom devices such as an altar, UID gate, relay controller or timer module. The admin selects an observed client, presses `Get config`, and the Quest Orchestrator sends `describe_interface`. Returned commands become device command capabilities/manual buttons; returned events become presets for room scenario waits.

## Room Scenarios

Room scenarios are executed by the GM room session runtime.

Supported room step types include:

- `DEVICE_COMMAND`
- `DEVICE_COMMAND_GROUP`
- `WAIT_DEVICE_EVENT`
- `WAIT_ANY_DEVICE_EVENT`
- `WAIT_ALL_DEVICE_EVENTS`
- `WAIT_TIME`
- `OPERATOR_APPROVAL`
- `SHOW_OPERATOR_MESSAGE`
- `SET_FLAG`
- `WAIT_FLAGS`
- `END_GAME`

Room scenarios support validation before start, a running-scenario snapshot on start, JSON import/export, and filesystem save/load. This avoids starting broken quest flows and prevents config edits from changing a scenario already in progress.

Room scenario branches have two product roles:

- `normal` branches are the quest flow. They can be required for completion and
  can synchronize with each other through flags.
- `reactive` branches are planned for reactions. A reactive branch listens for
  one trigger, runs a short action chain such as playing a sound or sending a
  command, then returns to listening. Reactive branches are shown separately
  from the main flow and do not count toward game completion. They share the
  same runtime flags as normal branches, so a reaction can `SET_FLAG` and a
  normal branch can continue from `WAIT_FLAGS`.

### Game Profiles

A game profile is a playable mode for a room.

It stores:

- profile id/name
- room id
- room scenario id
- game duration
- future hint/audio pack ids
- enabled flag

Starting a game by profile applies the duration, selects the scenario and starts the room scenario runtime.

## Audio

Audio is handled by `audio_player`.

Responsibilities:

- playback control
- runtime command routing
- background WAV playback
- effect WAV/MP3 playback
- background/effect PCM mixing through one I2S writer
- `system_audio.stop` without a channel stops both background and effect
- stop/reset silence drain so the I2S DAC does not hold the last audio sample
- reader and decode pipeline
- I2S output
- playback status
- volume persistence

Internal split:

- `audio_player.c` - public facade
- `audio_player_runtime.c` - runtime owner, command queue and playback lifecycle
- `audio_player_decode.c` - reader worker and decode path
- `audio_player_mixer.c` - background/effect PCM mixer and single I2S writer
- `audio_player_output.c` - I2S output
- `audio_player_status.c` - playback status
- `audio_player_volume.c` - volume persistence

The SD card is not owned by audio anymore. Audio uses `sd_storage` like any other consumer.

## MQTT

`mqtt_core` implements the local MQTT broker module.

Responsibilities:

- client sessions
- publish path
- injected local MQTT messages
- event mapping to internal bus
- stats for UI/status

The MQTT broker module is intended for local embedded devices and puzzle hardware, not as a general-purpose internet-facing broker.

Current MQTT broker sizing targets:

- max subscriptions per client: `16`
- max MQTT payload: `4096` bytes
- max MQTT packet: `6144` bytes

The larger packet budget is needed for on-demand `describe_interface` responses from universal quest devices.

Control-contract devices publish:

- `cp/v1/dev/{client_id}/heartbeat`
- `cp/v1/dev/{client_id}/status`
- `cp/v1/dev/{client_id}/diag`
- `cp/v1/dev/{client_id}/result`

The Quest Orchestrator sends control commands to:

- `cp/v1/dev/{client_id}/control/command`

## Event Bus

`event_bus` is the internal transport between modules.

Current design:

- typed events for MQTT, flags, runtime, device status and control
- priority posting for urgent device/control events
- fixed message pool with PSRAM-first allocation
- FreeRTOS queue stores message pointers, not full payload structs
- separate job queue for heavier handler work
- drop counters and slow-handler counters exposed through `/api/status`

GM room scenario event matching uses its own value-copy queue of
`event_bus_message_t`, so scenario event handling does not allocate/free heap
memory on every device event.

Handlers should stay short. Work that can block, publish MQTT, scan runtime state or trigger scenario logic should be deferred with `event_bus_post_job()`.

## Status and Fault Monitoring

`error_monitor` drives the status LED and aggregates health signals.

Examples:

- Wi-Fi disconnected
- SD missing or mount failure
- audio fault indication

SD card state now reaches `error_monitor` through `event_bus`, not through direct calls from storage.

GM/orchestrator health uses both configured logical devices and observed control-contract clients:

- fresh telemetry means `online`
- a registered quest device with stale/missing telemetry is `offline`
- offline registered devices create `device_offline` errors and make the room/system `fault`
- configured devices that have not been observed yet are degraded/warning during setup
- UI status badges must prefer `offline`/`unknown` over stale `health=ok`

The default online timeout for observed control clients is currently 15 seconds.

## Tools

`tools/device_control_client` provides a Python simulator for control-contract devices.

It can run multiple sample clients, publish heartbeat/status/diag/result, answer `describe_interface`, subscribe to quest command topics and emit quest events. It is useful for testing GM Device Setup, manual buttons, observed registration and offline handling before real devices are available.

## Project Layout

```text
components/
  audio_player/
  config_store/
  device_control_ingest/
  quest_common/
  gm_control/
  gm_core/
  error_monitor/
  event_bus/
  mqtt_core/
  network/
  orchestrator_core/
    audit/
    control/
    registry/
    timeline/
  quest_device/
  ota_manager/
  room_catalog/
  room_scenario/
  sd_storage/
  status_led/
  web_ui/
docs/
  ARCHITECTURE.md
main/
  main.c
```

## Documentation

- `docs/ARCHITECTURE.md` - module boundaries and layered design
- `docs/gm_api_contract.md` - GM/orchestrator HTTP contracts and JSON formats
- `docs/gm_panel_ui_plan.md` - operator/admin GM panel direction
- `docs/device_control_contract_v1.md` - MQTT control contract and interface discovery
- `docs/QUEST_DEVICE_SETUP_RUS.md` - Quest Device setup guide
- `docs/ROOM_SCENARIO_SETUP_RUS.md` - Room Scenario and Game Mode setup guide

## License

SceneHub is source-available proprietary software. The code is visible for review and collaboration, but use, copying, modification, redistribution, deployment, production installation, commercial use, or derivative products require prior written permission from the copyright holder.

See `LICENSE` for details. Third-party components remain under their own license terms.

## Notes for Further Work

Likely next evolutions:

- unified startup orchestrator instead of procedural deferred bootstrap
- fixed-pool job contexts for heavy event bus jobs
- further cleanup of `audio_player` if playback complexity grows
- more OTA diagnostics
- more tests around config/profile/runtime transitions
- optional persistent audit storage (current audit is RAM-only ring buffer)
