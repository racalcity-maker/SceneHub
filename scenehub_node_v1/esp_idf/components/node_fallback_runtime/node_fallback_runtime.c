#include "node_fallback_runtime.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "node_rule_engine.h"

static const char *TAG = "node_fallback";

enum {
    NODE_FALLBACK_QUEUE_LEN = 8,
    NODE_FALLBACK_TASK_STACK_WORDS = 3072,
    NODE_FALLBACK_TICK_MS = 250,
};

typedef enum {
    NODE_FALLBACK_REQ_CONFIGURE = 0,
    NODE_FALLBACK_REQ_WIFI_STATE,
    NODE_FALLBACK_REQ_MQTT_STATE,
} node_fallback_request_kind_t;

typedef struct {
    node_fallback_request_kind_t kind;
    bool connected;
    node_fallback_runtime_config_t config;
} node_fallback_request_t;

static bool s_initialized;
static node_fallback_runtime_status_t s_status;
static StaticTask_t s_task_storage;
static StackType_t s_task_stack[NODE_FALLBACK_TASK_STACK_WORDS];
static TaskHandle_t s_task_handle;
static StaticQueue_t s_queue_storage;
static uint8_t s_queue_buffer[NODE_FALLBACK_QUEUE_LEN * sizeof(node_fallback_request_t)];
static QueueHandle_t s_queue;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

const char *node_fallback_runtime_state_name(node_fallback_runtime_state_t state)
{
    switch (state) {
    case NODE_FALLBACK_RUNTIME_STATE_HUB_OFFLINE_PENDING:
        return "hub_offline_pending";
    case NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE:
        return "fallback_active";
    case NODE_FALLBACK_RUNTIME_STATE_HUB_RETURN_PENDING:
        return "hub_return_pending";
    case NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY:
    default:
        return "hub_primary";
    }
}

const char *node_fallback_runtime_return_policy_name(node_fallback_runtime_return_policy_t policy)
{
    switch (policy) {
    case NODE_FALLBACK_RETURN_POLICY_MANUAL_STAY_ACTIVE:
        return "manual_stay_active";
    case NODE_FALLBACK_RETURN_POLICY_AUTO_ON_STABLE_MQTT:
    default:
        return "auto_on_stable_mqtt";
    }
}

static bool hub_available(void)
{
    return s_status.wifi_ready && s_status.mqtt_connected;
}

static void set_state(node_fallback_runtime_state_t state, uint32_t deadline_ms)
{
    s_status.state = state;
    s_status.state_since_ms = now_ms();
    s_status.deadline_ms = deadline_ms;
    ESP_LOGI(TAG,
             "state=%s wifi=%d mqtt=%d rules_active=%d deadline_ms=%lu",
             node_fallback_runtime_state_name(state),
             s_status.wifi_ready,
             s_status.mqtt_connected,
             s_status.fallback_rules_active,
             (unsigned long)deadline_ms);
}

static void pause_fallback_rules(void)
{
    esp_err_t err = ESP_OK;

    if (!s_status.fallback_rules_active) {
        return;
    }

    err = node_rule_engine_pause();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fallback exit pause failed: %s", esp_err_to_name(err));
        return;
    }
    err = node_rule_engine_set_runtime_enabled(false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fallback exit disable failed: %s", esp_err_to_name(err));
        return;
    }

    s_status.fallback_rules_active = false;
    ESP_LOGI(TAG, "fallback rules paused");
}

static void resume_fallback_rules(void)
{
    esp_err_t err = ESP_OK;

    if (s_status.fallback_rules_active) {
        return;
    }

    err = node_rule_engine_set_runtime_enabled(true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fallback entry enable failed: %s", esp_err_to_name(err));
        return;
    }

    err = node_rule_engine_reset();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fallback entry reset failed: %s", esp_err_to_name(err));
        return;
    }

    err = node_rule_engine_resume();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fallback entry resume failed: %s", esp_err_to_name(err));
        return;
    }

    s_status.fallback_rules_active = true;
    ESP_LOGI(TAG, "fallback rules resumed");
}

