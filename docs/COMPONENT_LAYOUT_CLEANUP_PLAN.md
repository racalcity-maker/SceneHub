# Component Layout Cleanup Plan

This temporary plan tracks source-tree cleanup for components whose root
directory is still flatter than it needs to be after the recent backend split.

It does not track cross-component ownership moves. Those belong in
`ARCHITECTURE.md`, component-specific plans, or `CHANGELOG.md`.

## Goals

- [x] Reduce file sprawl in component roots by grouping obvious file families.
- [x] Keep public include contracts stable while improving source discoverability.
- [x] Avoid semantic refactors during layout cleanup; move files, not behavior.

## Boundaries

- [x] This cleanup may:
      - [x] create subdirectories inside existing components
      - [x] move `.c` files and private headers into those subdirectories
      - [x] update `CMakeLists.txt` source lists and local include paths
- [x] This cleanup must not:
      - [x] change public header names in `include/` without strong reason
      - [x] mix behavioral refactors with file moves
      - [x] introduce deep or decorative nesting without a real file-family gain

## Priorities

- [x] `gm_core`
      - [x] `session/` for `gm_room_session*`
      - [x] `api/` for `gm_api.c`
      - [x] `profile/` for `gm_game_profile.c`
      - [x] `timer/` for `gm_timer.c`
      - [x] `hint/` for `gm_hint.c`
- [ ] `scenehub_read_model`
      - [ ] `views/` for `orch_*_view.c`, including `orch_room_profile_view.c`
      - [ ] `builders/` for `orch_snapshot_builder.c` and
            `orch_issue_builder.c`
      - [x] `registry/` for `orchestrator_registry.c` and its private header
      - [ ] decide whether `orch_common.c` stays at root or moves under a small
            shared family
- [x] `room_scenario`
      - [x] `json/` for `room_scenario_json*`
      - [x] `storage/` for `room_scenario_persistence.c`
      - [x] keep `room_scenario_validation.c` with the model component as the
            structural/domain validation layer; do not mix it with
            `scenehub_scenario_validation`, which already owns
            SceneHub-specific environment/runtime checks as a separate
            component
      - [x] keep `room_scenario.c` and `room_scenario_types.c` at root unless a
            clearer family appears
- [ ] `audio_player`
      - [ ] group runtime/decode/mixer/output/status/volume sources into
            clearer one-level families
      - [ ] keep `audio_player.c`, `audio_player_assets.c`, and the Helix
            wrapper entry points easy to discover from the root
      - [ ] do not churn `third_party/helix`
- [x] `scenehub_control`
      - [x] directory regrouping is no longer needed: the component is already
            split into focused `*_rooms`, `*_profiles`, `*_scenarios`, and
            `*_devices` sources, and the root stays small enough to navigate

## Root Families

- [x] Treat root component name families like `scenehub_*`, `gm_*`, `room_*`,
      and `quest_*` as intentional namespaces, not immediate rename targets.
- [ ] Do not move component directories themselves under umbrella folders like
      `components/scenehub/` without a separate build-migration plan for
      explicit component discovery.
- [ ] If root-level regrouping is ever needed, do it as a dedicated migration:
      first switch to explicit component-dir registration, then move folders,
      then update documentation.

## Rollout

- [x] P0. Establish cleanup rules and candidate components.
- [x] P1. Reorganize `components/gm_core` into obvious file-family folders.
      `gm_core` is now grouped into `session/`, `api/`, `profile/`, `timer/`,
      and `hint/`, with session-private headers living next to the sources that
      own them.
- [ ] P2. Reorganize `components/scenehub_read_model`.
- [x] P3. Reorganize `components/room_scenario`.
      `room_scenario` now groups `room_scenario_json*` under `json/` and
      `room_scenario_persistence.c` under `storage/`, while keeping
      `room_scenario_validation.c`, `room_scenario.c`, and
      `room_scenario_types.c` at the root as the model/domain layer.
- [x] P4. Drop `components/scenehub_control` from directory-regrouping scope.
      The backend extraction already left it in a readable flat layout.
- [ ] P5. Reorganize `components/audio_player` if the earlier passes stay
      low-risk.

## Guardrails

- [x] Prefer 1-level grouping like `session/` or `views/`; avoid multi-level
      trees unless the component already justifies them.
- [x] Keep private headers near the source family that owns them.
- [x] Leave tiny components alone.
- [x] If a component is already readable with a small flat root, prefer keeping
      it flat over introducing decorative folders.
- [x] Update this plan as each component is cleaned up.
- [x] Keep root component names stable during layout cleanup; treat component
      renames or umbrella-folder moves as a different class of change.

## Exit

- [ ] Delete this temporary plan after the component layouts are stabilized and
      any lasting conventions are captured in permanent docs, or after the
      remaining flat roots are explicitly accepted as-is.
