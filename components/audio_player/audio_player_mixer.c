#include "audio_player_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

#define AUDIO_MIXER_SAMPLE_RATE 44100
#define AUDIO_MIXER_CHANNELS 2
#define AUDIO_MIXER_FRAME_BYTES (AUDIO_MIXER_CHANNELS * sizeof(int16_t))
#define AUDIO_MIXER_CHUNK_FRAMES 256
#define AUDIO_MIXER_CHUNK_BYTES (AUDIO_MIXER_CHUNK_FRAMES * AUDIO_MIXER_FRAME_BYTES)
#define AUDIO_MIXER_STREAM_BYTES (128 * 1024)
#define AUDIO_MIXER_TRIGGER_BYTES AUDIO_MIXER_FRAME_BYTES
#define AUDIO_MIXER_WRITE_TIMEOUT_MS 100
#define AUDIO_MIXER_IDLE_WAIT_MS 5
#define AUDIO_MIXER_FADE_IN_MS 32
#define AUDIO_MIXER_FADE_IN_FRAMES ((AUDIO_MIXER_SAMPLE_RATE * AUDIO_MIXER_FADE_IN_MS) / 1000)
#define AUDIO_MIXER_FINISH_FADE_MS 24
#define AUDIO_MIXER_FINISH_FADE_FRAMES ((AUDIO_MIXER_SAMPLE_RATE * AUDIO_MIXER_FINISH_FADE_MS) / 1000)
#define AUDIO_MIXER_FINISH_FADE_BYTES (AUDIO_MIXER_FINISH_FADE_FRAMES * AUDIO_MIXER_FRAME_BYTES)
#define AUDIO_MIXER_PREROLL_FRAMES 8192
#define AUDIO_MIXER_PREROLL_BYTES (AUDIO_MIXER_PREROLL_FRAMES * AUDIO_MIXER_FRAME_BYTES)
#define AUDIO_MIXER_IDLE_SILENCE_MS 35
#define AUDIO_MIXER_STARVATION_LOG_MS 1000
#define AUDIO_MIXER_PARTIAL_READ_LOG_MS 1000
#define AUDIO_MIXER_FADE_IN_SIGNAL_THRESHOLD 96

static const char *TAG = "audio_mixer";

typedef enum {
    AUDIO_MIXER_OUTPUT_IDLE = 0,
    AUDIO_MIXER_OUTPUT_PRIMING,
    AUDIO_MIXER_OUTPUT_RUNNING,
    AUDIO_MIXER_OUTPUT_DRAINING,
} audio_mixer_output_state_t;

EXT_RAM_BSS_ATTR static uint8_t s_bg_a_storage[AUDIO_MIXER_STREAM_BYTES];
EXT_RAM_BSS_ATTR static uint8_t s_bg_b_storage[AUDIO_MIXER_STREAM_BYTES];
EXT_RAM_BSS_ATTR static uint8_t s_fx_storage[AUDIO_MIXER_STREAM_BYTES];
EXT_RAM_BSS_ATTR static int16_t s_channel_pcm[AUDIO_MIXER_CHANNEL_COUNT][AUDIO_MIXER_CHUNK_FRAMES * AUDIO_MIXER_CHANNELS];
EXT_RAM_BSS_ATTR static int16_t s_mixed_pcm[AUDIO_MIXER_CHUNK_FRAMES * AUDIO_MIXER_CHANNELS];
static StaticStreamBuffer_t s_bg_a_stream_struct;
static StaticStreamBuffer_t s_bg_b_stream_struct;
static StaticStreamBuffer_t s_fx_stream_struct;
static StreamBufferHandle_t s_streams[AUDIO_MIXER_CHANNEL_COUNT];
static SemaphoreHandle_t s_lock = NULL;
static TaskHandle_t s_task = NULL;
static bool s_active[AUDIO_MIXER_CHANNEL_COUNT];
static bool s_audible[AUDIO_MIXER_CHANNEL_COUNT];
static bool s_primed[AUDIO_MIXER_CHANNEL_COUNT];
static uint16_t s_fade_in_remaining[AUDIO_MIXER_CHANNEL_COUNT];
static uint32_t s_fade_out_remaining[AUDIO_MIXER_CHANNEL_COUNT];
static uint32_t s_fade_out_total[AUDIO_MIXER_CHANNEL_COUNT];
static bool s_fade_out_muted[AUDIO_MIXER_CHANNEL_COUNT];
static bool s_finish_fade_pending[AUDIO_MIXER_CHANNEL_COUNT];
static bool s_first_audible_logged[AUDIO_MIXER_CHANNEL_COUNT];
static bool s_signal_start_logged[AUDIO_MIXER_CHANNEL_COUNT];
static bool s_output_dirty = false;
static audio_mixer_output_state_t s_output_state = AUDIO_MIXER_OUTPUT_IDLE;
static uint32_t s_output_state_seq = 0;
static uint32_t s_source_primed_count[AUDIO_MIXER_CHANNEL_COUNT];
static uint32_t s_starvation_count[AUDIO_MIXER_CHANNEL_COUNT];
static uint32_t s_partial_read_count[AUDIO_MIXER_CHANNEL_COUNT];
static TickType_t s_starvation_log_tick[AUDIO_MIXER_CHANNEL_COUNT];
static TickType_t s_partial_read_log_tick[AUDIO_MIXER_CHANNEL_COUNT];

