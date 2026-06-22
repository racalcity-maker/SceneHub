#include "node_fallback_runtime.h"

#include <string.h>

#include "node_fallback_policy.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
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
static StaticSemaphore_t s_status_lock_storage;
static SemaphoreHandle_t s_status_lock;

static bool ensure_status_lock(void)
{
    if (!s_status_lock) {
        s_status_lock = xSemaphoreCreateMutexStatic(&s_status_lock_storage);
    }
    return s_status_lock != NULL;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
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

static esp_err_t pause_fallback_rules_unlocked(void)
{
    esp_err_t err = ESP_OK;

    err = node_rule_engine_pause();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fallback exit pause failed: %s", esp_err_to_name(err));
        return err;
    }
    err = node_rule_engine_set_runtime_enabled(false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fallback exit disable failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "fallback rules paused");
    return ESP_OK;
}

static esp_err_t resume_fallback_rules_unlocked(void)
{
    esp_err_t err = ESP_OK;

    err = node_rule_engine_set_runtime_enabled(true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fallback entry enable failed: %s", esp_err_to_name(err));
        return err;
    }

    err = node_rule_engine_reset();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fallback entry reset failed: %s", esp_err_to_name(err));
        return err;
    }

    err = node_rule_engine_resume();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fallback entry resume failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "fallback rules resumed");
    return ESP_OK;
}

static node_fallback_policy_transition_t evaluate_state_locked(void)
{
    node_fallback_policy_transition_t transition = {0};

    node_fallback_policy_evaluate(&s_status, now_ms(), &transition);
    return transition;
}

static esp_err_t perform_transition_action_unlocked(node_fallback_policy_action_t action)
{
    switch (action) {
    case NODE_FALLBACK_POLICY_ACTION_ENTER_FALLBACK:
        return resume_fallback_rules_unlocked();
    case NODE_FALLBACK_POLICY_ACTION_EXIT_FALLBACK:
        return pause_fallback_rules_unlocked();
    case NODE_FALLBACK_POLICY_ACTION_NONE:
    default:
        return ESP_OK;
    }
}

static void finalize_transition_locked(const node_fallback_policy_transition_t *transition,
                                       esp_err_t action_err)
{
    if (!transition) {
        return;
    }
    if (transition->action != NODE_FALLBACK_POLICY_ACTION_NONE && action_err != ESP_OK) {
        return;
    }

    if (transition->action == NODE_FALLBACK_POLICY_ACTION_ENTER_FALLBACK) {
        s_status.fallback_rules_active = true;
    } else if (transition->action == NODE_FALLBACK_POLICY_ACTION_EXIT_FALLBACK) {
        s_status.fallback_rules_active = false;
    }

    if (transition->state_changed) {
        set_state(transition->next_state, transition->next_deadline_ms);
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
        break;
    case NODE_FALLBACK_REQ_MQTT_STATE:
        s_status.mqtt_connected = request->connected;
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
        node_fallback_policy_transition_t transition = {0};
        esp_err_t action_err = ESP_OK;

        if (xQueueReceive(s_queue, &request, pdMS_TO_TICKS(NODE_FALLBACK_TICK_MS)) == pdTRUE) {
            if (!ensure_status_lock()) {
                continue;
            }
            xSemaphoreTake(s_status_lock, portMAX_DELAY);
            handle_request(&request);
            while (xQueueReceive(s_queue, &request, 0) == pdTRUE) {
                handle_request(&request);
            }
            xSemaphoreGive(s_status_lock);
        }
        if (!ensure_status_lock()) {
            continue;
        }
        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        transition = evaluate_state_locked();
        xSemaphoreGive(s_status_lock);

        action_err = perform_transition_action_unlocked(transition.action);

        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        finalize_transition_locked(&transition, action_err);
        xSemaphoreGive(s_status_lock);
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
    if (!ensure_queue() || !ensure_status_lock() || !ensure_task()) {
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    memset(&s_status, 0, sizeof(s_status));
    s_initialized = true;
    s_status.initialized = true;
    apply_config(&disabled);
    xSemaphoreGive(s_status_lock);
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
    if (!ensure_status_lock()) {
        memset(out_status, 0, sizeof(*out_status));
        return;
    }
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    *out_status = s_status;
    xSemaphoreGive(s_status_lock);
}
