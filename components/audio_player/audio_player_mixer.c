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
#define AUDIO_MIXER_FADE_IN_MS 12
#define AUDIO_MIXER_FADE_IN_FRAMES ((AUDIO_MIXER_SAMPLE_RATE * AUDIO_MIXER_FADE_IN_MS) / 1000)
#define AUDIO_MIXER_PREROLL_FRAMES 8192
#define AUDIO_MIXER_PREROLL_BYTES (AUDIO_MIXER_PREROLL_FRAMES * AUDIO_MIXER_FRAME_BYTES)
#define AUDIO_MIXER_IDLE_SILENCE_MS 35
#define AUDIO_MIXER_STARVATION_LOG_MS 1000

static const char *TAG = "audio_mixer";

EXT_RAM_BSS_ATTR static uint8_t s_bg_storage[AUDIO_MIXER_STREAM_BYTES];
EXT_RAM_BSS_ATTR static uint8_t s_fx_storage[AUDIO_MIXER_STREAM_BYTES];
EXT_RAM_BSS_ATTR static int16_t s_bg_pcm[AUDIO_MIXER_CHUNK_FRAMES * AUDIO_MIXER_CHANNELS];
EXT_RAM_BSS_ATTR static int16_t s_fx_pcm[AUDIO_MIXER_CHUNK_FRAMES * AUDIO_MIXER_CHANNELS];
EXT_RAM_BSS_ATTR static int16_t s_mixed_pcm[AUDIO_MIXER_CHUNK_FRAMES * AUDIO_MIXER_CHANNELS];
static StaticStreamBuffer_t s_bg_stream_struct;
static StaticStreamBuffer_t s_fx_stream_struct;
static StreamBufferHandle_t s_streams[AUDIO_MIXER_CHANNEL_COUNT];
static SemaphoreHandle_t s_lock = NULL;
static TaskHandle_t s_task = NULL;
static bool s_active[AUDIO_MIXER_CHANNEL_COUNT];
static bool s_primed[AUDIO_MIXER_CHANNEL_COUNT];
static uint16_t s_fade_in_remaining[AUDIO_MIXER_CHANNEL_COUNT];
static uint32_t s_fade_out_remaining[AUDIO_MIXER_CHANNEL_COUNT];
static uint32_t s_fade_out_total[AUDIO_MIXER_CHANNEL_COUNT];
static bool s_fade_out_muted[AUDIO_MIXER_CHANNEL_COUNT];
static bool s_output_dirty = false;
static TickType_t s_starvation_log_tick[AUDIO_MIXER_CHANNEL_COUNT];

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

static size_t receive_pcm(audio_mixer_channel_t channel, int16_t *out, size_t bytes)
{
    StreamBufferHandle_t stream = mixer_stream(channel);

    if (!stream || !out || bytes == 0) {
        return 0;
    }

    memset(out, 0, bytes);

    size_t got = xStreamBufferReceive(stream, out, bytes, 0);
    got -= got % AUDIO_MIXER_FRAME_BYTES;

    if (got == 0) {
        return 0;
    }

    return bytes;
}

