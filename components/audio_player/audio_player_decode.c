#include "audio_player_internal.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "event_bus.h"
#include "helix_mp3_wrapper.h"
#include "error_monitor.h"

#define AUDIO_FINISHED_POST_RETRIES 5
#define AUDIO_FINISHED_POST_WAIT_MS 20

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_CHANNELS    2
#define AUDIO_WAV_EDGE_FADE_MS 24
#define AUDIO_WAV_EDGE_FADE_FRAMES ((AUDIO_SAMPLE_RATE * AUDIO_WAV_EDGE_FADE_MS) / 1000)

static const char *TAG = "audio_player";

static void post_audio_finished(const char *path)
{
    if (!path || !path[0]) {
        return;
    }
    event_bus_message_t msg = {0};
    msg.type = EVENT_AUDIO_FINISHED;
    strncpy(msg.payload, path, sizeof(msg.payload) - 1);
    msg.payload[sizeof(msg.payload) - 1] = 0;
    for (int attempt = 0; attempt < AUDIO_FINISHED_POST_RETRIES; ++attempt) {
        esp_err_t err = event_bus_post(&msg, pdMS_TO_TICKS(AUDIO_FINISHED_POST_WAIT_MS));
        if (err == ESP_OK) {
            return;
        }
        if (err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "audio finished event post failed: %s", esp_err_to_name(err));
            return;
        }
    }
    ESP_LOGW(TAG, "audio finished event dropped for %s", path);
}

static size_t mp3_i2s_write_cb(const uint8_t *data, size_t len, void *user)
{
    const audio_reader_ctx_t *ctx = (const audio_reader_ctx_t *)user;
    if (audio_player_reader_stop_requested(ctx)) {
        return 0;
    }
    audio_player_reader_wait_while_paused(ctx);

    return audio_player_mixer_write(AUDIO_MIXER_CHANNEL_EFFECT, data, len);
}

static void mp3_progress_cb(size_t bytes_read, size_t total_bytes, uint32_t elapsed_ms, uint32_t est_total_ms, void *user)
{
    const audio_reader_ctx_t *ctx = (const audio_reader_ctx_t *)user;
    int *bitrate_ptr = audio_player_reader_bitrate_ptr(ctx);
    int kbps = bitrate_ptr ? *bitrate_ptr : 0;
    audio_player_status_update_progress(bytes_read, total_bytes, elapsed_ms, est_total_ms);
    if (kbps > 0) {
        audio_player_status_update_bitrate(kbps);
    }
}

static audio_format_t detect_format(const uint8_t *hdr, size_t len)
{
    if (len >= 12 && memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WAVE", 4) == 0) {
        return AUDIO_FMT_WAV;
    }
    if (len >= 3 && memcmp(hdr, "ID3", 3) == 0) {
        return AUDIO_FMT_MP3;
    }
    if (len >= 2 && hdr[0] == 0xFF && (hdr[1] & 0xE0) == 0xE0) {
        return AUDIO_FMT_MP3;
    }
    if (len >= 4 && memcmp(hdr, "OggS", 4) == 0) {
        return AUDIO_FMT_OGG;
    }
    return AUDIO_FMT_UNKNOWN;
}

