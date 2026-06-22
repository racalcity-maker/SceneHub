# SceneHub Node v2 Engine Contract

This document freezes the intended behavior of the Node v2 local rule engine.
It exists so schema, compile and runtime work can proceed against one stable
contract instead of re-deciding semantics during implementation.

This is the engine contract for the first serious Node v2 implementation. It is
not yet the final user-facing product contract for every future schema version.

## Core Direction

Node v2 uses a compiled reactive state machine.

It does not use:

- Lua;
- JavaScript;
- a generic scripting VM;
- blocking waits;
- loops or recursion;
- arbitrary expression strings.

Complex logic is still allowed, but only through bounded declarative
constructs:

- triggers;
- conditions;
- actions;
- phase transitions;
- timers;
- bounded branching;
- bounded sequences.

## Rule Ordering

Rule execution order is compile order.

For schema v2 initial implementation this means:

- rules are compiled in the order they appear in `rules[]`;
- when multiple rules match the same event in the same tick, they run in that
  order;
- if later explicit priority is added, it must compile into a deterministic
  final order, but that is not part of the first contract.

This is simple, predictable and easy to inspect.

## State Model

Initial state types:

- `bool`
- `int32`

Strings should be avoided in runtime state for the first real engine version.
They may be allowed later for selected driver scenarios, but they should not be
the default state type.

Reason:

- `int32` is enough for counters, modes, IDs, driver status codes and most
  simple device integrations;
- avoiding free-form string state keeps memory, comparisons and compile limits
  predictable.

## Runtime Model

The engine is event-driven and non-blocking.

The intended flow is:

1. An event is received.
2. Candidate rules are checked in compile order.
3. Conditions read current phase, state, inputs and timer state.
4. Matching rules enqueue or execute bounded actions.
5. Any delayed continuation happens through a later timer or input event.

The engine must never suspend a rule and keep a hidden waiting stack frame in
memory.

## Exact Initial Whitelist

This section answers the question "what exact set do we support first?".

### Triggers

Initial target whitelist:

- `boot`
- `input_edge`
- `input_level`
- `input_hold`
- `all_inputs_level`
- `timer`
- `local_event`

Not in the first real slice:

- arbitrary MQTT-triggered rule entry;
- dynamic driver-generated trigger kinds beyond normal routed events.

### Conditions

Initial target whitelist:

- `phase_is`
- `state_equals`
- `input_equals`
- `event_field_equals`
- `all_inputs_equal`
- `all`
- `any`
- `not`

### Actions

Initial target whitelist:

- `command`
- `set_state`
- `set_phase`
- `emit_event`
- `start_timer`
- `cancel_timer`
- `choose`
- `sequence`

`choose` is the bounded replacement for inline `if/else`.

`sequence` is a bounded ordered action group.

## `choose` and `sequence`

`choose` means:

- evaluate one validated condition immediately;
- run `then` actions if true;
- otherwise run `else` actions if provided.

`sequence` means:

- execute a bounded list of actions in declared order;
- no looping;
- no unbounded nesting;
- no blocking delay semantics.

Both constructs must compile into bounded runtime tables.

## Timers

The engine must support more than one timer mode.

Initial timer model:

- `oneshot`: fires once after duration and stops;
- `repeat`: fires periodically with a fixed interval until cancelled;
- `cooldown`: suppresses repeated execution until a cooldown interval expires.

Expected behavior:

- `start_timer` replaces an existing timer with the same logical name;
- `cancel_timer` is idempotent;
- timer names are bundle-local;
- timers must compile to fixed slots, not dynamic allocations.

Cooldown is important because some room logic wants "trigger once, then ignore
repeats for N ms" without building that pattern manually in every bundle.

## Input Debounce and Hold Ownership

Signal conditioning belongs to `node_event_router`, not to the hot rule engine.

That means:

- hardware-facing input sampling, debounce and hold tracking live near the input
  event source;
- the rule engine consumes normalized events and current input state;
- hold timing should not require the rule engine to poll raw GPIO transitions by
  itself.

Why this is the correct architecture:

- debounce is tied to noisy physical input behavior, not business logic;
- input normalization should be shared by all rules;
- it keeps the engine simpler and prevents duplicate hold/debounce logic inside
  rule evaluation.

Initial input policy:

- debounce is configurable per input and may be disabled;
- `input_edge` and `input_hold` use debounced input state;
- hold tracking resets when debounced state leaves the required target value.

## Waiting Semantics

The engine must not implement a blocking `wait until ...` instruction.

Correct patterns:

- phase change plus later input trigger;
- phase change plus timeout timer;
- `choose` only for immediate branch decisions;
- `repeat` or `cooldown` timers for repeated/time-gated behavior.

