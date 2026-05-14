# SceneHub Events Refactor Plan

This temporary plan tracks a cleanup of the shared SceneHub event contract so
that domain event semantics live in `scenehub_events`, while `event_bus`
shrinks back to transport and dispatch responsibilities.

The goal is not to redesign scenario execution. The goal is to separate:

- what happened in the system
- how that event is delivered

## Current State

- [x] `scenehub_events` already exists as the shared event-contract component:
      [components/scenehub_events/include/scenehub_events.h](/d:/Projects/SceneHub/components/scenehub_events/include/scenehub_events.h:1)
- [x] `event_bus` already depends on `scenehub_events`, not the other way
      around:
      [components/event_bus/include/event_bus.h](/d:/Projects/SceneHub/components/event_bus/include/event_bus.h:1)
- [x] The current contract is structurally a SceneHub event model, but it is
      now fully named as a SceneHub contract:
      `scenehub_event_type_t`, `scenehub_event_payload_type_t`,
      `scenehub_event_device_status_payload_t`, `scenehub_event_t`
- [x] The current event struct mixes different concerns in one payload:
      - domain event type and domain payload
      - event-bus posting priority
      - MQTT/web-style `topic`
      - generic text `payload`
- [x] Consumers already duplicate domain helpers that should belong to the
      event contract:
      - event type naming in
        [gm_room_session_events.c](/d:/Projects/SceneHub/components/gm_core/session/gm_room_session_events.c:15)
      - source-id extraction in
        [gm_room_session_events.c](/d:/Projects/SceneHub/components/gm_core/session/gm_room_session_events.c:49)
- [x] Producers currently hand-build event payloads in multiple places:
      - [device_control_ingest_events.c](/d:/Projects/SceneHub/components/device_control_ingest/device_control_ingest_events.c:11)
      - [hardware_io_io_runtime.c](/d:/Projects/SceneHub/components/hardware_io/hardware_io_io_runtime.c:29)
      - [sd_storage.c](/d:/Projects/SceneHub/components/sd_storage/sd_storage.c:25)
      - [audio_player_decode.c](/d:/Projects/SceneHub/components/audio_player/audio_player_decode.c:31)

## Direction

- [x] `scenehub_events` should own domain event types, payload structures,
      event constants, builders, validation, and debug/string helpers.
- [x] `event_bus` should own queueing, dispatch, handler registration, job
      queue, pooling, stats, and backpressure only.
- [x] `gm_core`, `command_executor`, `device_control_ingest`, and read-side
      consumers should depend on `scenehub_events` for event semantics.
- [x] `scenehub_events` must not depend on `event_bus`.

## Important Boundaries

- [x] Keep transport priority in `event_bus`.
      Priority affects delivery policy, not event meaning. Prefer:
      `event_bus_post_priority(const scenehub_event_t *event, ...)`
      over embedding event-bus priority into the domain event struct.
- [x] Keep MQTT topic mapping in `mqtt_core`.
      Topic-to-event translation is protocol/bridge logic, not domain-event
      ownership:
      [mqtt_core_bridge.c](/d:/Projects/SceneHub/components/mqtt_core/mqtt_core_bridge.c:11)
- [x] Keep scenario meaning in `gm_core`.
      Wait matching, reactive branches, cooldowns, transitions, command-result
      progression, and branch state stay outside `scenehub_events`.
- [x] Do not mix this refactor with queue-policy changes or scenario-behavior
      changes.

## Target Ownership

- [ ] `scenehub_events`
      - [x] domain event enums
      - [x] event payload structs
      - [x] event constants and field length limits
      - [x] source-string constants like `"event"` / `"result"`
      - [x] event builders/factories
      - [x] event validation helpers
      - [x] event type to string helpers
      - [x] simple match helpers for `device_id`, `action_id`, and source kind
- [ ] `event_bus`
      - [ ] queue
      - [ ] priority scheduling
      - [ ] handler registration
      - [ ] dispatch
      - [ ] job queue
      - [ ] memory pool
      - [ ] overflow/backpressure handling
      - [ ] bus stats and slow-handler warnings
