#include "audio_player_internal.h"

#include <string.h>

#include "esp_check.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "error_monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sd_storage.h"

#define AUDIO_QUEUE_LEN 4
#define AUDIO_READER_STOP_TIMEOUT_MS 500
#define AUDIO_BACKGROUND_FADE_OUT_MS 2500
#define AUDIO_EFFECT_STOP_FADE_OUT_MS 96
#define AUDIO_STOP_FADE_OUT_POLL_MS 10
#define AUDIO_STOP_FADE_OUT_SLACK_MS 250
#define AUDIO_RUNTIME_TASK_STACK 8192
#define AUDIO_READER_BG_STACK 8192
#define AUDIO_READER_FX_STACK 8192
#define AUDIO_READER_TASK_PRIORITY 5
#define AUDIO_FLAG_PAUSED BIT0
#define AUDIO_FLAG_BG_A_STOP_REQUESTED BIT1
#define AUDIO_FLAG_BG_B_STOP_REQUESTED BIT2
#define AUDIO_FLAG_FX_STOP_REQUESTED BIT3
#define AUDIO_BACKGROUND_PRIME_TIMEOUT_MS 1500

typedef enum {
    AUDIO_RUNTIME_SLOT_BG_A = 0,
    AUDIO_RUNTIME_SLOT_BG_B,
    AUDIO_RUNTIME_SLOT_EFFECT,
    AUDIO_RUNTIME_SLOT_COUNT,
} audio_runtime_slot_t;

typedef struct {
    TaskHandle_t task;
    TaskHandle_t waiter;
    bool done;
    int bitrate_kbps;
    char active_path[256];
} audio_channel_runtime_t;

static const char *TAG = "audio_player";

static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_runtime_lock = NULL;
static EventGroupHandle_t s_audio_flags = NULL;
static audio_channel_runtime_t s_channels[AUDIO_RUNTIME_SLOT_COUNT];
static EXT_RAM_BSS_ATTR audio_reader_ctx_t s_reader_ctxs[AUDIO_RUNTIME_SLOT_COUNT];
static bool s_reader_ctx_in_use[AUDIO_RUNTIME_SLOT_COUNT];
static audio_runtime_slot_t s_active_background_slot = AUDIO_RUNTIME_SLOT_BG_A;
static audio_runtime_state_t s_runtime_state = AUDIO_RUNTIME_IDLE;

static audio_player_channel_t normalize_channel(audio_player_channel_t channel)
{
    return channel == AUDIO_PLAYER_CHANNEL_BACKGROUND ? AUDIO_PLAYER_CHANNEL_BACKGROUND : AUDIO_PLAYER_CHANNEL_EFFECT;
}

static bool runtime_slot_is_background(audio_runtime_slot_t slot)
{
    return slot == AUDIO_RUNTIME_SLOT_BG_A || slot == AUDIO_RUNTIME_SLOT_BG_B;
}

static audio_runtime_slot_t alternate_background_slot(void)
{
    return s_active_background_slot == AUDIO_RUNTIME_SLOT_BG_A ?
        AUDIO_RUNTIME_SLOT_BG_B : AUDIO_RUNTIME_SLOT_BG_A;
}

static EventBits_t slot_stop_bit(audio_runtime_slot_t slot)
{
    switch (slot) {
    case AUDIO_RUNTIME_SLOT_BG_A:
        return AUDIO_FLAG_BG_A_STOP_REQUESTED;
    case AUDIO_RUNTIME_SLOT_BG_B:
        return AUDIO_FLAG_BG_B_STOP_REQUESTED;
    case AUDIO_RUNTIME_SLOT_EFFECT:
    default:
        return AUDIO_FLAG_FX_STOP_REQUESTED;
    }
}

static audio_mixer_channel_t mixer_channel_for_slot(audio_runtime_slot_t slot)
{
    switch (slot) {
    case AUDIO_RUNTIME_SLOT_BG_A:
        return AUDIO_MIXER_CHANNEL_BACKGROUND_A;
    case AUDIO_RUNTIME_SLOT_BG_B:
        return AUDIO_MIXER_CHANNEL_BACKGROUND_B;
    case AUDIO_RUNTIME_SLOT_EFFECT:
    default:
        return AUDIO_MIXER_CHANNEL_EFFECT;
    }
}

static void audio_flags_set(EventBits_t flags)
{
    if (s_audio_flags) {
        xEventGroupSetBits(s_audio_flags, flags);
    }
}