static esp_err_t parse_wav_header(FILE *f, audio_info_t *info, long *data_offset, uint32_t *data_size)
{
    struct __attribute__((packed)) riff_header {
        char riff[4];
        uint32_t size;
        char wave[4];
    } riff;
    struct __attribute__((packed)) chunk_header {
        char id[4];
        uint32_t size;
    } chunk;
    struct __attribute__((packed)) fmt_chunk {
        uint16_t audio_format;
        uint16_t num_channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_per_sample;
    } fmt;

    if (!f || !info || !data_offset || !data_size) {
        return ESP_FAIL;
    }
    memset(info, 0, sizeof(*info));
    *data_offset = 0;
    *data_size = 0;

    if (fread(&riff, 1, sizeof(riff), f) != sizeof(riff)) {
        return ESP_FAIL;
    }
    if (memcmp(riff.riff, "RIFF", 4) != 0 || memcmp(riff.wave, "WAVE", 4) != 0) {
        return ESP_FAIL;
    }

    bool fmt_found = false;
    while (fread(&chunk, 1, sizeof(chunk), f) == sizeof(chunk)) {
        long payload_offset = ftell(f);
        if (payload_offset < 0) {
            return ESP_FAIL;
        }

        if (memcmp(chunk.id, "fmt ", 4) == 0) {
            if (chunk.size < sizeof(fmt) ||
                fread(&fmt, 1, sizeof(fmt), f) != sizeof(fmt)) {
                return ESP_FAIL;
            }
            if (fmt.audio_format != 1 || fmt.bits_per_sample != 16 ||
                fmt.num_channels == 0 || fmt.num_channels > 2 ||
                fmt.sample_rate == 0 || fmt.block_align == 0) {
                return ESP_FAIL;
            }
            info->fmt = AUDIO_FMT_WAV;
            info->sample_rate = fmt.sample_rate;
            info->channels = (uint8_t)fmt.num_channels;
            info->bits_per_sample = (uint8_t)fmt.bits_per_sample;
            fmt_found = true;
        } else if (memcmp(chunk.id, "data", 4) == 0) {
            if (!fmt_found || chunk.size == 0) {
                return ESP_FAIL;
            }
            *data_offset = payload_offset;
            *data_size = chunk.size;
            fseek(f, *data_offset, SEEK_SET);
            return ESP_OK;
        }

        long next = payload_offset + (long)chunk.size + (long)(chunk.size & 1U);
        if (fseek(f, next, SEEK_SET) != 0) {
            return ESP_FAIL;
        }
    }

    return ESP_FAIL;
}

static size_t convert_pcm_to_output(const int16_t *in, size_t frames_in, const audio_info_t *info, int16_t *out, float gain)
{
    if (info->sample_rate == AUDIO_SAMPLE_RATE && info->channels == AUDIO_CHANNELS) {
        size_t samples = frames_in * AUDIO_CHANNELS;
        for (size_t i = 0; i < samples; ++i) {
            int32_t v = (int32_t)((float)in[i] * gain);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out[i] = (int16_t)v;
        }
        return frames_in;
    }

    float ratio = (float)info->sample_rate / (float)AUDIO_SAMPLE_RATE;
    size_t out_frames = (size_t)((float)frames_in / ratio);
    for (size_t o = 0; o < out_frames; ++o) {
        size_t src_idx = (size_t)(o * ratio);
        int32_t l = 0, r = 0;
        if (info->channels == 1) {
            int16_t s = in[src_idx];
            l = r = s;
        } else {
            l = in[src_idx * 2];
            r = in[src_idx * 2 + 1];
        }
        l = (int32_t)((float)l * gain);
        r = (int32_t)((float)r * gain);
        if (l > 32767) {
            l = 32767;
        } else if (l < -32768) {
            l = -32768;
        }
        if (r > 32767) {
            r = 32767;
        } else if (r < -32768) {
            r = -32768;
        }
        out[o * 2] = (int16_t)l;
        out[o * 2 + 1] = (int16_t)r;
    }
    return out_frames;
}

static void apply_output_fade_in_at(int16_t *out,
                                    size_t frames,
                                    size_t fade_frames,
                                    size_t start_frame)
{
    if (!out || frames == 0 || fade_frames == 0) {
        return;
    }

    for (size_t frame = 0; frame < frames; ++frame) {
        size_t absolute_frame = start_frame + frame;

        if (absolute_frame >= fade_frames) {
            break;
        }

        int32_t gain = (int32_t)(absolute_frame + 1);

        for (size_t ch = 0; ch < AUDIO_CHANNELS; ++ch) {
            size_t sample = frame * AUDIO_CHANNELS + ch;
            out[sample] = (int16_t)(((int32_t)out[sample] * gain) / (int32_t)fade_frames);
        }
    }
}

