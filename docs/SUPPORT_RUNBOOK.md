# Support Runbook

This runbook is the operator and developer first-response guide for SceneHub
alpha support.

Use it together with:

- `KNOWN_ISSUES.md`
- `VERSION_COMPATIBILITY_MATRIX.md`
- `NODE_IMPLEMENTATION_CHECKLIST.md`
- `scenehub_node_v1/docs/PROVISIONING_AND_CONFIG.md`

## First Triage

Capture these facts before changing anything:

- hub commit or build label
- node firmware commit or build label
- physical node id
- operation mode: `scenehub`, `standalone`, or `fallback`
- whether PN532 is enabled
- current GM health: `ok`, `degraded`, `offline`, `fault`
- exact action that failed
- exact log line or error code

## Common Cases

### Node offline or fault

Check:

- node has power and boot logs;
- Wi-Fi STA joined successfully;
- MQTT connected to the expected broker;
- node is publishing `heartbeat` and `status`.

Actions:

- verify node id matches the GM-bound physical client id;
- reboot node once from admin action or local UI;
- if still offline, inspect Wi-Fi, broker reachability, and `cp/v1` topic ids.

### Node degraded with NFC reader problem

Expected alpha behavior:

- node stays usable;
- GM shows degraded health;
- reader-specific state shows `offline` or `init_failed`.

Actions:

- confirm PN532 is intentionally enabled;
- verify configured I2C pins and address;
- if reader is absent, either disable the driver or accept degraded state;
- if reader is present but unhealthy, inspect wiring and power first.

### `describe_interface` import fails

Check:

- node publishes a valid compact manifest result;
- hub logs do not show `invalid payload`;
- manifest size remains inside current runtime budget.

Actions:

- retry once after node reconnect;
- if the node changed recently, ensure hub and node come from a tested pair in
  `VERSION_COMPATIBILITY_MATRIX.md`;
- if import succeeds but save does not persist, inspect GM save path separately.

### Bundle validate/apply fails

Common error meanings:

- `bundle_too_large`: bundle exceeds the current shipped node bundle contract.
  Today that stable alpha contract is still `8 KB`; the tracked `32 KB`
  rollout is documented in
  `scenehub_node_v1/docs/plans/NODE_V2_LARGE_BUNDLE_32KB_PLAN.md`.
- `bundle_too_large_for_mqtt_admin`: bundle may fit the node storage/HTTP
  path, but does not fit the compact MQTT admin transport budget.
- `runtime_emit_not_supported`: bundle uses a runtime feature not yet shipped.
- `store_failed`: node could not persist the bundle.
- `invalid_json` or schema errors: bundle is malformed.
- identifier/schema errors may also mean a technical id used unsupported
  characters. Current ids are limited to a bounded ASCII whitelist such as
  `open_room_2`, `reader_1`, `phase.locked`.

Actions:

- validate before apply;
- reduce bundle size or remove unsupported sections;
- re-check command ids, event ids, state keys, phase names, timer names and
  driver ids for spaces, quotes, slashes or other unsupported characters;
- reboot after apply when the flow requires activation on next boot;
- use `Load stored bundle` to inspect what is actually on the node.

### Bundle appears applied but logic does not run

Check:

- node is really in `standalone` mode;
- stored bundle metadata matches the intended bundle;
- rule runtime is not paused;
- exported trigger conditions can actually fire on current hardware.

Actions:

- load stored bundle and compare metadata;
- verify pause/resume state;
- test with a minimal known-good example bundle before debugging custom logic.

### Provisioning UI unreachable

Check:

- node has an IP address;
- provisioning auto-close did not already expire for this boot;
- browser is pointed at the node IP, not the hub IP.

Actions:

- reboot node and reopen provisioning immediately;
- use keep-open if needed for longer admin work.

## Safe Recovery Order

Use this order unless a stronger incident procedure exists:

1. Read logs and capture the failing action.
2. Retry the same action once.
3. Reboot the node.
4. Reconnect Wi-Fi and MQTT.
5. Reload compact config in GM.
6. Re-validate bundle.
7. Re-apply bundle.
8. Factory reset only when configuration is already backed up.

## What Not To Assume

- degraded PN532 state does not automatically mean the whole node is broken;
- standalone bundle admin does not replace the GM quest-device contract;
- save-device and apply-bundle are different write paths;
- fallback behavior is not the default alpha authority mode unless explicitly
  configured and tested.

## Escalate When

Escalate to development when:

- the node resets or panics;
- GM save path silently drops imported commands/events;
- `describe_interface` payloads become invalid again;
- storage or NVS failures repeat after reboot;
- degraded state blocks unrelated node functions.
