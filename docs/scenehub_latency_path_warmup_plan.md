# Latency And Audio Asset Warmup

This document tracks the current latency/audio warmup work. The original P0/P1 plan is closed; this file now keeps the useful decisions and remaining roadmap.

## Status

P0 closed:

- Runtime actions return lightweight accepted JSON where appropriate.
- Hot-path GM panel actions refresh room runtime state instead of reloading the full GM state.
- Manual device commands, room scenario runtime actions, game actions, timer actions, and profile selection avoid unnecessary full-page state rebuilds where possible.
- Room timer display runs locally between backend refreshes.
- POST handlers log `PERF` timings for expensive paths.

P1 closed:

- Selected profiles walk the active scenario and collect `system_audio` play paths.
- Audio paths are checked through `stat/open/header-read/close`.
- Metadata/status is cached in `audio_player`.
- Room runtime JSON exposes an asset summary.
- GM panel shows asset READY/ERROR/PENDING state before start.

Not included yet:

- Blocking game start when critical assets are missing.
- Larger audio streaming changes.
- WAV/PCM-first policy for short effects.

## Current Rules

- Runtime button handlers should not call full `loadGM(true, true)` unless the action changes global data.
- Hot-path actions should update only the state they actually changed.
- Room timer UI should render locally and use backend refreshes for correction.
- Audio path validation should happen before the room reaches the hot playback path.
- Audio files should not be fully preloaded into memory.

## Known Risk

First playback can still be sensitive to SD/FAT latency, codec startup, and concurrent HTTP/MQTT/JSON work.

`docs/KNOWN_ISSUES.md` tracks the separate OTA-confirm audio noise issue.

## Remaining Roadmap

### P2.1 Streaming Buffer Tuning

- Increase background audio ring buffer if RAM budget allows.
- Use a small start threshold instead of waiting for the full buffer.
- Keep the effect channel responsive.

### P2.2 Short Effect Policy

- Prefer WAV/PCM for critical short effects.
- Keep MP3 support through Helix, but avoid MP3 for instant-response effects where timing matters.

### P2.3 Critical Asset Policy

- Add per-profile or per-scenario policy for missing assets:
  - warn only;
  - block start;
  - allow operator override.

### P2.4 Diagnostics

- Keep enough timing metrics to see whether latency comes from HTTP, JSON, SD/FAT, decoder startup, or command execution.
- Avoid noisy logs during normal room operation.

## Definition Of Done For P2

- Background playback starts consistently without long first-run delay.
- Short effects start predictably under room load.
- Missing critical files are visible before game start.
- Hot-path runtime actions stay responsive during audio activity.

