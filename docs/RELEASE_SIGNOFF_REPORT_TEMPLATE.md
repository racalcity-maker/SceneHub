# Release Sign-Off Report Template

Use this report before tagging or handing a build to support.

## Build Identity

- Release label: `TODO`
- Date/time: `TODO`
- Hub commit: `TODO`
- Node commit: `TODO`
- Branch: `TODO`
- Builder: `TODO`
- Intended audience: `private alpha`
- Artifact scope: `source and documentation only`

## Hardware

| Unit | Board/profile | Connected hardware | Result |
| --- | --- | --- | --- |
| Hub | `TODO` | SD, I2S, relay/MOSFET as applicable | `TODO` |
| Node 1 | ESP32-S3 N16R8, configured pin profile | relay, MOSFET, input, LED, PN532 | `TODO` |
| Node 2 | ESP32-S3 N16R8, configured pin profile | relay, MOSFET, input, LED, PN532 | `TODO` |
| Node 3 | ESP32-S3 N16R8, configured pin profile | relay, MOSFET, input, LED, PN532 | `TODO` |
| Node 4 | ESP32-S3 N16R8, configured pin profile | relay, MOSFET, input, LED, PN532 | `TODO` |

## Hub Checks

- [ ] Clean boot.
- [ ] Setup/recovery path known.
- [ ] Web UI reachable.
- [ ] Admin login checked.
- [ ] GM panel opens.
- [ ] Device list opens.
- [ ] Room/profile/scenario storage loads.
- [ ] MQTT broker starts.
- [ ] No active P0/P1 not listed in `KNOWN_ISSUES.md`.

## Node Checks

- [ ] Fresh provisioning works.
- [ ] Saved config survives reboot.
- [ ] MQTT connects to hub.
- [ ] `heartbeat`, `status`, `diag`, `result`, `event` are visible.
- [ ] `describe_interface` imports into GM.
- [ ] Hardware commands work on configured outputs.
- [ ] PN532 starts or reports degraded state correctly.
- [ ] Known cards survive reboot.

## Standalone Bundle Checks

- [ ] Validate known-good bundle.
- [ ] Reject invalid JSON with clear error.
- [ ] Apply bundle.
- [ ] Reboot and load stored bundle.
- [ ] Trigger expected local events.
- [ ] Pause/resume rules.
- [ ] Remove card/input reset paths behave as expected.

## Logs Captured

- Hub boot log: `TODO`
- Node boot log: `TODO`
- Failure logs: `TODO`

## Accepted Issues

| Issue | Severity | Reason accepted | Follow-up |
| --- | --- | --- | --- |
| `TODO` | `TODO` | `TODO` | `TODO` |

## Decision

- [ ] Release accepted
- [ ] Release blocked

Decision owner: Aleksandr Berezin
