#include "scenehub_unity_runner.h"

extern void register_command_executor_tests(void);
extern void register_device_control_ingest_tests(void);

static const scenehub_test_register_fn_t s_registrars[] = {
    register_command_executor_tests,
    register_device_control_ingest_tests,
};

static const scenehub_test_suite_t s_suite = {
    .registrars = s_registrars,
    .registrar_count = sizeof(s_registrars) / sizeof(s_registrars[0]),
};

void app_main(void)
{
    scenehub_test_runner_start(&s_suite);
}