static void apply_output_fade_out_at(int16_t *out,
                                     size_t frames,
                                     size_t fade_frames,
                                     size_t start_frame,
                                     size_t total_frames)
{
    if (!out || frames == 0 || fade_frames == 0 || total_frames == 0) {
        return;
    }

    size_t fade_start = total_frames > fade_frames ? total_frames - fade_frames : 0;

    for (size_t frame = 0; frame < frames; ++frame) {
        size_t absolute_frame = start_frame + frame;

        if (absolute_frame < fade_start) {
            continue;
        }

        if (absolute_frame >= total_frames) {
            for (size_t ch = 0; ch < AUDIO_CHANNELS; ++ch) {
                out[frame * AUDIO_CHANNELS + ch] = 0;
            }
            continue;
        }

        size_t frames_left = total_frames - absolute_frame - 1;
        int32_t gain = (int32_t)(frames_left > fade_frames ? fade_frames : frames_left);

        for (size_t ch = 0; ch < AUDIO_CHANNELS; ++ch) {
            size_t sample = frame * AUDIO_CHANNELS + ch;
            out[sample] = (int16_t)(((int32_t)out[sample] * gain) / (int32_t)fade_frames);
        }
    }
}

static size_t estimate_output_frames(size_t input_frames, const audio_info_t *info)
{
    if (!info || info->sample_rate == 0) {
        return input_frames;
    }

    if (info->sample_rate == AUDIO_SAMPLE_RATE) {
        return input_frames;
    }

    return (input_frames * AUDIO_SAMPLE_RATE) / info->sample_rate;
}

static bool wav_edge_fade_enabled(const audio_reader_ctx_t *ctx)
{
    if (!ctx) {
        return false;
    }
    return ctx->cmd.channel == AUDIO_PLAYER_CHANNEL_BACKGROUND ||
           ctx->cmd.type == AUDIO_CMD_PLAY ||
           ctx->cmd.type == AUDIO_CMD_SEEK;
}

