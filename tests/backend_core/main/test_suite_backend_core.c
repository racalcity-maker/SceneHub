#include "scenehub_unity_runner.h"

extern void register_service_status_tests(void);
extern void register_event_bus_tests(void);
extern void register_config_store_utils_tests(void);
extern void register_audio_player_state_tests(void);
extern void register_ota_manager_tests(void);
extern void register_quest_device_tests(void);
extern void register_room_catalog_tests(void);
extern void register_room_scenario_tests(void);
extern void register_gm_game_profile_tests(void);

static const scenehub_test_register_fn_t s_registrars[] = {
    register_service_status_tests,
    register_event_bus_tests,
    register_config_store_utils_tests,
    register_audio_player_state_tests,
    register_ota_manager_tests,
    register_quest_device_tests,
    register_room_catalog_tests,
    register_room_scenario_tests,
    register_gm_game_profile_tests,
};

static const scenehub_test_suite_t s_suite = {
    .registrars = s_registrars,
    .registrar_count = sizeof(s_registrars) / sizeof(s_registrars[0]),
};

void app_main(void)
{
    scenehub_test_runner_start(&s_suite);
}