Human requirement:

> "Wait until input 4 becomes active, but fail after 10 seconds."

Expected compiled shape:

- `set_phase("waiting_input_4")`
- `start_timer("wait_input_4_timeout", mode=oneshot, duration_ms=10000)`
- one rule continues on `input_edge/input_level` while phase matches
- another rule handles `timer` timeout while phase matches

## Activation Lifecycle

`node.rules.apply` should not hot-activate the bundle in the first engine
contract.

Initial lifecycle:

1. Validate uploaded bundle.
2. Save it as the next candidate bundle.
3. Require reboot for activation.
4. On boot, firmware loads the stored candidate.
5. Firmware validates and compiles it before runtime starts.
6. If compile succeeds, it becomes active and appears in
   `device_description.v2`.
7. If compile fails, the node stays observable and reports inactive/error
   status.

This is intentionally conservative and keeps activation behavior easier to
reason about while the engine is still young.

## SceneHub and Standalone Behavior

If the node is in `standalone` mode and also has Wi-Fi and MQTT connectivity,
it may still accept commands from SceneHub.

Initial policy:

- local standalone rules remain authoritative for local autonomous behavior;
- incoming SceneHub commands are still allowed through normal command handling;
- no special hard lock-out of hub commands in standalone mode;
- if local logic later changes the same output again, that is normal engine
  behavior, not a transport error.

This is a pragmatic first contract. More advanced arbitration can be designed
later if real scenarios need it.

## Conflicts and Output Ownership

For the first engine contract:

- rule-vs-rule conflicts are resolved by compile order;
- later matching rules in the same event tick may overwrite earlier desired
  output state through normal command execution;
- hub commands are external commands, not part of compile-order arbitration.

This is not the most sophisticated model, but it is transparent and stable.

## Initial Capacity Targets

The engine should start conservative but not artificially tiny.

Suggested initial limits:

- max bundle JSON size: `8 KB`
- max rules: `32`
- max actions total across bundle: `128`
- max actions per rule: `12`
- max timers: `16`
- max nested `sequence` / `choose` depth: `4`
- max emitted local event names: `32`
- max resource claims per exported command: `4`
- max phase name length: `24`
- max state keys: `32`

These are not final forever. They are a safe starting contract with room to
grow, especially because the project already prefers static storage and can use
preallocated memory strategies.

If PSRAM-backed static regions are later introduced for compiled tables, the
limits may be increased without changing the engine model.

## Error Surface

For now the API/runtime only needs simple stable outcomes for SceneHub:

- accepted / saved for reboot
- rejected
- failed

Detailed machine-readable diagnostics can be expanded later.

The engine implementation should still log richer internal reasons for local
debugging.

## Events

To keep the first contract simple:

- `local_event` is an internal engine event source used for rule-to-rule flow;
- `emit_event` is the action that produces a node event intended for normal node
  event publication;
- timer firings are internal engine events;
- state changes are internal data changes and do not need to be externally
  published by default.

In other words:

- not every engine event goes to MQTT;
- externally visible events come from explicit `emit_event` or from future
  explicit driver publication policy.

## Driver Direction

The state model and event model must be compatible with future device drivers.

Near-term driver target:

- minimal RFID/NFC reader support first.

The first planned reader direction is a minimal `pn532`-class reader contract
with bounded events and no broad smart-card scripting behavior.

See `NODE_V2_DRIVER_PLAN.md`.

## RFID/NFC Identity Handling

The rule engine should not compare raw RFID/NFC UID strings as a normal core
rule pattern.

Preferred architecture:

1. reader driver reads raw UID bytes;
2. driver normalizes UID into a stable internal representation;
3. driver resolves the UID against configured known cards/tokens;
4. engine receives either:
   - a bounded `int32 token_id`, or
   - a logical event that already represents the known card class.

Examples of acceptable engine-facing forms:

- `reader_1_card_seen` with `token_id = 3`
- `reader_1_master_card`
- `reader_1_reset_card`

Examples of forms to avoid in the first engine version:

- `if uid == "04AABBCCDD"`
- arbitrary string matching in normal rule conditions
- storing raw UID text in generic runtime state for rule comparisons

Why:

- raw UID matching is driver-domain logic, not core engine logic;
- integer token IDs fit the bounded state model;
- logical events are even simpler for many quest-room scenarios;
- raw UID can still be exposed in diagnostics/MQTT payloads if needed.

Recommended first reader policy:

- maintain a configured allowlist of known cards;
- each known card maps to a `token_id`;
- driver may optionally also map a known card to a specific logical event name;
- unknown cards may still emit a generic observed event for diagnostics.
