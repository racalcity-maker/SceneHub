#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef AUDIO_PLAYER_DEBUG
#define AUDIO_PLAYER_DEBUG 0
#endif

typedef enum {
    AUDIO_PLAYER_FMT_UNKNOWN = 0,
    AUDIO_PLAYER_FMT_WAV,
    AUDIO_PLAYER_FMT_MP3,
    AUDIO_PLAYER_FMT_OGG,
} audio_player_format_t;

typedef enum {
    AUDIO_PLAYER_CHANNEL_EFFECT = 0,
    AUDIO_PLAYER_CHANNEL_BACKGROUND,
} audio_player_channel_t;

typedef struct {
    bool playing;
    bool paused;
    int volume;
    int progress;   // 0-100, bytes-based estimate
    int pos_ms;     // elapsed, ms (best effort)
    int dur_ms;     // estimated duration, ms (0 if unknown)
    int bitrate_kbps;
    char path[256];
    char message[64];
    audio_player_format_t fmt;
} audio_player_status_t;

typedef enum {
    AUDIO_PLAYER_ASSET_UNKNOWN = 0,
    AUDIO_PLAYER_ASSET_READY,
    AUDIO_PLAYER_ASSET_MISSING,
    AUDIO_PLAYER_ASSET_BAD_HEADER,
    AUDIO_PLAYER_ASSET_UNSUPPORTED_FORMAT,
    AUDIO_PLAYER_ASSET_IO_ERROR,
} audio_player_asset_status_t;

typedef struct {
    char path[256];
    audio_player_asset_status_t status;
    audio_player_format_t fmt;
    size_t size_bytes;
    uint32_t prepared_at_ms;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t duration_ms;
} audio_player_asset_info_t;

esp_err_t audio_player_init(void);
esp_err_t audio_player_start(void);
esp_err_t audio_player_play(const char *path);
esp_err_t audio_player_play_seek(const char *path, float seek_ratio);
esp_err_t audio_player_play_background_wav(const char *path, int volume_percent);
esp_err_t audio_player_play_background_wav_repeat(const char *path, int volume_percent, bool repeat);
esp_err_t audio_player_play_effect(const char *path, int volume_percent);
esp_err_t audio_player_seek(uint32_t pos_ms);
esp_err_t audio_player_set_volume(int percent);
void audio_player_stop(void);
void audio_player_stop_background(void);
void audio_player_stop_effect(void);
void audio_player_stop_all(void);
void audio_player_pause(void);
void audio_player_resume(void);
int audio_player_get_volume(void);
void audio_player_get_status(audio_player_status_t *out);
esp_err_t audio_player_prepare_path(const char *path, audio_player_asset_info_t *out);
bool audio_player_asset_is_prepared(const char *path);
esp_err_t audio_player_asset_get(const char *path, audio_player_asset_info_t *out);
uint32_t audio_player_asset_generation(void);
void audio_player_asset_cache_clear(void);
