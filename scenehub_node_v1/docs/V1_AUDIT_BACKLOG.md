# SceneHub Node v1 Audit Backlog

This file tracks the remaining v1 cleanup work found during the audit against:

- `MEMORY_POLICY.md`
- `LOCKING_POLICY.md`
- `ARCHITECTURE_POLICY.md`
- `API_POLICY.md`

Only v1 scope is tracked here. Node v2 work stays in the dedicated v2 design
documents.

## Open

- [ ] Move idempotency/cache ownership from `mqtt_transport` into `node_control` or another explicit runtime owner.
- [ ] Reconcile node v1 documented payload targets with current enlarged `device_description` and provisioning buffer sizes.
- [ ] Add focused node-side regression coverage for duplicate `request_id`, result delivery, input-event publishing, reset paths, and config validation.
- [ ] Finish the memory-policy cleanup for large scratch/config/editor structs:
  no wide stack-local copies in boot/admin paths, and a PSRAM-first with
  internal-RAM fallback plan for large non-DMA scratch owners.
- [x] Verify the LED worker task under PSRAM-enabled builds:
  owner-held effect snapshot, PSRAM-first task stack, internal-RAM fallback,
  and no stack overflows in `node_led_fx`.

## Done

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
