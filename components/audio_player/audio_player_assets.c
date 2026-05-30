#include "audio_player.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_attr.h"
#include "esp_timer.h"
#include "sd_storage.h"

#define AUDIO_ASSET_CACHE_MAX 48

static EXT_RAM_BSS_ATTR audio_player_asset_info_t s_audio_asset_cache[AUDIO_ASSET_CACHE_MAX];
static size_t s_audio_asset_cache_count;
static size_t s_audio_asset_cache_next;
static uint32_t s_audio_asset_generation;

static uint32_t audio_asset_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static audio_player_format_t audio_asset_detect_format(const uint8_t *hdr, size_t len)
{
    if (len >= 12 && memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WAVE", 4) == 0) {
        return AUDIO_PLAYER_FMT_WAV;
    }
    if (len >= 3 && memcmp(hdr, "ID3", 3) == 0) {
        return AUDIO_PLAYER_FMT_MP3;
    }
    if (len >= 2 && hdr[0] == 0xFF && (hdr[1] & 0xE0) == 0xE0) {
        return AUDIO_PLAYER_FMT_MP3;
    }
    if (len >= 4 && memcmp(hdr, "OggS", 4) == 0) {
        return AUDIO_PLAYER_FMT_OGG;
    }
    return AUDIO_PLAYER_FMT_UNKNOWN;
}