static const char *mixer_channel_name(audio_mixer_channel_t channel)
{
    switch (channel) {
    case AUDIO_MIXER_CHANNEL_BACKGROUND_A:
        return "background_a";
    case AUDIO_MIXER_CHANNEL_BACKGROUND_B:
        return "background_b";
    case AUDIO_MIXER_CHANNEL_EFFECT:
        return "effect";
    default:
        return "unknown";
    }
}

static const char *output_state_name(audio_mixer_output_state_t state)
{
    switch (state) {
    case AUDIO_MIXER_OUTPUT_PRIMING:
        return "priming";
    case AUDIO_MIXER_OUTPUT_RUNNING:
        return "running";
    case AUDIO_MIXER_OUTPUT_DRAINING:
        return "draining";
    case AUDIO_MIXER_OUTPUT_IDLE:
    default:
        return "idle";
    }
}

static void mixer_set_output_state(audio_mixer_output_state_t state, const char *reason)
{
    if (s_output_state == state) {
        return;
    }
    ESP_LOGD(TAG,
             "output state: %s -> %s reason=%s seq=%lu",
             output_state_name(s_output_state),
             output_state_name(state),
             reason ? reason : "-",
             (unsigned long)++s_output_state_seq);
    s_output_state = state;
}

static bool mixer_lock(void)
{
    return s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void mixer_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static StreamBufferHandle_t mixer_stream(audio_mixer_channel_t channel)
{
    if (channel < 0 || channel >= AUDIO_MIXER_CHANNEL_COUNT) {
        return NULL;
    }
    return s_streams[channel];
}

static bool mixer_any_active_locked(void)
{
    for (int i = 0; i < AUDIO_MIXER_CHANNEL_COUNT; ++i) {
        if (s_active[i] || (s_streams[i] && xStreamBufferBytesAvailable(s_streams[i]) > 0)) {
            return true;
        }
    }
    return false;
}

static bool mixer_any_active(void)
{
    bool active = false;
    if (mixer_lock()) {
        active = mixer_any_active_locked();
        mixer_unlock();
    }
    return active;
}

static TickType_t ticks_at_least_one(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks > 0 ? ticks : 1;
}

static esp_err_t mixer_start_output_if_needed(void)
{
    if (s_output_state == AUDIO_MIXER_OUTPUT_RUNNING) {
        return ESP_OK;
    }
    mixer_set_output_state(AUDIO_MIXER_OUTPUT_PRIMING, "active_source");
    esp_err_t err = audio_player_output_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "output explicit start failed: %s", esp_err_to_name(err));
        return err;
    }
    mixer_set_output_state(AUDIO_MIXER_OUTPUT_RUNNING, "output_started");
    return ESP_OK;
}

static int16_t clamp_i16(int32_t value)
{
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (int16_t)value;
}

static int16_t pcm_max_abs_i16(const int16_t *pcm, size_t samples)
{
    int32_t max_abs = 0;
    if (!pcm) {
        return 0;
    }
    for (size_t i = 0; i < samples; ++i) {
        int32_t v = pcm[i];
        if (v < 0) {
            v = -v;
        }
        if (v > max_abs) {
            max_abs = v;
        }
    }
    if (max_abs > 32767) {
        max_abs = 32767;
    }
    return (int16_t)max_abs;
}

static size_t receive_pcm(audio_mixer_channel_t channel, int16_t *out, size_t bytes)
{
    StreamBufferHandle_t stream = mixer_stream(channel);
    TickType_t now = 0;

    if (!stream || !out || bytes == 0) {
        return 0;
    }

    memset(out, 0, bytes);

    size_t got = xStreamBufferReceive(stream, out, bytes, 0);
    got -= got % AUDIO_MIXER_FRAME_BYTES;

    if (got == 0) {
        return 0;
    }

    if (got < bytes && channel >= 0 && channel < AUDIO_MIXER_CHANNEL_COUNT) {
        s_partial_read_count[channel]++;
        now = xTaskGetTickCount();
        if (s_partial_read_log_tick[channel] == 0 ||
            (now - s_partial_read_log_tick[channel]) >= pdMS_TO_TICKS(AUDIO_MIXER_PARTIAL_READ_LOG_MS)) {
            ESP_LOGD(TAG,
                     "partial stream read: channel=%s got=%u requested=%u count=%lu",
                     mixer_channel_name(channel),
                     (unsigned)got,
                     (unsigned)bytes,
                     (unsigned long)s_partial_read_count[channel]);
            s_partial_read_log_tick[channel] = now;
        }
    }

    return got;
}

