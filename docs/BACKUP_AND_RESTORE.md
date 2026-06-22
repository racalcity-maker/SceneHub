# Backup And Restore

This document defines the safe alpha procedure for preserving SceneHub and node
configuration before risky maintenance.

## What To Back Up

At minimum, preserve:

- hub configuration and quest-device definitions;
- any operator-facing quick actions that were manually curated;
- node base configuration values;
- standalone bundle JSON that is actually stored on the node;
- NFC known-card assignments if the reader slice is in use.

## Before Risky Changes

Take a backup before:

- factory reset;
- partition/storage repair;
- major hub/node version change;
- replacing a node board;
- rewriting standalone bundles on a production-like setup.

## Minimum Node Backup

For a node, capture:

1. physical node id
2. operation mode
3. network settings needed for reprovisioning
4. current compact manifest import in GM
5. stored standalone bundle via `Load stored bundle`
6. NFC known-card table if enabled

Store the exported JSON in versioned files under a dated support folder.

## Minimum Hub Backup

Capture:

1. quest-device definitions
2. admin templates and quick actions
3. scenario references that depend on imported node commands/events
4. current hub commit/build label

## Restore Order

Use this order to avoid partial recovery confusion:

1. restore hub build/config baseline
2. reprovision node base config
3. restore node operation mode
4. restore standalone bundle
5. reboot node if required for activation
6. restore or verify NFC known cards
7. re-import compact config in GM if needed
8. verify quick actions and scenario bindings

## Verification After Restore

Confirm:

- node is online in GM;
- compact import counts are correct;
- expected admin actions are present;
- standalone bundle metadata matches the intended file;
- degraded reader state is explained and expected;
- scenario-facing exported commands still exist.

## Recovery Notes

- `Save device` in GM and `Apply bundle` on the node are separate writes.
- `Load stored bundle` is the truth source for what the node currently has.
- do not use screenshots as the only backup for JSON-bearing configuration;
- keep the last known-good bundle file outside the browser UI.
