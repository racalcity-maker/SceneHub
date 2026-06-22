# SceneHub Node v1 Audit Backlog

This file tracks the remaining v1 cleanup work found during the audit against:

- `../policies/MEMORY_POLICY.md`
- `../policies/LOCKING_POLICY.md`
- `../policies/ARCHITECTURE_POLICY.md`
- `../policies/API_POLICY.md`

Only v1 scope is tracked here. Node v2 work stays in the dedicated v2 design
documents.

## Open

- [ ] Move idempotency/cache ownership from `mqtt_transport` into `node_control` or another explicit runtime owner.
- [ ] Reconcile node v1 documented payload targets with current enlarged `device_description` and provisioning buffer sizes.
- [ ] Add focused node-side regression coverage for duplicate `request_id`, result delivery, input-event publishing, reset paths, and config validation.
- [ ] Add node-side stress coverage for terminal-result overflow so command
  overload is verified as explicit rejection/reconnect behavior, not silent
  loss.

## Done

- [x] Prevent silent terminal-result loss when the node command queue and
  deferred-result queue are both saturated: the transport now tries one
  non-blocking overflow publish and otherwise schedules MQTT reconnect so the
  hub observes a transport failure instead of hidden command loss.
- [x] Move input polling/event publish flow out of MQTT transport lock scope.
- [x] Ensure MQTT command handling does not silently drop terminal results when a publish mutex is busy.
- [x] Move command execution off the MQTT event callback path into a bounded owner-task/queue model.
- [x] Make LED effects non-blocking and cancelable so `led.off` and `led.solid` can preempt a running strip effect.
- [x] Keep LED effect tuning node-owned by reducing hub-exposed LED schemas to `off`, `solid(color)`, `breathe(color)`, and `effect(type)`.
- [x] Add node-owned LED preset editing for blink, breathe, and advanced effect timings/colors in provisioning UI/API.
- [x] Add explicit `led.blink(channel,color,times)` with 1 Hz node defaults and keep advanced effect tuning on the node side.
- [x] Keep hardware command execution free of dynamic allocation.
- [x] Keep MQTT RX/TX payload handling on fixed buffers.
- [x] Keep provisioning routes split from provisioning UI/static HTML rendering.
- [x] Keep `describe_interface` on the compact manifest identity:
  `manifest_version=2`, `format=compact_resources`,
  `capability_contract=scenehub.node.compact.v1`.
- [x] Document that large scratch/config/editor structs must move out of task
  stacks and should prefer PSRAM-first owner storage with safe fallback.
- [x] Document that `node_hw_led_init()` is boot-only and that live LED wiring
  reconfigure needs an explicit deinit/restart path.
- [x] Move provisioning write/reset/apply/restart actions behind the dedicated
  `node_admin_control` owner path instead of mutating config/storage directly
  in HTTP handlers.
- [x] Finish the memory-policy cleanup for large scratch/config/editor/admin
  owners: no wide stack-local copies in boot/admin paths, PSRAM-first scratch
  with internal-RAM fallback, and large transient admin payloads kept out of
  ordinary steady-state result slots.
- [x] Verify the LED worker task under PSRAM-enabled builds:
  owner-held effect snapshot, PSRAM-first task stack, internal-RAM fallback,
  and no stack overflows in `node_led_fx`.
