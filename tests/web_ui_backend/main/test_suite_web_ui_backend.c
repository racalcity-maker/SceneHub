#include "scenehub_unity_runner.h"

#include "gm_room_session_internal.h"
#include "scenehub_control.h"

extern void register_web_ui_contract_tests(void);
extern void register_web_ui_handler_tests(void);
extern void register_orchestrator_registry_tests(void);
extern void register_orchestrator_audit_tests(void);
extern void register_orchestrator_timeline_tests(void);

static const scenehub_test_register_fn_t s_registrars[] = {
    register_web_ui_contract_tests,
    register_web_ui_handler_tests,
    register_orchestrator_registry_tests,
    register_orchestrator_audit_tests,
    register_orchestrator_timeline_tests,
};

static const scenehub_test_suite_t s_suite = {
    .registrars = s_registrars,
    .registrar_count = sizeof(s_registrars) / sizeof(s_registrars[0]),
};

void app_main(void)
{
    gm_room_session_set_async_workers_enabled_for_test(false);
    scenehub_control_set_persistence_enabled_for_test(false);
    scenehub_test_runner_start(&s_suite);
}
