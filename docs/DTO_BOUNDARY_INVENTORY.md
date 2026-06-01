# DTO Boundary Inventory

This inventory classifies the DTO-like structures that are allowed to cross
module boundaries. The rule is simple: a DTO is valid only when it is a boundary
contract. Internal state should stay internal, and Web UI should not consume
storage/domain DTOs directly.

## DTO Rules

- Domain/storage DTOs are owned by their module and may be persisted.
- Core projection DTOs expose a stable, lock-safe read snapshot from runtime
  modules. They are not HTTP contracts.
- Read-model DTOs are the public read-side contract consumed by Web UI and API
  serializers.
- Control DTOs are write-side result or request envelopes.
- Persistent-store structs are owner storage entities first. Reusing them in
  control/web paths is acceptable only while ownership and payload width remain
  clear.
- Internal scratch structs and admin/storage buffers are not boundary DTOs.
  They should stay private to their owner and must not leak into public module
  APIs as convenience return types.
- Web UI JSON writers are serializers, not domain owners.
- A new DTO is suspect if it copies another DTO 1:1 without changing layer,
  ownership, lifetime or payload width.

## Current DTO Classes

| Type / family | Owner | Class | Status | Notes |
|---|---|---|---|---|
| `quest_device_t`, `quest_device_command_t`, `quest_device_event_t` | `quest_device` | domain/storage | keep | Saved capability model. Web UI should use read-model catalog DTOs for listing. |
| `room_scenario_t` and step structs | `room_scenario` | domain/storage | keep | Scenario persistence and validation source. UI should receive projected detail/layout views. |
| `gm_game_profile_t` | `gm_profile_store` | domain/storage | keep | Profile persistence source. Public model name remains `gm_game_profile_t`. |
| `gm_sidebar_preset_t` | `gm_sidebar_store` | persistent-store | watch | Controller-backed GM quick-action store. Acceptable as the owner storage entity, but it should stay narrow and must not silently grow into a second UI/view model. |
| `gm_room_session_*_view_t`, `gm_room_session_projection_view_t` | `gm_core` | core projection | keep | Lock-safe session projection for read-model. Not an HTTP contract. |
| `scenehub_control_result_t` | `scenehub_control` | control envelope | keep | Shared write-side result envelope for HTTP/control responses. |
| `scenehub_control_device_command_info_t` | `scenehub_control` | control response detail | keep | Narrow response metadata for manual command execution. |
| `scenehub_control_device_interface_info_t` | `scenehub_control` | control response detail | keep | Narrow discovery response ownership for device interface JSON. |
| `device_control_ingest_device_t` | `device_control_ingest` | ingest/read snapshot | watch | Useful for diagnostics and read-side projections, but too wide for many narrow operational questions. Prefer focused accessors such as presence or result-summary when callers do not need the full snapshot. |
| `orch_device_entry_t` | `scenehub_read_model` | read-model | keep | Device status projection for dashboard/device views. |
| `orch_control_device_entry_t` | `scenehub_read_model` | read-model | keep | Observed device-control telemetry projection. |
| `orch_quest_device_catalog_entry_t` | `scenehub_read_model` | read-model catalog adapter | watch | Replaces Web UI use of `quest_device_t`; acceptable as a boundary DTO, but should not grow into a second storage model. |
| `orch_room_entry_t` | `scenehub_read_model` | read-model | keep | Room summary/detail card projection. |
| `orch_room_runtime_summary_view_t` | `scenehub_read_model` | read-model hot path | keep | Lightweight runtime refresh contract. |
| `orch_room_runtime_detail_view_t` | `scenehub_read_model` | read-model detail | keep | Wider room detail contract. Must not be polled unnecessarily. |
| `orch_room_scenario_entry_t` | `scenehub_read_model` | read-model summary | keep | Scenario list contract. |
| `orch_room_scenario_detail_t` and nested step/action entries | `scenehub_read_model` | read-model detail/layout | watch | Needed by scenario editor. Keep out of hot runtime paths. |
| `orch_room_scenario_step_schema_t` | `scenehub_read_model` | read-model schema | keep | Editor schema contract. |
| `orch_issue_entry_t` | `scenehub_read_model` | read-model | keep | Issue projection used by dashboard/device/room views. |
| `orch_room_profile_entry_t` | `scenehub_read_model` | read-model | keep | Profile list projection. |
| `orch_registry_snapshot_t` | `scenehub_read_model` | aggregate read-model | watch | Useful for dashboard/system snapshot. Do not add unrelated fields when narrow APIs suffice. |
| `orch_gm_system_summary_t` | `scenehub_read_model` | read-model summary | keep | Dashboard/header summary. |

## Cleanup Targets

- Audit public `device_control_ingest` APIs and replace narrow-question call
  sites with focused accessors instead of returning the full ingest snapshot by
  default.
- Keep `gm_sidebar_preset_t` as a storage owner struct, but revisit control/web
  payload reuse if sidebar preset behavior diverges from the persisted shape.
- Keep `orch_quest_device_catalog_entry_t` narrow. If compact resource parsing
  expands, add a compact-resource read DTO rather than copying storage fields
  into Web UI.
- Keep `orch_room_scenario_detail_t` out of live runtime refresh paths.
- Keep internal scratch structs and admin/storage buffers private. Do not
  promote them into public DTOs just because they already contain "enough"
  fields for one more caller.
- Do not grow `orchestrator_registry.h` into a dumping ground; add family
  headers under `scenehub_read_model/include` instead.
- When a Web UI handler needs a new field, first decide whether it belongs to
  `scenehub_control`, `scenehub_read_model`, or a serializer-only JSON field.
