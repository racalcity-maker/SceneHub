# Node V1 Runtime Audit Plan

This file tracks runtime and control-path issues confirmed during audit, plus a short list of items that still need explicit verification before code changes.

## Scope

- `scenehub_node_v1`
- runtime correctness
- LED worker/control paths
- MQTT result/status behavior
- provisioning/config robustness
- control/transport architecture boundaries

## Confirmed P0 / P1 Fixes

### P0 Correctness

- [x] Guard `node_hw_led` active state with synchronization.
  - Confirmed in `components/node_hardware_io/led/node_hw_led.c`.
  - `active_effect`, `active_config`, `effect_active`, and `effect_seq` are written in `node_hw_led_run_effect()` and `stop_effect()` without synchronization.
  - The worker task copies the same fields without synchronization.
  - Risk: mixed effect config during rapid preview / quick effect switching.
  - Target fix: protect the active snapshot with the strip mutex or a dedicated critical section.

- [x] Fix `led.off` result semantics.
  - Confirmed in `components/node_control/node_control.c`.
  - `execute_led_off()` currently returns `started`.
  - Expected semantics for current contract:
    - `led.off` -> `done`
    - `led.solid` -> `done`
    - `led.blink` -> `started`
    - `led.breathe` -> `started`
    - `led.effect` -> `started`

- [x] Publish MQTT status after successful async state-changing commands.
  - Confirmed in `components/node_mqtt_transport/node_mqtt_command.c`.
  - Status publish is currently gated by `strcmp(status, "done") == 0`.
  - That misses `started` and `accepted`, including LED and other long-running effects.
  - Minimum fix:
    - publish after `done`
    - publish after `started`
    - publish after `accepted`
  - Preferred fix:
    - add semantic flags such as `publish_status` or `state_changed` to `node_control_result_t`.

- [x] Reject repeated `node_hw_led_init()` calls at runtime.
  - Confirmed in `components/node_hardware_io/led/node_hw_led.c`.
  - Policy already says LED wiring init is boot-only.
  - Code still tries to tear down handles and `memset()` strip state on repeat call.
  - Risk: loss of worker/task state and unsafe pseudo-reinit.
  - Target fix: add `s_led_initialized` guard and return `ESP_ERR_INVALID_STATE` on repeated init.

### P1 Robustness / Safety

- [x] Clamp and sanitize LED editor numeric fields before save.
  - Confirmed in `components/node_provisioning/node_provisioning_config_api.c`.
  - `apply_led_editor_fields()` casts incoming integers to `uint16_t` without upper bounds.
  - Risk: wrap/truncation for `repeat_count`, `size`, `intensity`, `density`, `fade`, and related timing fields.
  - Target fix: add `sanitize_led_editor_config()` before `node_config_save_led_editor()`.

- [x] Escape JSON string output in provisioning/config/schema responses.
  - Confirmed in provisioning JSON writers.
  - Labels and names are interpolated with raw `%s`.
  - Risk: broken JSON when user-entered labels contain quotes or backslashes.
  - Target fix: central `json_escape_string()` helper and consistent use in all writers.

- [x] Protect shared provisioning GET response buffers.
  - Confirmed in `components/node_provisioning/node_provisioning_config_api.c`.
  - `s_config_json` is shared across GET handlers without a lock.
  - Risk: concurrent GET clients can interleave response generation.
  - Target fix options:
    - lock around shared response buffer
    - request-local allocation
    - chunked writer path

- [x] Add one-time guard for runtime init after network.
  - Confirmed in `main/node_main.c`.
  - `init_runtime_after_network()` has no explicit `s_runtime_initialized` guard.
  - Normal boot paths are distinct, but a hard guard is still needed.

## Confirmed Architectural Debt