static bool channel_should_read(audio_mixer_channel_t channel)
{
    bool read = false;
    bool became_primed = false;
    size_t primed_available = 0;
    if (channel < 0 || channel >= AUDIO_MIXER_CHANNEL_COUNT) {
        return false;
    }
    if (mixer_lock()) {
        StreamBufferHandle_t stream = mixer_stream(channel);
        size_t available = stream ? xStreamBufferBytesAvailable(stream) : 0;
        if (!s_active[channel]) {
            read = available > 0;
            if (available == 0) {
                s_audible[channel] = false;
                s_primed[channel] = false;
                s_fade_in_remaining[channel] = 0;
                s_fade_out_total[channel] = 0;
                s_fade_out_remaining[channel] = 0;
                s_fade_out_muted[channel] = false;
                s_finish_fade_pending[channel] = false;
            } else if (s_finish_fade_pending[channel] &&
                       s_fade_out_remaining[channel] == 0 &&
                       available <= AUDIO_MIXER_FINISH_FADE_BYTES) {
                uint32_t frames = AUDIO_MIXER_FINISH_FADE_FRAMES;
                if (frames == 0) {
                    frames = 1;
                }
                s_fade_out_total[channel] = frames;
                s_fade_out_remaining[channel] = frames;
                s_fade_out_muted[channel] = false;
                s_finish_fade_pending[channel] = false;
            }
        } else if (!s_primed[channel] && available >= AUDIO_MIXER_PREROLL_BYTES) {
            s_primed[channel] = true;
            s_source_primed_count[channel]++;
            became_primed = true;
            primed_available = available;
        }
        if (s_active[channel] && s_primed[channel] && s_audible[channel]) {
            read = true;
        }
        mixer_unlock();
    }
    if (became_primed) {
        ESP_LOGD(TAG,
                 "source primed: channel=%s available=%u preroll=%u count=%lu",
                 mixer_channel_name(channel),
                 (unsigned)primed_available,
                 (unsigned)AUDIO_MIXER_PREROLL_BYTES,
                 (unsigned long)s_source_primed_count[channel]);
    }
    return read;
}

static void apply_fade_in(audio_mixer_channel_t channel, int16_t *pcm, size_t bytes)
{
    if (channel < 0 || channel >= AUDIO_MIXER_CHANNEL_COUNT || !pcm || bytes == 0) {
        return;
    }

    uint16_t remaining = s_fade_in_remaining[channel];
    if (remaining == 0) {
        return;
    }

    const uint16_t total = AUDIO_MIXER_FADE_IN_FRAMES > 0 ? AUDIO_MIXER_FADE_IN_FRAMES : 1;
    size_t frames = bytes / AUDIO_MIXER_FRAME_BYTES;
    if (remaining == total &&
        pcm_max_abs_i16(pcm, frames * AUDIO_MIXER_CHANNELS) < AUDIO_MIXER_FADE_IN_SIGNAL_THRESHOLD) {
        return;
    }
    for (size_t frame = 0; frame < frames && remaining > 0; ++frame) {
        uint16_t done = (uint16_t)(total - remaining);
        uint16_t gain = (uint16_t)(done + 1);
        for (size_t ch = 0; ch < AUDIO_MIXER_CHANNELS; ++ch) {
            size_t sample_index = frame * AUDIO_MIXER_CHANNELS + ch;
            int32_t scaled = ((int32_t)pcm[sample_index] * (int32_t)gain) / (int32_t)total;
            pcm[sample_index] = (int16_t)scaled;
        }
        --remaining;
    }
    s_fade_in_remaining[channel] = remaining;
}

static void maybe_log_first_effect_block(const int16_t *pcm, size_t bytes)
{
    const audio_mixer_channel_t channel = AUDIO_MIXER_CHANNEL_EFFECT;
    bool log_block = false;
    bool active = false;
    bool audible = false;
    bool primed = false;
    uint16_t fade_in_remaining = 0;
    size_t available_after_read = 0;
    char path[256] = {0};
    size_t bytes_done = 0;
    uint32_t loop_gap_ms = 0;
    long read_offset = -1;
    uint32_t read_elapsed_ms = 0;
    long slow_read_offset = -1;
    uint32_t slow_read_elapsed_ms = 0;

    if (!pcm || bytes < AUDIO_MIXER_FRAME_BYTES) {
        return;
    }

    if (mixer_lock()) {
        StreamBufferHandle_t stream = mixer_stream(channel);
        if (!s_first_audible_logged[channel]) {
            s_first_audible_logged[channel] = true;
            active = s_active[channel];
            audible = s_audible[channel];
            primed = s_primed[channel];
            fade_in_remaining = s_fade_in_remaining[channel];
            available_after_read = stream ? xStreamBufferBytesAvailable(stream) : 0;
            log_block = true;
        }
        mixer_unlock();
    }

    if (!log_block) {
        return;
    }

    (void)audio_player_runtime_reader_snapshot(
        AUDIO_PLAYER_CHANNEL_EFFECT,
        path,
        sizeof(path),
        &bytes_done,
        &loop_gap_ms,
        &read_offset,
        &read_elapsed_ms,
        &slow_read_offset,
        &slow_read_elapsed_ms
    );

    ESP_LOGD(TAG,
             "effect first audible block: bytes=%u first_l=%d first_r=%d max_abs=%d fade_in_remaining=%u available_after_read=%u active=%d audible=%d primed=%d path=%s bytes_done=%u loop_gap_ms=%lu read_offset=%ld read_ms=%lu slow_offset=%ld slow_ms=%lu",
             (unsigned)bytes,
             (int)pcm[0],
             (int)pcm[1],
             (int)pcm_max_abs_i16(pcm, bytes / sizeof(int16_t)),
             (unsigned)fade_in_remaining,
             (unsigned)available_after_read,
             active ? 1 : 0,
             audible ? 1 : 0,
             primed ? 1 : 0,
             path[0] ? path : "-",
             (unsigned)bytes_done,
             (unsigned long)loop_gap_ms,
             read_offset,
             (unsigned long)read_elapsed_ms,
             slow_read_offset,
             (unsigned long)slow_read_elapsed_ms);
}

