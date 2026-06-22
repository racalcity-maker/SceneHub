# Node Describe Interface Refactor Plan

## Goal

Keep large `describe_interface` payloads from forcing large permanent buffers
across normal runtime paths.

The desired end state:

- routine command, event, status and result traffic uses compact buffers;
- large metadata payloads are accepted only where needed;
- `quest_device.device_description_json` remains the persistent owner of saved
  manifests;
- `device_control_ingest` does not reserve giant per-device blobs for rare
  metadata responses;
- future node growth toward `16` channels and richer LED metadata does not
  require endlessly inflating every runtime buffer.

## Scope

This plan covers:

- MQTT payload budgets for rare large metadata responses;
- control-layer handling of `describe_interface`;
- ingest/runtime memory layout;
- future split between compact execution manifest and rich UI metadata.

This plan does not cover:

- node command runtime behavior;
- LED effect implementation details;
- broader `gm_core` decomposition work.

## Current Problem

Today the same general pipeline is asked to handle both:

- small operational messages:
  commands, events, command results, status, heartbeat;
- large metadata messages:
  `describe_interface` / `device_description`.

That leads to two bad outcomes:

1. transport limits are too small for the rich manifest; or
2. every layer inherits a large fixed-size buffer just to support a rare path.

The second outcome is the one to avoid.

## Current Inventory

### Large-payload producer path on the node

- `scenehub_node_v1/esp_idf/components/node_control/node_control_describe.c`
  `execute_describe_interface(node_control_result_t *result)` builds the
  manifest into `result->data_json` and wraps it as
  `{"device_description": ... }`.
- `scenehub_node_v1/esp_idf/components/node_mqtt_transport/node_mqtt_command.c`
  keeps one reusable global `node_control_result_t s_result` for command
  execution.
- `scenehub_node_v1/esp_idf/components/node_mqtt_transport/node_mqtt_publish.c`
  formats `/result` JSON into the static `s_tx_payload` buffer and includes
  `result->data_json` as `"data"` for `describe_interface`.

### Large-payload consumer path on the hub

- `components/device_control_ingest/device_control_ingest_apply.c`
  `dci_apply_result_text_locked()` copies `/result.data` into
  `slot->state.result_data_json`.
- `components/device_control_ingest/include/device_control_ingest.h`
  `device_control_ingest_device_t` permanently owns:
  - `result_data_json[DEVICE_CONTROL_INGEST_RESULT_DATA_JSON_MAX_LEN]`
  - `event_args_json[DEVICE_CONTROL_INGEST_EVENT_ARGS_JSON_MAX_LEN]`
- `components/scenehub_control/scenehub_control_dispatch.c`
  `scenehub_control_dispatch_describe_interface(...)` sends the request through
  the dispatch owner and consumes the transient metadata response with
  `device_control_ingest_take_describe_interface_data(...)`.

### Persistent manifest owner

- `components/quest_device/include/quest_device.h`
  `quest_device_t` owns the saved
  `device_description_json[QUEST_DEVICE_DESCRIPTION_JSON_MAX_LEN]`.
- `components/quest_device/quest_device_json.c`
  parses, saves and exports that JSON.
- `components/scenehub_device_command_resolver/scenehub_device_command_resolver.c`
  consumes `device->device_description_json` as the runtime command-resolution
  contract for compact devices.

### Read-side and diagnostics consumers that currently pull the full ingest DTO

- `components/scenehub_read_model/orch_issue_builder.c`
  reads the full ingest device through `device_control_ingest_get_device(...)`.
- `components/scenehub_read_model/orch_device_view.c`
  reads both `quest_device.device_description_json` and ingest device state.
- `components/scenehub_read_model/registry/orchestrator_registry.c`
  also reads full ingest snapshots.

### Current architectural mismatch

- Large payload support is needed mostly for `describe_interface`, not for
  routine command results, events, status or heartbeat.
- Today the large metadata path still inflates steady-state per-device ingest
  storage because `describe_interface` reuses the same general `result_data`
  ownership path as ordinary command execution.
- The correct long-term ownership split is:
  transport may accept a large metadata payload, control may hold it
  transiently, and `quest_device` persists it only after import/apply.

## Desired Architecture

### Runtime traffic

- `event_args_json`: small
- ordinary `result_data_json`: moderate
- live ingest/read-model/runtime state: compact