- [x] Move provisioning write/reset paths behind a dedicated admin-control owner module.
  - Confirmed in `components/node_provisioning/node_provisioning_config_api.c`.
  - First pass implemented with `components/node_admin_control/`.
  - Provisioning POST handlers now submit typed requests instead of calling:
    - `node_config_save()`
    - `node_config_save_led_editor()`
    - `node_control_update_led_config()`
    - `node_config_reset_wifi()`
    - `node_config_factory_reset()`
    - `esp_restart()`
    directly from HTTP handler context.
  - `node_admin_control` now serializes:
    - base config save
    - LED editor save
    - LED runtime apply
    - Wi-Fi reset
    - factory reset
    - deferred restart

- [ ] `g_node_prov` is shared mutable state without a single owner.
  - Confirmed in `components/node_provisioning/node_provisioning.c` and `components/node_provisioning/node_provisioning_config_api.c`.
  - `g_node_prov.status` is mutated from Wi-Fi/IP event handlers.
  - `g_node_prov.config` is mutated from HTTP handlers.
  - `g_node_prov.callbacks` and web-server lifecycle also live in the same global state object.
  - Existing mutexes protect HTTP scratch buffers only; they do not make `g_node_prov` an owner-disciplined state machine.
  - This is acceptable for current v1 scale, but it is still open architectural debt and the main reason write/reset paths are not fully closed.
  - Short-term target:
    - keep `g_node_prov` as shared state if needed
    - but move all provisioning write/reset/apply transitions behind `node_admin_control`
    - do not expand direct mutation from HTTP handlers further
  - Progress:
    - provisioning GET config handlers now read a snapshot through `node_admin_control_get_config(...)`
    - remaining shared-state debt is mostly around broader provisioning lifecycle/status ownership, not config response building
  - v1 decision:
    - config read/write ownership is now considered sufficiently closed for v1
    - remaining `g_node_prov` shared state is accepted as deferred lifecycle debt:
      - Wi-Fi / IP status fields
      - web server handle
      - netif handles
      - provisioning callbacks
    - do not expand direct mutation of this lifecycle state further without a separate provisioning-runtime owner model

- [x] Split `components/node_control/node_control.c`.
  - Confirmed god-file pressure.
  - First pass implemented:
    - `node_control_json.c`
    - `node_control_led.c`
    - `node_control_internal.h`
    - `node_control_output.c`
    - `node_control_mosfet.c`
    - `node_control_describe.c`
  - Public API stayed in `node_control.c`; JSON parsing and LED command path are now separate compile units.
  - Output, MOSFET, and describe/status paths are now also separate compile units.
  - Remaining split pressure in `node_control.c` is now low enough for v1 unless new command families are added.
  - Suggested split:
    - `node_control.c`
    - `node_control_json.c`
    - `node_control_led.c`
    - `node_control_mosfet.c`
    - `node_control_output.c`
    - `node_control_describe.c`

- [x] Split `components/node_hardware_io/led/node_hw_led.c`.
  - Confirmed god-file pressure.
  - First pass implemented:
    - `node_hw_led_internal.h`
    - `node_hw_led_driver.c`
    - `node_hw_led_worker.c`
    - `node_hw_led_effects_basic.c`
    - `node_hw_led_effects_motion.c`
    - `node_hw_led_effects_noise.c`
  - Driver/pixel/color/palette helpers are now separate from the main orchestration file.
  - Worker lifecycle, effect start/cancel/done orchestration, and PSRAM stack allocation are now separate from the main orchestration file.
  - Effect implementations are now split by family:
    - basic
    - motion
    - noise
  - Remaining split pressure in `node_hw_led.c` is now mostly around init/public API only and is acceptable for v1 unless the module grows again.
  - Suggested split:
    - `node_hw_led.c`
    - `node_hw_led_driver.c`
    - `node_hw_led_worker.c`
    - `node_hw_led_effects_basic.c`
    - `node_hw_led_effects_motion.c`
    - `node_hw_led_effects_noise.c`