static void audio_flags_clear(EventBits_t flags)
{
    if (s_audio_flags) {
        xEventGroupClearBits(s_audio_flags, flags);
    }
}

static bool runtime_lock(void)
{
    return s_runtime_lock && xSemaphoreTake(s_runtime_lock, portMAX_DELAY) == pdTRUE;
}

static void runtime_unlock(void)
{
    if (s_runtime_lock) {
        xSemaphoreGive(s_runtime_lock);
    }
}


static void runtime_set_state(audio_runtime_state_t state)
{
    audio_player_status_set_runtime_state(state);
    if (runtime_lock()) {
        s_runtime_state = state;
        runtime_unlock();
    } else {
        s_runtime_state = state;
    }
}

static bool channel_has_reader(audio_player_channel_t channel)
{
    bool has_reader = false;
    bool bg_a_reader = false;
    bool bg_b_reader = false;

    channel = normalize_channel(channel);

    if (runtime_lock()) {
        if (channel == AUDIO_PLAYER_CHANNEL_BACKGROUND) {
            bg_a_reader = s_channels[AUDIO_RUNTIME_SLOT_BG_A].task != NULL &&
                          !s_channels[AUDIO_RUNTIME_SLOT_BG_A].done;
            bg_b_reader = s_channels[AUDIO_RUNTIME_SLOT_BG_B].task != NULL &&
                          !s_channels[AUDIO_RUNTIME_SLOT_BG_B].done;
        } else {
            has_reader = s_channels[AUDIO_RUNTIME_SLOT_EFFECT].task != NULL &&
                         !s_channels[AUDIO_RUNTIME_SLOT_EFFECT].done;
        }
        runtime_unlock();
    }

    if (channel == AUDIO_PLAYER_CHANNEL_BACKGROUND) {
        bool bg_a_mixer = audio_player_mixer_stream_active(AUDIO_MIXER_CHANNEL_BACKGROUND_A);
        bool bg_b_mixer = audio_player_mixer_stream_active(AUDIO_MIXER_CHANNEL_BACKGROUND_B);
        has_reader = bg_a_reader || bg_b_reader || bg_a_mixer || bg_b_mixer;
    }

    return has_reader;
}

static void start_slot_fade_out(audio_runtime_slot_t slot, int duration_ms)
{
    audio_player_mixer_fade_out_stream(
        mixer_channel_for_slot(slot),
        duration_ms
    );
}

static void wait_slot_fade_out_complete(audio_runtime_slot_t slot, int duration_ms)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(duration_ms + AUDIO_STOP_FADE_OUT_SLACK_MS);
    audio_mixer_channel_t mixer_channel = mixer_channel_for_slot(slot);

    while (audio_player_mixer_fade_out_active(mixer_channel)) {
        if ((xTaskGetTickCount() - start) > timeout) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(AUDIO_STOP_FADE_OUT_POLL_MS));
    }
}

static void wait_slot_fade_out(audio_runtime_slot_t slot, int duration_ms)
{
    start_slot_fade_out(slot, duration_ms);
    wait_slot_fade_out_complete(slot, duration_ms);
}

static void wait_background_fade_out(audio_runtime_slot_t slot)
{
    wait_slot_fade_out(slot, AUDIO_BACKGROUND_FADE_OUT_MS);
}

static void wait_effect_fade_out(void)
{
    wait_slot_fade_out(AUDIO_RUNTIME_SLOT_EFFECT, AUDIO_EFFECT_STOP_FADE_OUT_MS);
}

