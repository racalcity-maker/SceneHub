# SceneHub Node Memory Policy

SceneHub Node firmware controls physical outputs. Memory behavior must be
predictable, especially when Node v2 adds custom JSON/rule execution.

## Main Rules

- No dynamic allocation in hardware command execution.
- No dynamic allocation in interrupt handlers.
- No dynamic allocation in rule-engine hot paths.
- No dynamic allocation in MQTT command dispatch after payload parse.
- Prefer static storage, fixed pools or startup-only allocation.
- Large scratch/config/editor/import structs must not live on task stacks.
- If a struct is wide enough to materially grow `main`, `httpd` or transport
  task stack usage, move it into owner-held static storage immediately instead
  of keeping a stack-local copy.
- Prefer PSRAM for large non-DMA buffers when the target has PSRAM.
- For large scratch storage, prefer PSRAM-first placement with a clean fallback
  to internal RAM/static storage when PSRAM is unavailable or unsafe for that
  path.
- Keep DMA/peripheral buffers in internal DMA-capable memory.
- Admin/config JSON import may allocate only in bounded, fail-cleanly paths.

## Memory Classes

| Class | Rule | Examples |
| --- | --- | --- |
| `startup-static` | static or allocate once | MQTT buffers, event queues, rule slots |
| `runtime-hot` | no malloc/free/cJSON | command dispatch, rule tick, event match |
| `hardware-critical` | no allocation | relay/MOSFET/input operations |
| `transport` | fixed packet buffers | MQTT RX/TX payloads |
| `admin-config` | bounded allocation allowed | rule upload, config import |
| `provisioning` | bounded allocation allowed | AP setup web UI, config forms |
| `storage` | bounded allocation allowed | load/save config or rule bundle |

## V1 Targets

V1 should use fixed storage for:

- MQTT RX payload buffer;
- MQTT TX payload buffer;
- command parse scratch;
- result envelope buffer;
- heartbeat/status envelope buffer;
- idempotency cache;
- hardware state snapshot;
- device_description publish buffer or streamed writer.
- config migration scratch;
- LED editor/provisioning overlay scratch;
- LED worker task stack/snapshot owner storage;
- large admin/import/export temporary structs.

## V2 Targets

V2 rule execution must compile uploaded JSON into bounded runtime tables:

- trigger table;
- condition table;
- action table;
- timer table;
- string/value pool;
- runtime state table.

Rule bundle limits must be explicit:

- maximum rules;
- maximum triggers;
- maximum actions per rule;
- maximum JSON payload size;
- maximum string bytes;
- maximum timers;
- maximum nested expression depth.

If a bundle exceeds limits, reject it before activation.

## JSON Policy

- MQTT command payload may be parsed from a fixed RX buffer.
- Rule upload/admin paths may use a parser, but must validate size first.
- Provisioning web UI may allocate bounded request/response buffers.
- Do not store parser object pointers after validation.
- Do not run rules from raw JSON trees.
- Generate heartbeat/status/result/event JSON into bounded output buffers.

## Failure Behavior

On allocation or capacity failure:

- reject the command/config cleanly;
- publish `status=rejected` with `error.code=invalid_request` or
  `internal_error`;
- keep the previous active config/rules;
- do not partially activate a rule bundle.

## LED Runtime Lifecycle

- `node_hw_led_init()` is boot-only.
- Live LED wiring reconfigure is forbidden on the current v1 runtime path.
- Do not call `node_hw_led_init()` again after startup unless an explicit
  `deinit/restart` sequence is added for effect tasks, strip handles and
  static task storage.
- Preset updates may be applied at runtime; hardware wiring changes still
  require restart or a future dedicated deinit path.

## PSRAM Guidance

If PSRAM is available:

- use it for large JSON import scratch;
- use it for rule bundle storage and compiled tables;
- use it for large static admin/config/editor scratch before consuming internal
  RAM;
- keep an explicit internal-RAM/static fallback for targets or code paths where
  PSRAM is not present or not appropriate;
- do not use it for DMA buffers;
- do not put tiny lock/event primitives there unless the RTOS target supports
  it safely.