### Metadata traffic

- `describe_interface`: allowed to use a larger transient budget
- manifest storage: owned by `quest_device.device_description_json`
- large metadata blobs: request-local or control-layer temporary ownership, not
  permanent per-device hot-path state

### Long-term manifest shape

- compact execution manifest:
  command ids, event ids, targets, match rules, minimal schema/type/options
- rich UI metadata:
  labels, hints, effect catalogs, editor extras, optional descriptive fields

## Refactor Checklist

- [x] Separate payload budgets by purpose instead of sharing one large limit
  across all device-control paths.
  Target split:
  `event_args_json` small, ordinary command `result_data_json` moderate,
  `device_description` large.
- [ ] Keep MQTT transport capable of receiving large metadata payloads without
  forcing equally large permanent per-device ingest buffers.
- [x] Rework `describe_interface` into a metadata-specialized path rather than
  treating it as an ordinary command result blob.
- [x] Move large `describe_interface` response ownership to a transient
  request-local/control-layer buffer or cache instead of permanent
  `device_control_ingest_device_t` state.
- [ ] Keep `quest_device.device_description_json` as the persistent owner of
  saved device manifests after import/apply.
- [x] Reduce ordinary `device_control_ingest` steady-state memory again after
  the metadata path is split:
  `event_args_json` small, `result_data_json` moderate, no giant fixed metadata
  blob in the hot path.
- [ ] Audit stack and scratch usage again after the split so large metadata
  payload support does not reintroduce giant stack-local DTOs or broad shared
  scratch retention.
- [ ] Design a two-layer manifest model for future growth:
  compact execution manifest for runtime/contract needs, and separate rich UI
  metadata/effect catalog for labels, hints and editor extras.
- [ ] Revisit overall payload ceilings after the split.
  Goal: transport may stay larger than runtime state buffers, instead of all
  layers inheriting one monolithic maximum.

## Suggested Execution Order

1. Make budget ownership explicit with separate constants for:
   `event args`, `ordinary result data`, `device description`.
2. Introduce a transient metadata-response path for `describe_interface`.
3. Stop storing large metadata blobs inside ordinary ingest steady-state.
4. Re-shrink normal ingest/runtime buffers after the metadata path is stable.
5. Re-measure stack/scratch/memory pressure.
6. Design and implement compact-vs-rich manifest split if channel/effect growth
   continues.

## PR1 Touch Points

The first refactor slice should likely touch these areas:

- `components/device_control_ingest/include/device_control_ingest.h`
- `components/device_control_ingest/device_control_ingest_apply.c`
- `components/scenehub_control/scenehub_control_devices.c`
- `components/quest_device/include/quest_device.h`
- `components/mqtt_core/mqtt_core_internal.h`

The immediate objective for that slice:

- keep transport capable of receiving a larger metadata payload;
- stop treating large `describe_interface` data as an ordinary steady-state
  ingest result blob;
- preserve `quest_device.device_description_json` as the only long-lived
  manifest owner after import/apply.

## Current Progress

Completed in the first implementation slice:

- ordinary `device_control_ingest.result_data_json` was reduced to a moderate
  size instead of carrying full manifest budgets;
- `describe_interface` payloads are no longer stored in ordinary steady-state
  per-device ingest result storage;
- large `describe_interface` payloads now go through a dedicated transient
  metadata cache and are consumed through a take-and-clear path by
  `scenehub_control_device_describe_interface(...)`;
- regression coverage was added for:
  ordinary small command results staying in steady-state ingest storage,
  and large `describe_interface` payloads being routed through transient
  metadata ownership instead.

Still pending after that slice:

- review whether current MQTT transport ceilings are still appropriate after
  the split;
- keep `quest_device.device_description_json` as the only durable manifest
  owner after save/import flows;
- decide whether the transient metadata cache should remain inside ingest or
  move further toward a narrower control-owned pending-response path in a later
  refactor.

## Success Criteria

- Large `describe_interface` payloads no longer require giant fixed buffers in
  routine runtime structures.
- `device_control_ingest_device_t` stays sized for live operational state, not
  worst-case metadata.
- `quest_device` remains the saved manifest owner.
- Transport can accept rare larger metadata payloads without making the whole
  system memory-heavy by default.