static bool wait_mixer_primed(audio_mixer_channel_t channel, int timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    while (!audio_player_mixer_stream_primed(channel)) {
        if ((xTaskGetTickCount() - start) > timeout) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

static audio_runtime_state_t runtime_get_state(void)
{
    audio_runtime_state_t state = s_runtime_state;
    if (runtime_lock()) {
        state = s_runtime_state;
        runtime_unlock();
    }
    return state;
}

static void runtime_set_effect_active_path(const char *path)
{
    audio_channel_runtime_t *ch = &s_channels[AUDIO_RUNTIME_SLOT_EFFECT];
    if (runtime_lock()) {
        if (path && path[0]) {
            strncpy(ch->active_path, path, sizeof(ch->active_path) - 1);
            ch->active_path[sizeof(ch->active_path) - 1] = 0;
        } else {
            ch->active_path[0] = 0;
        }
        runtime_unlock();
        return;
    }
    if (path && path[0]) {
        strncpy(ch->active_path, path, sizeof(ch->active_path) - 1);
        ch->active_path[sizeof(ch->active_path) - 1] = 0;
    } else {
        ch->active_path[0] = 0;
    }
}

static void runtime_clear_active_path_if_matches(audio_runtime_slot_t slot, const char *path)
{
    audio_channel_runtime_t *ch = &s_channels[slot];
    if (runtime_lock()) {
        if (!path || !path[0] || strcmp(ch->active_path, path) == 0) {
            ch->active_path[0] = 0;
        }
        runtime_unlock();
        return;
    }
    if (!path || !path[0] || strcmp(ch->active_path, path) == 0) {
        ch->active_path[0] = 0;
    }
}

static bool stop_reader_slot(audio_runtime_slot_t slot)
{
    audio_channel_runtime_t *ch = &s_channels[slot];
    TaskHandle_t waiter = xTaskGetCurrentTaskHandle();
    bool wait_needed = false;

    audio_flags_set(slot_stop_bit(slot));
    audio_player_mixer_stop_stream(mixer_channel_for_slot(slot));
    runtime_set_state(AUDIO_RUNTIME_STOPPING);

    if (runtime_lock()) {
        if (!ch->task) {
            ch->done = true;
            ch->waiter = NULL;
        } else if (!ch->done) {
            ch->waiter = waiter;
            wait_needed = true;
        }
        runtime_unlock();
    }

    if (wait_needed) {
        (void)ulTaskNotifyTake(pdTRUE, 0);
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AUDIO_READER_STOP_TIMEOUT_MS)) == 0) {
            ESP_LOGW(TAG, "reader stop timeout");
            audio_player_status_set_message("Audio stop timeout");
            error_monitor_report_audio_fault();
            if (runtime_lock()) {
                if (ch->waiter == waiter) {
                    ch->waiter = NULL;
                }
                runtime_unlock();
            }
            runtime_set_state(AUDIO_RUNTIME_ERROR);
            return false;
        }
    }

    return true;
}

static bool stop_reader(audio_player_channel_t channel)
{
    channel = normalize_channel(channel);
    if (channel == AUDIO_PLAYER_CHANNEL_BACKGROUND) {
        bool ok_a = stop_reader_slot(AUDIO_RUNTIME_SLOT_BG_A);
        bool ok_b = stop_reader_slot(AUDIO_RUNTIME_SLOT_BG_B);
        return ok_a && ok_b;
    }
    return stop_reader_slot(AUDIO_RUNTIME_SLOT_EFFECT);
}

static void handle_pause(void)
{
    audio_runtime_state_t state = runtime_get_state();
    if (state != AUDIO_RUNTIME_PLAYING && state != AUDIO_RUNTIME_STARTING) {
        return;
    }
    audio_flags_set(AUDIO_FLAG_PAUSED);
    s_channels[AUDIO_RUNTIME_SLOT_EFFECT].bitrate_kbps = 0;
    runtime_set_state(AUDIO_RUNTIME_PAUSED);
}

static void handle_resume(void)
{
    if (runtime_get_state() != AUDIO_RUNTIME_PAUSED) {
        return;
    }
    audio_flags_clear(AUDIO_FLAG_PAUSED);
    runtime_set_state(AUDIO_RUNTIME_PLAYING);
}

static void handle_stop_channel(audio_player_channel_t channel)
{
    channel = normalize_channel(channel);

    if (channel == AUDIO_PLAYER_CHANNEL_BACKGROUND) {
        wait_background_fade_out(AUDIO_RUNTIME_SLOT_BG_A);
        wait_background_fade_out(AUDIO_RUNTIME_SLOT_BG_B);
    } else if (channel == AUDIO_PLAYER_CHANNEL_EFFECT) {
        wait_effect_fade_out();
    }

    (void)stop_reader(channel);
    if (channel == AUDIO_PLAYER_CHANNEL_EFFECT) {
        runtime_set_effect_active_path(NULL);
        s_channels[AUDIO_RUNTIME_SLOT_EFFECT].bitrate_kbps = 0;
    }
    runtime_set_state(AUDIO_RUNTIME_IDLE);
}

