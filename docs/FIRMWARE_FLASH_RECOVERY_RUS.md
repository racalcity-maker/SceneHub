# Прошивка и восстановление

Документ описывает базовые операции для hub firmware и node firmware:
прошивка, monitor, OTA, reset и безопасное восстановление после неудачного
обновления.

## Hub firmware

Из корня репозитория:

```powershell
cd D:\Projects\SceneHub
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash monitor
```

Заменить `COM4` на фактический порт.

Только monitor:

```powershell
idf.py -p COM4 monitor
```

## Node firmware

```powershell
cd D:\Projects\SceneHub\scenehub_node_v1\esp_idf
idf.py set-target esp32s3
idf.py -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.esp32s3 build
idf.py -p COM4 flash monitor
```

## Перед прошивкой

- Зафиксировать текущий commit.
- Сохранить текущие Wi-Fi/MQTT/node настройки, если они нужны после reset.
- Для hub проверить SD-карту и наличие нужных room/device/scenario файлов.
- Для node записать `node_id`, board profile, pins, PN532 config и known cards.

## После прошивки

Hub:

- проверить boot log;
- открыть Web UI;
- выполнить admin login;
- открыть GM panel;
- проверить MQTT broker;
- проверить видимость устройств.

Node:

- проверить provisioning или STA IP;
- проверить MQTT connect/subscribed;
- проверить `describe_interface`;
- проверить hardware commands;
- проверить PN532 ready/degraded state, если reader включен.

## OTA

OTA используется только когда текущий build уже достаточно стабилен, чтобы
принять обновление через Web UI.

Порядок:

1. Сделать backup критичных конфигов.
2. Загрузить firmware image через Update UI.
3. Дождаться перезагрузки.
4. Проверить boot и Web UI.
5. Подтвердить, что система работает штатно.

Если после OTA устройство не выходит в рабочее состояние, использовать serial
flash recovery.

## Recovery order

Использовать этот порядок, не начиная сразу с factory reset:

1. Снять boot log.
2. Перезагрузить устройство.
3. Проверить питание, USB-UART, SD-карту, Wi-Fi.
4. Открыть monitor и проверить первый явный error.
5. Перепрошить тот же known-good build.
6. Сбросить Wi-Fi/settings только если config явно мешает boot.
7. Factory reset только после сохранения нужных данных или когда данные уже
   считаются потерянными.

## Hub reset notes

- Bootstrap admin is `admin / admin` until forced password change completes.
- Setup AP and credential reset depend on configured reset/setup policy.
- Runtime storage uses NVS and SD card; reset paths may not erase both.

## Node reset notes

Node reset pin policy:

- hold 5 seconds: reset Wi-Fi settings and enter provisioning;
- hold 30 seconds: factory reset node config and enter provisioning.

Factory reset clears local node config, custom bundle and driver config. It does
not replace firmware.

## When to stop

Stop and capture logs before continuing when:

- repeated panic/reset loop appears;
- NVS or storage failures repeat after reflashing;
- OTA rollback behavior is unclear;
- relay/MOSFET outputs behave unexpectedly;
- PN532 failure causes unrelated node functions to stop.
