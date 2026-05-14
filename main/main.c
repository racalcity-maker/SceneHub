#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_err.h"

#include "config_store.h"
#include "network.h"
#include "mqtt_core.h"
#include "web_ui.h"
#include "audio_player.h"
#include "event_bus.h"
#include "device_control_ingest.h"
#include "error_monitor.h"
#include "ota_manager.h"
#include "gm_game_profile.h"
#include "hardware_io.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "scenehub_state.h"
#include "service_status.h"
#include "esp_task_wdt.h"
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static const char *TAG = "app_main";

static void app_abort_startup(const char *step, esp_err_t err)
{
    ESP_LOGE(TAG, "startup fatal: %s failed: %s", step ? step : "unknown", esp_err_to_name(err));
    abort();
}

static void app_abort_task_startup(const char *task_name)
{
    ESP_LOGE(TAG, "startup fatal: failed to create task %s", task_name ? task_name : "unknown");
    abort();
}

static void app_abort_background_startup(const char *step, esp_err_t err)
{
    ESP_LOGE(TAG, "background startup fatal: %s failed: %s",
             step ? step : "unknown",
             esp_err_to_name(err));
    abort();
}

static void app_remove_current_task_from_wdt(bool wdt_registered, const char *task_name)
{
    if (!wdt_registered) {
        return;
    }

    esp_err_t err = esp_task_wdt_delete(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to remove %s from task WDT: %s",
                 task_name ? task_name : "task",
                 esp_err_to_name(err));
    }
}

static void app_init_task_wdt(void)
{
    static const esp_task_wdt_config_t twdt_cfg = {
        .timeout_ms = 10000,
        .idle_core_mask = 0,
        .trigger_panic = false,
    };

    esp_err_t err = esp_task_wdt_init(&twdt_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "task WDT initialized");
        return;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "task WDT already initialized");
        return;
    }
    ESP_LOGE(TAG, "task WDT init failed: %s", esp_err_to_name(err));
}

static bool app_create_task_checked(TaskFunction_t fn,
                                    const char *name,
                                    uint32_t stack_size,
                                    void *param,
                                    UBaseType_t priority,
                                    bool fatal)
{
    BaseType_t ok = xTaskCreate(fn, name, stack_size, param, priority, NULL);
    if (ok == pdPASS) {
        ESP_LOGI(TAG, "task %s created", name ? name : "unknown");
        return true;
    }
    if (fatal) {
        app_abort_task_startup(name);
    }
    ESP_LOGE(TAG, "failed to create task %s", name ? name : "unknown");
    return false;
}

#if (configUSE_TRACE_FACILITY == 1)
static void stats_task(void *param)
{
    (void)param;
    const TickType_t period = pdMS_TO_TICKS(10000);
    const uint32_t low_heap_threshold = 50U * 1024U;
    const uint32_t low_largest_block_threshold = 24U * 1024U;
    bool low_heap_active = false;
    uint32_t lowest_logged_heap_int = UINT32_MAX;
    uint32_t lowest_logged_largest_int = UINT32_MAX;
    uint32_t prev_total_runtime = 0;
    uint32_t prev_idle_runtime = 0;
    bool have_prev_runtime = false;

    vTaskDelay(period);
    while (1) {
        UBaseType_t count = uxTaskGetNumberOfTasks();
        TaskStatus_t *tasks = calloc(count, sizeof(TaskStatus_t));
        uint32_t total = 0;
        uint32_t idle = 0;
        if (tasks) {
            count = uxTaskGetSystemState(tasks, count, &total);
            for (UBaseType_t i = 0; i < count; ++i) {
                const char *name = tasks[i].pcTaskName;
                if (name && strstr(name, "IDLE")) {
                    idle += tasks[i].ulRunTimeCounter;
                }
            }
        }

        float cpu = 0.0f;
        if (have_prev_runtime) {
            uint32_t delta_total = total - prev_total_runtime;
            uint32_t delta_idle = idle - prev_idle_runtime;
            if (delta_total > 0 && delta_idle <= delta_total) {
                cpu = ((float)(delta_total - delta_idle) * 100.0f) / (float)delta_total;
            }
        }
        prev_total_runtime = total;
        prev_idle_runtime = idle;
        have_prev_runtime = total > 0;

        uint32_t heap_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        uint32_t largest_int = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        uint32_t heap_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        uint32_t min_int = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

        bool current_low = heap_int < low_heap_threshold || largest_int < low_largest_block_threshold;
        bool new_low_watermark = heap_int + 4096U < lowest_logged_heap_int ||
                                 largest_int + 4096U < lowest_logged_largest_int;
        if (current_low && (!low_heap_active || new_low_watermark)) {
            ESP_LOGW("stats",
                     "low heap now: heap_int=%" PRIu32 " largest_int=%" PRIu32 " min_int=%" PRIu32
                     " psram=%" PRIu32 " cpu=%.1f%% tasks=%" PRIu32,
                     heap_int,
                     largest_int,
                     min_int,
                     heap_spiram,
                     cpu,
                     (uint32_t)count);
            low_heap_active = true;
            if (heap_int < lowest_logged_heap_int) {
                lowest_logged_heap_int = heap_int;
            }
            if (largest_int < lowest_logged_largest_int) {
                lowest_logged_largest_int = largest_int;
            }
        } else if (!current_low && low_heap_active) {
            ESP_LOGI("stats",
                     "heap recovered: heap_int=%" PRIu32 " largest_int=%" PRIu32 " min_int=%" PRIu32
                     " psram=%" PRIu32 " cpu=%.1f%% tasks=%" PRIu32,
                     heap_int,
                     largest_int,
                     min_int,
                     heap_spiram,
                     cpu,
                     (uint32_t)count);
            low_heap_active = false;
            lowest_logged_heap_int = UINT32_MAX;
            lowest_logged_largest_int = UINT32_MAX;
        }
        if (tasks) {
            free(tasks);
        }
        vTaskDelay(period);
    }
}
#endif  // configUSE_TRACE_FACILITY