- [ ] `gm_core`
      - [ ] scenario wait semantics
      - [ ] reactive branch behavior
      - [ ] cooldown logic
      - [ ] transition logic
      - [ ] command-plan and command-result progression

## Proposed Contract Shape

- [ ] Introduce SceneHub-native names in `scenehub_events`, for example:
- [x] Introduce SceneHub-native names in `scenehub_events`, for example:
      - [x] `scenehub_event_type_t`
      - [x] `scenehub_event_payload_type_t`
      - [x] `scenehub_event_device_status_payload_t`
      - [x] `scenehub_event_device_runtime_payload_t`
      - [x] `scenehub_event_device_control_payload_t`
      - [x] `scenehub_event_t`
- [x] Keep temporary compatibility aliases during migration so this can be
      rolled out incrementally instead of as a flag day.
- [ ] Re-evaluate whether `payload_type` remains necessary after the semantic
      helpers land. It may still be useful as a compatibility field during the
      migration even if it becomes partially redundant with `type`.

## Shared Helpers To Move Into `scenehub_events`

- [x] `scenehub_event_type_to_string(...)`
- [x] `scenehub_event_payload_type_to_string(...)`
- [x] `scenehub_event_source_id(...)`
- [x] `scenehub_event_matches_device(...)`
- [x] `scenehub_event_matches_action(...)`
- [x] `scenehub_event_is_valid(...)`
- [x] `scenehub_event_is_device_status(...)`
- [x] `scenehub_event_is_device_runtime(...)`
- [x] `scenehub_event_is_device_control(...)`
- [x] `scenehub_event_is_device_control_event(...)`
- [x] `scenehub_event_is_device_control_result(...)`

These helpers are currently either duplicated or implicitly reimplemented in
consumers, especially in
[gm_room_session_events.c](/d:/Projects/SceneHub/components/gm_core/session/gm_room_session_events.c:15).

## Non-Goals

- [x] Do not move `event_bus_stats_t` into `scenehub_events`.
- [x] Do not move handler registration or queue internals into `scenehub_events`.
- [x] Do not move MQTT topic maps into `scenehub_events`.
- [x] Do not move scenario wait matching or reactive-branch logic into
      `scenehub_events`.
- [x] Do not require a single big-bang rename before helper extraction starts.

## Rollout

- [x] P0. Record the target architecture and boundaries in this plan.
- [x] P1. Normalize `scenehub_events` naming without breaking callers.
      SceneHub-native event names and field/source constants now exist in
      [scenehub_events.h](/d:/Projects/SceneHub/components/scenehub_events/include/scenehub_events.h:1),
      while existing `event_bus_*` type names remain as compatibility aliases.
      - [x] add SceneHub-native typedef names beside existing `event_bus_*`
            names
      - [x] add event-specific constants for field sizes and source strings
      - [x] keep the current header path stable
- [x] P2. Add a real `scenehub_events.c` implementation file.
      The component now has a compiled implementation file:
      [scenehub_events.c](/d:/Projects/SceneHub/components/scenehub_events/scenehub_events.c:1)
      with string helpers, validation, semantic checks, and base builders for
      status/runtime/device-control events.
      - [x] string helpers
      - [x] validation helpers
      - [x] simple match helpers
      - [x] builder/factory helpers
