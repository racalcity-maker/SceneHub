#include "scenehub_unity_runner.h"

#include "gm_room_session_internal.h"

extern void register_gm_core_primitive_tests(void);
extern void register_gm_room_session_tests(void);
extern void register_gm_room_session_runtime_chaos_tests(void);
extern void register_integration_quest_flow_tests(void);

static const scenehub_test_register_fn_t s_registrars[] = {
    register_gm_core_primitive_tests,
    register_gm_room_session_tests,
    register_gm_room_session_runtime_chaos_tests,
    register_integration_quest_flow_tests,
};

static const scenehub_test_suite_t s_suite = {
    .registrars = s_registrars,
    .registrar_count = sizeof(s_registrars) / sizeof(s_registrars[0]),
};

void app_main(void)
{
    gm_room_session_set_async_workers_enabled_for_test(false);
    scenehub_test_runner_start(&s_suite);
}
