# Node V2 Large Bundle 32 KB Plan

This document defines the work required to move Node v2 standalone bundles
from the current `8 KB` alpha budget to a practical `32 KB` HTTP-admin
workflow without violating the node owner, memory and locking policies.

It is a forward plan. It does not change the current shipped contract until
the listed phases are implemented and verified.

## Current Reality

Current code/documented limits:

- `NODE_RULE_BUNDLE_MAX_LEN = 8192`
- provisioning rules HTTP body is effectively bounded by
  `NODE_PROVISIONING_POST_BODY_CAPACITY = 10241`
- MQTT admin payload uses
  `NODE_MQTT_ADMIN_ARGS_MAX = NODE_RULE_BUNDLE_MAX_LEN + 256`
- ordinary command/result/admin envelopes still rely on bounded fixed DTO
  buffers and must not be widened casually
- schema/compile still parse the full raw bundle through bounded `cJSON`
  validation/compile paths

This means the current firmware can support a stable `8 KB` bundle contract,
but not an honest `32 KB` bundle contract end to end.

## Target Contract

Target product result:

- node accepts, validates, stores and re-loads standalone bundles up to
  `32 KB`
- large bundle workflow is HTTP-first
- large bundle workflow does not depend on MQTT admin transport
- runtime hot paths remain allocation-free
- large bundle support does not widen ordinary small result DTOs
- large bundle support remains PSRAM-first and fail-cleanly on memory pressure

## Non-Goals

This plan does not require:

- runtime hot-swap of active compiled bundle
- arbitrary-size bundles beyond explicit fixed limits
- replacing the current engine model with scripting or streaming runtime
  interpretation
- making MQTT the preferred path for large bundle upload/download

## Policy Constraints

The implementation must preserve these rules:

- no dynamic allocation in rule-engine hot paths
- no large stack-local admin/import/export objects
- large transient admin payloads must live in owner-held scratch/cache,
  preferably PSRAM-first
- runtime DTOs stay compact; rare large payloads must not inflate steady-state
  result slots
- lock scopes must stay short; no large IO/serialization under broad owner
  locks

## Main Architectural Decision

`32 KB` bundle support should be implemented as:

- `storage budget`: `32 KB`
- `HTTP admin budget`: `32 KB`
- `MQTT admin budget`: keep compact, smaller than `32 KB`
- `rules.get` raw bundle path: dedicated large-response path, not normal small
  `node_control_result_t.data_json`

In other words:

- storage and HTTP should support `32 KB`
- MQTT may continue to support only smaller bundle admin requests
- oversize MQTT bundle admin requests must be rejected explicitly

This keeps the architecture honest instead of pretending one transport fits
all payload sizes equally well.

## Required Workstreams

### 1. Split The Limits

Today one constant effectively drives too many paths.

We need separate explicit limits for:

- `NODE_RULE_BUNDLE_STORE_MAX_LEN`
- `NODE_RULE_BUNDLE_HTTP_MAX_LEN`
- `NODE_RULE_BUNDLE_MQTT_MAX_LEN`
- `NODE_RULE_BUNDLE_GET_RESPONSE_MAX_LEN`

Target first values:

- store: `32768`
- HTTP validate/apply/get: `32768`
- MQTT validate/apply: keep smaller, for example `8192` or `12288`
- raw bundle get response: `32768` plus envelope overhead when needed

This split is mandatory. Without it, one broad limit change keeps creating
accidental pressure across unrelated owner surfaces.

### 2. Large HTTP Request Body Path

Provisioning/admin body handling must become explicitly large-bundle capable:

- raise rules-specific HTTP body limit to `32 KB`
- keep unrelated config/LED/NFC endpoints on their smaller current limits
- keep the large body buffer as provisioning-owner scratch, PSRAM-first
- do not place `32 KB` buffers on `httpd` stack or in broad always-live DTOs

Implementation direction:

- keep one shared provisioning post-body owner buffer
- make it large enough for `32 KB` rules admin only
- guard allocation failure cleanly with `no_mem`

### 3. Large `rules.get` Response Path

This is the biggest structural issue.

Current `rules.get` behavior is convenient, but the raw bundle should not be
forced through ordinary small result buffers.

Required direction:

- separate `rules.get metadata` from `rules.get raw bundle`
- or keep one logical operation but serialize it through a dedicated
  transient-cache/streaming path

Acceptable implementations:

1. HTTP-only raw bundle response with direct `httpd` send from owner-held
   scratch/cache
2. metadata JSON plus separate endpoint for raw bundle body
3. chunked/streamed HTTP response if that reduces peak copy pressure

Not acceptable:

- expanding every normal control/admin result DTO to `32 KB+`
- copying the raw bundle multiple times through generic result structs

### 4. MQTT Admin Contract For Large Bundles

MQTT admin should remain available for compact operations:

- `node.rules.clear`
- `node.rules.pause`
- `node.rules.resume`
- `node.reboot`
- `node.rules.get` metadata if useful

For `validate` / `apply`:

- either keep MQTT support only up to the smaller MQTT limit
- or reject explicitly when payload exceeds MQTT admin budget

Expected explicit error behavior:

- `bundle_too_large_for_mqtt_admin`
- or equivalent stable code, distinct from generic `bundle_too_large`

This preserves support clarity:

- bundle fits node but not MQTT admin path
- bundle does not fit node contract at all

Those are different failures and should not collapse into one vague rejection.

### 5. Compile / Parse Memory Budget

`cJSON` parsing a `32 KB` bundle is feasible only if the path is treated as
admin-config memory, not runtime memory.

Required work:

- measure peak memory during parse + schema + compile on real ESP32-S3 + PSRAM
- ensure schema/compile temporary allocations stay PSRAM-first
- keep failure clean if PSRAM is absent or too fragmented
- verify no hot/runtime owners inherit large parser state

If `cJSON` remains too heavy at `32 KB`, later follow-up options are:

- two-stage parsing for only the required sections
- a narrower custom parser for known schema shapes

But that is a fallback plan, not the first move. First, measure the current
bounded admin path honestly.

### 6. UI / GM Workflow

For the node provisioning UI and GM modal:

- file upload/paste must support `32 KB`
- status messages must distinguish transport/body-limit failures from schema
  failures
- `Load stored bundle` must remain truthful for what is really on the node
- GM should not assume MQTT is the right path for a large raw bundle

Likely product direction:

- large raw bundle edit/apply/get remains HTTP-driven from GM device edit modal
- quick MQTT admin actions remain small operational controls only

### 7. Tests

Minimum verification for closing the `32 KB` rollout:

- validate a `31-32 KB` bundle via local provisioning HTTP
- apply it, reboot, load it back, compare metadata and raw size
- verify compile success/failure remains explicit
- verify insufficient-memory failures remain clean and non-destructive
- verify MQTT admin still works for compact commands after the change
- verify ordinary status/result/event payloads do not grow unexpectedly

Recommended extra coverage:

- one synthetic large bundle near the size cap
- one intentionally oversize bundle
- one large bundle with many rules but still within compile table limits

## Delivery Phases

### Phase A - Limit Split And Contracts

- add separate store/http/mqtt/get constants
- update docs and error-code guidance
- keep runtime behavior unchanged except cleaner explicit rejections

### Phase B - Large HTTP Upload Path

- raise rules HTTP body path to `32 KB`
- move all wide request handling into provisioning-owner PSRAM-first scratch
- validate/apply over HTTP up to `32 KB`

### Phase C - Large Raw Bundle Read Path

- rework `rules.get` raw bundle response
- stop using ordinary small result buffers for raw large bundle body
- keep metadata path compact

### Phase D - Memory/Compile Hardening

- measure parse/compile memory on target board
- verify PSRAM-first admin allocations
- tighten failure handling and logs

### Phase E - GM/Admin UX Alignment

- make GM modal use the correct large-bundle path
- keep inline feedback explicit for body-limit vs schema vs storage failures

## Acceptance Criteria

We can claim `32 KB` bundle support only when all are true:

- bundle storage limit is really `32 KB`
- HTTP validate/apply/get really handle `32 KB`
- raw bundle read path is honest and stable
- MQTT path has explicit documented limits and failure mode
- runtime hot path remains allocation-free
- no broad result DTO was inflated just to carry rare large payloads

## Recommended First Implementation Slice

The first real code slice should be:

1. split bundle limits into store/http/mqtt/get constants
2. raise HTTP rules body path to `32 KB`
3. keep MQTT smaller and reject oversize MQTT bundle admin explicitly
4. rework `rules.get` so raw bundle does not depend on ordinary result DTO

That is the minimum honest path to start supporting larger bundles without
breaking the current architecture.