static void maybe_log_effect_signal_start(const int16_t *pcm, size_t bytes)
{
    const audio_mixer_channel_t channel = AUDIO_MIXER_CHANNEL_EFFECT;
    int16_t max_abs = 0;
    bool log_block = false;
    bool active = false;
    bool audible = false;
    bool primed = false;
    uint16_t fade_in_remaining = 0;
    size_t available_after_read = 0;
    char path[256] = {0};
    size_t bytes_done = 0;
    uint32_t loop_gap_ms = 0;
    long read_offset = -1;
    uint32_t read_elapsed_ms = 0;
    long slow_read_offset = -1;
    uint32_t slow_read_elapsed_ms = 0;

    if (!pcm || bytes < AUDIO_MIXER_FRAME_BYTES) {
        return;
    }

    max_abs = pcm_max_abs_i16(pcm, bytes / sizeof(int16_t));
    if (max_abs < AUDIO_MIXER_FADE_IN_SIGNAL_THRESHOLD) {
        return;
    }

    if (mixer_lock()) {
        StreamBufferHandle_t stream = mixer_stream(channel);
        if (!s_signal_start_logged[channel]) {
            s_signal_start_logged[channel] = true;
            active = s_active[channel];
            audible = s_audible[channel];
            primed = s_primed[channel];
            fade_in_remaining = s_fade_in_remaining[channel];
            available_after_read = stream ? xStreamBufferBytesAvailable(stream) : 0;
            log_block = true;
        }
        mixer_unlock();
    }

    if (!log_block) {
        return;
    }

    (void)audio_player_runtime_reader_snapshot(
        AUDIO_PLAYER_CHANNEL_EFFECT,
        path,
        sizeof(path),
        &bytes_done,
        &loop_gap_ms,
        &read_offset,
        &read_elapsed_ms,
        &slow_read_offset,
        &slow_read_elapsed_ms
    );

    ESP_LOGD(TAG,
             "effect signal start: bytes=%u first_l=%d first_r=%d max_abs=%d threshold=%d fade_in_remaining=%u available_after_read=%u active=%d audible=%d primed=%d path=%s bytes_done=%u loop_gap_ms=%lu read_offset=%ld read_ms=%lu slow_offset=%ld slow_ms=%lu",
             (unsigned)bytes,
             (int)pcm[0],
             (int)pcm[1],
             (int)max_abs,
             (int)AUDIO_MIXER_FADE_IN_SIGNAL_THRESHOLD,
             (unsigned)fade_in_remaining,
             (unsigned)available_after_read,
             active ? 1 : 0,
             audible ? 1 : 0,
             primed ? 1 : 0,
             path[0] ? path : "-",
             (unsigned)bytes_done,
             (unsigned long)loop_gap_ms,
             read_offset,
             (unsigned long)read_elapsed_ms,
             slow_read_offset,
             (unsigned long)slow_read_elapsed_ms);
}