static esp_err_t app_init_service(const char *name, service_status_id_t id, esp_err_t (*fn)(void))
{
    esp_err_t err = fn ? fn() : ESP_ERR_INVALID_ARG;
    service_status_mark_init(id, err);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "init %s ok", name ? name : "service");
    } else {
        ESP_LOGE(TAG, "init %s failed: %s", name ? name : "service", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t app_start_service(const char *name, service_status_id_t id, esp_err_t (*fn)(void))
{
    esp_err_t err = fn ? fn() : ESP_ERR_INVALID_ARG;
    service_status_mark_start(id, err);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "start %s ok", name ? name : "service");
    } else {
        ESP_LOGE(TAG, "start %s failed: %s", name ? name : "service", esp_err_to_name(err));
    }
    return err;
}

static bool app_init_optional(const char *name, service_status_id_t id, esp_err_t (*fn)(void))
{
    return app_init_service(name, id, fn) == ESP_OK;
}

static bool app_start_optional(const char *name, service_status_id_t id, esp_err_t (*fn)(void))
{
    return app_start_service(name, id, fn) == ESP_OK;
}

static void app_init_deferred_fatal(const char *name, service_status_id_t id, esp_err_t (*fn)(void))
{
    esp_err_t err = app_init_service(name, id, fn);
    if (err != ESP_OK) {
        app_abort_background_startup(name, err);
    }
}

static void app_start_deferred_fatal(const char *name, service_status_id_t id, esp_err_t (*fn)(void))
{
    esp_err_t err = app_start_service(name, id, fn);
    if (err != ESP_OK) {
        app_abort_background_startup(name, err);
    }
}

static void product_bootstrap_task(void *param)
{
    bool wdt_registered = false;
    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err == ESP_OK) {
        wdt_registered = true;
    } else {
        ESP_LOGW(TAG, "failed to add product_boot to task WDT: %s", esp_err_to_name(wdt_err));
    }
    ESP_LOGI(TAG, "deferred fatal bootstrap start");

    app_init_deferred_fatal("network", SERVICE_STATUS_NETWORK, network_init);
    app_start_deferred_fatal("network", SERVICE_STATUS_NETWORK, network_start);

    app_init_deferred_fatal("mqtt_core", SERVICE_STATUS_MQTT, mqtt_core_init);
    app_start_deferred_fatal("mqtt_core", SERVICE_STATUS_MQTT, mqtt_core_start);

    app_init_deferred_fatal("web_ui", SERVICE_STATUS_WEB_UI, web_ui_init);
    app_start_deferred_fatal("web_ui", SERVICE_STATUS_WEB_UI, web_ui_start);

    ESP_LOGI(TAG, "quest runtime bootstrap complete");
    ota_manager_notify_system_ready();

    app_remove_current_task_from_wdt(wdt_registered, "product_boot");
    vTaskDelete(NULL);
}