static bool channel_should_read(audio_mixer_channel_t channel)
{
    bool read = false;
    if (channel < 0 || channel >= AUDIO_MIXER_CHANNEL_COUNT) {
        return false;
    }
    if (mixer_lock()) {
        StreamBufferHandle_t stream = mixer_stream(channel);
        size_t available = stream ? xStreamBufferBytesAvailable(stream) : 0;
        if (!s_active[channel]) {
            read = available > 0;
        } else if (s_primed[channel]) {
            read = true;
        } else if (available >= AUDIO_MIXER_PREROLL_BYTES) {
            s_primed[channel] = true;
            read = true;
        }
        mixer_unlock();
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

static void maybe_log_starvation(audio_mixer_channel_t channel, bool read_attempted, size_t bytes_read)
{
    const char *name = channel == AUDIO_MIXER_CHANNEL_BACKGROUND ? "background" : "effect";
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

    if (mixer_lock()) {
        StreamBufferHandle_t stream = mixer_stream(channel);
        size_t available = stream ? xStreamBufferBytesAvailable(stream) : 0;
        bool active = s_active[channel];
        bool primed = s_primed[channel];
        uint32_t fade_out = s_fade_out_remaining[channel];
        mixer_unlock();
        (void)audio_player_runtime_reader_snapshot(
            channel == AUDIO_MIXER_CHANNEL_BACKGROUND ? AUDIO_PLAYER_CHANNEL_BACKGROUND : AUDIO_PLAYER_CHANNEL_EFFECT,
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
                 "channel starvation: channel=%s active=%d primed=%d available=%u fade_out=%lu path=%s bytes_done=%u last_loop_gap_ms=%lu last_offset=%ld last_read_ms=%lu last_slow_offset=%ld last_slow_ms=%lu",
                 name,
                 active ? 1 : 0,
                 primed ? 1 : 0,
                 (unsigned)available,
                 (unsigned long)fade_out,
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
                audio_player_output_drain_silence(AUDIO_MIXER_IDLE_SILENCE_MS);
                s_output_dirty = false;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        bool bg_should_read = channel_should_read(AUDIO_MIXER_CHANNEL_BACKGROUND);
        bool fx_should_read = channel_should_read(AUDIO_MIXER_CHANNEL_EFFECT);
        size_t bg_bytes = bg_should_read ?
            receive_pcm(AUDIO_MIXER_CHANNEL_BACKGROUND, s_bg_pcm, sizeof(s_bg_pcm)) : 0;
        size_t fx_bytes = fx_should_read ?
            receive_pcm(AUDIO_MIXER_CHANNEL_EFFECT, s_fx_pcm, sizeof(s_fx_pcm)) : 0;
        maybe_log_starvation(AUDIO_MIXER_CHANNEL_BACKGROUND, bg_should_read, bg_bytes);
        maybe_log_starvation(AUDIO_MIXER_CHANNEL_EFFECT, fx_should_read, fx_bytes);
        if (bg_bytes == 0) {
            memset(s_bg_pcm, 0, sizeof(s_bg_pcm));
        }
        if (fx_bytes == 0) {
            memset(s_fx_pcm, 0, sizeof(s_fx_pcm));
        }
        size_t bg_process_bytes = bg_bytes;
        if (audio_player_mixer_fade_out_active(AUDIO_MIXER_CHANNEL_BACKGROUND) &&
            bg_process_bytes == 0) {
            bg_process_bytes = sizeof(s_bg_pcm);
            memset(s_bg_pcm, 0, sizeof(s_bg_pcm));
        }

        apply_fade_in(AUDIO_MIXER_CHANNEL_BACKGROUND, s_bg_pcm, bg_bytes);
        apply_fade_out(AUDIO_MIXER_CHANNEL_BACKGROUND, s_bg_pcm, bg_process_bytes);

        apply_fade_in(AUDIO_MIXER_CHANNEL_EFFECT, s_fx_pcm, fx_bytes);

        size_t out_bytes = bg_process_bytes > fx_bytes ? bg_process_bytes : fx_bytes;

        if (out_bytes == 0) {
            memset(s_mixed_pcm, 0, sizeof(s_mixed_pcm));

            size_t written = 0;
            esp_err_t err = audio_player_output_write(s_mixed_pcm,
                                                    sizeof(s_mixed_pcm),
                                                    &written,
                                                    pdMS_TO_TICKS(100));

            if (err == ESP_OK && written > 0) {
                s_output_dirty = true;
            } else {
                vTaskDelay(ticks_at_least_one(AUDIO_MIXER_IDLE_WAIT_MS));
            }

            continue;
        }

        size_t samples = out_bytes / sizeof(int16_t);
        for (size_t i = 0; i < samples; ++i) {
            s_mixed_pcm[i] = clamp_i16((int32_t)s_bg_pcm[i] + (int32_t)s_fx_pcm[i]);
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
            vTaskDelay(ticks_at_least_one(AUDIO_MIXER_IDLE_WAIT_MS));
        } else if (written != out_bytes) {
            ESP_LOGW(TAG,
                    "i2s partial write: written=%u expected=%u",
                    (unsigned)written,
                    (unsigned)out_bytes);
            audio_player_output_reset();
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
    if (!s_streams[AUDIO_MIXER_CHANNEL_BACKGROUND]) {
        s_streams[AUDIO_MIXER_CHANNEL_BACKGROUND] =
            xStreamBufferCreateStatic(sizeof(s_bg_storage),
                                      AUDIO_MIXER_TRIGGER_BYTES,
                                      s_bg_storage,
                                      &s_bg_stream_struct);
    }
    if (!s_streams[AUDIO_MIXER_CHANNEL_EFFECT]) {
        s_streams[AUDIO_MIXER_CHANNEL_EFFECT] =
            xStreamBufferCreateStatic(sizeof(s_fx_storage),
                                      AUDIO_MIXER_TRIGGER_BYTES,
                                      s_fx_storage,
                                      &s_fx_stream_struct);
    }
    if (!s_streams[AUDIO_MIXER_CHANNEL_BACKGROUND] || !s_streams[AUDIO_MIXER_CHANNEL_EFFECT]) {
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

void audio_player_mixer_start_stream(audio_mixer_channel_t channel)
{
    StreamBufferHandle_t stream = mixer_stream(channel);
    if (!stream) {
        return;
    }
    if (mixer_lock()) {
        xStreamBufferReset(stream);
        s_active[channel] = true;
        s_primed[channel] = false;
        s_fade_in_remaining[channel] = AUDIO_MIXER_FADE_IN_FRAMES;
        s_fade_out_total[channel] = 0;
        s_fade_out_remaining[channel] = 0;
        s_fade_out_muted[channel] = false;
        mixer_unlock();
    }
}

void audio_player_mixer_finish_stream(audio_mixer_channel_t channel)
{
    StreamBufferHandle_t stream = mixer_stream(channel);
    if (!stream) {
        return;
    }
    if (mixer_lock()) {
        s_active[channel] = false;
        s_primed[channel] = false;
        mixer_unlock();
    }
}

void audio_player_mixer_fade_out_stream(audio_mixer_channel_t channel, int duration_ms)
{
    StreamBufferHandle_t stream = mixer_stream(channel);

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
        }

        mixer_unlock();
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

void audio_player_mixer_stop_stream(audio_mixer_channel_t channel)
{
    StreamBufferHandle_t stream = mixer_stream(channel);
    if (!stream) {
        return;
    }
    if (mixer_lock()) {
        s_active[channel] = false;
        s_primed[channel] = false;
        s_fade_in_remaining[channel] = 0;
        s_fade_out_total[channel] = 0;
        s_fade_out_remaining[channel] = 0;
        s_fade_out_muted[channel] = false;
        xStreamBufferReset(stream);
        mixer_unlock();
    }
}

void audio_player_mixer_stop_all(void)
{
    if (!mixer_lock()) {
        return;
    }
    for (int i = 0; i < AUDIO_MIXER_CHANNEL_COUNT; ++i) {
        s_active[i] = false;
        s_primed[i] = false;
        s_fade_in_remaining[i] = 0;
        s_fade_out_total[i] = 0;
        s_fade_out_remaining[i] = 0;
        s_fade_out_muted[i] = false;
        if (s_streams[i]) {
            xStreamBufferReset(s_streams[i]);
        }
    }
    mixer_unlock();
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
        size_t chunk = len - written;
        if (chunk > AUDIO_MIXER_CHUNK_BYTES) {
            chunk = AUDIO_MIXER_CHUNK_BYTES;
        }
        size_t sent = xStreamBufferSend(stream,
                                        src + written,
                                        chunk,
                                        pdMS_TO_TICKS(AUDIO_MIXER_WRITE_TIMEOUT_MS));
        sent -= sent % AUDIO_MIXER_FRAME_BYTES;
        if (sent == 0) {
            vTaskDelay(ticks_at_least_one(AUDIO_MIXER_IDLE_WAIT_MS));
            continue;
        }
        written += sent;
    }
    return written;
}
