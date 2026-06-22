# Hardware Installation

Этот документ фиксирует минимальные правила подключения hub и node перед
alpha/field release.

## Общие правила

- Всегда должна быть общая земля между контроллером, питанием периферии и
  внешними модулями.
- Не питать 5V периферию от 3.3V вывода ESP32.
- Не подавать 5V logic на GPIO ESP32 без согласования уровней.
- Реле, MOSFET и LED strips должны иметь отдельный запас по току.
- Для длинных линий входов использовать нормальную разводку, подтяжки и
  подавление помех.
- Перед field use проверить каждую линию отдельной командой.

## Hub hardware

Hub может использовать:

- ESP32-S3;
- SD card по SPI;
- I2S audio output;
- локальные relay/MOSFET/GPIO outputs;
- Wi-Fi STA/AP;
- USB-UART для recovery.

Compile-time defaults are documented in `CONFIG_REFERENCE.md`.

## Node hardware

Текущий release hardware baseline для node:

- поддерживаемая плата: ESP32-S3 N16R8;
- проверено на 4 одинаковых ESP32-S3 N16R8 платах;
- другие платы на этот release не считаются поддерживаемыми, даже если
  firmware теоретически может на них собраться;
- релизные пины считаются теми, что заданы в текущем factory/profile config;
- пины можно менять через factory profile/provisioning, но такой вариант нужно
  заново записать в sign-off report.

Node firmware поддерживает:

- до 8 relay outputs;
- до 8 MOSFET outputs;
- до 8 universal IO lines;
- до 2 LED strips;
- reset/config pin;
- PN532 NFC reader over I2C.

Фактический набор зависит от board profile и provisioning config.

## PN532

Текущий рабочий PN532 driver:

- bus: `i2c_1`;
- speed: `100000`;
- default address: `0x24`;
- reset pin optional, may be `-1`;
- no-card polling is normal and must not be treated as driver fault.

Для текущей ESP32-S3 проверки использовались SDA `8`, SCL `9`, но релизный
board profile должен быть записан отдельно в sign-off report.

## LED strips

- Проверить chipset: `WS2812`, `WS2815` или `SK6812`.
- Проверить color order.
- Проверить pixel count.
- Для длинной ленты использовать отдельное питание.
- Общая земля с node обязательна.

## Relay and MOSFET outputs

- Проверить `active_low`.
- Проверить safe default state после boot.
- Проверить pulse duration limits.
- Для индуктивной нагрузки использовать защиту по месту.

## Universal IO

- Зафиксировать роль каждой линии: disabled, input, output.
- Для input проверить active_low и debounce.
- Для внешних датчиков проверить уровень сигнала и подтяжки.

## Release hardware checklist

- [ ] Hub board/profile recorded.
- [ ] Node board/profile recorded as ESP32-S3 N16R8.
- [ ] Tested count recorded; current baseline is 4 identical ESP32-S3 N16R8 boards.
- [ ] Power supply current margin checked.
- [ ] Common ground checked.
- [ ] SD card mounted on hub.
- [ ] Audio output checked if used.
- [ ] Each relay output checked.
- [ ] Each MOSFET output checked.
- [ ] Each input checked.
- [ ] LED strip effect checked.
- [ ] PN532 starts or degraded state is accepted.
- [ ] Reset/recovery path checked.