- [x] Split `components/node_config/node_config.c`.
  - Confirmed mixed responsibilities:
    - base config
    - NVS I/O
    - legacy migrations
    - LED editor overlay
  - First pass implemented:
    - `node_config_legacy.h`
    - `node_config_migrations.h`
    - `node_config_internal.h`
    - `node_config_defaults.c`
    - `node_config_led_overlay.c`
    - `node_config_storage.c`
    - `node_config_migrations.c`
  - Factory defaults and shared LED preset-default helpers are now out of the main migration/load router.
  - LED editor overlay load/save path now lives in a dedicated compile unit with its own scratch buffers.
  - Storage-tail helpers (`save`, `reset_wifi`, `factory_reset`, `needs_provisioning`) are now out of the main migration/load router.
  - Legacy layout typedefs now live in a dedicated internal header.
  - v1-v8 migration functions now live in a dedicated compile unit.
  - Remaining split pressure is now concentrated in:
    - `node_config_load_or_default()` routing
    - the long legacy-version dispatch chain inside that router
  - Suggested split:
    - `node_config.c`
    - `node_config_legacy.h`
    - `node_config_migrations.h`
    - `node_config_defaults.c`
    - `node_config_migrations.c`
    - `node_config_led_overlay.c`
    - `node_config_storage.c`

## Verification Still Required

These items were not proven false, but they were not fully closed by spot-checking. They need an explicit audit pass before any fix plan is converted into code.

### Control / Policy

- [ ] Define the node-side owner path for provisioning/admin writes.
  - This is the node equivalent of the hub `scenehub_control` write boundary.
  - First pass is now implemented with `node_admin_control`.
  - Remaining gap:
    - `g_node_prov.config` reads are still not fully routed through one owner boundary everywhere
    - shared provisioning state is still broader than the new write-side owner path
  - v1 closure:
    - config read/write/apply/reset/restart boundary is now considered closed enough for v1
    - broader provisioning lifecycle ownership is deferred and should be treated as a future refactor, not as an open blocker

- [ ] Verify command policy enforcement end-to-end.
  - Required fields:
    - `manual_allowed`
    - `scenario_allowed`
    - `requires_confirmation`
    - `result_required`
    - `timeout_ms`
    - `danger_level`
  - Initial spot-checks found enforcement points, but not full closure.

- [ ] Verify scenario handling of async command results.
  - Especially with node-side `started` status.
  - Need explicit proof that scenario/runtime correctly distinguishes:
    - sync completion
    - async started
    - accepted for remote dispatch
    - timeout
    - failed

### Runtime / Transport

- [ ] Re-evaluate MQTT duplicate cache size and TTL policy.
  - Current cache is small.
  - Need explicit decision whether current size is acceptable for quest traffic.

- [ ] Decide whether runtime status should expose active LED / MOSFET effects.
  - Current status is too close to static idle/ok shape.
  - Need explicit product decision before code changes.

- [ ] Decide whether LED worker priority and optional RMT DMA need Kconfig controls.
  - This depends on real target strip sizes and Wi-Fi load expectations.

### Parsing

- [ ] Decide whether current custom JSON parsing remains acceptable for v1.
  - Current parsing is bounded but permissive and custom.
  - If kept, it still needs centralization and tests.

## Current Open Items

The main remaining runtime audit work is now smaller and narrower:

1. Decide whether broader provisioning lifecycle ownership beyond config
   read/write is worth a dedicated runtime owner model.
2. Re-run explicit end-to-end verification for command policy and async result
   lifecycle after the recent contract/runtime changes.
3. Decide whether current custom JSON parsing remains acceptable for v1 or
   needs a dedicated bounded parser cleanup pass.
4. Revisit MQTT duplicate-cache sizing, runtime status richness, and optional
   LED worker priority/DMA controls only if real traffic or hardware size shows
   pressure.

## Non-Goals For This Plan

- adding new LED effects
- changing hub UX
- changing node provisioning UX
- replacing the whole JSON layer immediately
- introducing live LED wiring reconfigure
