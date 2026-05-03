# Changelog

All notable project changes are documented in this file.

## Unreleased

### Changed

- Renamed product-facing project/configuration identifiers to `SceneHub`.
- Replaced broad project settings with `CONFIG_SCENEHUB_*` settings while keeping MQTT broker terminology for the MQTT module itself.
- Updated setup AP, mDNS, Web UI titles, default hostname, and tooling documentation to use SceneHub naming.
- Removed archived project cleanup references and old transition-plan documentation from the active project tree.
- Kept MQTT broker terminology for `mqtt_core` and kept the Helix third-party MP3 decoder as active audio functionality.

## 2026-05-03

### Added

- Expanded `quest_backend` test coverage across room catalog, room scenarios, GM API, GM room sessions, orchestrator snapshots/timeline/audit/registry, event bus, service status, config store utilities, OTA manager, audio player state, web UI contracts, web UI handlers, and integration quest flows.
- Added a `quest_backend` custom partition table and default test sdkconfig so the enlarged backend test binary fits the configured flash layout.
- Added test adapters for web UI HTTP handling so request/response behavior can be tested without a live HTTP server.
- Added OTA manager backend injection hooks so OTA state transitions can be tested without real flash OTA operations.

### Changed

- Increased room scenario validation code storage so long validation codes such as `REACTIVE_BRANCH_TRIGGER_REQUIRED` are preserved without truncation.
- Tightened reactive branch validation to check the first physical branch step rather than skipping disabled steps.
- Updated system audio command validation so unsupported background audio formats are rejected before file lookup.
- Updated backend test registration and component dependencies for the broader test suite.
- Updated testing documentation paths for the current project location.

### Fixed

- Fixed the truncated validation code regression where `REACTIVE_BRANCH_TRIGGER_REQUIRED` could appear as `REACTIVE_BRANCH_TRIGGER_REQUIRE`.
- Fixed test project flash/partition configuration issues caused by the expanded test binary size.
