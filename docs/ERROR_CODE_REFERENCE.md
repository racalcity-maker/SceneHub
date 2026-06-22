# Error Code Reference

This file collects the error codes and failure labels that support is expected
to see during the current alpha phase.

It is intentionally practical, not exhaustive. If a new code starts appearing
in support logs, add it here together with the operator meaning and first
action.

## Rule Bundle Admin

### `bundle_too_large`

Meaning:

- standalone bundle JSON exceeds the current shipped node bundle budget.
- the current stable alpha contract is still `8 KB`, even though `32 KB`
  support is now tracked as a separate rollout plan in
  `scenehub_node_v1/docs/NODE_V2_LARGE_BUNDLE_32KB_PLAN.md`.

First action:

- remove unused sections;
- shorten labels, exports, or repeated action blocks;
- re-validate before applying again.

### `invalid_json`

Meaning:

- payload is not valid JSON or is truncated.

First action:

- reformat the JSON;
- verify the full bundle was pasted or loaded.

### `bundle_too_large_for_mqtt_admin`

Meaning:

- bundle fits neither the compact MQTT admin request path nor the compact
  MQTT `node.rules.get` response path.
- use the node provisioning HTTP flow or GM HTTP-backed bundle workflow for
  wide raw bundle upload/download.

First action:

- retry the same bundle through local provisioning HTTP;
- do not treat this as storage corruption by itself;
- keep MQTT admin for compact bundle operations and small admin actions.

### `store_failed`

Meaning:

- node failed to persist the bundle or related admin state.

First action:

- reboot once;
- retry `Load stored bundle`;
- if repeated, inspect storage/NVS/SPIFFS logs.

### `runtime_emit_not_supported`

Meaning:

- bundle uses a runtime feature that is not enabled in the current node build.

First action:

- remove that construct from the bundle;
- verify the node firmware actually includes the required runtime slice.

## Driver And Hardware State

### `init_failed`

Meaning:

- driver initialization failed, typically PN532 bring-up or bus access.

First action:

- verify the driver should be enabled at all;
- verify I2C pins, address, power, and wiring;
- accept degraded state if the reader is intentionally absent.

### `offline`

Meaning:

- driver or device is not currently reachable.

First action:

- distinguish node offline from subdevice offline;
- if only the reader is offline, the rest of the node may still be usable.

### `degraded`

Meaning:

- node is alive, but one slice is unhealthy or partially unavailable.

First action:

- inspect the status detail string in GM;
- do not assume the whole node needs to be taken out of service.

## MQTT / Control Path

### `rejected`

Meaning:

- command was understood but rejected because of invalid args, unsupported
  command, or state policy.

First action:

- inspect command payload and command template;
- verify the node/hub pair is compatible.

### `failed`

Meaning:

- command started or was accepted but could not complete.

First action:

- inspect the result payload details and nearby node logs;
- retry once only if the side effect is safe.

### `invalid payload`

Meaning:

- hub ingest could not parse or accept the node payload for that topic.

First action:

- verify the hub and node builds are from a tested pairing;
- inspect compact manifest/result payload changes first.

## Storage / Platform

### `ESP_ERR_NVS_NOT_ENOUGH_SPACE`

Meaning:

- NVS partition does not have enough free space for the requested write.

First action:

- reduce stored payload size;
- inspect what persistent data is occupying NVS;
- use factory reset only after backup if recovery is necessary.

### `SPIFFS mount failed`

Meaning:

- SPIFFS partition could not mount, sometimes due to corruption or mismatched
  image/format state.

First action:

- allow one format-and-remount recovery attempt if that path is supported;
- if it repeats, treat as storage incident and capture logs.

## UI / GM Workflow Symptoms

### `node.rules.get` appears successful but nothing changes in GM

Meaning:

- bundle/admin fetch path worked, but the GM device save path did not persist
  imported contract changes.

First action:

- separate `Load stored bundle` from `Save device`;
- verify imported compact config is still visible before closing the modal;
- inspect GM save result and reopen the device to confirm persistence.

### `describe_interface` times out or returns empty import state

Meaning:

- node did not return a valid compact manifest in time, or GM rejected it.

First action:

- retry once;
- inspect hub logs for `invalid payload`;
- inspect node result payload generation.
