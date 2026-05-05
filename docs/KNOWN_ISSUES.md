# Known Issues

## Audio output can turn into loud noise around OTA confirmation

Observed on 2026-05-06: playback can become harsh noise/scrape around the log line `ota_manager: OTA image confirmed`. Restarting the device clears the issue and playback works normally afterwards.

Current status: mitigated by moving OTA confirmation earlier. OTA confirm now waits for `system_ready`, then confirms after a short stabilization delay instead of waiting a fixed 30 seconds after boot. A direct I2S/DAC reset around OTA confirmation was considered too intrusive and should not be used as the primary fix.

Notes for investigation:

- Check if the issue is caused by flash/cache stalls during `esp_ota_mark_app_valid_cancel_rollback()`.
- Capture logs around `audio_player_output`, mixer writes, and OTA confirmation if it reproduces.
- Preferred fix direction: confirm earlier before room audio starts, without resetting the DAC/I2S output as a side effect.
