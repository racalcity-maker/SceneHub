# Release Notes Template

Copy this file to `docs/releases/RELEASE_YYYY_MM_DD.md` for each private alpha,
field alpha, or production release.

## Release

- Release label: `TODO`
- Date: `TODO`
- Hub commit: `TODO`
- Node commit: `TODO`
- Built by: `TODO`
- Intended audience: `private alpha`
- Artifact scope: `source and documentation only`

## Summary

Short operator-facing summary of what changed.

## Included

- Hub source: `TODO`
- Node source: `TODO`
- Desktop app source: `TODO`
- Documentation: `TODO`
- Firmware binaries: `not included`

## Compatibility

| Area | Value |
| --- | --- |
| Device-control contract | `cp/v1` |
| Node compact manifest | `manifest_version=2` |
| Standalone bundle schema | `version=2` |
| Tested hub/node pairing | `TODO` |

Update `VERSION_COMPATIBILITY_MATRIX.md` when this release is accepted.

## New Features

- `TODO`

## Fixes

- `TODO`

## Known Limitations

- `TODO`

Use links to `KNOWN_ISSUES.md` when the limitation is expected to persist after
release.

## Hardware Tested

| Device | Board/profile | Firmware | Result |
| --- | --- | --- | --- |
| Hub | `TODO` | `TODO` | `TODO` |
| Node x4 | ESP32-S3 N16R8, configured pin profile | `TODO` | `TODO` |

## Required Manual Checks

- [ ] Hub boots and GM panel is reachable.
- [ ] Admin login and forced password flow are checked.
- [ ] Node is provisioned and visible in GM.
- [ ] `describe_interface` import works.
- [ ] Standalone bundle validate/apply/load works.
- [ ] PN532 degraded/offline state is visible and does not block unrelated node features.
- [ ] Accepted known issues are listed.

## Upgrade Notes

- `TODO`

## Rollback

- Hub rollback path: `TODO`
- Node rollback path: `TODO`
- Config backup required: `yes/no`

## Sign-Off

- Engineering: Aleksandr Berezin
- Hardware: Aleksandr Berezin
- Support/operator: Aleksandr Berezin

For a solo project, these may be the same person. The important point is to
record who accepted the release after testing.
