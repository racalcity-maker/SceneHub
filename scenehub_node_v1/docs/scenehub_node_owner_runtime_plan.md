# SceneHub / SceneHub Node — план внедрения owner-driven runtime

Актуально для состояния архива `SceneHub(56)`.

Файл хранить в `UTF-8`, чтобы русскоязычные секции не разваливались при чтении
в терминалах и редакторах с разными default-encoding.

Цель плана: довести SceneHub Node до чистой runtime-архитектуры с понятными owner-FSM, безопасной многопоточностью, контролируемыми зависимостями и синхронизированной документацией.

---

## 0. Текущий статус

### Уже хорошо

- У Hub и Node больше нет явных CMake/include cycles.
- `node_rule_engine` развязан от `node_action_router` через `node_rule_action_port`.
- NFC перестал напрямую зависеть от `rule_engine`.
- Появились отдельные слои:
  - `node_driver_nfc_contract`
  - `node_driver_nfc_api`
  - `node_driver_nfc_config_api`
  - `node_event_router`
  - `node_runtime_snapshot`
  - `node_rule_action_port`
- `node_provisioning_config_api.c` уже частично распилен и больше не выглядит как главный god-file.
- `apply_rules` / `clear_rules` в admin-control уже выставляют `restart_required`.
- `node_fallback_runtime` теперь считает переходы через узкий helper
  `node_fallback_policy`, поэтому fallback transition policy можно тестировать
  без запуска live fallback task.
- `scenehub_node_v1/esp_idf/tests/node_runtime` больше не является только
  scaffold: текущий test app под `esp32s3` + PSRAM проходит 13 тестов по
  control source-policy, rule-schema identifier guards, fallback transition
  policy и compact capability/status JSON.

### Что осталось кроме god-files

P0/P1 остатки:

1. Hub: мелкие CMake deps не закрыты.
2. Node: `node_control` пока остаётся синхронным сервисом, а не owner queue.
3. Документация должна различать:
   - `fallback` code path уже существует как alpha implementation
   - `fallback` ещё не является field-stable release baseline
4. Node-side unit/regression тесты нужно усилить:
   - `startup_grace_ms`
   - `hub_seen_once`
   - live rule-engine timer/input-hold/reset cases
   - `runtime_snapshot` focused assertions
5. Нужно проверить, что CI собирает и Hub, и Node firmware.

---

# Архитектурная цель

Нода не должна быть вторым SceneHub.

Правильная граница:

```text
SceneHub Hub:
  - комнаты
  - сценарии
  - Game Mode
  - GM/operator flow
  - аудит
  - timeline
  - глобальная логика игры

SceneHub Node:
  - железо
  - GPIO / relay / MOSFET / LED / NFC
  - локальные события
  - быстрые реакции
  - safe-state
  - fallback
  - bounded local rules
```

Правило:

> Hub владеет сценарием. Node владеет железом, локальными реакциями и безопасным fallback.

---

# Owner-FSM модель

Не делать одну огромную FSM на всю ноду.

Должны быть маленькие owner-FSM:

```text
connectivity FSM:
  - wifi down/up
  - mqtt disconnected/connected

fallback FSM:
  - hub_primary
  - hub_offline_pending
  - fallback_active
  - hub_return_pending

rules FSM:
  - no_bundle
  - bundle_loaded
  - active
  - paused
  - compile_error

driver FSM:
  например NFC:
    - disabled
    - init
    - ready
    - degraded
    - fault

control owner:
  - принимает команды
  - сериализует доступ к железу
  - возвращает result/rejected/error
```

Главное правило многопоточности:

> MQTT callback / HTTP handler / NFC poll task / rule_engine не должны напрямую менять чужой runtime-state или железо. Они должны отправлять request/event владельцу.

---

# Фаза 0 — зафиксировать чистую базу

## Цель

Перед большим runtime-рефакторингом закрыть мелкие dependency-ошибки и убедиться, что база чистая.

## Сделать

### 0.1. Исправить Hub CMake deps

#### `components/room_scenario/CMakeLists.txt`

`room_scenario.h` включает `quest_common_limits.h`, значит нужен `quest_common`.

Было условно:

```cmake
REQUIRES freertos json
```

Нужно:

```cmake
REQUIRES freertos json quest_common
```

#### `components/orchestrator_core/CMakeLists.txt`

