# Node v2 Hub Admin UI Plan

This document fixes the intended SceneHub UX for SceneHub Node v2 admin
operations. It separates ordinary device control from standalone-bundle
administration so the Hub does not treat rule editing as a normal scenario or
sidebar command flow.

## Goal

SceneHub should support Node v2 standalone bundle administration without
mixing it into:

- room scenario command selection;
- ordinary manual `Device Controls` buttons;
- player-facing or operator-fast-path actions.

The transport may still use MQTT admin commands, but the Hub must render them
through the correct UI surface.

## Current Status

Implementation status, 2026-06-20:

- The split between bundle workflow and quick admin actions is now implemented.
- `Devices -> Edit device` owns the standalone bundle workflow:
  `node.rules.get`, `node.rules.validate`, `node.rules.apply`, optional
  `node.rules.clear`, and `node.reboot`.
- `Device Controls` owns only quick operational admin actions such as
  `Pause rules`, `Resume rules` and `Restart node`.
- The edit-device modal now keeps the standalone rule-engine section collapsed
  by default and renders inline operation status so import/save/admin failures
  are visible in the same workflow surface.
- The save contract remains separate from standalone bundle administration:
  `Save device` persists the SceneHub GM quest-device contract, while
  `Apply bundle` stores node-local standalone rule JSON on the node itself.

## UI Split

Use two different Hub surfaces.

### 1. `Devices -> Edit device`

This is the owner surface for standalone bundle administration.

It should include a small admin panel for the selected physical node:

- load current stored bundle;
- show bundle metadata and runtime state;
- edit/paste bundle JSON;
- validate bundle;
- apply bundle;
- reboot node after apply when needed.

Underlying node admin commands:

- `node.rules.get`
- `node.rules.validate`
- `node.rules.apply`
- `node.reboot`

This is a workflow UI, not a button list. `validate` and `apply` should not be
presented as ordinary device-control buttons.

### 2. `Device Controls`

This surface may expose only quick operational admin actions that make sense as
single buttons.

Allowed quick admin actions:

- rules `pause`
- rules `resume`
- node `reboot`

UI labels may be friendlier than transport names, for example:

- `Pause rules` -> `node.rules.pause`
- `Resume rules` or `Play rules` -> `node.rules.resume`
- `Restart node` or `Reset node` -> `node.reboot`

These actions should appear in a separate admin section, not mixed into the
ordinary relay/MOSFET/LED control list.

## Manifest Contract

Node `describe_interface` should continue to separate:

- `command_templates`: runtime/scenario-facing commands;
- `event_templates`: runtime events such as `input.changed` or
  `rules.changed`;
- `admin_command_templates`: admin-only actions that SceneHub may render in
  admin surfaces.

SceneHub should not import `admin_command_templates` into the normal scenario
command resolver.

## Command Placement Rules

Place these operations in the Hub as follows.

### Bundle workflow only

- `node.rules.get`
- `node.rules.validate`
- `node.rules.apply`

These belong only in `Devices -> Edit device`.

### Quick admin actions

- `node.rules.pause`
- `node.rules.resume`
- `node.reboot`

These may appear in `Device Controls` under a dedicated admin section.

### Not needed in the first Hub slice

- `node.rules.clear`
- `node.config.reset_wifi`
- `node.config.factory_reset`

They may stay unsupported in the first Hub UI slice even if the node transport
supports or later adds them.

## Danger and Confirmation Policy

`Devices -> Edit device`:

- `Validate bundle`: no destructive confirmation required.
- `Apply bundle`: must require confirmation and clearly state whether restart is
  required.
- `Reboot node`: must require confirmation.

`Device Controls` quick admin section:

- `Pause rules`: no confirmation required.
- `Resume rules`: no confirmation required.
- `Reboot/Reset node`: confirmation required.

Danger styling should be applied at least to:

- `node.rules.apply`
- `node.reboot`

## Event and Status Expectations

SceneHub should use existing node data to keep this UI current:

- `node.get_status` for runtime rule state;
- `rules.changed` event for apply / clear / pause / resume transitions;
- `describe_interface` manifest refresh when the node interface contract
  changes.

## Non-Goals

This slice does not require:

- exposing bundle `validate` / `apply` in scenario builders;
- exposing bundle editing in the generic command sidebar;
- treating rule bundle JSON as a normal device command parameter form.

## Implementation Direction

1. Read and store `admin_command_templates` from node manifest.
2. Add `Devices -> Edit device` admin panel for bundle workflow.
3. Add `Device Controls` admin action group for `pause` / `resume` /
   `reboot`.
4. Keep `scenehub_device_command_resolver` scoped to normal
   `command_templates`; add a separate admin dispatch path for
   `admin_command_templates`.

Current state:

- Steps 1 through 4 are implemented in the current Hub slice.
- Remaining work is polish and correctness around modal feedback, contract save
  visibility and future fallback/NFC-specific admin surfaces, not a redesign of
  the UI split itself.
- NFC-specific admin surfaces are explicitly not part of the mandatory current
  slice. In particular, centralized known-card editing from GM is deferred
  until there is a clear product need for `node.nfc.cards.get/set`.

## Deferred NFC Admin Direction

If Hub-side NFC card editing is added later, it should follow the same split:

- belong only to `Devices -> Edit device`;
- stay out of scenario command selection;
- stay out of `Device Controls` quick-action lists;
- reuse admin transport semantics instead of becoming ordinary runtime
  `command_templates`.

Until then:

- node-local provisioning owns known-card CRUD;
- SceneHub only needs reader health/status visibility plus exported
  event/command contracts.
