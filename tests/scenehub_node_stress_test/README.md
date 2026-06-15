# SceneHub Node Stress Test

This test opens 20 MQTT sessions and emulates the current `scenehub_node_v1`
firmware contract.

Default node identities:

- MQTT client IDs: `dcc-scenehubnode-1` ... `dcc-scenehubnode-20`
- MQTT namespace IDs: `scenehubnode_1` ... `scenehubnode_20`
- Display names: `SceneHubNode 1` ... `SceneHubNode 20`

Each node exposes:

- 4 relays
- 4 MOSFET/transistor outputs
- 4 digital inputs
- 4 universal digital outputs
- 2 addressable LED strips with 30 pixels each

The compact manifest is based on
`scenehub_node_v1/esp_idf/components/node_capability/node_capability.c`.
Command parsing, duplicate cache size, queue length, command names, result
statuses, and important value limits follow the current node implementation.

## Install

```powershell
pip install paho-mqtt
```

## Run

From the repository root:

```powershell
python .\tests\scenehub_node_stress_test\scenehub_node_stress_test.py `
  --host 192.168.1.XX
```

Longer run:

```powershell
python .\tests\scenehub_node_stress_test\scenehub_node_stress_test.py `
  --host 192.168.1.XX `
  --nodes 20 `
  --rounds 5 `
  --burst 12 `
  --soak-seconds 600 `
  --log-level INFO
```

The broker must allow more sessions than the requested number of emulated
nodes. The current production-like default is 24, which leaves reconnect
headroom for a 20-node run.

Commands are published with QoS 1 by default, and nodes subscribe to command
topics with QoS 1. This exercises broker-side PUBACK tracking and retries
throughout the complete test. Use `--command-qos 0` to compare against the
older QoS 0 behavior.

Command results are published with QoS 1, matching the current SceneHub Node
transport. By default the emulated node does not subscribe to its own
`/result` topic; the test marks a result as observed when the broker sends
PUBACK for that QoS 1 result publish. Heartbeat, status, and input events
remain QoS 0.

Use `--subscribe-results` only to reproduce the older, heavier self-echo mode
where each emulated node also receives its own `/result` messages from the
broker.

## Phases

1. Connect all nodes and publish heartbeat/status.
2. Send valid relay, MOSFET, output, LED, and node commands.
3. Publish input-event scenario traffic for all four inputs on all nodes.
4. Disconnect and reconnect nodes in waves, then verify commands still work.
5. Connect duplicate MQTT clients with existing node client IDs, verify the
   original node sessions recover, and run a status command.
6. Send `describe_interface` to all nodes simultaneously, then retry only
   missed responses sequentially with a short delay.
7. Verify duplicate `request_id` idempotency and conflicting duplicates.
8. Send invalid channels, values, durations, colors, effects, malformed JSON,
   oversized args, and unknown commands.
9. Send command bursts beyond the node's four-item command queue and verify
   every command gets a terminal result, including `rejected/busy` responses.
10. Slow down a subset of nodes, burst all nodes, and verify slow nodes do not
    block command results from the remaining nodes.
11. Optionally run soak traffic with `--soak-seconds`: periodic input events
    and status probes while all clients remain connected.
12. Run a final `node.get_status` command through every connection.
13. Keep all clients connected in interactive mode and log incoming MQTT
   commands until `quit` or `Ctrl+C`.

After the summary, the process normally remains connected at the
`scenehub-nodes>` prompt. Use `--no-hold` for the previous exit-after-test
behavior. The eventual process exit code is `1` when a node disconnects, a
command result is missing, a valid command is rejected, an invalid command is
accepted, or the post-stress health probe fails.

Interactive receive logging is intentionally quiet by default: only incoming
`/control/command` messages are logged, and payload text is truncated to 240
characters. Add `--subscribe-results --log-results` to also log self-echoed
`/result` messages, and use `--log-payload-bytes -1` only when full JSON
payloads are needed.

Normal `INFO` logging is intentionally aggregated by phase. Use
`--log-level DEBUG` when every command case or duplicate-client connect detail
is needed.

An initial missed `describe_interface` response is reported as a warning, not
an immediate failure. The run fails only when the response is still missing
after the configured sequential retries. Defaults are two retries with a
250 ms delay; use `--interface-retries` and `--interface-retry-delay` to change
them.

The virtual node keeps the real four-command queue limit. When that queue is
full it defers `rejected/busy` responses through a small result queue and
retries result publication, matching the node transport behavior expected from
the firmware.

To separately test commands published by an external MQTT client with QoS 1,
add `--qos1-probe`. This probe runs last because a broker PUBACK handling
problem may disconnect the emulated nodes:

```powershell
python .\tests\scenehub_node_stress_test\scenehub_node_stress_test.py `
  --host 192.168.1.XX `
  --qos1-probe
```

## Interactive input events

The interactive mode accepts:

```text
input <node|all> <channel 1-4> <0|1>
pulse <node|all> <channel 1-4> [duration_ms]
inputs <node|all> <in1> <in2> <in3> <in4>
status
help
quit
```

For example:

```text
input 1 1 1
input 1 1 0
pulse scenehubnode_2 4 250
inputs all 0 0 0 0
```

Each input operation publishes the same `input.changed` event shape as the
real node. Ready-to-copy variants are in `interactive_commands.txt`.