`orchestrator_audit.h` и `orchestrator_timeline.h` используют `quest_common_limits.h`.

Нужно добавить:

```cmake
REQUIRES
    event_bus
    esp_timer
    freertos
    quest_common
```

### 0.2. Убедиться, что Node cycles остаются нулевыми

Проверить:

```text
Node CMake cycles:  0
Node include cycles: 0
Missing direct deps: 0
```

### 0.3. Проверить CI

CI должен собирать:

```text
1. Root SceneHub Hub firmware
2. scenehub_node_v1/esp_idf firmware
3. test apps, если они есть отдельно
```

Минимально:

```yaml
- name: Build SceneHub Hub
  run: idf.py build

- name: Build SceneHub Node
  working-directory: scenehub_node_v1/esp_idf
  run: idf.py build
```

### 0.4. Почистить мусор

Убрать из repo/archive:

```text
__pycache__/
*.pyc
build_unused.log
sdkconfig.old, если он не нужен как reference
временные generated/cache файлы
```

## После фазы обновить документацию

Обновить:

```text
docs/KNOWN_ISSUES.md
docs/TESTING.md
docs/ARCHITECTURE_LAYER_RISK_MAP.md
scenehub_node_v1/docs/README.md
```

Что указать:

```text
- Hub/Node dependency cycles закрыты.
- Node build добавлен в CI.
- Остаточные архитектурные риски перенесены в следующие фазы.
```

---

# Фаза 1 — сделать `node_control` настоящим owner-path

## Цель

Убрать ситуацию, где разные задачи напрямую вызывают `node_control_submit()` и потенциально трогают железо из разных контекстов.

## Сейчас

```text
MQTT task
  -> node_control_submit()
  -> hardware

HTTP provisioning
  -> node_control_submit()
  -> hardware

rule_engine owner
  -> action_router
  -> node_control_submit()
  -> hardware
```

## Нужно

```text
MQTT / HTTP / rule_engine
  -> node_control_submit()
  -> node_control queue
  -> node_control owner task
  -> hardware
```

## Предлагаемый API

```c
typedef enum {
    NODE_CONTROL_REQ_COMMAND,
    NODE_CONTROL_REQ_APPLY_LED_CONFIG,
    NODE_CONTROL_REQ_PREVIEW_LED,
    NODE_CONTROL_REQ_SAFE_OFF,
} node_control_req_kind_t;

typedef struct {
    node_control_req_kind_t kind;
    node_control_command_t command;
    node_control_result_t *result;
    SemaphoreHandle_t done;
} node_control_request_t;

esp_err_t node_control_submit(
    const node_control_command_t *cmd,
    node_control_result_t *out,
    TickType_t timeout
);
```

### Ownership contract

`node_control_submit()` должен остаться синхронным wait-wrapper API, а не
асинхронной передачей владения наружу.

Правила:

```text
- request копируется в bounded static queue по значению;
- caller остаётся владельцем `out_result` и completion primitive;
- caller ждёт completion внутри `node_control_submit()`;
- stack-owned `out_result` допустим только пока submit не вернул управление;
- owner task не хранит внешние указатели после завершения request.
```

Практически это значит:

```text
- публичный `node_control_command_t` остаётся лёгким входным DTO;
- внутренний queue request DTO принадлежит только control owner path;
- completion/sync примитивы не становятся частью общего runtime state.
```

Старый прямой executor оставить только внутри owner task:

```c
static esp_err_t node_control_execute_inline(
    const node_control_command_t *command,
    node_control_result_t *out_result
);
```

## Перевести callers

```text
node_mqtt_command.c:
  node_control_execute -> node_control_submit

node_action_router.c:
  node_control_execute -> node_control_submit

node_provisioning_led_api.c:
  node_control_execute -> node_control_submit

node_admin_control.c:
  node_control_update_led_config -> request через control owner
```

## Политика

```text
node_control owner может трогать hardware.
MQTT, HTTP, rule_engine напрямую hardware не трогают.
```

## Риски

- Нужно аккуратно избежать deadlock, если `node_control_submit()` будет вызван из самой control task.
- Для таких случаев добавить fast-path:

```c
if (node_control_is_owner_task()) {
    return node_control_execute_inline(cmd, out);
}
```

## После фазы обновить документацию

Обновить:

```text
scenehub_node_v1/docs/ARCHITECTURE_POLICY.md
scenehub_node_v1/docs/LOCKING_POLICY.md
scenehub_node_v1/docs/NODE_V1_RUNTIME_AUDIT_PLAN.md
scenehub_node_v1/docs/NODE_V2_ENGINE_CONTRACT.md
```

Что указать:

```text
- node_control стал единственным owner hardware command path.
- Внешние источники команд работают через submit/queue.
- Прямой hardware access из MQTT/HTTP/rule_engine запрещён.
```

---

# Фаза 2 — довести fallback FSM до policy-clean состояния

## Цель

Убрать внешние вызовы в `rule_engine` из-под `s_status_lock`.

## Сейчас проблема

В `node_fallback_runtime.c` fallback task берёт `s_status_lock` и внутри transition может вызвать:

```c
node_rule_engine_pause();
node_rule_engine_set_runtime_enabled(false);
node_rule_engine_set_runtime_enabled(true);
node_rule_engine_reset();
node_rule_engine_resume();
```

Это нарушение политики:

> Под локальным lock нельзя вызывать внешний owner/runtime.

## Нужно

Сделать transition-action модель.

### Ввести action enum

```c
typedef enum {
    NODE_FALLBACK_ACTION_NONE,
    NODE_FALLBACK_ACTION_ENTER_FALLBACK,
    NODE_FALLBACK_ACTION_EXIT_FALLBACK,
    NODE_FALLBACK_ACTION_PAUSE_RULES,
    NODE_FALLBACK_ACTION_RESUME_RULES,
} node_fallback_transition_action_t;
```

### Разделить evaluate и perform

```text
evaluate_state_locked()
  - читает/меняет только fallback status
  - возвращает action

perform_action_unlocked()
  - вызывает node_rule_engine_*
  - не держит s_status_lock

finalize_status_locked()
  - коротко фиксирует результат action
```

## Дополнительно решить boot policy

Нужно явно определить:

```text
fallback_timeout считается:
  А) сразу от boot, если hub unavailable
  Б) только после hub_seen_once
  В) после startup_grace_ms
```

Рекомендуемый вариант:

```text
startup_grace_ms + hub_seen_once
```

Поля:

```c
bool hub_seen_once;
uint32_t startup_grace_ms;
```

Поведение:

```text
Если boot только начался, не входить в fallback до истечения startup_grace_ms.
Если hub уже был seen once, тогда offline timeout считается нормально.
```

## После фазы обновить документацию

Обновить:

```text
scenehub_node_v1/docs/NODE_V2_TRANSITION_PLAN.md
scenehub_node_v1/docs/PROVISIONING_AND_CONFIG.md
docs/VERSION_COMPATIBILITY_MATRIX.md
docs/SUPPORT_RUNBOOK.md
docs/KNOWN_ISSUES.md
docs/ALPHA_RELEASE_CHECKLIST.md
```

Что указать:

```text
fallback status:
  implemented alpha slice

limitations:
  requires field verification
  not yet declared field-stable
  not yet the default shipped baseline

policy:
  fallback FSM owns only fallback state
  rule_engine actions are executed outside fallback locks
```

---

# Фаза 3 — добить command source policy в tests/docs

## Цель

Зафиксировать уже внедрённую source policy в тестах, документации и узких
guard-ветках, не открывая заново уже закрытый recursion fix.

## Источники команд

```text
NODE_CONTROL_SOURCE_HUB
NODE_CONTROL_SOURCE_LOCAL_RULE
NODE_CONTROL_SOURCE_LOCAL_UI
NODE_CONTROL_SOURCE_PREVIEW
NODE_CONTROL_SOURCE_ADMIN
```

## Базовая политика

```text
HUB:
  - обычные hardware commands
  - exported rule commands

LOCAL_RULE:
  - hardware/system commands
  - emit local/device events
  - НЕ может заново dispatch-ить exported mqtt_command

LOCAL_UI:
  - admin/preview/local testing commands
  - может dispatch-ить exported command только если это явно разрешено

PREVIEW:
  - только preview-safe LED commands
  - не должен менять permanent config

ADMIN:
  - config/rules/apply/clear/restart
  - slow operations через admin owner
```

## Важно

Базовый recursion guard уже должен оставаться в коде:

