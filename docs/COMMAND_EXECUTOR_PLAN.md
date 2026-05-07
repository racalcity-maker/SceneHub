# Command Executor

The command executor is the SceneHub boundary between scenario/runtime decisions and command side effects.

This document used to be the P2.2 migration plan. P2.2 is now closed for the current MQTT/system-audio paths, so this file documents the current shape and the remaining roadmap.

## Responsibility Split

Scenario runtime:

- accepts events;
- advances branch/session state;
- decides that a command action should run;
- records lightweight wait state for result-required commands.

Command executor:

- resolves the target command;
- dispatches through the correct backend;
- creates request ids;
- tracks result-required pending commands;
- normalizes `accepted/done/failed/rejected/timeout`;
- emits terminal timeout events back to runtime.

## Current Backends

- SceneHub-native MQTT quest devices.
- Built-in `system_audio`.
- Built-in `system_relay`.
- Built-in `system_mosfet`.

Planned:

- more local `hardware_io` targets such as universal GPIO/input;
- Universal IO Node over the same MQTT contract;
- optional RS485 transport later.

## Result Rules

- `accepted` never advances a scenario step or reactive action.
- `done` advances a result-required command wait.
- `failed`, `rejected`, and `timeout` fail the current command wait according to runtime policy.
- `result_required=false` lets runtime continue after successful dispatch.
- Timeout ownership lives in the executor.

## Implemented

- `components/command_executor` owns MQTT, system-audio, system-relay and system-mosfet command dispatch.
- `gm_core` calls the executor instead of directly executing MQTT/audio side effects for scenario commands.
- Manual/API command paths use the executor policy gate.
- Device-control ingest normalizes result statuses before runtime consumes them.
- Shared status helpers live in `quest_common/include/scenehub_command_result.h`.
- Executor tracks pending result-required requests and emits timeout result events.
- Session stop/reset cleanup cancels matching pending requests.
- `DEVICE_COMMAND_GROUP` passes per-command params for non-result-required commands.
- `DEVICE_COMMAND_GROUP` rejects result-required commands until batch result aggregation exists.
- GM session lifecycle audio cleanup goes through executor-backed `system_audio` stop.
- Local relay/MOSFET commands route through `hardware_io`; current local relay
  pulse and MOSFET fade/pulse continue locally after scenario dispatch because
  these system commands are not result-required.

## Remaining

### Hardware IO Expansion

Base relay/MOSFET/input/GPIO targets are implemented. Remaining local hardware targets:

- optional `system_led`

MQTT retained-message payloads now use fixed retained-table storage instead of
allocating/freeing a payload buffer for each retained update.

Expected command behavior:

- invalid channel returns `rejected`;
- timeout/failure emits terminal result.

### Batch Result Policy

`DEVICE_COMMAND_GROUP` currently supports only commands that do not require individual command results.

Before enabling result-required groups, define:

- all-success vs any-success;
- first-failure behavior;
- per-command timeout;
- aggregate request id/result shape.

### Retries

Retry policy is intentionally not implemented yet. Add it only after command execution metrics and failure modes are clearer.

## Non-Goals

Do not add these through the executor:

- local scenario engine;
- command queue UI;
- complex retry policy UI;
- ESP-NOW transport;
- RS485 transport before the base hardware IO path is stable.