static void maybe_log_starvation(audio_mixer_channel_t channel, bool read_attempted, size_t bytes_read)
{
    const char *name = mixer_channel_name(channel);
    TickType_t now = xTaskGetTickCount();
    char path[256] = {0};
    size_t bytes_done = 0;
    uint32_t loop_gap_ms = 0;
    long read_offset = -1;
    uint32_t read_elapsed_ms = 0;
    long slow_read_offset = -1;
    uint32_t slow_read_elapsed_ms = 0;

    if (!read_attempted || bytes_read > 0 || channel < 0 || channel >= AUDIO_MIXER_CHANNEL_COUNT) {
        if (channel >= 0 && channel < AUDIO_MIXER_CHANNEL_COUNT) {
            s_starvation_log_tick[channel] = 0;
        }
        return;
    }

    if (s_starvation_log_tick[channel] != 0 &&
        (now - s_starvation_log_tick[channel]) < pdMS_TO_TICKS(AUDIO_MIXER_STARVATION_LOG_MS)) {
        return;
    }

    s_starvation_count[channel]++;
    if (mixer_lock()) {
        StreamBufferHandle_t stream = mixer_stream(channel);
        size_t available = stream ? xStreamBufferBytesAvailable(stream) : 0;
        bool active = s_active[channel];
        bool audible = s_audible[channel];
        bool primed = s_primed[channel];
        uint32_t fade_out = s_fade_out_remaining[channel];
        mixer_unlock();
        (void)audio_player_runtime_reader_snapshot(
            channel == AUDIO_MIXER_CHANNEL_EFFECT ? AUDIO_PLAYER_CHANNEL_EFFECT : AUDIO_PLAYER_CHANNEL_BACKGROUND,
            path,
            sizeof(path),
            &bytes_done,
            &loop_gap_ms,
            &read_offset,
            &read_elapsed_ms,
            &slow_read_offset,
            &slow_read_elapsed_ms
        );
        ESP_LOGW(TAG,
                 "channel starvation: channel=%s active=%d audible=%d primed=%d available=%u fade_out=%lu count=%lu path=%s bytes_done=%u last_loop_gap_ms=%lu last_offset=%ld last_read_ms=%lu last_slow_offset=%ld last_slow_ms=%lu",
                 name,
                 active ? 1 : 0,
                 audible ? 1 : 0,
                 primed ? 1 : 0,
                 (unsigned)available,
                 (unsigned long)fade_out,
                 (unsigned long)s_starvation_count[channel],
                 path[0] ? path : "-",
                 (unsigned)bytes_done,
                 (unsigned long)loop_gap_ms,
                 read_offset,
                 (unsigned long)read_elapsed_ms,
                 slow_read_offset,
                 (unsigned long)slow_read_elapsed_ms);
    }

    s_starvation_log_tick[channel] = now;
}

static bool apply_fade_out(audio_mixer_channel_t channel, int16_t *pcm, size_t bytes)
{
    if (channel < 0 || channel >= AUDIO_MIXER_CHANNEL_COUNT || !pcm || bytes == 0) {
        return false;
    }

    if (s_fade_out_muted[channel]) {
        memset(pcm, 0, bytes);
        return false;
    }

    uint32_t remaining = s_fade_out_remaining[channel];
    uint32_t total = s_fade_out_total[channel];

    if (remaining == 0 || total == 0) {
        return false;
    }

    size_t frames = bytes / AUDIO_MIXER_FRAME_BYTES;
    size_t frame = 0;

    for (; frame < frames && remaining > 0; ++frame) {
        for (size_t ch = 0; ch < AUDIO_MIXER_CHANNELS; ++ch) {
            size_t sample_index = frame * AUDIO_MIXER_CHANNELS + ch;
            int32_t scaled = ((int32_t)pcm[sample_index] * (int32_t)remaining) / (int32_t)total;
            pcm[sample_index] = (int16_t)scaled;
        }

        --remaining;
    }

    s_fade_out_remaining[channel] = remaining;

    if (remaining == 0) {
        s_fade_out_muted[channel] = true;

        if (frame < frames) {
            memset(
                &pcm[frame * AUDIO_MIXER_CHANNELS],
                0,
                bytes - frame * AUDIO_MIXER_FRAME_BYTES
            );
        }
    }

    return false;
}

