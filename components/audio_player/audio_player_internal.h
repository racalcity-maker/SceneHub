#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#include "audio_player.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

typedef enum {
    AUDIO_FMT_UNKNOWN = 0,
    AUDIO_FMT_WAV,
    AUDIO_FMT_MP3,
    AUDIO_FMT_OGG,
} audio_format_t;

typedef enum {
    AUDIO_CMD_NONE = 0,
    AUDIO_CMD_PLAY,
    AUDIO_CMD_PLAY_BACKGROUND,
    AUDIO_CMD_PLAY_EFFECT,
    AUDIO_CMD_SEEK,
    AUDIO_CMD_STOP,
    AUDIO_CMD_STOP_BACKGROUND,
    AUDIO_CMD_STOP_EFFECT,
    AUDIO_CMD_PAUSE,
    AUDIO_CMD_RESUME,
} audio_cmd_type_t;

typedef struct {
    audio_cmd_type_t type;
    audio_player_channel_t channel;
    char path[256];
    int volume;
    float seek_ratio;
    bool repeat;
    TaskHandle_t completion_task;
} audio_cmd_t;

typedef enum {
    AUDIO_RUNTIME_IDLE = 0,
    AUDIO_RUNTIME_STARTING,
    AUDIO_RUNTIME_PLAYING,
    AUDIO_RUNTIME_PAUSED,
    AUDIO_RUNTIME_STOPPING,
    AUDIO_RUNTIME_ERROR,
} audio_runtime_state_t;

typedef struct {
    audio_format_t fmt;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
} audio_info_t;

typedef struct {
    audio_cmd_t cmd;
    uint8_t runtime_slot;
    uint8_t mixer_channel;
    EventGroupHandle_t flags;
    EventBits_t stop_bit;
    int *bitrate_kbps;
    int *volume_percent;
    size_t last_bytes_done;
    uint32_t last_loop_gap_ms;
    long last_read_offset;
    uint32_t last_read_elapsed_ms;
    long last_slow_read_offset;
    uint32_t last_slow_read_elapsed_ms;
} audio_reader_ctx_t;

typedef enum {
    AUDIO_MIXER_CHANNEL_BACKGROUND_A = 0,
    AUDIO_MIXER_CHANNEL_BACKGROUND_B,
    AUDIO_MIXER_CHANNEL_EFFECT,
    AUDIO_MIXER_CHANNEL_COUNT,
} audio_mixer_channel_t;

esp_err_t audio_player_volume_init(void);
int *audio_player_volume_ptr(void);
esp_err_t audio_player_output_init(void);
esp_err_t audio_player_output_enable(void);
esp_err_t audio_player_output_start(void);
void audio_player_output_disable(void);
void audio_player_output_pause(void);
void audio_player_output_resume(void);
void audio_player_output_drain_silence(int duration_ms);
void audio_player_output_reset(void);
void audio_player_output_play_tone(int freq_hz, int duration_ms, int volume_percent);
esp_err_t audio_player_output_write(const void *data, size_t len, size_t *bytes_written, TickType_t timeout);
i2s_chan_handle_t audio_player_output_channel(void);

esp_err_t audio_player_mixer_init(void);
esp_err_t audio_player_mixer_start(void);
void audio_player_mixer_start_stream(audio_mixer_channel_t channel);
void audio_player_mixer_start_stream_muted(audio_mixer_channel_t channel);
void audio_player_mixer_finish_stream(audio_mixer_channel_t channel);
void audio_player_mixer_stop_stream(audio_mixer_channel_t channel);
void audio_player_mixer_fade_out_stream(audio_mixer_channel_t channel, int duration_ms);
bool audio_player_mixer_fade_out_active(audio_mixer_channel_t channel);
void audio_player_mixer_set_stream_audible(audio_mixer_channel_t channel, bool audible);
void audio_player_mixer_stop_all(void);
bool audio_player_mixer_stream_active(audio_mixer_channel_t channel);
bool audio_player_mixer_stream_primed(audio_mixer_channel_t channel);
size_t audio_player_mixer_write(audio_mixer_channel_t channel, const void *data, size_t len);

esp_err_t audio_player_status_init(void);
audio_player_format_t audio_player_to_public_format(audio_format_t fmt);
void audio_player_status_reset(int volume);
void audio_player_status_set_play(const char *path, audio_format_t fmt, int volume);
void audio_player_status_set_runtime_state(audio_runtime_state_t state);
void audio_player_status_update_progress(size_t bytes_read, size_t total_bytes, uint32_t pos_ms, uint32_t est_total_ms);
void audio_player_status_update_bitrate(int kbps);
void audio_player_status_set_message(const char *msg);
void audio_player_status_set_volume(int volume);
void audio_player_status_get(audio_player_status_t *out);
void audio_player_status_mark_seek_play(const char *path, audio_format_t fmt);
bool audio_player_status_prepare_seek(char *path, size_t path_len, int *dur_ms);
void audio_player_status_set_seek_position(uint32_t pos_ms, int progress);

int audio_player_runtime_volume(void);
esp_err_t audio_player_runtime_init(void);
esp_err_t audio_player_runtime_start(void);
esp_err_t audio_player_runtime_enqueue(const audio_cmd_t *cmd);
esp_err_t audio_player_runtime_enqueue_wait(const audio_cmd_t *cmd, uint32_t timeout_ms);
audio_reader_ctx_t *audio_player_runtime_create_reader_ctx(const audio_cmd_t *cmd, uint8_t runtime_slot);
void audio_player_runtime_destroy_reader_ctx(audio_reader_ctx_t *ctx);
esp_err_t audio_player_runtime_prepare_seek(audio_cmd_t *cmd, uint32_t pos_ms);
void audio_player_runtime_reader_started(const audio_reader_ctx_t *ctx);
void audio_player_runtime_reader_finished(audio_reader_ctx_t *ctx);
bool audio_player_reader_stop_requested(const audio_reader_ctx_t *ctx);
void audio_player_reader_wait_while_paused(const audio_reader_ctx_t *ctx);
int *audio_player_reader_bitrate_ptr(const audio_reader_ctx_t *ctx);
int audio_player_reader_volume(const audio_reader_ctx_t *ctx);
bool audio_player_runtime_reader_snapshot(audio_player_channel_t channel,
                                          char *path,
                                          size_t path_len,
                                          size_t *bytes_done,
                                          uint32_t *loop_gap_ms,
                                          long *read_offset,
                                          uint32_t *read_elapsed_ms,
                                          long *slow_read_offset,
                                          uint32_t *slow_read_elapsed_ms);
size_t audio_player_wav_decode_chunk_frames(const audio_info_t *info);
void audio_player_reader_task(void *param);
