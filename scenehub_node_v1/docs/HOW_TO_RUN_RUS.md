# Как запустить SceneHub Node v1

Этот файл описывает практический запуск ESP-IDF ноды: прошивка, первичная
настройка, проверка MQTT, NFC PN532 и standalone bundle.

## Что нужно

- ESP32-S3 плата с включенным PSRAM.
- ESP-IDF 5.3.x в окружении терминала.
- Wi-Fi сеть, в которой доступен SceneHub controller или MQTT broker.
- USB-UART порт платы, например `COM4`.
- Для PN532: модуль в I2C-режиме, питание 3.3V, общая земля, SDA/SCL по
  factory profile или по настройкам provisioning.

## Структура

Код ноды лежит отдельно от controller firmware:

```text
scenehub_node_v1/
  esp_idf/        ESP-IDF firmware ноды
  docs/           документация ноды
  examples/       старые protocol examples
```

Основной ESP-IDF проект:

```text
scenehub_node_v1/esp_idf
```

## Сборка и прошивка

Из ESP-IDF терминала:

```powershell
cd D:\Projects\SceneHub\scenehub_node_v1\esp_idf
idf.py set-target esp32s3
idf.py -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.esp32s3 build
idf.py -p COM4 flash monitor
```

Заменить `COM4` на свой порт.

Если плата уже настроена и нужно только смотреть лог:

```powershell
cd D:\Projects\SceneHub\scenehub_node_v1\esp_idf
idf.py -p COM4 monitor
```

## Первичная настройка

При первом запуске нода поднимает provisioning AP, если нет сохраненных Wi-Fi
настроек.

Ожидаемый AP:

```text
SSID: SceneNode-XXXX
Password: setup-XXXXXX
```

Дальше:

1. Подключиться к AP ноды.
2. Открыть локальный web UI ноды.
3. Задать `node_id`, имя ноды, Wi-Fi SSID/password.
4. Задать адрес controller/MQTT, например `192.168.43.203:1883`.
5. Выбрать режим:
   - `scenehub` для работы через SceneHub controller;
   - `standalone` для локальных правил без controller.
6. Сохранить настройки и перезагрузить ноду, если UI попросит.

Подробности provisioning описаны в `PROVISIONING_AND_CONFIG.md`.

## Минимальная проверка после загрузки

В нормальном логе должны быть видны только ключевые события:

```text
node_provisioning: STA got IP
node_mqtt_transport: connected
node_mqtt_transport: subscribed cp/v1/dev/<node_id>/control/command
node_drv_pn532: pn532 ready fw=...
```

Штатные события вроде `reader event seen`, `led.effect`, `effect start` теперь
находятся на debug-уровне и не должны засорять обычный монитор.

## Проверка MQTT

Нода подписывается на:

```text
cp/v1/dev/{node_id}/control/command
```

И публикует heartbeat/status/diag/result/event по контракту:

```text
docs/device_control_contract_v1.md
```

На стороне controller нода должна появиться как observed control device.

## Проверка PN532

Для PN532 в текущем рабочем профиле используются:

```text
bus: i2c_1
addr: 0x24
freq: 100000
```

SDA/SCL берутся из factory profile или из конфигурации ноды. Для текущей
ESP32-S3 платы рабочий тестовый профиль использовал SDA `8`, SCL `9`.

Ожидаемый успешный старт:

```text
node_drv_pn532: pn532 ready fw=1.6 support=0x07 ic=0x32
```

Если карты нет, это не ошибка. Poll timeout в обычном режиме считается
отсутствием карты и не должен печататься как warning.

Подробности по PN532: `NFC_PN532_SETUP_AND_DIAGNOSTICS_RUS.md`.

## Запуск standalone bundle

Примеры bundle лежат здесь:

```text
scenehub_node_v1/docs/examples/node_v2_bundles/
```

Для сценария с тремя NFC-картами:

```text
nfc_3_cards_effect_relay_input.json
```

Поведение примера:

- карта с `token_id=1`: включает эффект 1, при снятии карты выключает LED;
- карта с `token_id=2`: включает эффект 2, при снятии карты выключает LED;
- карта с `token_id=3`: включает эффект, дает pulse relay на 3 секунды,
  ждет `input_1`, после входа включает третий эффект;
- повторное прикладывание любой из трех карт снова запускает свою ветку.

Важно: список known cards должен содержать только реально добавленные карты.
Пустые слоты без UID не должны участвовать в runtime.

## Где смотреть состояние

Основные источники:

- node local web UI;
- MQTT status/diag;
- controller observed devices;
- serial monitor.

Для обычной проверки достаточно `INFO`-логов. Для детальной трассировки LED/NFC
нужно временно включать debug-уровень для конкретного тега, а не держать весь
проект в verbose.

## Если что-то не работает

Порядок проверки:

1. Нода получила IP.
2. MQTT подключен и подписка создана.
3. PN532 пишет `pn532 ready`.
4. Known card добавлена с UID и token id.
5. Bundle активен и generation совпадает с ожидаемой.
6. Для presence-сценариев снятие карты реально генерирует `card_removed`.
7. LED strip и relay включены в hardware config.

Если PN532 стартует, но карта не читается, сначала проверить known card mapping
и bundle conditions. Если PN532 не стартует, смотреть `NFC_PN532_SETUP_AND_DIAGNOSTICS_RUS.md`.