static void evaluate_state(void)
{
    uint32_t now = now_ms();
    bool available = hub_available();

    if (!s_status.enabled) {
        pause_fallback_rules();
        if (s_status.state != NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY) {
            set_state(NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY, 0);
        }
        return;
    }

    switch (s_status.state) {
    case NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY:
        if (!available) {
            if (s_status.fallback_timeout_ms == 0) {
                return;
            }
            set_state(NODE_FALLBACK_RUNTIME_STATE_HUB_OFFLINE_PENDING,
                      now + s_status.fallback_timeout_ms);
        }
        break;

    case NODE_FALLBACK_RUNTIME_STATE_HUB_OFFLINE_PENDING:
        if (available) {
            set_state(NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY, 0);
            break;
        }
        if (time_reached(now, s_status.deadline_ms)) {
            resume_fallback_rules();
            set_state(NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE, 0);
        }
        break;

    case NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE:
        if (available) {
            if (s_status.return_policy == NODE_FALLBACK_RETURN_POLICY_MANUAL_STAY_ACTIVE) {
                break;
            }
            if (s_status.fallback_return_delay_ms == 0) {
                pause_fallback_rules();
                set_state(NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY, 0);
                break;
            }
            set_state(NODE_FALLBACK_RUNTIME_STATE_HUB_RETURN_PENDING,
                      now + s_status.fallback_return_delay_ms);
        }
        break;

    case NODE_FALLBACK_RUNTIME_STATE_HUB_RETURN_PENDING:
        if (!available) {
            set_state(NODE_FALLBACK_RUNTIME_STATE_FALLBACK_ACTIVE, 0);
            break;
        }
        if (time_reached(now, s_status.deadline_ms)) {
            pause_fallback_rules();
            set_state(NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY, 0);
        }
        break;

    default:
        set_state(NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY, 0);
        break;
    }
}

static void apply_config(const node_fallback_runtime_config_t *config)
{
    if (!config) {
        memset(&s_status, 0, sizeof(s_status));
        s_status.initialized = s_initialized;
        set_state(NODE_FALLBACK_RUNTIME_STATE_HUB_PRIMARY, 0);
        return;
    }

    s_status.enabled = config->enabled;
    s_status.fallback_timeout_ms = config->fallback_timeout_ms;
    s_status.fallback_return_delay_ms = config->fallback_return_delay_ms;
    s_status.return_policy = config->return_policy;
    evaluate_state();
}

static void handle_request(const node_fallback_request_t *request)
{
    if (!request) {
        return;
    }

    switch (request->kind) {
    case NODE_FALLBACK_REQ_CONFIGURE:
        apply_config(&request->config);
        break;
    case NODE_FALLBACK_REQ_WIFI_STATE:
        s_status.wifi_ready = request->connected;
        evaluate_state();
        break;
    case NODE_FALLBACK_REQ_MQTT_STATE:
        s_status.mqtt_connected = request->connected;
        evaluate_state();
        break;
    default:
        break;
    }
}

static void fallback_task(void *arg)
{
    (void)arg;

    while (true) {
        node_fallback_request_t request = {0};

        if (xQueueReceive(s_queue, &request, pdMS_TO_TICKS(NODE_FALLBACK_TICK_MS)) == pdTRUE) {
            handle_request(&request);
            while (xQueueReceive(s_queue, &request, 0) == pdTRUE) {
                handle_request(&request);
            }
        }
        evaluate_state();
    }
}

static bool ensure_queue(void)
{
    if (s_queue) {
        return true;
    }

    s_queue = xQueueCreateStatic(NODE_FALLBACK_QUEUE_LEN,
                                 sizeof(node_fallback_request_t),
                                 s_queue_buffer,
                                 &s_queue_storage);
    return s_queue != NULL;
}

static bool ensure_task(void)
{
    if (s_task_handle) {
        return true;
    }

    s_task_handle = xTaskCreateStatic(fallback_task,
                                      "node_fallback",
                                      NODE_FALLBACK_TASK_STACK_WORDS,
                                      NULL,
                                      tskIDLE_PRIORITY + 1,
                                      s_task_stack,
                                      &s_task_storage);
    return s_task_handle != NULL;
}

static esp_err_t submit_request(const node_fallback_request_t *request)
{
    if (!request || !s_initialized || !s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_queue, request, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t node_fallback_runtime_init(void)
{
    node_fallback_runtime_config_t disabled = {0};

    if (s_initialized) {
        return ESP_OK;
    }
    if (!ensure_queue() || !ensure_task()) {
        return ESP_ERR_NO_MEM;
    }

    memset(&s_status, 0, sizeof(s_status));
    s_initialized = true;
    s_status.initialized = true;
    apply_config(&disabled);
    return ESP_OK;
}

esp_err_t node_fallback_runtime_configure(const node_fallback_runtime_config_t *config)
{
    node_fallback_request_t request = {0};

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    request.kind = NODE_FALLBACK_REQ_CONFIGURE;
    request.config = *config;
    return submit_request(&request);
}

esp_err_t node_fallback_runtime_note_wifi_state(bool connected)
{
    node_fallback_request_t request = {
        .kind = NODE_FALLBACK_REQ_WIFI_STATE,
        .connected = connected,
    };

    return submit_request(&request);
}

esp_err_t node_fallback_runtime_note_mqtt_state(bool connected)
{
    node_fallback_request_t request = {
        .kind = NODE_FALLBACK_REQ_MQTT_STATE,
        .connected = connected,
    };

    return submit_request(&request);
}

void node_fallback_runtime_get_status(node_fallback_runtime_status_t *out_status)
{
    if (!out_status) {
        return;
    }
    *out_status = s_status;
}
