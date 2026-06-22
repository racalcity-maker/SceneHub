# Hub Config Reference

This is the release-facing configuration reference for the SceneHub hub
firmware. It summarizes the main persisted config and compile-time options.

## Runtime Config Store

Stored through `config_store`.

| Area | Field | Notes |
| --- | --- | --- |
| Wi-Fi | `ssid` | STA network name. |
| Wi-Fi | `password` | Stored in config; do not expose in logs or support screenshots. |
| Wi-Fi | `hostname` | Device hostname. |
| MQTT | `broker_id` | Local broker identity. |
| MQTT | `port` | Broker port, normally `1883`. |
| MQTT | `keepalive_seconds` | MQTT keepalive. |
| MQTT | `users[]` | Up to 16 users, each with client id, username and password. |
| Web auth | `web.username` | Admin username. |
| Web auth | `web.password_hash` | Stored as salted hash. |
| Web auth | `web_user` | Optional operator/user account. |
| Time | `ntp_server` | NTP source. |
| Time | `timezone_offset_min` | Local offset in minutes. |
| Logging | `verbose_logging` | Runtime verbosity flag. |

## Important Compile-Time Options

Defined through Kconfig/sdkconfig and `scenehub_config`.

| Option | Default/fallback | Notes |
| --- | --- | --- |
| `CONFIG_SCENEHUB_SETUP_AP_PASSWORD` | `12345678` | Setup AP password. Change before real field release. |
| `CONFIG_SCENEHUB_WEB_AUTH_DEFAULT_USER` | `admin` | Bootstrap admin user. |
| `CONFIG_SCENEHUB_WEB_AUTH_DEFAULT_PASS` | `admin` | Bootstrap password before forced change. |
| `CONFIG_SCENEHUB_WEB_AUTH_RESET_GPIO` | `-1` | Optional reset/setup GPIO. |
| `CONFIG_SCENEHUB_MQTT_MAX_CLIENTS` | `25` | Broker client limit. |
| `CONFIG_SCENEHUB_MAX_ROOMS` | `1` | Room catalog capacity. |
| `CONFIG_SCENEHUB_I2S_BCK_PIN` | `4` | Audio I2S BCK. |
| `CONFIG_SCENEHUB_I2S_WS_PIN` | `5` | Audio I2S WS/LRCK. |
| `CONFIG_SCENEHUB_I2S_DATA_PIN` | `6` | Audio I2S data. |
| `CONFIG_SCENEHUB_SD_MISO_PIN` | `13` | SD SPI MISO. |
| `CONFIG_SCENEHUB_SD_MOSI_PIN` | `11` | SD SPI MOSI. |
| `CONFIG_SCENEHUB_SD_CLK_PIN` | `12` | SD SPI clock. |
| `CONFIG_SCENEHUB_SD_CS_PIN` | `10` | SD chip select. |
| `CONFIG_SCENEHUB_RELAY*_GPIO` | `-1` | Disabled unless configured. |
| `CONFIG_SCENEHUB_MOSFET*_GPIO` | `-1` | Disabled unless configured. |
| `CONFIG_SCENEHUB_GPIO*_GPIO` | `-1` | Disabled unless configured. |

## Release Rules

- Record every changed sdkconfig option in the sign-off report.
- Do not ship field builds with default admin credentials accepted as final
  credentials.
- Do not expose Wi-Fi or MQTT passwords in screenshots, logs or release notes.
- If hardware pins change, update `HARDWARE_INSTALLATION_RUS.md` and the
  release notes.