static void handle_stop(void)
{
    audio_flags_clear(AUDIO_FLAG_PAUSED);
    start_slot_fade_out(AUDIO_RUNTIME_SLOT_BG_A, AUDIO_BACKGROUND_FADE_OUT_MS);
    start_slot_fade_out(AUDIO_RUNTIME_SLOT_BG_B, AUDIO_BACKGROUND_FADE_OUT_MS);
    start_slot_fade_out(AUDIO_RUNTIME_SLOT_EFFECT, AUDIO_EFFECT_STOP_FADE_OUT_MS);
    wait_slot_fade_out_complete(AUDIO_RUNTIME_SLOT_BG_A, AUDIO_BACKGROUND_FADE_OUT_MS);
    wait_slot_fade_out_complete(AUDIO_RUNTIME_SLOT_BG_B, AUDIO_BACKGROUND_FADE_OUT_MS);
    wait_slot_fade_out_complete(AUDIO_RUNTIME_SLOT_EFFECT, AUDIO_EFFECT_STOP_FADE_OUT_MS);
    (void)stop_reader(AUDIO_PLAYER_CHANNEL_BACKGROUND);
    (void)stop_reader(AUDIO_PLAYER_CHANNEL_EFFECT);
    runtime_set_effect_active_path(NULL);
    s_channels[AUDIO_RUNTIME_SLOT_EFFECT].bitrate_kbps = 0;
    runtime_set_state(AUDIO_RUNTIME_IDLE);
}

static bool start_reader_slot(const audio_cmd_t *cmd, audio_runtime_slot_t slot, bool audible)
{
    audio_player_channel_t channel = normalize_channel(cmd->channel);
    int volume = cmd->volume >= 0 ? cmd->volume : audio_player_runtime_volume();
    audio_reader_ctx_t *reader_ctx = audio_player_runtime_create_reader_ctx(cmd, slot);
    if (!reader_ctx) {
        ESP_LOGE(TAG, "reader ctx unavailable");
        error_monitor_report_audio_fault();
        runtime_set_state(AUDIO_RUNTIME_ERROR);
        return false;
    }

    audio_flags_clear(AUDIO_FLAG_PAUSED | slot_stop_bit(slot));
    if (audible) {
        audio_player_mixer_start_stream(mixer_channel_for_slot(slot));
    } else {
        audio_player_mixer_start_stream_muted(mixer_channel_for_slot(slot));
    }
    ESP_LOGD(TAG,
             "reader slot start: slot=%d mixer_channel=%d audible=%d path=%s",
             (int)slot,
             (int)mixer_channel_for_slot(slot),
             audible ? 1 : 0,
             cmd->path);

    TaskHandle_t reader_task = NULL;
    runtime_set_state(AUDIO_RUNTIME_STARTING);
    if (slot == AUDIO_RUNTIME_SLOT_EFFECT) {
        runtime_set_effect_active_path(cmd->path);
    }
    const uint32_t reader_stack = channel == AUDIO_PLAYER_CHANNEL_BACKGROUND ?
        AUDIO_READER_BG_STACK : AUDIO_READER_FX_STACK;
    BaseType_t ok = xTaskCreatePinnedToCore(audio_player_reader_task,
                                            runtime_slot_is_background(slot) ? "audio_bg" : "audio_fx",
                                            reader_stack,
                                            reader_ctx,
                                            AUDIO_READER_TASK_PRIORITY,
                                            &reader_task,
                                            1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "reader task create failed");
        audio_player_runtime_destroy_reader_ctx(reader_ctx);
        audio_player_mixer_stop_stream(mixer_channel_for_slot(slot));
        audio_player_output_play_tone(660, 150, volume);
        audio_player_output_play_tone(440, 150, volume);
        error_monitor_report_audio_fault();
        if (slot == AUDIO_RUNTIME_SLOT_EFFECT) {
            runtime_set_effect_active_path(NULL);
        }
        runtime_set_state(AUDIO_RUNTIME_ERROR);
        return false;
    }

    if (runtime_lock()) {
        s_channels[slot].task = reader_task;
        s_channels[slot].done = false;
        s_channels[slot].waiter = NULL;
        if (cmd->path[0]) {
            strncpy(s_channels[slot].active_path, cmd->path, sizeof(s_channels[slot].active_path) - 1);
            s_channels[slot].active_path[sizeof(s_channels[slot].active_path) - 1] = 0;
        }
        runtime_unlock();
    }
    return true;
}