```c
if (command->source == NODE_CONTROL_SOURCE_HUB ||
    command->source == NODE_CONTROL_SOURCE_LOCAL_UI) {
    esp_err_t err = node_rule_api_dispatch_mqtt_command(command->command);
    ...
}
```

`NODE_CONTROL_SOURCE_LOCAL_RULE` не должен re-enter в `mqtt_command` trigger.

Задача этой фазы:

```text
- не откатить этот guard при следующих рефакторингах;
- явно описать source policy в docs;
- покрыть policy узкими node-side tests.
```

## Тесты

Добавить тесты:

```text
LOCAL_RULE -> exported command denied
HUB -> exported command accepted
LOCAL_UI -> exported command accepted/denied по policy
PREVIEW -> only preview-safe commands
unsupported command -> rejected
```

## После фазы обновить документацию

Обновить:

```text
docs/COMMAND_RESULT_SEMANTICS.md
docs/device_control_contract_v1.md
scenehub_node_v1/docs/API_POLICY.md
scenehub_node_v1/docs/NODE_V2_ENGINE_CONTRACT.md
```

---

# Фаза 4 — node-side unit/regression tests

## Цель

Перестать полагаться только на stress emulator и ручную проверку.

## Текущий статус

Уже реализовано и проходит на отдельном test app под `esp32s3` + PSRAM:

```text
- node_control source policy
- LOCAL_RULE recursion guard
- rule-schema exported-command identifier validation
- fallback transition policy через pure helper
- compact device-description/status JSON parseability и escaping smoke tests
```

В этой фазе ещё осталось добить:

```text
- startup_grace_ms
- hub_seen_once
- live runtime timer/input-hold/reset cases
- runtime_snapshot-specific assertions
- более широкое hardware-free command/result coverage
```

## Добавить test app

Предложенная структура:

```text
scenehub_node_v1/esp_idf/tests/node_runtime/
  main/
    test_node_control_policy.c
    test_node_rule_engine.c
    test_node_fallback_runtime.c
    test_node_rule_compile.c
    test_node_capability_json.c
  CMakeLists.txt
  sdkconfig.defaults
```

## Минимальный набор тестов

### fallback

```text
manual_stay_active
auto_on_stable_mqtt
startup_grace_ms
hub_seen_once
boot без Wi-Fi
Wi-Fi есть, MQTT нет
MQTT connected -> disconnected -> connected
hub flapping
timeout wraparound
```

### rule engine

```text
boot trigger
local_event trigger from NFC
mqtt_command exported command
pause/resume
reset clears timers/state
timer action
input_hold
```

### node_control

```text
hardware command serialization
safe-off behavior
duplicate request behavior, если применимо
unsupported command
preview-only commands
```

### manifest/status JSON

```text
pin label with quotes
NFC card name with quotes
event name with slash/backslash
bundle_id escaping
exported command/event labels
fallback status fields
```

## После фазы обновить документацию

Обновить:

```text
docs/TESTING.md
scenehub_node_v1/docs/README.md
docs/ALPHA_RELEASE_CHECKLIST.md
```

Что указать:

```text
- как запускать node runtime tests
- какие кейсы покрыты
- какие кейсы остаются manual/hardware-only
```

---

# Фаза 5 — JSON/read-side hardening

## Цель

Сделать manifest/status/admin JSON устойчивыми к пользовательским строкам.

## Проблема

`node_capability.c` всё ещё использует безопасный только от NULL helper:

```c
static const char *safe_text(const char *text)
{
    return text ? text : "";
}
```

Но это не JSON escaping.

## Сделать общий helper в `node_common`

Например:

```c
esp_err_t node_json_escape_string(
    const char *input,
    char *out,
    size_t out_size
);

esp_err_t node_json_writer_append_escaped(
    node_json_writer_t *writer,
    const char *value
);
```

## Перевести на helper

```text
node_capability.c
node_provisioning_admin_json.c
node_provisioning_rules_api.c
node_rule_compile JSON args
node_driver_nfc_api/config JSON writers
```

## Ввести whitelist для identifiers

Для технических id:

```text
[a-zA-Z0-9_.:-]{1,32}
```

Применить к:

```text
command id
event id
state key
reader id
bundle id
driver id
```

Для labels/names:

```text
разрешить обычный printable text,
но всегда escape перед JSON output
```

## После фазы обновить документацию

Обновить:

