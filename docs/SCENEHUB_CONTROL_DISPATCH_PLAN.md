# SceneHub Control Dispatch Plan

## Goal

Introduce one shared `scenehub_control` dispatch owner for async write-side
transport work without creating per-endpoint workers or paying large dedicated
task stacks for single HTTP paths.

This plan intentionally starts narrow.

## Why

Current hub behavior is functionally correct, but some async transport-facing
work still runs too close to the `httpd` request thread.

The wrong fix is:

- one worker per endpoint
- one large stack per worker
- duplicated scratch per path

The intended fix is:

- one shared owner inside `scenehub_control`
- one queue
- one task
- one reused scratch context

## Scope

Implemented scope:

- `scenehub_control_device_describe_interface(...)`
- `scenehub_control_device_command_run(...)`

Status:

- `describe_interface` uses the shared dispatch owner
- `manual device command run` uses the same shared owner
- both paths were manually verified after the owner move

## Non-Goals

Do not do any of this:

- move all `scenehub_control` writes behind the owner
- create per-endpoint workers
- add a second owner alongside `scenehub_control`
- allocate one scratch block per queued request
- pay for large stack-local DTOs inside the owner task

## Target Shape

Add an internal dispatch submodule:

- `components/scenehub_control/scenehub_control_dispatch.c`
- `components/scenehub_control/scenehub_control_dispatch.h`

It owns:

- one static queue
- one static task
- one reused owner scratch context for `describe_interface`
- one reused owner scratch context for manual device-command dispatch

The public `scenehub_control` API should stay unchanged.

## Owner Rules

The dispatch owner is for async write-side transport work only.

It should:

- serialize request execution
- keep heavy scratch off task stacks
- return typed result data to `scenehub_control`
- remain internal to the `scenehub_control` component

It should not become a generic application runtime owner.

## Scratch Policy

Owner-held scratch includes:

- one `device_control_ingest_device_t`
- one `describe_interface` JSON buffer
- `scenehub_resolved_device_command_t`
- `command_executor_request_t`

## Execution Model

`scenehub_control_device_describe_interface(...)` should become:

1. validate inputs
2. prepare typed request
3. submit request to the shared dispatch owner
4. wait for typed reply
5. translate reply into `scenehub_control_result_t`

The owner task should do:

1. publish `describe_interface`
2. wait/poll for ingest result
3. extract `device_description`
4. return typed success/failure payload

## Memory Discipline

Requirements:

- no large stack-local DTOs inside the owner task
- no dedicated worker per endpoint
- no duplicated scratch per queued request
- owner scratch must be reused across requests

If the first-pass owner still needs a larger stack than expected, measure why
before increasing it.

## External Contract

The owner move keeps the existing external contract:

- `status = accepted`
- `request_id`

## Success Criteria

This work is complete when:

- `describe_interface` no longer performs its async transport/wait loop in the
  request thread
- manual device-command dispatch no longer performs its async transport hop in
  the request thread
- no per-endpoint worker was introduced
- no large dedicated stack was introduced for a single HTTP path
- public `scenehub_control` and Web UI contracts remain unchanged
- both paths are verified in runtime testing
