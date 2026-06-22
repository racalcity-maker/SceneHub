# SceneHub Hub Architecture Map

This document is a compact map of the SceneHub controller firmware. The durable
architecture text remains in `ARCHITECTURE.md`; this file is the quick visual
reference for ownership and data flow.

## Component Map

```mermaid
flowchart LR
    Web[web_ui HTTP + WebSocket]
    Control[scenehub_control]
    Read[scenehub_read_model]
    State[scenehub_state]
    GM[gm_core]
    Scenario[room_scenario]
    Profiles[gm_profile_store]
    Devices[quest_device]
    Ingest[device_control_ingest]
    Cmd[command_executor]
    Events[event_bus]
    MQTT[mqtt_core broker]
    Audio[audio_player]
    HW[hardware_io]
    OTA[ota_manager]
    Config[config_store]
    SD[sd_storage]
    Status[service_status + error_monitor]
    Node[Physical SceneHub Nodes]

    Web -->|POST commands| Control
    Web -->|GET projections| Read
    Read --> State
    Read --> GM
    Read --> Devices
    Read --> Status

    Control --> GM
    Control --> Scenario
    Control --> Profiles
    Control --> Devices
    Control --> Cmd
    Control --> Config
    Control --> OTA

    GM --> Scenario
    GM --> Cmd
    GM --> Events
    Scenario --> Cmd
    Devices --> SD
    Profiles --> SD
    Config --> SD

    Cmd --> MQTT
    Cmd --> Audio
    Cmd --> HW
    MQTT <-->|device contract| Node
    Node -->|heartbeat/status/diag/result/event| Ingest
    Ingest --> Devices
    Ingest --> Events
    Events --> GM
    Events --> Read
    Status --> Read
```

## Command Flow

```mermaid
sequenceDiagram
    participant UI as Web UI / GM panel
    participant Control as scenehub_control
    participant GM as gm_core
    participant Cmd as command_executor
    participant MQTT as mqtt_core
    participant Node as SceneHub Node
    participant Ingest as device_control_ingest
    participant Read as scenehub_read_model

    UI->>Control: POST action
    Control->>GM: validate/session command
    GM->>Cmd: command plan
    Cmd->>MQTT: publish device command
    MQTT->>Node: cp/v1/dev/{node_id}/control/command
    Node->>MQTT: result/event/status
    MQTT->>Ingest: incoming telemetry
    Ingest->>Read: update projection source state
    Read->>UI: refreshed state via HTTP/WS
```

## Scenario And Local System Flow

```mermaid
flowchart TD
    Start[GM starts game/profile] --> Load[Load room scenario]
    Load --> Step[Execute scenario step]
    Step --> Wait{Wait kind}
    Wait -->|time| Timer[Timer wait]
    Wait -->|device event| DeviceEvent[device_control_ingest event]
    Wait -->|operator| GMAction[GM operator action]
    Wait -->|none| Next[Next step]
    DeviceEvent --> Events[event_bus]
    Events --> Step
    Timer --> Step
    GMAction --> Step
    Step -->|system_audio| Audio[audio_player]
    Step -->|system_relay/system_mosfet| HW[hardware_io]
    Step -->|quest device command| Cmd[command_executor]
    Cmd --> MQTT[mqtt_core]
    MQTT --> Node[physical node]
    Next --> Step
```

## Ownership Rules

- `web_ui` serializes requests and serves projections; it should not own domain
  execution.
- `scenehub_control` is the write-side application boundary.
- `scenehub_read_model` is read-side projection only.
- `gm_core` owns session/scenario runtime state and command planning.
- `command_executor` owns dispatch to MQTT, local hardware and system devices.
- `device_control_ingest` owns physical node telemetry parsing.
- `event_bus` transports events upward; handlers must stay light.
- `audio_player`, `hardware_io`, `mqtt_core` and storage modules own their own
  platform resources.

## Related Docs

- `ARCHITECTURE.md`
- `ARCHITECTURE_LAYER_RISK_MAP.md`
- `policies/API_HTTP_POLICY.md`
- `policies/LOCKING_POLICY.md`
- `policies/MEMORY_ALLOCATION_POLICY.md`
- `device_control_contract_v1.md`
- `../scenehub_node_v1/docs/ARCHITECTURE_MAP.md`
