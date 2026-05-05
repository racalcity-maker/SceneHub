#include "audio_player_internal.h"

#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
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
#define AUDIO_RUNTIME_TASK_STACK 8192
#define AUDIO_READER_BG_STACK 8192
#define AUDIO_READER_FX_STACK 8192
#define AUDIO_READER_TASK_PRIORITY 5
#define AUDIO_FLAG_PAUSED BIT0
#define AUDIO_FLAG_BG_STOP_REQUESTED BIT1
#define AUDIO_FLAG_FX_STOP_REQUESTED BIT2

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
static audio_channel_runtime_t s_channels[AUDIO_PLAYER_CHANNEL_BACKGROUND + 1];
static audio_runtime_state_t s_runtime_state = AUDIO_RUNTIME_IDLE;

static audio_player_channel_t normalize_channel(audio_player_channel_t channel)
{
    return channel == AUDIO_PLAYER_CHANNEL_BACKGROUND ? AUDIO_PLAYER_CHANNEL_BACKGROUND : AUDIO_PLAYER_CHANNEL_EFFECT;
}

static EventBits_t channel_stop_bit(audio_player_channel_t channel)
{
    return normalize_channel(channel) == AUDIO_PLAYER_CHANNEL_BACKGROUND ?
        AUDIO_FLAG_BG_STOP_REQUESTED : AUDIO_FLAG_FX_STOP_REQUESTED;
}

static audio_mixer_channel_t mixer_channel(audio_player_channel_t channel)
{
    return normalize_channel(channel) == AUDIO_PLAYER_CHANNEL_BACKGROUND ?
        AUDIO_MIXER_CHANNEL_BACKGROUND : AUDIO_MIXER_CHANNEL_EFFECT;
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

    channel = normalize_channel(channel);

    if (runtime_lock()) {
        has_reader = s_channels[channel].task != NULL && !s_channels[channel].done;
        runtime_unlock();
    }

    return has_reader;
}

