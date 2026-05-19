# SceneHub Node Locking Policy

The node should avoid complex locking by using owner tasks and queues. Locks are
allowed only for short local state protection.

## Main Rules

1. No MQTT publish/subscribe calls while holding hardware locks.
2. No hardware IO calls while holding MQTT transport locks.
3. No storage writes while holding runtime or hardware locks.
4. No rule-engine execution while holding transport locks.
5. Event handlers copy bounded data and return quickly.
6. Provisioning web handlers must request config changes through owner tasks,
   not mutate runtime state under arbitrary web callbacks.
7. New nested lock paths must be documented before implementation.

## Preferred Ownership Model

```text
MQTT task
  -> command queue
  -> node runtime task
  -> hardware command queue / direct validated adapter

hardware/input ISR or poll task
  -> event queue
  -> node runtime task
  -> result/event/status publisher queue
```

The runtime task owns high-level state transitions. Hardware modules own low
level peripheral state.

## Lock Scope

Allowed under locks:

- copy local state;
- update counters/flags;
- reserve/release fixed-pool slots;
- enqueue bounded messages.

Forbidden under locks:

- MQTT publish;
- storage read/write;
- provisioning web request handling;
- JSON parse/print;
- rule bundle validation;
- long hardware effects;
- delays/sleeps;
- callbacks into other owners.

## V2 Rule Engine

The rule engine may have one runtime lock or run as a single owner task.
Prefer the owner-task model.

Rule activation is two phase:

1. Validate and compile candidate rule bundle outside the active runtime lock.
2. Swap active bundle pointer/table under a short lock or owner-task message.

Never mutate the active rule tables in place while rules are executing.

## Event Queue Policy

- Use fixed-size queue items.
- Drop or coalesce non-critical diagnostic events if the queue is full.
- Never drop command terminal results silently.
- Prefer explicit overflow diagnostics over blocking critical tasks.

## Lock Ordering

If locks become unavoidable, use this order:

1. pool/queue bookkeeping lock;
2. runtime state lock;
3. hardware local lock.

Do not acquire transport or storage locks from inside runtime/hardware locks.
Prefer queue messages instead.