static void handle_play(const audio_cmd_t *cmd)
{
    audio_player_channel_t channel = normalize_channel(cmd->channel);
    int volume = cmd->volume >= 0 ? cmd->volume : audio_player_runtime_volume();

    ESP_LOGD(TAG,
             "play request: channel=%d path=%s volume=%d repeat=%d seek=%.3f",
             (int)channel,
             cmd->path,
             volume,
             cmd->repeat ? 1 : 0,
             (double)cmd->seek_ratio);

    if (!sd_storage_available()) {
        ESP_LOGE(TAG, "SD not mounted, beep");
        audio_player_output_play_tone(660, 150, volume);
        audio_player_output_play_tone(440, 150, volume);
        error_monitor_report_sd_fault();
        runtime_set_state(AUDIO_RUNTIME_ERROR);
        return;
    }

    if (channel == AUDIO_PLAYER_CHANNEL_BACKGROUND) {
        bool switching = channel_has_reader(AUDIO_PLAYER_CHANNEL_BACKGROUND);
        audio_runtime_slot_t old_slot = s_active_background_slot;
        audio_runtime_slot_t next_slot = switching ? alternate_background_slot() : s_active_background_slot;

        ESP_LOGD(TAG,
                 "background play route: switching=%d old_slot=%d next_slot=%d old_active=%d next_active=%d",
                 switching ? 1 : 0,
                 (int)old_slot,
                 (int)next_slot,
                 audio_player_mixer_stream_active(mixer_channel_for_slot(old_slot)) ? 1 : 0,
                 audio_player_mixer_stream_active(mixer_channel_for_slot(next_slot)) ? 1 : 0);

        if (!switching) {
            (void)stop_reader_slot(next_slot);
        }
        if (!start_reader_slot(cmd, next_slot, !switching)) {
            return;
        }
        if (switching) {
            ESP_LOGD(TAG,
                     "background switch priming: old_slot=%d new_slot=%d",
                     (int)old_slot,
                     (int)next_slot);
            if (!wait_mixer_primed(mixer_channel_for_slot(next_slot), AUDIO_BACKGROUND_PRIME_TIMEOUT_MS)) {
                ESP_LOGW(TAG, "background switch prime timeout; keeping current background");
                (void)stop_reader_slot(next_slot);
                runtime_set_state(AUDIO_RUNTIME_PLAYING);
                return;
            }
            wait_background_fade_out(old_slot);
            (void)stop_reader_slot(old_slot);
            s_active_background_slot = next_slot;
            audio_player_mixer_set_stream_audible(mixer_channel_for_slot(next_slot), true);
            runtime_set_state(AUDIO_RUNTIME_PLAYING);
            ESP_LOGD(TAG, "background switch complete: active_slot=%d", (int)s_active_background_slot);
        }
        return;
    }

    wait_effect_fade_out();
    if (!stop_reader_slot(AUDIO_RUNTIME_SLOT_EFFECT)) {
        return;
    }
    (void)start_reader_slot(cmd, AUDIO_RUNTIME_SLOT_EFFECT, true);
}

static void audio_runtime_task(void *param)
{
    (void)param;
    audio_cmd_t cmd;
    while (xQueueReceive(s_queue, &cmd, portMAX_DELAY) == pdTRUE) {
        switch (cmd.type) {
        case AUDIO_CMD_PLAY:
        case AUDIO_CMD_PLAY_EFFECT:
            cmd.channel = AUDIO_PLAYER_CHANNEL_EFFECT;
            handle_play(&cmd);
            break;
        case AUDIO_CMD_PLAY_BACKGROUND:
            cmd.channel = AUDIO_PLAYER_CHANNEL_BACKGROUND;
            handle_play(&cmd);
            break;
        case AUDIO_CMD_SEEK:
            cmd.channel = AUDIO_PLAYER_CHANNEL_EFFECT;
            handle_play(&cmd);
            break;
        case AUDIO_CMD_STOP:
            handle_stop();
            break;
        case AUDIO_CMD_STOP_BACKGROUND:
            handle_stop_channel(AUDIO_PLAYER_CHANNEL_BACKGROUND);
            break;
        case AUDIO_CMD_STOP_EFFECT:
            handle_stop_channel(AUDIO_PLAYER_CHANNEL_EFFECT);
            break;
        case AUDIO_CMD_PAUSE:
            handle_pause();
            break;
        case AUDIO_CMD_RESUME:
            handle_resume();
            break;
        case AUDIO_CMD_NONE:
        default:
            break;
        }
        if (cmd.completion_task) {
            xTaskNotifyGive(cmd.completion_task);
        }
    }
    vTaskDelete(NULL);
}