static void audio_mixer_task(void *param)
{
    (void)param;

    while (true) {
        if (!mixer_any_active()) {
            if (s_output_dirty) {
                mixer_set_output_state(AUDIO_MIXER_OUTPUT_DRAINING, "no_active_sources");
                audio_player_output_drain_silence(AUDIO_MIXER_IDLE_SILENCE_MS);
                s_output_dirty = false;
            }
            mixer_set_output_state(AUDIO_MIXER_OUTPUT_IDLE, "no_active_sources");
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        esp_err_t start_err = mixer_start_output_if_needed();
        if (start_err != ESP_OK) {
            vTaskDelay(ticks_at_least_one(AUDIO_MIXER_IDLE_WAIT_MS));
            continue;
        }

        size_t channel_process_bytes[AUDIO_MIXER_CHANNEL_COUNT] = {0};
        size_t out_bytes = 0;
        for (audio_mixer_channel_t ch = 0; ch < AUDIO_MIXER_CHANNEL_COUNT; ++ch) {
            bool should_read = channel_should_read(ch);
            size_t bytes = should_read ? receive_pcm(ch, s_channel_pcm[ch], sizeof(s_channel_pcm[ch])) : 0;
            maybe_log_starvation(ch, should_read, bytes);
            if (bytes == 0) {
                memset(s_channel_pcm[ch], 0, sizeof(s_channel_pcm[ch]));
            }
            channel_process_bytes[ch] = bytes;
            if (audio_player_mixer_fade_out_active(ch) && channel_process_bytes[ch] == 0) {
                channel_process_bytes[ch] = sizeof(s_channel_pcm[ch]);
                memset(s_channel_pcm[ch], 0, sizeof(s_channel_pcm[ch]));
            }
            if (ch == AUDIO_MIXER_CHANNEL_EFFECT && bytes > 0) {
                maybe_log_first_effect_block(s_channel_pcm[ch], bytes);
                maybe_log_effect_signal_start(s_channel_pcm[ch], bytes);
            }
            apply_fade_in(ch, s_channel_pcm[ch], bytes);
            apply_fade_out(ch, s_channel_pcm[ch], channel_process_bytes[ch]);
            if (channel_process_bytes[ch] > out_bytes) {
                out_bytes = channel_process_bytes[ch];
            }
        }

        if (out_bytes == 0) {
            memset(s_mixed_pcm, 0, sizeof(s_mixed_pcm));

            size_t written = 0;
            esp_err_t err = audio_player_output_write(s_mixed_pcm,
                                                    sizeof(s_mixed_pcm),
                                                    &written,
                                                    pdMS_TO_TICKS(100));

            if (err == ESP_OK && written > 0) {
                s_output_dirty = true;
            } else if (err == ESP_ERR_INVALID_STATE) {
                mixer_set_output_state(AUDIO_MIXER_OUTPUT_IDLE, "output_not_started");
                vTaskDelay(ticks_at_least_one(AUDIO_MIXER_IDLE_WAIT_MS));
            } else {
                vTaskDelay(ticks_at_least_one(AUDIO_MIXER_IDLE_WAIT_MS));
            }

            continue;
        }

        size_t samples = out_bytes / sizeof(int16_t);
        for (size_t i = 0; i < samples; ++i) {
            int32_t mixed = 0;
            for (audio_mixer_channel_t ch = 0; ch < AUDIO_MIXER_CHANNEL_COUNT; ++ch) {
                mixed += s_channel_pcm[ch][i];
            }
            s_mixed_pcm[i] = clamp_i16(mixed);
        }

        size_t written = 0;
        esp_err_t err = audio_player_output_write(s_mixed_pcm,
                                                out_bytes,
                                                &written,
                                                pdMS_TO_TICKS(1000));

        if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "i2s write timeout");
            vTaskDelay(ticks_at_least_one(AUDIO_MIXER_IDLE_WAIT_MS));
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s write failed: %s", esp_err_to_name(err));
            audio_player_output_reset();
            mixer_set_output_state(AUDIO_MIXER_OUTPUT_IDLE, "output_reset");
            vTaskDelay(ticks_at_least_one(AUDIO_MIXER_IDLE_WAIT_MS));
        } else if (written != out_bytes) {
            ESP_LOGW(TAG,
                    "i2s partial write: written=%u expected=%u",
                    (unsigned)written,
                    (unsigned)out_bytes);
            audio_player_output_reset();
            mixer_set_output_state(AUDIO_MIXER_OUTPUT_IDLE, "output_reset");
            vTaskDelay(ticks_at_least_one(AUDIO_MIXER_IDLE_WAIT_MS));
        } else {
            s_output_dirty = true;
        }
    }
}

esp_err_t audio_player_mixer_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    if (!s_streams[AUDIO_MIXER_CHANNEL_BACKGROUND_A]) {
        s_streams[AUDIO_MIXER_CHANNEL_BACKGROUND_A] =
            xStreamBufferCreateStatic(sizeof(s_bg_a_storage),
                                      AUDIO_MIXER_TRIGGER_BYTES,
                                      s_bg_a_storage,
                                      &s_bg_a_stream_struct);
    }
    if (!s_streams[AUDIO_MIXER_CHANNEL_BACKGROUND_B]) {
        s_streams[AUDIO_MIXER_CHANNEL_BACKGROUND_B] =
            xStreamBufferCreateStatic(sizeof(s_bg_b_storage),
                                      AUDIO_MIXER_TRIGGER_BYTES,
                                      s_bg_b_storage,
                                      &s_bg_b_stream_struct);
    }
    if (!s_streams[AUDIO_MIXER_CHANNEL_EFFECT]) {
        s_streams[AUDIO_MIXER_CHANNEL_EFFECT] =
            xStreamBufferCreateStatic(sizeof(s_fx_storage),
                                      AUDIO_MIXER_TRIGGER_BYTES,
                                      s_fx_storage,
                                      &s_fx_stream_struct);
    }
    if (!s_streams[AUDIO_MIXER_CHANNEL_BACKGROUND_A] ||
        !s_streams[AUDIO_MIXER_CHANNEL_BACKGROUND_B] ||
        !s_streams[AUDIO_MIXER_CHANNEL_EFFECT]) {
        return ESP_ERR_NO_MEM;
    }
    audio_player_mixer_stop_all();
    return ESP_OK;
}