static bool decode_wav_to_output(FILE *f,
                                 const audio_reader_ctx_t *ctx,
                                 const audio_info_t *info,
                                 uint32_t data_size,
                                 size_t initial_bytes_done)
{
    const size_t in_buf_frames = 512;
    int16_t *in_buf = heap_caps_malloc(in_buf_frames * info->channels * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *out_buf = heap_caps_malloc(in_buf_frames * AUDIO_CHANNELS * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!in_buf || !out_buf) {
        ESP_LOGE(TAG, "no mem for wav decode");
        if (in_buf) heap_caps_free(in_buf);
        if (out_buf) heap_caps_free(out_buf);
        error_monitor_report_audio_fault();
        return false;
    }

    size_t bytes_read = 0;
    size_t bytes_done = initial_bytes_done;
    size_t output_frames_done = 0;
    const uint32_t byte_rate = info->sample_rate * info->channels * (info->bits_per_sample / 8);
    const size_t block_align = (size_t)info->channels * ((size_t)info->bits_per_sample / 8U);
    const size_t remaining_data_bytes = data_size > initial_bytes_done ? data_size - initial_bytes_done : data_size;
    const size_t total_input_frames = block_align > 0 ? remaining_data_bytes / block_align : 0;
    const size_t total_output_frames = estimate_output_frames(total_input_frames, info);
    while ((bytes_read = fread(in_buf, 1, in_buf_frames * info->channels * sizeof(int16_t), f)) > 0) {
        if (audio_player_reader_stop_requested(ctx)) {
            break;
        }
        audio_player_reader_wait_while_paused(ctx);

        float gain = audio_player_reader_volume(ctx) / 100.0f;

        size_t frames = bytes_read / (info->channels * sizeof(int16_t));
        size_t out_frames = convert_pcm_to_output(in_buf, frames, info, out_buf, gain);
        bool edge_fade = wav_edge_fade_enabled(ctx);
        size_t chunk_start_frame = output_frames_done;

        if (edge_fade && initial_bytes_done == 0) {
            apply_output_fade_in_at(
                out_buf,
                out_frames,
                AUDIO_WAV_EDGE_FADE_FRAMES,
                chunk_start_frame
            );
        }

        if (edge_fade) {
            apply_output_fade_out_at(
                out_buf,
                out_frames,
                AUDIO_WAV_EDGE_FADE_FRAMES,
                chunk_start_frame,
                total_output_frames
            );
        }
        size_t out_bytes = out_frames * AUDIO_CHANNELS * sizeof(int16_t);
        audio_mixer_channel_t mixer_ch = ctx->cmd.channel == AUDIO_PLAYER_CHANNEL_BACKGROUND ?
            AUDIO_MIXER_CHANNEL_BACKGROUND : AUDIO_MIXER_CHANNEL_EFFECT;
        size_t written = audio_player_mixer_write(mixer_ch, out_buf, out_bytes);
        if (written == 0 && audio_player_reader_stop_requested(ctx)) {
            break;
        }
        if (written != out_bytes) {
            if (!audio_player_reader_stop_requested(ctx)) {
                ESP_LOGW(TAG,
                         "mixer write incomplete: written=%u expected=%u channel=%d",
                         (unsigned)written,
                         (unsigned)out_bytes,
                         (int)ctx->cmd.channel);
            }
            break;
        }

        bytes_done += bytes_read;
        output_frames_done += out_frames;
        if (byte_rate > 0) {
            uint32_t pos_ms = (uint32_t)((bytes_done * 1000ULL) / byte_rate);
            uint32_t dur_ms = data_size > 0 ? (uint32_t)((data_size * 1000ULL) / byte_rate) : 0;
            audio_player_status_update_progress(bytes_done, data_size, pos_ms, dur_ms);
        } else {
            audio_player_status_update_progress(bytes_done, data_size, 0, 0);
        }
    }

    heap_caps_free(in_buf);
    heap_caps_free(out_buf);
    return true;
}

void audio_player_reader_task(void *param)
{
    audio_reader_ctx_t *ctx = (audio_reader_ctx_t *)param;
    audio_cmd_t cmd = ctx ? ctx->cmd : (audio_cmd_t){0};
    audio_player_runtime_reader_started(ctx);

    FILE *f = fopen(cmd.path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s", cmd.path);
        audio_player_status_set_message("Cannot open file");
        error_monitor_report_audio_fault();
        if (!audio_player_reader_stop_requested(ctx)) {
            post_audio_finished(cmd.path);
        }
        audio_player_runtime_reader_finished(ctx);
        audio_player_runtime_destroy_reader_ctx(ctx);
        vTaskDelete(NULL);
        return;
    }

    fseek(f, 0, SEEK_END);
#if AUDIO_PLAYER_DEBUG
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t head[32] = {0};
    fread(head, 1, sizeof(head), f);
    fseek(f, 0, SEEK_SET);
    ESP_LOGI(TAG, "file %s size=%ld head=%02X %02X %02X %02X %02X %02X %02X %02X",
             cmd.path,
             fsize,
             head[0],
             head[1],
             head[2],
             head[3],
             head[4],
             head[5],
             head[6],
             head[7]);
#else
    fseek(f, 0, SEEK_END);
    fseek(f, 0, SEEK_SET);
#endif

    uint8_t hdr[16] = {0};
    size_t hdr_len = fread(hdr, 1, sizeof(hdr), f);
    fseek(f, 0, SEEK_SET);
    audio_format_t fmt = detect_format(hdr, hdr_len);
    audio_info_t info = {0};
    info.fmt = fmt;

    if (cmd.seek_ratio < 0.0f) {
        audio_player_status_set_play(cmd.path, fmt, audio_player_reader_volume(ctx));
    } else {
        audio_player_status_mark_seek_play(cmd.path, fmt);
    }

    if (fmt == AUDIO_FMT_WAV) {
        long data_off = 0;
        uint32_t data_size = 0;
        if (parse_wav_header(f, &info, &data_off, &data_size) == ESP_OK) {
            uint32_t byte_rate = info.sample_rate * info.channels * (info.bits_per_sample / 8);
            size_t skip_bytes = 0;
            if (cmd.seek_ratio >= 0.0f && cmd.seek_ratio <= 1.0f) {
                skip_bytes = (size_t)((float)data_size * cmd.seek_ratio);
                if (info.channels > 0 && info.bits_per_sample > 0) {
                    size_t block_align = (size_t)info.channels * ((size_t)info.bits_per_sample / 8U);
                    if (block_align > 0) {
                        skip_bytes -= skip_bytes % block_align;
                    }
                }
                if (skip_bytes >= data_size) {
                    skip_bytes = data_size > 0 ? data_size - 1 : 0;
                    if (info.channels > 0 && info.bits_per_sample > 0) {
                        size_t block_align = (size_t)info.channels * ((size_t)info.bits_per_sample / 8U);
                        if (block_align > 0) {
                            skip_bytes -= skip_bytes % block_align;
                        }
                    }
                }
            }
            bool decode_ok = true;
            do {
                fseek(f, data_off + (long)skip_bytes, SEEK_SET);
                if (skip_bytes > 0 && byte_rate > 0) {
                    uint32_t est_ms = (uint32_t)((skip_bytes * 1000ULL) / byte_rate);
                    uint32_t dur_ms = (uint32_t)((data_size * 1000ULL) / byte_rate);
                    audio_player_status_update_progress(skip_bytes, data_size, est_ms, dur_ms);
                } else {
                    audio_player_status_update_progress(0, data_size, 0, byte_rate > 0 ? (uint32_t)((data_size * 1000ULL) / byte_rate) : 0);
                }
                decode_ok = decode_wav_to_output(f, ctx, &info, data_size, skip_bytes);
                skip_bytes = 0;
            } while (cmd.repeat &&
                     cmd.channel == AUDIO_PLAYER_CHANNEL_BACKGROUND &&
                     decode_ok &&
                     !audio_player_reader_stop_requested(ctx));
        } else {
            ESP_LOGE(TAG, "bad wav header");
            audio_player_status_set_message("Bad WAV header");
            error_monitor_report_audio_fault();
        }
    } else if (fmt == AUDIO_FMT_MP3) {
        if (cmd.channel == AUDIO_PLAYER_CHANNEL_BACKGROUND) {
            ESP_LOGE(TAG, "background audio supports WAV only");
            audio_player_status_set_message("Background requires WAV");
            error_monitor_report_audio_fault();
        } else {
        fclose(f);
        f = NULL;
        int *bitrate_kbps = audio_player_reader_bitrate_ptr(ctx);
        if (bitrate_kbps) {
            *bitrate_kbps = 0;
        }
        helix_mp3_decode_file(cmd.path,
                              audio_player_reader_volume(ctx),
                              mp3_i2s_write_cb,
                              ctx,
                              mp3_progress_cb,
                              ctx,
                              cmd.seek_ratio);
        }
    } else if (fmt == AUDIO_FMT_OGG) {
        ESP_LOGW(TAG, "OGG decode not implemented yet");
        audio_player_status_set_message("OGG not supported");
        error_monitor_report_audio_fault();
    } else {
        ESP_LOGW(TAG, "unknown audio format");
        audio_player_status_set_message("Unknown format");
        error_monitor_report_audio_fault();
    }

    if (f) {
        fclose(f);
    }
    if (!audio_player_reader_stop_requested(ctx)) {
        post_audio_finished(cmd.path);
    }
    audio_player_runtime_reader_finished(ctx);
    audio_player_runtime_destroy_reader_ctx(ctx);
    vTaskDelete(NULL);
}
