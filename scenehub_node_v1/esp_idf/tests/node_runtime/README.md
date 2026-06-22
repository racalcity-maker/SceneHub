# Node Runtime Tests

This app is the phase-4 home for narrow SceneHub Node runtime tests.

Current scope:

- owner/control policy tests
- rule engine contract tests
- fallback runtime tests
- compact capability/status JSON tests

Target assumptions:

- primary target: `esp32s3`
- PSRAM-enabled board/profile
- test app should inherit S3 + PSRAM expectations explicitly instead of relying
  on host defaults
- Unity is started from a dedicated test task with a larger stack, using PSRAM
  when available, so heavy JSON/schema assertions do not depend on production
  `main` task limits

Current live coverage:

- `test_node_control_policy.c`
  - `LOCAL_RULE` does not re-dispatch exported commands
  - `HUB` and `LOCAL_UI` surface `rules_inactive` while rule runtime is absent
- `test_node_rule_engine.c`
  - minimal exported-command bundle validates
  - exported command ids with spaces are rejected by schema validation
- `test_node_fallback_runtime.c`
  - primary -> offline pending on hub loss
  - offline pending -> fallback active on timeout
  - fallback active -> hub primary / return pending according to return policy
  - return pending -> fallback active if hub drops again
- `test_node_capability_json.c`
  - compact device description stays valid JSON with quoted/backslashed labels
  - status JSON stays parseable without active runtime owners

The suite is still intentionally narrow. It establishes a stable test app
layout before the full hardware-free coverage is filled in.

Linking note:

- the `main` test component is built with `WHOLE_ARCHIVE`
- this is required so ESP-IDF does not drop unreferenced object files that only
  contribute Unity `TEST_CASE(...)` constructor registrations

Expected structure:

- `main/test_node_control_policy.c`
- `main/test_node_rule_engine.c`
- `main/test_node_fallback_runtime.c`
- `main/test_node_capability_json.c`

Future additions should stay narrow and domain-focused. Do not turn this test
app into a second firmware entrypoint with broad runtime side effects.

Suggested run path:

```powershell
cd D:\Projects\SceneHub\scenehub_node_v1\esp_idf\tests\node_runtime
idf.py set-target esp32s3
idf.py -DSDKCONFIG_DEFAULTS=sdkconfig.defaults build
idf.py -p COM34 flash monitor
```

If the environment already has a generated `sdkconfig` from another target,
delete only the test-app-local `build/` and `sdkconfig` before switching target.