esp_err_t audio_player_mixer_start(void)
{
    if (!s_task) {
        BaseType_t ok = xTaskCreatePinnedToCore(audio_mixer_task,
                                                "audio_mixer",
                                                4096,
                                                NULL,
                                                7,
                                                &s_task,
                                                1);
        if (ok != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static void audio_player_mixer_start_stream_internal(audio_mixer_channel_t channel, bool audible)
{
    StreamBufferHandle_t stream = mixer_stream(channel);
    uint32_t fade_frames = 0;
    bool started = false;
    if (!stream) {
        return;
    }
    if (mixer_lock()) {
        xStreamBufferReset(stream);
        s_active[channel] = true;
        s_audible[channel] = audible;
        s_primed[channel] = false;
        s_fade_in_remaining[channel] = audible ? AUDIO_MIXER_FADE_IN_FRAMES : 0;
        s_fade_out_total[channel] = 0;
        s_fade_out_remaining[channel] = 0;
        s_fade_out_muted[channel] = false;
        s_finish_fade_pending[channel] = false;
        s_first_audible_logged[channel] = false;
        s_signal_start_logged[channel] = false;
        fade_frames = s_fade_in_remaining[channel];
        started = true;
        mixer_unlock();
    }
    if (started) {
        ESP_LOGD(TAG,
                 "source start: channel=%s audible=%d fade_in_frames=%lu preroll_bytes=%u",
                 mixer_channel_name(channel),
                 audible ? 1 : 0,
                 (unsigned long)fade_frames,
                 (unsigned)AUDIO_MIXER_PREROLL_BYTES);
    }
}

void audio_player_mixer_start_stream(audio_mixer_channel_t channel)
{
    audio_player_mixer_start_stream_internal(channel, true);
}

void audio_player_mixer_start_stream_muted(audio_mixer_channel_t channel)
{
    audio_player_mixer_start_stream_internal(channel, false);
}

void audio_player_mixer_finish_stream(audio_mixer_channel_t channel)
{
    StreamBufferHandle_t stream = mixer_stream(channel);
    bool finished = false;
    bool tail_fade = false;
    bool tail_pending = false;
    size_t remaining = 0;
    if (!stream) {
        return;
    }
    if (mixer_lock()) {
        remaining = xStreamBufferBytesAvailable(stream);
        s_active[channel] = false;
        if (remaining > 0 && s_audible[channel]) {
            s_audible[channel] = true;
            if (remaining <= AUDIO_MIXER_FINISH_FADE_BYTES) {
                uint32_t frames = AUDIO_MIXER_FINISH_FADE_FRAMES;
                if (frames == 0) {
                    frames = 1;
                }
                s_fade_out_total[channel] = frames;
                s_fade_out_remaining[channel] = frames;
                s_fade_out_muted[channel] = false;
                tail_fade = true;
            } else {
                s_fade_out_total[channel] = 0;
                s_fade_out_remaining[channel] = 0;
                s_fade_out_muted[channel] = false;
                s_finish_fade_pending[channel] = true;
                tail_pending = true;
            }
        } else {
            s_audible[channel] = false;
            s_primed[channel] = false;
            s_fade_in_remaining[channel] = 0;
            s_fade_out_total[channel] = 0;
            s_fade_out_remaining[channel] = 0;
            s_fade_out_muted[channel] = false;
            s_finish_fade_pending[channel] = false;
        }
        finished = true;
        mixer_unlock();
    }
    if (finished) {
        ESP_LOGD(TAG,
                 "source finish: channel=%s tail_bytes=%u tail_fade=%d tail_pending=%d",
                 mixer_channel_name(channel),
                 (unsigned)remaining,
                 tail_fade ? 1 : 0,
                 tail_pending ? 1 : 0);
    }
}

void audio_player_mixer_fade_out_stream(audio_mixer_channel_t channel, int duration_ms)
{
    StreamBufferHandle_t stream = mixer_stream(channel);
    bool fade_started = false;

    if (!stream) {
        return;
    }

    if (duration_ms <= 0) {
        audio_player_mixer_stop_stream(channel);
        return;
    }

    uint32_t frames = ((uint32_t)AUDIO_MIXER_SAMPLE_RATE * (uint32_t)duration_ms) / 1000;

    if (frames == 0) {
        frames = 1;
    }

    if (mixer_lock()) {
        if (s_active[channel] || xStreamBufferBytesAvailable(stream) > 0) {
            s_fade_out_total[channel] = frames;
            s_fade_out_remaining[channel] = frames;
            s_fade_out_muted[channel] = false;
            s_finish_fade_pending[channel] = false;
            fade_started = true;
        }

        mixer_unlock();
    }
    if (fade_started) {
        ESP_LOGD(TAG,
                 "source fade out: channel=%s duration_ms=%d frames=%lu",
                 mixer_channel_name(channel),
                 duration_ms,
                 (unsigned long)frames);
    }
}

bool audio_player_mixer_fade_out_active(audio_mixer_channel_t channel)
{
    bool active = false;

    if (channel < 0 || channel >= AUDIO_MIXER_CHANNEL_COUNT) {
        return false;
    }

    if (mixer_lock()) {
        active = s_fade_out_remaining[channel] > 0;
        mixer_unlock();
    }

    return active;
}

void audio_player_mixer_set_stream_audible(audio_mixer_channel_t channel, bool audible)
{
    bool changed = false;
    uint32_t fade_frames = 0;
    if (channel < 0 || channel >= AUDIO_MIXER_CHANNEL_COUNT) {
        return;
    }
    if (mixer_lock()) {
        if (s_active[channel] && s_audible[channel] != audible) {
            s_audible[channel] = audible;
            if (audible) {
                s_fade_in_remaining[channel] = AUDIO_MIXER_FADE_IN_FRAMES;
                fade_frames = s_fade_in_remaining[channel];
            }
            changed = true;
        }
        mixer_unlock();
    }
    if (changed) {
        ESP_LOGD(TAG,
                 "source audible: channel=%s audible=%d fade_in_frames=%lu",
                 mixer_channel_name(channel),
                 audible ? 1 : 0,
                 (unsigned long)fade_frames);
    }
}

void audio_player_mixer_stop_stream(audio_mixer_channel_t channel)
{
    StreamBufferHandle_t stream = mixer_stream(channel);
    size_t dropped = 0;
    bool stopped = false;
    if (!stream) {
        return;
    }
    if (mixer_lock()) {
        dropped = xStreamBufferBytesAvailable(stream);
        s_active[channel] = false;
        s_audible[channel] = false;
        s_primed[channel] = false;
        s_fade_in_remaining[channel] = 0;
        s_fade_out_total[channel] = 0;
        s_fade_out_remaining[channel] = 0;
        s_fade_out_muted[channel] = false;
        s_finish_fade_pending[channel] = false;
        s_first_audible_logged[channel] = false;
        s_signal_start_logged[channel] = false;
        xStreamBufferReset(stream);
        stopped = true;
        mixer_unlock();
    }
    if (stopped) {
        ESP_LOGD(TAG,
                 "source stop: channel=%s dropped_bytes=%u",
                 mixer_channel_name(channel),
                 (unsigned)dropped);
    }
}

void audio_player_mixer_stop_all(void)
{
    if (!mixer_lock()) {
        return;
    }
    for (int i = 0; i < AUDIO_MIXER_CHANNEL_COUNT; ++i) {
        s_active[i] = false;
        s_audible[i] = false;
        s_primed[i] = false;
        s_fade_in_remaining[i] = 0;
        s_fade_out_total[i] = 0;
        s_fade_out_remaining[i] = 0;
        s_fade_out_muted[i] = false;
        s_finish_fade_pending[i] = false;
        s_first_audible_logged[i] = false;
        s_signal_start_logged[i] = false;
        if (s_streams[i]) {
            xStreamBufferReset(s_streams[i]);
        }
    }
    mixer_unlock();
    mixer_set_output_state(AUDIO_MIXER_OUTPUT_IDLE, "stop_all");
    ESP_LOGD(TAG, "source stop all");
}

bool audio_player_mixer_stream_active(audio_mixer_channel_t channel)
{
    bool active = false;
    if (channel < 0 || channel >= AUDIO_MIXER_CHANNEL_COUNT) {
        return false;
    }
    if (mixer_lock()) {
        active = s_active[channel];
        mixer_unlock();
    }
    return active;
}

bool audio_player_mixer_stream_primed(audio_mixer_channel_t channel)
{
    bool primed = false;
    if (channel < 0 || channel >= AUDIO_MIXER_CHANNEL_COUNT) {
        return false;
    }
    if (mixer_lock()) {
        primed = s_primed[channel];
        mixer_unlock();
    }
    return primed;
}

size_t audio_player_mixer_write(audio_mixer_channel_t channel, const void *data, size_t len)
{
    StreamBufferHandle_t stream = mixer_stream(channel);
    const uint8_t *src = (const uint8_t *)data;
    size_t written = 0;
    if (!stream || !data || len == 0) {
        return 0;
    }
    len -= len % AUDIO_MIXER_FRAME_BYTES;
    while (written < len) {
        if (!audio_player_mixer_stream_active(channel)) {
            break;
        }
        size_t space = xStreamBufferSpacesAvailable(stream);
        space -= space % AUDIO_MIXER_FRAME_BYTES;
        if (space == 0) {
            vTaskDelay(ticks_at_least_one(AUDIO_MIXER_IDLE_WAIT_MS));
            continue;
        }
        size_t chunk = len - written;
        if (chunk > AUDIO_MIXER_CHUNK_BYTES) {
            chunk = AUDIO_MIXER_CHUNK_BYTES;
        }
        if (chunk > space) {
            chunk = space;
        }
        chunk -= chunk % AUDIO_MIXER_FRAME_BYTES;
        if (chunk == 0) {
            vTaskDelay(ticks_at_least_one(AUDIO_MIXER_IDLE_WAIT_MS));
            continue;
        }
        size_t sent = xStreamBufferSend(stream,
                                        src + written,
                                        chunk,
                                        0);
        if ((sent % AUDIO_MIXER_FRAME_BYTES) != 0) {
            ESP_LOGE(TAG,
                     "unaligned stream write: channel=%s sent=%u frame=%u",
                     mixer_channel_name(channel),
                     (unsigned)sent,
                     (unsigned)AUDIO_MIXER_FRAME_BYTES);
            audio_player_mixer_stop_stream(channel);
            break;
        }
        if (sent == 0) {
            vTaskDelay(ticks_at_least_one(AUDIO_MIXER_IDLE_WAIT_MS));
            continue;
        }
        written += sent;
    }
    return written;
}