```text
scenehub_node_v1/docs/API_POLICY.md
scenehub_node_v1/docs/NODE_V2_RULE_SCHEMA_DRAFT.md
docs/VERSION_COMPATIBILITY_MATRIX.md
```

---

# Фаза 6 — field verification

## Цель

Проверить не только архитектуру, но и реальное поведение на железе.

## Минимальный чеклист

```text
1. Node boot в scenehub mode.
2. Node boot в standalone mode.
3. Node boot в fallback mode без Wi-Fi.
4. Wi-Fi появляется поздно.
5. MQTT появляется поздно.
6. MQTT отваливается во время активного сценария.
7. fallback_timeout срабатывает.
8. hub возвращается.
9. fallback_return_delay срабатывает.
10. manual_stay_active остаётся в fallback.
11. NFC отсутствует при boot.
12. PN532 подключён, потом отключён.
13. карта быстро туда-сюда.
14. одна карта заменяется другой.
15. unknown card event.
16. known card mapped event.
17. exported NFC events видны в GM.
18. Apply bundle -> reboot -> bundle active.
19. Clear bundle -> reboot -> no active bundle.
20. Rules pause/resume.
21. LED preview не ломает runtime LED config.
22. 20 emulated nodes stress test.
23. Hub reboot while node alive.
24. Node reboot while hub alive.
25. Long soak test 12-24 hours.
```

## После фазы обновить документацию

Обновить:

```text
docs/SUPPORT_RUNBOOK.md
docs/KNOWN_ISSUES.md
docs/CHANGELOG.md
README.md
scenehub_node_v1/docs/NODE_PROVISIONING_QUICKSTART_RUS.md
```

Что указать:

```text
- confirmed hardware scenarios
- known unstable scenarios
- fallback limitations
- required operator recovery actions
```

---

# Что пока не делать

До закрытия owner/FSM не расширять систему новыми крупными слоями.

Не делать пока:

```text
1. RS-485 / ESP-NOW.
2. Hot-swap active rule bundle без reboot.
3. Полный сценарный движок комнаты на ноде.
4. Централизованный NFC cards CRUD в SceneHub.
5. Сложные ветвления fallback.
6. Синхронизацию state между несколькими нодами.
```

Особенно не делать hot-apply rules до отдельного owner-safe swap active compiled bundle.

---

# Приоритетный backlog

## P0

```text
1. Hub CMake: room_scenario/orchestrator_core -> quest_common.
2. Убрать external rule_engine calls из-под fallback status lock.
3. Синхронизировать docs: fallback alpha implemented, но ещё не field-stable baseline.
4. Проверить/добавить CI build для node firmware.
```

## P1

```text
5. Сделать node_control owner queue.
6. Централизовать JSON escaping и закрыть node_capability.
7. Зафиксировать command source policy в tests и документации, не ломая уже
   внедрённый LOCAL_RULE recursion guard.
```

## P2

```text
8. Расширить node-side tests boot-policy и более глубокими runtime cases.
9. Убрать pycache/build мусор из repo/archive.
10. Решить hub_seen_once/startup_grace_ms для fallback.
11. Перенести duplicate request cache из mqtt_transport в control/runtime owner, если начнёт расти число источников команд.
12. Постепенно дальше дробить оставшиеся крупные файлы:
    - node_rule_compile.c
    - node_rule_schema.c
    - node_capability.c
```

---

# Definition of Done

Node runtime architecture считается доведённой до нормального alpha/beta уровня, когда:

```text
1. Hub и Node собираются в CI.
2. CMake/include cycles = 0.
3. Missing direct deps = 0.
4. node_control является единственным hardware command owner.
5. fallback FSM не вызывает внешние runtime owner под lock.
6. command source policy покрыта тестами.
7. LOCAL_RULE recursion в exported mqtt_command запрещён.
8. fallback docs соответствуют коду и release-статусу:
   alpha implemented, но не field-stable baseline.
9. node_capability/status JSON не ломается на пользовательских строках.
10. Есть node-side unit/regression tests.
11. Есть field verification checklist с результатами.
```

---

# Короткая формула архитектуры

```text
Hub owns scenario.
Node owns hardware.

MQTT/HTTP/NFC/rules do not own hardware.
They submit events/commands to owners.

Every runtime state has exactly one owner.
Locks protect local state only.
No external owner calls under local locks.
```
