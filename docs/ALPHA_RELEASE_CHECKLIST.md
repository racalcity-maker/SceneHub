# Alpha Release Checklist

This checklist is the minimum sign-off gate before calling a SceneHub build
alpha-ready for team testing or field support.

## Scope

The release is considered alpha-ready only when:

- hub boots and GM panel is reachable;
- at least one SceneHub Node v2 device is provisioned and visible in GM;
- `describe_interface` import works end-to-end;
- standalone bundle validate/apply/reboot flow works on a real node;
- degraded driver state is visible in GM without hiding the rest of the node;
- all known blockers are either fixed or listed in `KNOWN_ISSUES.md`.

## Hub Gate

- GM login works after clean boot.
- `Device Controls` page renders without crashes or stuck polling.
- quest-device modal opens, imports compact node config, and saves device
  contract changes.
- admin commands from GM work:
  - restart node
  - pause rules
  - resume rules
  - load stored bundle
  - validate bundle
  - apply bundle
- command results return terminal status and visible operator feedback.

## Node Gate

- provisioning UI loads on a fresh boot.
- base config save works and survives reboot.
- `scenehub` and `standalone` modes both boot correctly.
- `standalone_mqtt_enabled` behaves as configured.
- node publishes `heartbeat`, `status`, `result`, and `event` on `cp/v1`.
- `describe_interface` response is accepted by the hub without payload errors.
- compact manifest stays within the current transport/runtime limits.
- compact manifest/status/admin JSON remains valid when labels contain quotes or
  other escaped characters.

## Standalone Rule Engine Gate

- bundle validate rejects invalid JSON with stable error codes.
- bundle validate rejects invalid technical ids outside the current identifier
  whitelist.
- bundle apply stores candidate bundle and activates it only after reboot.
- rebooted node loads the stored bundle without crashes or stack overflows.
- pause/resume works on hardware and via GM admin actions.
- local rules do not keep running in `scenehub` mode unless the mode explicitly
  allows them.
- fallback-specific behavior is not assumed shipped unless explicitly enabled.

## NFC Slice Gate

- PN532 driver can be enabled from config without breaking unrelated node
  features.
- missing reader reports degraded/offline state instead of full node fault.
- reader health is visible in node status and in GM device/admin surfaces.
- known-card storage survives reboot.
- reader UI can show current driver state and known cards.
- exported NFC events and admin actions do not break core bundle flow.

## Support Gate

- `docs/SUPPORT_RUNBOOK.md` is current for the shipped build.
- `docs/VERSION_COMPATIBILITY_MATRIX.md` lists the tested hub/node pairing.
- `scenehub_node_v1/docs/README.md` links the active node v2 docs and examples.
- example bundles open and remain valid reference assets.

## Sign-Off

Before shipping an alpha build, record:

- tested repo commit
- tested node firmware image commit
- tested GM build commit
- hardware board/profile used
- open known issues accepted for alpha
