# LED Remaining

Current baseline already shipped:

- dedicated `LED Setup` and `LED Editor`
- separate LED preset storage
- preview path without saving
- advanced effect registry with schema endpoint
- public hub-facing LED contract kept simple
- `background_color` separated from `secondary_color`
- effect runtime split out of the old god files

This file tracks only the remaining open work.

## Open Product Work

### Provisioning UI

- [ ] Add clearer preview-state indication in the UI.
- [ ] Tighten effect-control rendering so every effect shows only the controls
  it actually uses, with cleaner labels/hints.

### Persistence / Verification

- [ ] Re-run the full persistence path after the latest config/runtime splits:
  - [ ] save base LED wiring
  - [ ] save LED presets
  - [ ] reboot
  - [ ] reload provisioning UI
  - [ ] run command from hub
- [ ] Re-check that strip wiring and LED presets never overwrite each other.
- [ ] Re-check unknown/old config recovery against the current LED preset model.

### Logs / Diagnostics

- [ ] Add an explicit log for LED preset save/apply path, not only effect
  start/done/cancel/fail.

### Hub Integration

- [ ] Add a normal `Refresh interface` flow in SceneHub so new node effect
  names/options do not require manual device re-import.
- [ ] Re-check quick actions / imported manifests after interface refresh.

### Optional Future Work

- [ ] Add explicit LED runtime lifecycle events as a contract, not only logs.
- [ ] Add `node_hw_led_deinit()` only if live LED wiring reconfigure becomes a
  real requirement.

## Closed In This Slice

- [x] Preview on/off works without saving presets.
- [x] Public `led.blink`, `led.breathe`, and `led.effect` no longer accept
  advanced overrides.
- [x] Schema duplication between runtime descriptors and provisioning UI is
  removed.
- [x] Advanced effect set is implemented and manually verified.
- [x] `count = 0` semantics are aligned as infinite where intended.
- [x] `led.off` cancels running effects immediately.
- [x] New effects preempt old effects cleanly.
