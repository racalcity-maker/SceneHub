# Changelog

## 2026-05-18

- Split the overloaded embedded GM Panel source part
  `gm_panel_07_loaders_and_runtime_actions.js` into narrower families for
  static/view loading, room runtime refresh, version/invalidation refresh, and
  runtime/manual actions. The behavior is unchanged, but the split sources are
  easier to maintain and now align better with the intended ownership seams.
- Reworked the embedded GM Panel operator flow around `Rooms`: removed the
  `Dashboard` navigation entry, made `Rooms` the default view, and kept device
  quick actions in the right sidebar instead of a broad all-device overview.
- Turned the embedded `Devices` area into an admin-side `Device Controls`
  screen with a mini wizard for sidebar quick actions. Admins now choose a
  saved device, a concrete resource/channel, an action/effect, optional fixed
  params, and a human-readable operator label.
- Replaced the old auto-generated sidebar manual-button dump with curated
  quick-action presets. The live sidebar now shows only configured operator
  presets, grouped by device, while danger/confirmation behavior still follows
  the saved Quest Device command policy.
- Reused the existing compact/system device normalization path so built-in
  relay/MOSFET/IO devices and compact SceneHub Node manifests expand into
  concrete resources instead of collapsing to one template-level button.
- Added browser-local persistence for embedded GM sidebar presets through
  `localStorage` on the controller host, avoiding any firmware API or SD-card
  storage change in this slice.
- Fixed the embedded quick-action wizard so resource/action dropdowns populate
  correctly for compact and system devices, and tightened the admin preset UX
  into a denser step-card layout with live preview updates and better default
  selection flow.
- Hardened embedded GM live refresh against invalid `/api/gm/state` snapshots:
  the panel now keeps the last usable room state instead of replacing it with
  an incomplete payload, and websocket invalidation refresh now logs and
  exposes the underlying error message instead of a generic ws failure badge.
- Fixed the embedded GM bundle manifest after splitting `gm_panel_07`: the
  actual CMake-side `GM_PANEL_PARTS` list now includes `07b/07c/07d`, so the
  firmware-embedded `gm_panel.js` matches the locally rebuilt bundle and no
  longer drops runtime refresh functions such as
  `refreshGMByInvalidationSlices`.
- Restored embedded GM runtime helper compatibility after the panel split:
  room/scenario runtime text paths still referenced the legacy
  `questDeviceCommandName` / `questDeviceEventName` helpers, so opening a room
  after scenario selection could fail during render. The compatibility wrappers
  now map those calls to the current scenario helper names.
- Moved embedded GM sidebar quick actions out of browser-local `localStorage`
  into controller-backed SD storage at `/sdcard/quest/gm_sidebar_presets.json`.
  The GM panel now loads presets through dedicated backend APIs, supports
  export/import/load flows, and offers one-time import of legacy browser
  presets into controller storage.
- Added controller-side validation for sidebar presets and tightened shared
  command param validation so flat/system commands are checked alongside
  compact manifest commands. This closes the gap where presets or scenarios
  could save `system_audio.play` without `params.file` or other required
  command arguments and only fail later at runtime.
- Hardened scenario save on the backend: scenario payload saves now run through
  scenario validation before persistence, so invalid command params are
  rejected on save instead of being stored and discovered only after start.
- Split `components/gm_core/sidebar/gm_sidebar_presets.c` into smaller sidebar
  preset source parts for core state/validation, JSON import-export, and SD
  storage persistence. This keeps the new controller-backed quick-action store
  out of god-file territory and removes an unused helper warning from
  `scenehub_control_sidebar_presets.c`.
- Moved the long-lived GM sidebar preset table out of internal RAM into PSRAM
  and removed `GM_SIDEBAR_PRESET_MAX_ITEMS` stack snapshots from the
  list/export/import JSON path. This recovers roughly 21 KB of internal RAM
  and removes a likely `httpd` panic source when the GM panel loads quick
  actions.
- Audited DTO/buffer ownership boundaries: the inventory now covers persistent
  store entities such as `gm_sidebar_preset_t`, explicitly flags the wide
  `device_control_ingest_device_t` snapshot as a watch item, and records
  follow-up backlog work for narrow accessor APIs and admin/storage stack
  discipline.
- `Start game` room-action failures now preserve and surface scenario
  validation causes instead of collapsing to a generic `invalid_request`.
  Failed game start validation writes `scenario_last_error`, `scenehub_control`
  maps it to `scenario_invalid`, and the web JSON error response now includes a
  human-readable `message` field.
- Fixed an embedded GM scenario editor regression where the scenarios view
  could open a summary-only scenario entry as if it were full detail,
  collapsing the editor to an empty `Main` branch. The panel now treats
  `Scenarios` as a full-detail cache requirement and fetches layout detail for
  a scenario before opening the editor when needed.
- Moved `gm_control` into `gm_core/control` and folded its public header into
  `gm_core/include`. The old standalone `gm_control` component is gone; the
  `room_unhealthy` start-game preflight now lives in `scenehub_control`, which
  avoids creating a new `gm_core <-> scenehub_read_model` dependency cycle.

## 2026-05-16

- Completed the compact SceneHub Node manifest v2 migration. SceneHub now
  treats Node devices as compact resources plus command/event templates,
  preserves `device_description.manifest_version=2` without per-channel command
  expansion, and keeps scenario `command_id` equal to the command template id.
- Updated Device Setup, scenario editor rendering, runtime command resolution,
  read-model catalog DTOs and the reference Python device client for compact
  Node manifests. Flat `commands[]` / `events[]` remain only as the custom
  Quest Device path, not as a SceneHub Node compatibility layer.
- Started the compact Node manifest v2 migration: SceneHub now preserves raw
  `device_description` data and the GM panel shows compact node summaries
  instead of expanding node resources into command rows.
- Added strict compact Node manifest identity validation and rejected GPIO
  metadata at the SceneHub import boundary.
- Split scenario editor layout streaming out of
  `components/web_ui/orchestrator/web_ui_orchestrator.c` into
  `orchestrator_scenario_layout_writer.*`. The endpoint contract is unchanged,
  but the HTTP handler no longer embeds the large layout JSON writer.
- Started consolidating temporary planning documents into `KNOWN_ISSUES.md` so
  active work is tracked in one place and completed plan history does not keep
  accumulating.
