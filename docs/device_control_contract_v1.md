# Device Control Contract v1

## Optional Quest Interface Discovery

`describe_interface` is an optional discovery command for universal quest devices.
It is requested explicitly by the admin UI and must not be sent in every `status`.

Devices that support it may include `describe_interface` in `status.capabilities`.
The Quest Orchestrator sends the command to `cp/v1/dev/{device_id}/control/command`:

```json
{
  "request_id": "req-8c9f1d",
  "command": "describe_interface",
  "args": {},
  "ts_ms": 1713900000000
}
```

The device responds on `cp/v1/dev/{device_id}/result`:

```json
{
  "ts_ms": 1713900000100,
  "request_id": "req-8c9f1d",
  "command": "describe_interface",
  "status": "ok",
  "error_code": "",
  "message": "",
  "data": {
    "quest_interface": {
      "version": 1,
      "commands": [
        {
          "id": "start",
          "label": "Start altar",
          "kind": "mqtt_publish",
          "topic": "altar/cmd/start",
          "payload": "1",
          "button_enabled": true,
          "dangerous": false,
          "params_schema": []
        },
        {
          "id": "force_complete",
          "label": "Force complete",
          "topic": "altar/cmd/force_complete",
          "payload": "1",
          "button_enabled": true,
          "dangerous": true
        }
      ],
      "events": [
        {
          "id": "completed",
          "label": "Altar completed",
          "topic": "altar/event",
          "payload": "completed",
          "event_type": "completed"
        }
      ]
    }
  }
}
```

Rules:

- `commands[].id` and `commands[].topic` are required.
- `commands[].kind` defaults to `mqtt_publish` if omitted.
- `commands[].params_schema` is optional and is preserved by discovery/import when present.
- `events[].id` and `events[].topic` are required.
- Empty event `payload` means any payload on that topic.
- `event_type` defaults to `id` if omitted.
- Quest Orchestrator/UI import this metadata only after admin confirmation.
- Saved Quest Device capabilities describe available commands/events; Room Scenarios remain the authoritative quest-flow configuration.
- Health, connectivity, firmware and diagnostics still come from `heartbeat/status/diag/result`, not from `quest_interface`.
- `quest_interface` is discovery/config metadata. It must be requested on demand, not sent in every heartbeat/status.
- A device may subscribe to each `commands[].topic`. When it receives a matching command payload it may perform local logic and optionally publish one of its declared events.
- Quest Orchestrator MQTT packet limits must allow discovery results larger than a normal heartbeat/status. Current MQTT broker target is at least 4 KB payload and 6 KB packet.

Quest Orchestrator UI semantics:

- Physical clients appear in GM `Observed`.
- Quest Devices store a `client_id` that points to the physical control-contract client.
- A physical client is considered registered when it is referenced by a saved Quest Device `client_id`, even if the Quest Device id/name is different.
- If a previously observed registered device stops sending fresh telemetry, GM treats it as `offline`.
- `offline` registered quest devices are critical (`fault`) for room/system health.
- `not observed` during setup is warning/degraded until the first valid telemetry arrives.

## 1. Назначение

`Device Control Contract v1` задает единый служебный MQTT-контракт для всех будущих smart-устройств.

Покрывает:

- `heartbeat`
- `status`
- `diag`
- `control`
- `result`

Не покрывает:

- игровые/квестовые топики
- сценарии и настройки Quest Orchestrator / GM Panel
- внутреннюю бизнес-логику устройства

Цель: чтобы control plane был единым и не зависел от конкретного шаблона устройства.

## 2. Topic Namespace

Базовый namespace:

- `cp/v1/dev/{device_id}/...`

Топики, куда устройство публикует:

- `cp/v1/dev/{device_id}/heartbeat`
- `cp/v1/dev/{device_id}/status`
- `cp/v1/dev/{device_id}/diag`
- `cp/v1/dev/{device_id}/result`

Топик, который устройство подписывает для команд:

- `cp/v1/dev/{device_id}/control/command`

Опциональный broadcast для групповых команд:

- `cp/v1/dev/all/control/command`

## 3. Payload Schema

### `heartbeat`

```json
{
  "ts_ms": 1713900000000,
  "boot_id": "5f8d-2a11",
  "uptime_ms": 1234567,
  "status_seq": 42
}
```

### `status`

```json
{
  "ts_ms": 1713900000000,
  "boot_id": "5f8d-2a11",
  "fw_version": "1.3.0",
  "mode": "normal",
  "state": "idle",
  "health": "ok",
  "capabilities": ["heartbeat","status","diag","refresh_status","reboot"],
  "runtime": {
    "active": false
  }
}
```

### `diag`

```json
{
  "ts_ms": 1713900000000,
  "level": "warn",
  "code": "sensor_timeout",
  "message": "No pulse in 3s",
  "details": {
    "sensor_id": "beam_1"
  }
}
```

### `result`

```json
{
  "ts_ms": 1713900000000,
  "request_id": "req-8c9f1d",
  "command": "refresh_status",
  "status": "ok",
  "error_code": "",
  "message": ""
}
```

## 4. Обязательные Capability

Каждый smart-узел обязан поддерживать:

- отправку `heartbeat`
- отправку `status`
- отправку `diag`
- команду `refresh_status`

## 5. Опциональные Capability

Могут поддерживаться не всеми устройствами:

- `reboot`
- `reset_runtime`
- `apply_preset`
- `set_mode`

Если capability не поддерживается, устройство должно вернуть `result.status=error` и `error_code=not_supported`.

## 6. Command Semantics

Команды отправляются в `cp/v1/dev/{device_id}/control/command`.

Базовый формат команды:

```json
{
  "request_id": "req-8c9f1d",
  "command": "refresh_status",
  "args": {},
  "ts_ms": 1713900000000
}
```

Семантика:

- `refresh_status`: немедленно отправить актуальный `status`
- `reboot`: перезапустить устройство
- `reset_runtime`: сбросить runtime-состояние устройства
- `apply_preset`: применить preset из `args.preset_id`
- `set_mode`: установить рабочий режим из `args.mode`

На каждую команду устройство публикует `result` с тем же `request_id`.

## 7. Result/Error Semantics

Допустимые `result.status`:

- `ok`
- `accepted`
- `error`

Допустимые `error_code`:

- `invalid_request`
- `not_supported`
- `busy`
- `timeout`
- `internal_error`
- `unauthorized`

Правила:

- при `status=ok` поле `error_code` должно быть пустым
- при `status=error` поле `error_code` обязательно
- `request_id` обязателен для корреляции command/result

## 8. Mapping в Quest Orchestrator / GM Panel

Минимальный mapping:

- `connectivity`: по наличию свежего `heartbeat`
- `last_seen`: из `heartbeat.ts_ms` (или server receive time)
- `health`: из `status.health` + `diag.level`
- `fw_version`: из `status.fw_version`
- `boot_id`: из `heartbeat.boot_id` / `status.boot_id`
- `issues`: из `diag.code`, `diag.message`, `result.error_code`

Этот mapping обязателен для read-model слоя Quest Orchestrator / GM Panel и не должен смешиваться с игровыми шаблонами.
