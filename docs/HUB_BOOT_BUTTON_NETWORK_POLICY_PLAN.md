# Hub Boot Button / Network Policy Plan

## Goal

Replace the current automatic AP fallback with an explicit physical-button
policy:

- boot hold -> setup AP
- runtime long hold -> reset to defaults + reboot
- normal failed STA connect -> keep retrying without raising AP automatically

## Status

Implemented.

Current behavior now is:

- boot hold on the configured reset/setup pin requests setup AP
- runtime long hold restores defaults and reboots
- failed STA startup no longer auto-enables AP
- an empty Wi-Fi config still starts setup AP so factory-default recovery
  remains usable without an extra second reset cycle

## Why

Current hub behavior mixes two unrelated recovery models:

1. `components/network/network.c`
   - after `MAX_RETRY` failed STA connects, it enables AP automatically
2. `components/web_ui/web_ui_auth_reset.c`
   - a reset pin monitor exists, but it only resets web auth defaults

That creates poor recovery semantics:

- AP appears automatically on transient Wi-Fi failures
- physical recovery and network recovery are not aligned
- the reset pin policy is too narrow for the product role

## Target Behavior

### Boot-time behavior

- if the configured reset/setup pin is held during boot for at least a short
  threshold, start in setup AP mode
- if the pin is not held, start in normal STA mode only

### Runtime behavior

- do not auto-enable AP after failed STA retries
- continue STA reconnect in the background with bounded backoff
- if the reset/setup pin is held for a long threshold during runtime, perform
  reset-to-defaults and reboot

## Desired Policy

Recommended thresholds:

- boot hold: about `2s`
- runtime reset hold: `15s`

The exact values can stay as local constants in the first pass.

## Scope

First implementation pass should cover:

- network startup policy
- physical setup-AP request on boot
- runtime default-reset action
- UI/login hint text updates that describe the new recovery behavior

Out of scope for this pass:

- new provisioning/auth model for AP mode
- per-device AP credentials redesign
- remote-triggered setup AP enablement
- generic button policy framework for every future feature

## Current Code Touch Points

### Network

- `components/network/network.c`

Current behavior to remove:

- startup retry limit of `MAX_RETRY = 5` leading to automatic `start_ap_mode(...)`

Current runtime reconnect path to keep/adapt:

- `schedule_runtime_reconnect()`

### Existing reset pin monitor

- `components/web_ui/web_ui_auth_reset.c`

Current behavior to replace:

- long hold resets only web auth defaults

### UI text

- `components/web_ui/web_ui_page.c`

Current login/help text references holding the reset pin low for 10s.
That must be updated when the runtime action becomes full defaults reset and
the AP mode becomes boot-hold driven.

## Implementation Shape

### 1. Introduce a small hub reset/setup policy module

Create a dedicated module instead of expanding `web_ui_auth_reset.c`.

Suggested shape:

- `components/system_reset_policy/`
  - `include/system_reset_policy.h`
  - `system_reset_policy.c`

Responsibilities:

- configure and sample the reset/setup GPIO
- detect boot-hold for setup AP request
- monitor runtime long-hold for reset-to-defaults

This keeps naming honest. The current `web_ui_auth_reset.c` should not become a
catch-all owner for network recovery and device reset policy.

### 2. Add a boot-time setup AP decision

Need one narrow path such as:

- `bool system_reset_policy_boot_setup_requested(void);`

This must be decided before `network_start()`.

### 3. Adjust network API

Network start needs an explicit way to enter setup AP mode on boot.

Possible shapes:

- `esp_err_t network_start(bool force_ap_setup);`
- or a small setter such as `network_request_setup_ap_boot()`

Preferred rule:

- explicit boot decision from the reset/setup policy
- no hidden AP fallback after STA retry failure

### 4. Remove automatic AP fallback from STA failure

In `components/network/network.c`:

- stop calling `start_ap_mode(...)` after startup retry exhaustion
- instead schedule ongoing reconnect attempts with the runtime reconnect path

Result:

- Wi-Fi failures do not silently expose setup AP
- recovery remains available only through physical access

### 5. Replace web-auth-only reset monitor

Remove or repurpose:

- `components/web_ui/web_ui_auth_reset.c`

New runtime long-hold action should be:

- reset config to defaults
- clear auth as part of defaults reset
- reboot

This keeps one physical recovery action instead of overlapping partial resets.

## Memory / Task Policy

Do not solve this with multiple monitoring tasks.

Requirements:

- one small monitoring task at most
- no large stack-local snapshots
- no new ad hoc workers for network recovery

This feature is policy/orchestration, not a data-heavy path.

## Success Criteria

This work is complete when:

- failed STA startup no longer auto-enables AP
- boot-hold on the reset/setup pin reliably starts setup AP
- runtime long-hold reliably resets to defaults and reboots
- login/help UI text matches the new behavior
- no extra large worker stacks are introduced

## Follow-Up

After this lands, backlog/docs should be updated to state:

- setup AP is physical-access only
- AP is no longer part of automatic Wi-Fi failure fallback
- reset/setup pin policy is the supported recovery path
