# Known Issues

No active release-blocking known issues are currently tracked here.

## Resolved

### Audio output could turn into loud noise around OTA confirmation

Observed on 2026-05-06: playback could become harsh noise/scrape around the
log line `ota_manager: OTA image confirmed`. Restarting the device cleared the
issue.

Status: resolved/mitigated. OTA confirmation now waits for `system_ready`, then
confirms after a short stabilization delay instead of waiting a fixed 30 seconds
after boot. The audio player was also hardened against timing interruptions by
keeping output writes frame-aligned, handling partial writes, writing silence
when needed, and resetting output on detected bad I2S write state.

A direct DAC/I2S reset around OTA confirmation was intentionally not used as the
primary fix.