- [x] P3. Migrate producers to event builders instead of hand-populating
      structs.
      Producers now build events through `scenehub_events` helpers, while
      keeping existing event-bus posting calls and priorities unchanged.
      - [x] `device_control_ingest`
            [device_control_ingest_events.c](/d:/Projects/SceneHub/components/device_control_ingest/device_control_ingest_events.c:1)
            now uses `scenehub_event_make_device_status`,
            `scenehub_event_make_device_runtime`, and
            `scenehub_event_make_device_control_*` builders.
      - [x] `hardware_io`
            [hardware_io_io_runtime.c](/d:/Projects/SceneHub/components/hardware_io/hardware_io_io_runtime.c:1)
            now uses `scenehub_event_make_device_control_event` and only adds
            compatibility `topic` / `payload` fields afterward.
      - [x] `sd_storage`
            [sd_storage.c](/d:/Projects/SceneHub/components/sd_storage/sd_storage.c:1)
            now uses `scenehub_event_make_text` for card-state events.
      - [x] `audio_player`
            [audio_player_decode.c](/d:/Projects/SceneHub/components/audio_player/audio_player_decode.c:1)
            now uses `scenehub_event_make_text` for
            `SCENEHUB_EVENT_AUDIO_FINISHED`.
      - [x] `web_ui`
            [web_ui_system.c](/d:/Projects/SceneHub/components/web_ui/web_ui_system.c:1)
            now uses `scenehub_event_make_text` for
            `SCENEHUB_EVENT_WEB_COMMAND`.
- [ ] P4. Migrate consumers to semantic helpers instead of local knowledge.
      - [x] `gm_core`
            [gm_room_session_events.c](/d:/Projects/SceneHub/components/gm_core/session/gm_room_session_events.c:1)
            now uses `scenehub_event_type_to_string`,
            `scenehub_event_source_id`, and
            `scenehub_event_is_device_control*` helpers instead of local
            duplicated event semantic helpers.
      - [x] `orchestrator_core`
            [orchestrator_timeline.c](/d:/Projects/SceneHub/components/orchestrator_core/timeline/orchestrator_timeline.c:1)
            now uses `scenehub_event_is_device_*` helpers for typed event
            handling.
      - [x] `scenehub_read_model`
            [orchestrator_registry.c](/d:/Projects/SceneHub/components/scenehub_read_model/registry/orchestrator_registry.c:1)
            now uses `scenehub_event_is_device_*` helpers for cache
            invalidation decisions instead of reading transport-era payload
            typing fields directly.
      - [ ] `error_monitor`
      - [x] `command_executor`
            [command_executor_result.c](/d:/Projects/SceneHub/components/command_executor/command_executor_result.c:1)
            now uses `scenehub_event_is_device_control_result(...)` instead of
            spelling out transport-era field checks inline.
- [x] P5. Trim `event_bus` to transport-only coupling.
      The public bus surface in
      [event_bus.h](/d:/Projects/SceneHub/components/event_bus/include/event_bus.h:1)
      now uses `scenehub_event_t`, while queue, pool, priority, jobs, and bus
      stats remain local transport concerns inside
      [event_bus.c](/d:/Projects/SceneHub/components/event_bus/event_bus.c:1).
      Event-bus logs also use `scenehub_event_type_to_string(...)` instead of
      transport-era integer event codes. Posting also validates
      `scenehub_event_is_valid(...)` before queueing unless
      `CONFIG_SCENEHUB_EVENT_BUS_SKIP_EVENT_VALIDATION` is enabled.
      - [x] update handler typedefs to use the SceneHub-native event type names
      - [x] keep posting priority and queue logic local to `event_bus`
      - [x] make `event_bus` stop owning domain helper knowledge entirely
- [ ] P6. Remove compatibility aliases only after the migration is complete and
      the old `event_bus_*` naming no longer carries its weight.
      Initial alias-burndown is underway:
      - [x] primary function signatures and queue storage now use
            `scenehub_event_t`
      - [x] MQTT bridge surfaces now use `scenehub_event_type_t` /
            `scenehub_event_t`
      - [x] `gm_core`, `command_executor`, timeline, and basic consumers now
            use SceneHub-native event typedefs
      - [x] runtime/event modules now use `SCENEHUB_EVENT_*` constants instead
            of transport-era `EVENT_*` aliases
      - [x] migrate tests/docs off compatibility aliases
      - [x] delete compatibility aliases from
            [scenehub_events.h](/d:/Projects/SceneHub/components/scenehub_events/include/scenehub_events.h:1)

## Exit

- [ ] Delete this temporary plan after the event contract is stabilized and the
      lasting ownership rules are reflected in permanent architecture docs.