static int audio_asset_find(const char *path)
{
    if (!path || !path[0]) {
        return -1;
    }
    for (size_t i = 0; i < s_audio_asset_cache_count; ++i) {
        if (strcmp(s_audio_asset_cache[i].path, path) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void audio_asset_copy(char *dst, size_t dst_size, const char *src)
{
    size_t i = 0;
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static audio_player_asset_info_t *audio_asset_cache_slot(const char *path)
{
    int existing = audio_asset_find(path);
    if (existing >= 0) {
        return &s_audio_asset_cache[existing];
    }
    if (s_audio_asset_cache_count < AUDIO_ASSET_CACHE_MAX) {
        return &s_audio_asset_cache[s_audio_asset_cache_count++];
    }
    audio_player_asset_info_t *slot = &s_audio_asset_cache[s_audio_asset_cache_next++ % AUDIO_ASSET_CACHE_MAX];
    memset(slot, 0, sizeof(*slot));
    return slot;
}

static esp_err_t audio_asset_parse_wav(FILE *f, audio_player_asset_info_t *info)
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
    bool fmt_found = false;

    if (!f || !info) {
        return ESP_ERR_INVALID_ARG;
    }
    if (fseek(f, 0, SEEK_SET) != 0 || fread(&riff, 1, sizeof(riff), f) != sizeof(riff)) {
        return ESP_FAIL;
    }
    if (memcmp(riff.riff, "RIFF", 4) != 0 || memcmp(riff.wave, "WAVE", 4) != 0) {
        return ESP_FAIL;
    }
    while (fread(&chunk, 1, sizeof(chunk), f) == sizeof(chunk)) {
        long payload_offset = ftell(f);
        if (payload_offset < 0) {
            return ESP_FAIL;
        }
        if (memcmp(chunk.id, "fmt ", 4) == 0) {
            if (chunk.size < sizeof(fmt) || fread(&fmt, 1, sizeof(fmt), f) != sizeof(fmt)) {
                return ESP_FAIL;
            }
            if (fmt.audio_format != 1 || fmt.num_channels == 0 || fmt.num_channels > 2 ||
                fmt.sample_rate == 0 || fmt.block_align == 0 || fmt.bits_per_sample != 16) {
                return ESP_FAIL;
            }
            info->fmt = AUDIO_PLAYER_FMT_WAV;
            info->sample_rate = fmt.sample_rate;
            info->channels = (uint8_t)fmt.num_channels;
            info->bits_per_sample = (uint8_t)fmt.bits_per_sample;
            fmt_found = true;
        } else if (memcmp(chunk.id, "data", 4) == 0) {
            if (!fmt_found || chunk.size == 0) {
                return ESP_FAIL;
            }
            info->data_offset = (uint32_t)payload_offset;
            info->data_size = chunk.size;
            if (info->sample_rate && info->channels && info->bits_per_sample) {
                uint32_t bytes_per_second =
                    info->sample_rate * info->channels * ((uint32_t)info->bits_per_sample / 8U);
                if (bytes_per_second) {
                    info->duration_ms = (uint32_t)(((uint64_t)info->data_size * 1000ULL) / bytes_per_second);
                }
            }
            return ESP_OK;
        }
        long next = payload_offset + (long)chunk.size + (long)(chunk.size & 1U);
        if (fseek(f, next, SEEK_SET) != 0) {
            return ESP_FAIL;
        }
    }
    return ESP_FAIL;
}

esp_err_t audio_player_prepare_path(const char *path, audio_player_asset_info_t *out)
{
    audio_player_asset_info_t info = {0};
    audio_player_asset_info_t *slot = NULL;
    struct stat st = {0};
    FILE *f = NULL;
    uint8_t hdr[16] = {0};
    size_t n = 0;
    uint32_t started_ms = sd_storage_trace_now_ms();
    char detail[96] = {0};
    esp_err_t result = ESP_OK;

    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(path) >= sizeof(info.path)) {
        return ESP_ERR_INVALID_ARG;
    }

    audio_asset_copy(info.path, sizeof(info.path), path);
    info.prepared_at_ms = audio_asset_now_ms();

    if (stat(path, &st) != 0) {
        info.status = AUDIO_PLAYER_ASSET_MISSING;
    } else {
        info.size_bytes = (size_t)st.st_size;
        f = fopen(path, "rb");
        if (!f) {
            info.status = AUDIO_PLAYER_ASSET_IO_ERROR;
        } else {
            n = fread(hdr, 1, sizeof(hdr), f);
            info.fmt = audio_asset_detect_format(hdr, n);
            if (info.fmt == AUDIO_PLAYER_FMT_WAV) {
                info.status = audio_asset_parse_wav(f, &info) == ESP_OK ?
                    AUDIO_PLAYER_ASSET_READY : AUDIO_PLAYER_ASSET_BAD_HEADER;
            } else if (info.fmt == AUDIO_PLAYER_FMT_MP3) {
                info.status = AUDIO_PLAYER_ASSET_READY;
            } else if (info.fmt == AUDIO_PLAYER_FMT_OGG) {
                info.status = AUDIO_PLAYER_ASSET_UNSUPPORTED_FORMAT;
            } else {
                info.status = AUDIO_PLAYER_ASSET_BAD_HEADER;
            }
            fclose(f);
        }
    }

    slot = audio_asset_cache_slot(path);
    if (!slot) {
        result = ESP_ERR_NO_MEM;
        goto trace_and_return;
    }
    *slot = info;
    if (out) {
        *out = info;
    }
    s_audio_asset_generation++;
    result = ESP_OK;

trace_and_return:
    snprintf(detail,
             sizeof(detail),
             "status=%d fmt=%d size=%u result=%s",
             (int)info.status,
             (int)info.fmt,
             (unsigned)info.size_bytes,
             esp_err_to_name(result));
    sd_storage_trace_log("audio_asset", "prepare", path, sd_storage_trace_now_ms() - started_ms, detail);
    return result;
}

bool audio_player_asset_is_prepared(const char *path)
{
    return audio_asset_find(path) >= 0;
}

esp_err_t audio_player_asset_get(const char *path, audio_player_asset_info_t *out)
{
    int index = audio_asset_find(path);
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    *out = s_audio_asset_cache[index];
    return ESP_OK;
}

uint32_t audio_player_asset_generation(void)
{
    return s_audio_asset_generation;
}

void audio_player_asset_cache_clear(void)
{
    memset(s_audio_asset_cache, 0, sizeof(s_audio_asset_cache));
    s_audio_asset_cache_count = 0;
    s_audio_asset_cache_next = 0;
    s_audio_asset_generation++;
}