esp_err_t audio_player_runtime_init(void)
{
    if (!s_runtime_lock) {
        s_runtime_lock = xSemaphoreCreateMutex();
    }
    ESP_RETURN_ON_FALSE(s_runtime_lock != NULL, ESP_ERR_NO_MEM, TAG, "runtime lock init failed");
    if (!s_audio_flags) {
        s_audio_flags = xEventGroupCreate();
    }
    ESP_RETURN_ON_FALSE(s_audio_flags != NULL, ESP_ERR_NO_MEM, TAG, "audio flags init failed");
    if (!s_queue) {
        s_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(audio_cmd_t));
    }
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "audio queue init failed");
    audio_flags_clear(AUDIO_FLAG_PAUSED |
                      AUDIO_FLAG_BG_A_STOP_REQUESTED |
                      AUDIO_FLAG_BG_B_STOP_REQUESTED |
                      AUDIO_FLAG_FX_STOP_REQUESTED);
    if (runtime_lock()) {
        memset(s_channels, 0, sizeof(s_channels));
        for (size_t i = 0; i < sizeof(s_channels) / sizeof(s_channels[0]); ++i) {
            s_channels[i].done = true;
        }
        audio_player_status_set_runtime_state(AUDIO_RUNTIME_IDLE);
        s_runtime_state = AUDIO_RUNTIME_IDLE;
        runtime_unlock();
    } else {
        memset(s_channels, 0, sizeof(s_channels));
        runtime_set_state(AUDIO_RUNTIME_IDLE);
    }
    return ESP_OK;
}

esp_err_t audio_player_runtime_start(void)
{
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_task) {
        BaseType_t ok = xTaskCreate(audio_runtime_task, "audio_task", AUDIO_RUNTIME_TASK_STACK, NULL, 6, &s_task);
        if (ok != pdPASS) {
            return ESP_FAIL;
        }
    }
    ESP_LOGD(TAG, "audio runtime started");
    return ESP_OK;
}

esp_err_t audio_player_runtime_enqueue(const audio_cmd_t *cmd)
{
    BaseType_t ok = pdFALSE;

    if (!cmd) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    ok = xQueueSend(s_queue, cmd, pdMS_TO_TICKS(50));
    if (ok == pdTRUE) {
        return ESP_OK;
    }

    ESP_LOGW(TAG,
             "audio enqueue timeout: type=%d channel=%d path=%s",
             (int)cmd->type,
             (int)cmd->channel,
             cmd->path);
    return ESP_ERR_TIMEOUT;
}

