## MQTT Stress / Chaos Tests

External broker stress tests for a real SceneHub controller on the LAN.

Install dependency:

```powershell
pip install paho-mqtt
```

Run the main stress suite against production-like MQTT limits:

```powershell
python .\mqtt_stress_test.py --host 192.168.1.XX --max-clients 24 --rounds 5
```

Run selected stress tests:

```powershell
python .\mqtt_stress_test.py --host 192.168.1.XX --max-clients 24 --tests 7-10
python .\mqtt_stress_test.py --host 192.168.1.XX --max-clients 24 --tests 8,9 --verbose
```

Stress test coverage:

- `1` Slot exhaustion
- `2` Rapid churn
- `3` Same client_id reconnect
- `4` LWT on ungraceful disconnect
- `5` Concurrent mixed churn
- `6` Publish flood
- `7` Subscribe fanout over the allowed broadcast command topic
- `8` Subscribe/unsubscribe churn over the allowed broadcast command topic
- `9` Duplicate client_id storm
- `10` Malformed raw packet smoke

Run protocol/semantics tests:

```powershell
python .\mqtt_protocol_semantics_test.py --host 192.168.1.XX --max-clients 24 --max-subs 16 --duration 60
```

Run selected protocol tests:

```powershell
python .\mqtt_protocol_semantics_test.py --host 192.168.1.XX --tests 1-3
python .\mqtt_protocol_semantics_test.py --host 192.168.1.XX --tests 6 --duration 300 --verbose
python .\mqtt_protocol_semantics_test.py --host 192.168.1.XX --tests 7 --verbose
```

Run GM runtime noise regression test:

```powershell
python .\gm_runtime_noise_test.py `
  --host 192.168.1.XX `
  --username admin `
  --password secret `
  --room-id room_a `
  --scenario-id scenario_wait_relay `
  --reset-first `
  --target-node-id relay_room_2 `
  --target-action input.pressed `
  --target-args "{\"channel\":2}" `
  --noise-count 100 `
  --noise-clients 4
```

Notes:

- These scripts intentionally use the current default-deny ACL contract.
- `dcc-foo-bar` is expected to publish/subscribe under `cp/v1/dev/foo_bar/...`.
- Broadcast command fanout uses `dcc-all -> cp/v1/dev/all/control/command`.
- Protocol test `5` verifies that denied QoS1 publishes are acknowledged but not delivered.
- Protocol test `2` verifies retained `QoS1 -> QoS1` delivery semantics for fresh subscribers.
- Protocol test `4` verifies that duplicate `SUBSCRIBE` updates QoS without consuming an extra subscription slot.
- Protocol test `7` verifies that denied QoS1 publishes are acknowledged but not delivered.

GM runtime / queue chaos coverage also belongs here when it needs real async
workers, MQTT timing and many simulated devices. Preferred harness:

- `tools/device_control_client/client.py` for multi-device heartbeat/status/event/result spam
- dedicated SceneHub runtime scenarios such as:
  - scenario wait survives 100+ noisy status/runtime updates before the target event
  - duplicate result ordering under real broker latency
  - describe_interface timeout while other command traffic continues

`gm_runtime_noise_test.py` is the first dedicated runtime/product regression in
this folder. It talks to the real controller over HTTP and MQTT and checks that
non-critical heartbeat/status noise does not prevent a later critical device
event from progressing the scenario wait.

Important:

- `--target-node-id` is the MQTT topic namespace id, usually the physical
  client id / node id, not necessarily the GM editor display name.
- The room should already have a compatible scenario and quest-device mapping.
