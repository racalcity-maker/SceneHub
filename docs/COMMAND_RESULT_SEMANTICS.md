# COMMAND_RESULT_SEMANTICS

This document defines command-result status meanings across:

- node/device command results
- scenario waiting behavior
- hub-side manual HTTP command responses
- audit/timeline correlation by `request_id`

## Core Statuses

Command result statuses in SceneHub are:

- `done`: command completed synchronously or reached terminal success.
- `started`: runtime accepted and launched a long-running action. Non-terminal.
- `accepted`: command was accepted by a queue or remote dispatch path. Non-terminal.
- `failed`: terminal execution failure.
- `rejected`: terminal validation or policy rejection.
- `timeout`: terminal hub-side timeout while waiting for a required result.

Scenario behavior:

- `result_required=true` waits for terminal success.
- `started` does not advance the scenario.
- `accepted` does not advance the scenario.
- only terminal `done` advances the scenario.
- `failed`, `rejected`, and `timeout` fail the waiting scenario step.

## Device / Runtime Result Rules

- `done` means the action is complete from the command issuer's perspective.
- `started` means the action has begun but is still running.
- `accepted` means the command entered an async queue/dispatch path and is still
  pending.
- `started` and `accepted` are both pending, non-terminal states.
- When `result_required=true`, a pending state must eventually be followed by a
  terminal result:
  - `done`
  - `failed`
  - `rejected`
  - `timeout`

## Scenario Semantics

Scenario/runtime treats both `started` and `accepted` as pending states.

Allowed lifecycle examples:

- `done`
- `started -> done`
- `started -> failed`
- `accepted -> started -> done`
- `accepted -> done`
- `accepted -> rejected`

Not allowed:

- treating `accepted` as terminal success
- treating `started` as terminal success
- inferring terminal success from the absence of an error

## Manual HTTP Command Responses

Manual HTTP command responses in the hub are a dispatch envelope, not a full
remote lifecycle mirror.

That means:

- the initial HTTP response tells the caller what happened on the hub/control
  side
- remote runtime completion may happen later
- the initial HTTP response must not pretend to be terminal remote success

### Write-side envelope status

For manual HTTP responses, `status` means:

- `done`:
  - synchronous local hub-owned action completed
- `accepted`:
  - async remote dispatch succeeded on the hub side
- `failed`:
  - dispatch/setup/publish failed on the hub side
- `rejected`:
  - validation/policy rejected on the hub side
- `timeout`:
  - hub-side wait path timed out before dispatch could complete

### Remote lifecycle metadata

Manual HTTP responses may additionally expose:

- `request_id`
- optional `remote_status`

Rules:

- `request_id` is the correlation key between:
  - HTTP manual action
  - transport dispatch
  - remote command result
  - audit/timeline rows
- `remote_status`, when present, describes remote/runtime state and does not
  replace the write-side `status`
- typical async manual response is:
  - `status = accepted`
  - `request_id = ...`
- if `remote_status` is absent, remote terminal state is unknown at HTTP
  response time

Example:

```json
{
  "status": "accepted",
  "request_id": "cmd_123456"
}
```

Future-compatible example:

```json
{
  "status": "accepted",
  "request_id": "cmd_123456",
  "remote_status": "started"
}
```

## Audit / Timeline Semantics

Audit and timeline should remain append-only.

Preferred event shape:

1. dispatch record
2. remote lifecycle record(s)

Example sequence:

- dispatch accepted
- remote started
- remote done

UI may group these by `request_id`, but storage should not rewrite an earlier
dispatch row into a terminal completion row.

## Controller Compatibility Notes

- controller/runtime treats both `started` and `accepted` as pending states.
- node implementations may use either `started -> done|failed|rejected` or
  `accepted -> done|failed|rejected`.
- pending states must eventually be followed by a terminal result when
  `result_required=true`.