esp_err_t audio_player_runtime_enqueue_wait(const audio_cmd_t *cmd, uint32_t timeout_ms)
{
    if (!cmd) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    audio_cmd_t wait_cmd = *cmd;
    wait_cmd.completion_task = xTaskGetCurrentTaskHandle();
    if (!wait_cmd.completion_task) {
        return ESP_ERR_INVALID_STATE;
    }

    (void)ulTaskNotifyTake(pdTRUE, 0);
    esp_err_t err = audio_player_runtime_enqueue(&wait_cmd);
    if (err != ESP_OK) {
        return err;
    }

    TickType_t timeout_ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return ulTaskNotifyTake(pdTRUE, timeout_ticks) > 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

audio_reader_ctx_t *audio_player_runtime_create_reader_ctx(const audio_cmd_t *cmd, uint8_t runtime_slot)
{
    if (!cmd) {
        return NULL;
    }
    audio_player_channel_t channel = normalize_channel(cmd->channel);
    audio_runtime_slot_t slot = (audio_runtime_slot_t)runtime_slot;
    if (slot < 0 || slot >= AUDIO_RUNTIME_SLOT_COUNT) {
        return NULL;
    }
    audio_reader_ctx_t *ctx = &s_reader_ctxs[slot];
    if (runtime_lock()) {
        if (s_reader_ctx_in_use[slot]) {
            runtime_unlock();
            return NULL;
        }
        s_reader_ctx_in_use[slot] = true;
        runtime_unlock();
    } else {
        if (s_reader_ctx_in_use[slot]) {
            return NULL;
        }
        s_reader_ctx_in_use[slot] = true;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->cmd = *cmd;
    ctx->cmd.channel = channel;
    ctx->runtime_slot = (uint8_t)slot;
    ctx->mixer_channel = (uint8_t)mixer_channel_for_slot(slot);
    ctx->flags = s_audio_flags;
    ctx->stop_bit = slot_stop_bit(slot);
    ctx->bitrate_kbps = &s_channels[slot].bitrate_kbps;
    ctx->volume_percent = audio_player_volume_ptr();
    return ctx;
}

void audio_player_runtime_destroy_reader_ctx(audio_reader_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }
    audio_runtime_slot_t slot = (audio_runtime_slot_t)ctx->runtime_slot;
    if (slot < 0 || slot >= AUDIO_RUNTIME_SLOT_COUNT || ctx != &s_reader_ctxs[slot]) {
        return;
    }
    if (runtime_lock()) {
        memset(ctx, 0, sizeof(*ctx));
        s_reader_ctx_in_use[slot] = false;
        runtime_unlock();
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    s_reader_ctx_in_use[slot] = false;
}

esp_err_t audio_player_runtime_prepare_seek(audio_cmd_t *cmd, uint32_t pos_ms)
{
    char current_path[256] = {0};
    int dur_ms = 0;
    float ratio = -1.0f;

    if (!cmd) {
        return ESP_ERR_INVALID_ARG;
    }

    if (runtime_lock()) {
        if (s_channels[AUDIO_RUNTIME_SLOT_EFFECT].active_path[0] != 0) {
            strncpy(current_path,
                    s_channels[AUDIO_RUNTIME_SLOT_EFFECT].active_path,
                    sizeof(current_path) - 1);
            current_path[sizeof(current_path) - 1] = 0;
        }
        runtime_unlock();
    }

    if (current_path[0] == 0 ||
        !audio_player_status_prepare_seek(NULL, 0, &dur_ms)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (dur_ms <= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    ratio = (float)pos_ms / (float)dur_ms;
    if (ratio < 0.0f) {
        ratio = 0.0f;
    }
    if (ratio > 1.0f) {
        ratio = 1.0f;
    }

    memset(cmd, 0, sizeof(*cmd));
    cmd->type = AUDIO_CMD_SEEK;
    cmd->channel = AUDIO_PLAYER_CHANNEL_EFFECT;
    cmd->volume = -1;
    cmd->seek_ratio = ratio;
    strncpy(cmd->path, current_path, sizeof(cmd->path) - 1);
    cmd->path[sizeof(cmd->path) - 1] = 0;
    audio_player_status_set_seek_position((uint32_t)((float)dur_ms * ratio), (int)(ratio * 100.0f));
    return ESP_OK;
}

void audio_player_runtime_reader_started(const audio_reader_ctx_t *ctx)
{
    audio_runtime_slot_t slot = ctx ? (audio_runtime_slot_t)ctx->runtime_slot : AUDIO_RUNTIME_SLOT_EFFECT;
    if (slot < 0 || slot >= AUDIO_RUNTIME_SLOT_COUNT) {
        slot = AUDIO_RUNTIME_SLOT_EFFECT;
    }
    audio_flags_clear(slot_stop_bit(slot));
    if (runtime_lock()) {
        audio_channel_runtime_t *ch = &s_channels[slot];
        ch->done = false;
        ch->waiter = NULL;
        if (ctx && ctx->cmd.path[0]) {
            strncpy(ch->active_path, ctx->cmd.path, sizeof(ch->active_path) - 1);
            ch->active_path[sizeof(ch->active_path) - 1] = 0;
        }
        if (s_runtime_state == AUDIO_RUNTIME_STARTING) {
            audio_player_status_set_runtime_state(AUDIO_RUNTIME_PLAYING);
            s_runtime_state = AUDIO_RUNTIME_PLAYING;
        }
        runtime_unlock();
        return;
    }
    runtime_set_state(AUDIO_RUNTIME_PLAYING);
}

void audio_player_runtime_reader_finished(audio_reader_ctx_t *ctx)
{
    audio_runtime_slot_t runtime_slot = ctx ? (audio_runtime_slot_t)ctx->runtime_slot : AUDIO_RUNTIME_SLOT_EFFECT;
    TaskHandle_t waiter = NULL;
    audio_reader_ctx_t *ctx_slot = ctx;

    if (runtime_slot < 0 || runtime_slot >= AUDIO_RUNTIME_SLOT_COUNT) {
        runtime_slot = AUDIO_RUNTIME_SLOT_EFFECT;
    }
    runtime_clear_active_path_if_matches(runtime_slot, ctx ? ctx->cmd.path : NULL);
    audio_player_mixer_finish_stream(mixer_channel_for_slot(runtime_slot));

    if (runtime_lock()) {
        audio_channel_runtime_t *ch = &s_channels[runtime_slot];
        ch->done = true;
        ch->task = NULL;
        waiter = ch->waiter;
        ch->waiter = NULL;
        if (ctx_slot == &s_reader_ctxs[runtime_slot]) {
            memset(ctx_slot, 0, sizeof(*ctx_slot));
            s_reader_ctx_in_use[runtime_slot] = false;
        }
        if (!s_channels[AUDIO_RUNTIME_SLOT_BG_A].task &&
            !s_channels[AUDIO_RUNTIME_SLOT_BG_B].task &&
            !s_channels[AUDIO_RUNTIME_SLOT_EFFECT].task) {
            audio_player_status_set_runtime_state(AUDIO_RUNTIME_IDLE);
            s_runtime_state = AUDIO_RUNTIME_IDLE;
        }
        runtime_unlock();
    } else {
        audio_player_runtime_destroy_reader_ctx(ctx);
        runtime_set_state(AUDIO_RUNTIME_IDLE);
    }

    if (waiter) {
        xTaskNotifyGive(waiter);
    }
}

bool audio_player_reader_stop_requested(const audio_reader_ctx_t *ctx)
{
    if (!ctx || !ctx->flags) {
        return false;
    }
    return (xEventGroupGetBits(ctx->flags) & ctx->stop_bit) != 0;
}

void audio_player_reader_wait_while_paused(const audio_reader_ctx_t *ctx)
{
    if (!ctx || !ctx->flags) {
        return;
    }
    while ((xEventGroupGetBits(ctx->flags) & AUDIO_FLAG_PAUSED) != 0 &&
           (xEventGroupGetBits(ctx->flags) & ctx->stop_bit) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int *audio_player_reader_bitrate_ptr(const audio_reader_ctx_t *ctx)
{
    return ctx ? ctx->bitrate_kbps : NULL;
}

int audio_player_reader_volume(const audio_reader_ctx_t *ctx)
{
    int volume = 100;
    if (ctx && ctx->cmd.volume >= 0) {
        volume = ctx->cmd.volume;
    } else if (ctx && ctx->volume_percent) {
        volume = *ctx->volume_percent;
    }
    if (volume < 0) {
        volume = 0;
    }
    if (volume > 100) {
        volume = 100;
    }
    return volume;
}

bool audio_player_runtime_reader_snapshot(audio_player_channel_t channel,
                                          char *path,
                                          size_t path_len,
                                          size_t *bytes_done,
                                          uint32_t *loop_gap_ms,
                                          long *read_offset,
                                          uint32_t *read_elapsed_ms,
                                          long *slow_read_offset,
                                          uint32_t *slow_read_elapsed_ms)
{
    bool ok = false;
    channel = normalize_channel(channel);
    audio_runtime_slot_t slot = channel == AUDIO_PLAYER_CHANNEL_BACKGROUND ?
        s_active_background_slot : AUDIO_RUNTIME_SLOT_EFFECT;

    if (runtime_lock()) {
        audio_reader_ctx_t *ctx = &s_reader_ctxs[slot];
        if (s_reader_ctx_in_use[slot]) {
            if (path && path_len > 0) {
                strncpy(path, ctx->cmd.path, path_len - 1);
                path[path_len - 1] = 0;
            }
            if (bytes_done) {
                *bytes_done = ctx->last_bytes_done;
            }
            if (loop_gap_ms) {
                *loop_gap_ms = ctx->last_loop_gap_ms;
            }
            if (read_offset) {
                *read_offset = ctx->last_read_offset;
            }
            if (read_elapsed_ms) {
                *read_elapsed_ms = ctx->last_read_elapsed_ms;
            }
            if (slow_read_offset) {
                *slow_read_offset = ctx->last_slow_read_offset;
            }
            if (slow_read_elapsed_ms) {
                *slow_read_elapsed_ms = ctx->last_slow_read_elapsed_ms;
            }
            ok = true;
        }
        runtime_unlock();
        return ok;
    }

    audio_reader_ctx_t *ctx = &s_reader_ctxs[slot];
    if (!s_reader_ctx_in_use[slot]) {
        return false;
    }
    if (path && path_len > 0) {
        strncpy(path, ctx->cmd.path, path_len - 1);
        path[path_len - 1] = 0;
    }
    if (bytes_done) {
        *bytes_done = ctx->last_bytes_done;
    }
    if (loop_gap_ms) {
        *loop_gap_ms = ctx->last_loop_gap_ms;
    }
    if (read_offset) {
        *read_offset = ctx->last_read_offset;
    }
    if (read_elapsed_ms) {
        *read_elapsed_ms = ctx->last_read_elapsed_ms;
    }
    if (slow_read_offset) {
        *slow_read_offset = ctx->last_slow_read_offset;
    }
    if (slow_read_elapsed_ms) {
        *slow_read_elapsed_ms = ctx->last_slow_read_elapsed_ms;
    }
    return true;
}