void app_main(void)
{
    app_init_task_wdt();

    ESP_LOGI(TAG, "init nvs_flash");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "init ota_manager");
    ESP_ERROR_CHECK(ota_manager_init());
    ESP_LOGI(TAG, "ota boot notify");
    ESP_ERROR_CHECK(ota_manager_notify_boot());
    ESP_LOGI(TAG, "init config_store");
    ESP_ERROR_CHECK(config_store_init());
    ESP_LOGI(TAG, "init service_status");
    ESP_ERROR_CHECK(service_status_init());
    ESP_LOGI(TAG, "init event_bus");
    ESP_ERROR_CHECK(app_init_service("event_bus", SERVICE_STATUS_EVENT_BUS, event_bus_init));
    ESP_LOGI(TAG, "init device_control_ingest");
    ESP_ERROR_CHECK(device_control_ingest_init());
    ESP_LOGI(TAG, "init error_monitor");
    ESP_ERROR_CHECK(error_monitor_init());
    ESP_LOGI(TAG, "init optional services");
    bool audio_ok = app_init_optional("audio_player", SERVICE_STATUS_AUDIO, audio_player_init);

    if (!audio_ok) {
        error_monitor_report_audio_fault();
    }
    ESP_LOGI(TAG, "init hardware_io");
    bool hardware_io_ok = app_init_optional("hardware_io", SERVICE_STATUS_HARDWARE_IO, hardware_io_init);
    if (!hardware_io_ok) {
        ESP_LOGW(TAG, "hardware_io unavailable; web UI and quest runtime will continue");
    }

    ESP_LOGI(TAG, "start event_bus");
    esp_err_t err = app_start_service("event_bus", SERVICE_STATUS_EVENT_BUS, event_bus_start);
    if (err != ESP_OK) {
        app_abort_startup("event_bus_start", err);
    }
    if (audio_ok) {
        ESP_LOGI(TAG, "start audio_player");
        if (!app_start_optional("audio_player", SERVICE_STATUS_AUDIO, audio_player_start)) {
            error_monitor_report_audio_fault();
        }
    } else {
        ESP_LOGW(TAG, "skip audio_player start: init failed");
    }

    ESP_LOGI(TAG, "init quest_device");
    err = quest_device_init();
    if (err == ESP_OK) {
        err = quest_device_load();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "quest devices loaded from %s", QUEST_DEVICE_STORAGE_PATH);
        } else if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "quest device file not found: %s", QUEST_DEVICE_STORAGE_PATH);
        } else {
            ESP_LOGW(TAG, "quest device load skipped: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "quest_device init failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "init room_catalog");
    err = room_catalog_init();
    if (err == ESP_OK) {
        err = room_catalog_load();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "rooms loaded from %s", ROOM_CATALOG_STORAGE_PATH);
        } else if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "room catalog file not found: %s", ROOM_CATALOG_STORAGE_PATH);
        } else {
            ESP_LOGW(TAG, "room catalog load skipped: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "room_catalog init failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "init room_scenario");
    err = room_scenario_init();
    if (err == ESP_OK) {
        err = room_scenario_store_load();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "room scenarios loaded from %s", ROOM_SCENARIO_STORAGE_PATH);
        } else if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "room scenario file not found: %s", ROOM_SCENARIO_STORAGE_PATH);
        } else {
            ESP_LOGW(TAG, "room scenario load skipped: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "room_scenario init failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "init gm_game_profile");
    err = gm_game_profile_init();
    if (err == ESP_OK) {
        err = gm_game_profile_load();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "game profiles loaded from %s", GM_GAME_PROFILE_STORAGE_PATH);
        } else if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "game profile file not found: %s", GM_GAME_PROFILE_STORAGE_PATH);
        } else {
            ESP_LOGW(TAG, "game profile load skipped: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "gm_game_profile init failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "init scenehub_state");
    err = scenehub_state_init();
    if (err != ESP_OK) {
        app_abort_startup("scenehub_state_init", err);
    }

#if (configUSE_TRACE_FACILITY == 1)
    (void)app_create_task_checked(stats_task, "stats_task", 4096, NULL, 3, false);
#else
    ESP_LOGW(TAG, "stats task disabled (configUSE_TRACE_FACILITY not enabled)");
#endif
    ESP_LOGI(TAG, "SceneHub started");
    ESP_LOGI(TAG, "create deferred fatal bootstrap task");
    (void)app_create_task_checked(product_bootstrap_task, "product_boot", 8192, NULL, 3, true);
    vTaskDelete(NULL);
}