static void wait_background_fade_out(void)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(AUDIO_BACKGROUND_FADE_OUT_MS + 250);

    audio_player_mixer_fade_out_stream(
        AUDIO_MIXER_CHANNEL_BACKGROUND,
        AUDIO_BACKGROUND_FADE_OUT_MS
    );

    while (audio_player_mixer_fade_out_active(AUDIO_MIXER_CHANNEL_BACKGROUND)) {
        if ((xTaskGetTickCount() - start) > timeout) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
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
    audio_channel_runtime_t *ch = &s_channels[AUDIO_PLAYER_CHANNEL_EFFECT];
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

static void runtime_clear_active_path_if_matches(audio_player_channel_t channel, const char *path)
{
    channel = normalize_channel(channel);
    audio_channel_runtime_t *ch = &s_channels[channel];
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

static bool stop_reader(audio_player_channel_t channel)
{
    channel = normalize_channel(channel);
    audio_channel_runtime_t *ch = &s_channels[channel];
    TaskHandle_t waiter = xTaskGetCurrentTaskHandle();
    bool wait_needed = false;

    audio_flags_set(channel_stop_bit(channel));
    audio_player_mixer_stop_stream(mixer_channel(channel));
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

static void handle_pause(void)
{
    audio_runtime_state_t state = runtime_get_state();
    if (state != AUDIO_RUNTIME_PLAYING && state != AUDIO_RUNTIME_STARTING) {
        return;
    }
    audio_flags_set(AUDIO_FLAG_PAUSED);
    s_channels[AUDIO_PLAYER_CHANNEL_EFFECT].bitrate_kbps = 0;
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

    if (channel == AUDIO_PLAYER_CHANNEL_BACKGROUND && channel_has_reader(channel)) {
        wait_background_fade_out();
    }

    (void)stop_reader(channel);
    if (channel == AUDIO_PLAYER_CHANNEL_EFFECT) {
        runtime_set_effect_active_path(NULL);
        s_channels[channel].bitrate_kbps = 0;
    }
    runtime_set_state(AUDIO_RUNTIME_IDLE);
}

static void handle_stop(void)
{
    audio_flags_clear(AUDIO_FLAG_PAUSED);
    (void)stop_reader(AUDIO_PLAYER_CHANNEL_BACKGROUND);
    (void)stop_reader(AUDIO_PLAYER_CHANNEL_EFFECT);
    audio_player_mixer_stop_all();
    audio_player_output_reset();
    runtime_set_effect_active_path(NULL);
    s_channels[AUDIO_PLAYER_CHANNEL_EFFECT].bitrate_kbps = 0;
    runtime_set_state(AUDIO_RUNTIME_IDLE);
}

static void handle_play(const audio_cmd_t *cmd)
{
    audio_player_channel_t channel = normalize_channel(cmd->channel);
    int volume = cmd->volume >= 0 ? cmd->volume : audio_player_runtime_volume();

    if (!sd_storage_available()) {
        ESP_LOGE(TAG, "SD not mounted, beep");
        audio_player_output_play_tone(660, 150, volume);
        audio_player_output_play_tone(440, 150, volume);
        error_monitor_report_sd_fault();
        runtime_set_state(AUDIO_RUNTIME_ERROR);
        return;
    }

    audio_reader_ctx_t *reader_ctx = audio_player_runtime_create_reader_ctx(cmd);
    if (!reader_ctx) {
        ESP_LOGE(TAG, "no mem for reader ctx");
        error_monitor_report_audio_fault();
        runtime_set_state(AUDIO_RUNTIME_ERROR);
        return;
    }

    if (channel == AUDIO_PLAYER_CHANNEL_BACKGROUND && channel_has_reader(channel)) {
        wait_background_fade_out();
    }

    if (!stop_reader(channel)) {
        audio_player_runtime_destroy_reader_ctx(reader_ctx);
        return;
    }

    audio_flags_clear(AUDIO_FLAG_PAUSED | channel_stop_bit(channel));
    audio_player_mixer_start_stream(mixer_channel(channel));

    TaskHandle_t reader_task = NULL;
    runtime_set_state(AUDIO_RUNTIME_STARTING);
    if (channel == AUDIO_PLAYER_CHANNEL_EFFECT) {
        runtime_set_effect_active_path(cmd->path);
    }
    const uint32_t reader_stack = channel == AUDIO_PLAYER_CHANNEL_BACKGROUND ?
        AUDIO_READER_BG_STACK : AUDIO_READER_FX_STACK;
    BaseType_t ok = xTaskCreatePinnedToCore(audio_player_reader_task,
                                            channel == AUDIO_PLAYER_CHANNEL_BACKGROUND ? "audio_bg" : "audio_fx",
                                            reader_stack,
                                            reader_ctx,
                                            AUDIO_READER_TASK_PRIORITY,
                                            &reader_task,
                                            1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "reader task create failed");
        audio_player_runtime_destroy_reader_ctx(reader_ctx);
        audio_player_mixer_stop_stream(mixer_channel(channel));
        audio_player_output_play_tone(660, 150, volume);
        audio_player_output_play_tone(440, 150, volume);
        error_monitor_report_audio_fault();
        if (channel == AUDIO_PLAYER_CHANNEL_EFFECT) {
            runtime_set_effect_active_path(NULL);
        }
        runtime_set_state(AUDIO_RUNTIME_ERROR);
        return;
    }

    if (runtime_lock()) {
        s_channels[channel].task = reader_task;
        s_channels[channel].done = false;
        s_channels[channel].waiter = NULL;
        if (cmd->path[0]) {
            strncpy(s_channels[channel].active_path, cmd->path, sizeof(s_channels[channel].active_path) - 1);
            s_channels[channel].active_path[sizeof(s_channels[channel].active_path) - 1] = 0;
        }
        runtime_unlock();
    }
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
    audio_flags_clear(AUDIO_FLAG_PAUSED | AUDIO_FLAG_BG_STOP_REQUESTED | AUDIO_FLAG_FX_STOP_REQUESTED);
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
    ESP_LOGI(TAG, "audio runtime started");
    return ESP_OK;
}

esp_err_t audio_player_runtime_enqueue(const audio_cmd_t *cmd)
{
    if (!cmd) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_queue, cmd, pdMS_TO_TICKS(50)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

audio_reader_ctx_t *audio_player_runtime_create_reader_ctx(const audio_cmd_t *cmd)
{
    if (!cmd) {
        return NULL;
    }
    audio_player_channel_t channel = normalize_channel(cmd->channel);
    audio_reader_ctx_t *ctx = heap_caps_calloc(1, sizeof(*ctx), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ctx) {
        ctx = heap_caps_calloc(1, sizeof(*ctx), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!ctx) {
        return NULL;
    }
    ctx->cmd = *cmd;
    ctx->cmd.channel = channel;
    ctx->flags = s_audio_flags;
    ctx->stop_bit = channel_stop_bit(channel);
    ctx->bitrate_kbps = &s_channels[channel].bitrate_kbps;
    ctx->volume_percent = audio_player_volume_ptr();
    return ctx;
}

void audio_player_runtime_destroy_reader_ctx(audio_reader_ctx_t *ctx)
{
    heap_caps_free(ctx);
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
        if (s_channels[AUDIO_PLAYER_CHANNEL_EFFECT].active_path[0] != 0) {
            strncpy(current_path,
                    s_channels[AUDIO_PLAYER_CHANNEL_EFFECT].active_path,
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
    audio_player_channel_t channel = ctx ? normalize_channel(ctx->cmd.channel) : AUDIO_PLAYER_CHANNEL_EFFECT;
    audio_flags_clear(channel_stop_bit(channel));
    if (runtime_lock()) {
        audio_channel_runtime_t *ch = &s_channels[channel];
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
    audio_player_channel_t channel = ctx ? normalize_channel(ctx->cmd.channel) : AUDIO_PLAYER_CHANNEL_EFFECT;
    TaskHandle_t waiter = NULL;

    runtime_clear_active_path_if_matches(channel, ctx ? ctx->cmd.path : NULL);
    audio_player_mixer_finish_stream(mixer_channel(channel));

    if (runtime_lock()) {
        audio_channel_runtime_t *ch = &s_channels[channel];
        ch->done = true;
        ch->task = NULL;
        waiter = ch->waiter;
        ch->waiter = NULL;
        if (!s_channels[AUDIO_PLAYER_CHANNEL_BACKGROUND].task &&
            !s_channels[AUDIO_PLAYER_CHANNEL_EFFECT].task) {
            audio_player_status_set_runtime_state(AUDIO_RUNTIME_IDLE);
            s_runtime_state = AUDIO_RUNTIME_IDLE;
        }
        runtime_unlock();
    } else {
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
