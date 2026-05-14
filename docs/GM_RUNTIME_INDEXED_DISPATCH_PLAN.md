# GM Runtime Indexed Dispatch Plan

This temporary plan tracks the next runtime follow-up after the move to
cause-driven wakeups and one-shot deadline timers.

The current GM runtime no longer polls on a fixed interval, but event routing
is still broader than it needs to be. Some event causes still wake runtime work
that then scans more sessions or branches than are actually relevant.

## Current State

- [x] Runtime wakeup is cause-driven instead of fixed-interval polling.
- [x] Deadline wakeups are driven by an explicit one-shot timer.
- [ ] Event dispatch is not fully indexed yet.
      Current gaps:
      - command result events can still scan waiting branches broadly
      - flag changes do not yet use targeted waiter routing
      - device-event waits and reactive triggers still rely on branch scans
      - nearest-deadline scheduling still comes from a broad session/branch scan

## Goal

- [ ] Route runtime work to the narrowest possible target set for each cause.
- [ ] Replace broad branch scans with explicit wait indexes where the payoff is
      clear and the ownership model stays simple.
- [ ] Keep all indexes owned by `gm_core`, not by `event_bus`.

## Non-Goals

- [x] Do not redesign scenario semantics.
- [x] Do not move runtime state ownership out of `gm_core`.
- [x] Do not build one universal mega-index for every wait shape in one pass.

## Priority Order

1. `request_id -> session/branch`
2. `flag_name -> session/branch`
3. `device/event key -> session/branch`
4. deadline registry follow-up, if the broad nearest-deadline scan becomes a
   measured problem

## Why This Order

- Command-result waits already have a stable key: `request_id`.
- Result events are terminal and high-value, so narrowing them is low-risk and
  directly useful.
- Flag waiters are also stable and easy to model.
- Device-event waits are more complex because of:
  - `WAIT_ANY`
  - `WAIT_ALL`
  - reactive triggers
  - optional source-id matching

## Rollout

- [x] P0. Record the target and boundaries in this plan.
- [x] P1. Add a command-result fast path keyed by `request_id`.
      Scope:
      - narrow command-result routing to the specific waiting branch when
        `request_id` is known
      - avoid broad branch scans for `DEVICE_CONTROL result` events
      Notes:
      - this begins as a targeted fast path before a full persistent index
      - current implementation uses a lightweight runtime lookup helper to
        route result events to the matching waiting branch
- [ ] P2. Add a flag waiter index keyed by `flag_name`.
      Progress so far:
      - [x] add a targeted `FLAG_CHANGED` fast path that narrows routing to
            branches that either wait on the flag or use a reactive
            `flag_changed` trigger
      - [x] allow targeted `WAIT_FLAGS` branches to resume directly from the
            flag event path once the wait condition is satisfied
      - [ ] decide whether this should become a persistent runtime index or
            stay a lightweight lookup helper
- [ ] P3. Add a device-event waiter index for direct event routing.
      Progress so far:
      - [x] add a targeted device-event fast path that narrows routing to
            branches whose wait metadata or reactive trigger can cheaply match
            the incoming device event
      - [ ] decide whether this should evolve into a persistent runtime index
            or remain a lightweight prefilter before exact matching
- [ ] P4. Re-evaluate whether deadline indexing is worth the added complexity.

## Acceptance

- [ ] Command-result events no longer scan unrelated waiting branches.
- [ ] Flag changes can wake only the sessions/branches that actually wait on
      that flag.
- [ ] Device-event routing is measurably narrower than a broad branch scan.
- [ ] Runtime ownership remains understandable and debuggable.
