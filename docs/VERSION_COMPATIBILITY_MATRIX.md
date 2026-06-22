# Version Compatibility Matrix

This file records the hub/node pairings that are expected to work together.

For alpha, the default rule is simple:

- ship hub and node from the same tested repo commit whenever possible;
- when they differ, write down the exact pair here before support starts using
  it.

## Active Contracts

| Area | Current baseline |
| --- | --- |
| Device-control MQTT contract | `cp/v1` |
| Node compact manifest | `manifest_version=2` |
| Compact manifest kind | `scenehub.node.compact.v1` |
| Standalone bundle schema | `version=2` |
| Node operation modes | `scenehub`, `standalone`, alpha `fallback` |

## Tested Pairing Template

Use this section format for every alpha pairing that support may encounter.

### Pairing: current alpha

- Hub repo: `SceneHub`
- Node repo subtree: `scenehub_node_v1`
- Recommended source: same repo commit for hub and node
- Status: baseline
- Supported node board: ESP32-S3 N16R8 only
- Verified node count: 4 identical ESP32-S3 N16R8 boards
- Pin profile: current configured/factory profile; changed pin profiles require
  a new sign-off entry

Expected capabilities:

- compact `describe_interface` import
- GM node admin actions
- standalone bundle validate/apply/load
- alpha fallback runtime slice with owner-driven timeout/return policy
- degraded PN532 health reporting without full node fault
- compact manifest/status JSON escaped via shared node JSON helpers
- rule/schema technical ids validated by the shared identifier whitelist

## Compatibility Notes

- GM compact import and node compact export must evolve together.
- If compact manifest counts or fields change, update GM import logic and this
  file in the same change.
- If identifier policy changes, update node schema validation, compact export
  docs and GM-side assumptions in the same change.
- If new rule-engine runtime features are added, unsupported old nodes should
  fail with explicit validate/apply errors instead of silent partial behavior.
- Support should not mix arbitrary older hub builds with newer node firmware
  unless the exact pair is documented here.
