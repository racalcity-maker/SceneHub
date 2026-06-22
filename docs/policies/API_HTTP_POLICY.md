# API HTTP Policy

This document defines the preferred HTTP API shape for SceneHub controller, web UI, GM panel, and node provisioning surfaces.

The goal is not theoretical purity. The goal is predictable behavior, cleaner contracts, and fewer accidental leaks through URLs, browser behavior, and logs.

## Core Rules

### 1. `GET` is read-only

`GET` handlers must not mutate persistent state, runtime state, or side-effecting control flow.

Allowed for `GET`:
- status
- snapshots
- listings
- schema
- read-only lookup/filter/pagination

Not allowed for `GET`:
- save config
- dispatch command
- restart/reboot
- reset/factory reset
- stop/start runtime features
- publish/send/play/seek/set volume

If an endpoint changes state, it must not be `GET`.

### 2. State-changing requests use `POST`/`PUT`/`PATCH`/`DELETE`

Preferred mapping:
- `POST` for commands, actions, imports, and create-style operations
- `PUT` for full replacement of a known resource
- `PATCH` for partial updates
- `DELETE` for deletion

For embedded surfaces where only `GET` and `POST` are practical, `POST` is acceptable for all state-changing operations.

### 3. Secrets and mutable config do not go into URL query strings

Do not place these into query parameters:
- passwords
- tokens
- credentials
- Wi-Fi secrets
- mutable config payloads

Reasons:
- browser history
- access logs
- screenshots
- copied URLs
- debug tooling
- accidental sharing

Use request body instead.

### 4. Mutation payloads belong in request body

For state-changing endpoints, payload should be carried in request body, preferably JSON.

Preferred:

```http
POST /api/config/wifi
Content-Type: application/json

{
  "ssid": "RoomNet",
  "password": "secret",
  "host": "scenehub.local"
}
```

Avoid:

```http
GET /api/config/wifi?ssid=RoomNet&password=secret&host=scenehub.local
```

### 5. Query string is for read selection, not command payload

Query params are appropriate for:
- filtering
- sorting
- pagination
- selecting a read-only view
- optional non-mutating lookup knobs

Query params are not the preferred vehicle for:
- save/update payloads
- command dispatch payloads
- configuration changes

### 6. Prefer structured JSON over ad hoc query parsing for mutable operations

For mutable endpoints, JSON body is preferred because it:
- scales better as fields grow
- is easier to validate
- is easier to document
- avoids long URLs
- reduces ad hoc parsing logic

## Result Semantics

HTTP method policy does not replace command lifecycle semantics.

For command/result meaning, use:
- [COMMAND_RESULT_SEMANTICS.md](/d:/Projects/SceneHub/docs/COMMAND_RESULT_SEMANTICS.md)

This policy only defines transport shape and request hygiene.

## Transition Policy

The codebase may temporarily contain legacy endpoints that do not match this document.

When touching an endpoint for feature work or bugfixing:
1. remove state-changing `GET` if present
2. move mutable payload from query string into request body
3. keep backward compatibility only if truly needed
4. prefer typed/structured parsing over permissive ad hoc parsing

This should be treated as progressive convergence, not a flag-day rewrite.

## Non-Goals

This document does not require:
- immediate conversion of every existing endpoint
- REST purity for its own sake
- replacing all custom parsers at once
- changing stable read-only endpoints that are already correct

## Current Preferred Shape

Good examples:
- `GET /api/status`
- `GET /api/config`
- `GET /api/led-effects-schema`
- `POST /api/config/wifi` with JSON body
- `POST /api/config/mqtt` with JSON body
- `POST /api/config/logging` with JSON body
- `POST /api/gm/device/command/run`
- `POST /api/audio/play`
- `POST /api/audio/seek`

Bad examples:
- state-changing `GET`
- passwords in URL
- config mutation through query strings
- command dispatch through bookmarkable URLs
