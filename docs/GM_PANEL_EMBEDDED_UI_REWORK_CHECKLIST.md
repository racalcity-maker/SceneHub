# GM Panel Embedded UI Rework Checklist

Status: closed

Scope:

- Embedded GM Panel only.
- Vanilla JavaScript bundle under `components/web_ui/assets/gm_panel*`.
- No desktop app work while the desktop UI is frozen.

Goals:

- Remove the low-value `Dashboard` screen from the embedded GM workflow.
- Make `Rooms` the first/default operator screen.
- Keep operator workflow centered on `Room Control` plus the right sidebar.
- Move full manual device control into an admin-only area.
- Fix compact/system device command rendering so relay/MOSFET/IO resources are
  shown completely instead of collapsing to one flat template row.
- Make the sidebar a curated operator quick-access list built from selected
  device resources/actions, not an automatic dump of every manual command on a
  device.
- Use a small admin-side wizard to create those sidebar presets from saved
  Quest Devices and their concrete resources/actions.

Constraints:

- Reuse the existing compact-device normalization logic already used by the
  scenario builder instead of inventing a second expansion path.
- Do not mix operator live-control surfaces with admin editor flows.
- Keep `manual_allowed` as an execution-policy flag, not a sidebar visibility
  flag.
- Treat sidebar entries as operator-facing presets with human-readable labels,
  not raw generated command ids or technical channel names.
- Prefer a guided preset-creation flow over broad checkbox matrices when the
  choice depends on device -> resource/channel -> action/effect.
- Keep HTTP/API behavior documented in `docs/gm_api_contract.md` when UI-facing
  contract changes are required.

## Product Decisions

- [x] Confirm the work targets the embedded GM panel, not the desktop app.
- [x] Confirm `Dashboard` is not the primary operator surface.
- [x] Confirm `Rooms` should become the first/default navigation item.
- [x] Confirm full manual device controls should be admin-only.
- [x] Confirm the operator should keep quick manual actions in the right
      sidebar.
- [x] Confirm the sidebar must show full device resource coverage through
      dropdown/accordion expansion for relays, MOSFETs, outputs and other
      native resources.
- [x] Confirm the sidebar should show only explicitly selected operator presets,
      not every manual-capable command on a device.
- [x] Confirm each sidebar preset needs a normal operator-facing label that can
      differ from raw device/channel/effect ids.
- [x] Confirm sidebar preset creation should use a mini wizard based on saved
      devices and their available resources/actions.

## UI Target Shape

- [x] Remove `Dashboard` from the embedded GM navigation.
- [x] Make `Rooms` the first button and default route/view.
- [ ] Keep `Room Control` as the main operator workspace.
- [x] Add or repurpose one admin-only manual-control view for full device
      commands.
- [x] Rework the right sidebar into curated operator presets instead of one
      flat auto-generated button list.
- [x] Show device name first and keep raw ids in debug/advanced sections only.
- [x] Keep danger/confirmation styling visible for risky manual commands.
- [x] Keep stale/offline/unknown device state obvious enough that operators do
      not confuse it with healthy live state.

## Operator Preset Model

- [x] Add a saved sidebar/quick-access preset model for manual controls.
- [x] Each preset must be configured by choosing:
      device -> resource/channel -> action/command/effect.
- [x] Each preset must allow a custom operator-facing label.
- [x] Each preset may optionally store fixed params needed by the chosen action
      or effect.
- [x] Each preset should be independently ordered and removable.
- [x] Presets must support compact/system resources such as relays, MOSFETs,
      outputs and LED-related actions without exposing raw internal ids in the
      default operator UI.
- [x] Decide whether presets belong inside Quest Device storage or in a
      dedicated GM/UI configuration store before implementation.
      For this slice they live in browser `localStorage` on the controller
      host, not in firmware storage.
- [x] If preset storage changes firmware API/storage shape, document the
      contract before wiring UI behavior to it.
      No firmware API/storage change was introduced in this slice.

## Admin Preset Wizard

- [x] Add a small admin-side wizard for creating sidebar quick-access presets.
- [x] Wizard step 1: choose one saved Quest Device.
- [x] Wizard step 2: choose one concrete resource/channel exposed by that
      device, for example `Relay 3`, `MOSFET 2`, `Output 1`.
- [x] Wizard step 3: choose one action/effect valid for that resource.
- [x] Wizard step 4: fill fixed params when required by the selected action.
- [x] Wizard step 5: enter a human-readable operator label and preview the
      resulting sidebar button/card.
- [x] Saving the wizard result creates exactly one operator preset entry.
- [x] The wizard should hide raw technical ids by default and keep them only in
      advanced/debug affordances if needed.
- [x] The wizard should reuse existing compact normalization logic so compact
      devices and built-in system devices resolve resources/actions the same way
      as the scenario builder.
- [x] After save, presets should be manageable in one list with reorder,
      rename and delete actions.

## Sidebar Behavior

- [x] Build sidebar entries from normalized device/resource data, not from raw
      flat command templates alone.
- [x] Expand compact/system devices into concrete resources/channels:
      `Relay 1..N`, `MOSFET 1..N`, `IO 1..N`, LED resources, and similar.
- [x] Use dropdown/accordion expansion in the admin selection flow so one
      device can expose many resources without flooding the picker UI.
- [x] Show only the chosen operator presets in the live sidebar.
- [x] Keep the live sidebar compact; resource browsing belongs in the admin
      selection/configuration flow, not in the operator surface.
- [x] Replace the earlier generic `show_in_sidebar` idea with a real preset
      list model and mini-wizard flow.

## Reuse Of Existing Logic

- [x] Reuse the compact resource expansion approach from
      `gm_panel_05a_scenario_model.js`.
- [x] Audit which helper functions can be shared directly versus extracted into
      a neutral helper area.
- [x] Avoid duplicating relay/MOSFET/IO channel normalization logic across
      sidebar, admin manual-control view and scenario builder.

## Implementation Slices

- [x] Update navigation/static page shell in `components/web_ui/web_ui_page.c`.
- [x] Update GM panel state/default-view handling in the split JS sources.
- [x] Replace dashboard-specific render/load assumptions with rooms-first
      behavior.
- [x] Rework sidebar rendering in
      `components/web_ui/assets/gm_panel/gm_panel_01c_quest_device_status.js`.
- [x] Add or adapt a full admin manual-control screen if needed.
- [x] Rebuild `components/web_ui/assets/gm_panel.js`.
- [x] Run syntax and bundle freshness checks.

## Validation

- [x] Operator login lands on `Rooms`, not `Dashboard`.
- [x] Operator can still run allowed quick actions from the sidebar.
- [x] Sidebar shows only configured presets, not all manual-capable device
      commands.
- [x] Operator labels are human-readable and do not default to raw technical
      ids unless the admin leaves them unchanged.
- [x] Admin can access the full manual device control area.
- [x] Compact/system devices show all relevant relay/MOSFET/IO resources, not
      only the first/default template.
- [x] Offline or unknown devices never render as healthy `ok`.
- [x] Confirmation still appears for dangerous manual commands.
- [x] `Room Control` remains usable without accidental editor-only controls.

## Closeout

- [x] Mark this checklist closed after implementation and smoke validation.
- [x] Add a concise completed-work entry to `docs/CHANGELOG.md`.
- [x] If user-facing contract/storage behavior changes, update:
      `docs/gm_api_contract.md`
- [x] If operator/admin workflow expectations change, update:
      `docs/ARCHITECTURE.md` and `docs/TESTING.md`
      No contract or policy document changes were required for the browser-local
      preset slice.
