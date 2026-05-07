#include "audio_player_internal.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "error_monitor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// I2S pins for PCM5102A. Configurable via menuconfig defaults.
#define I2S_BCK_PIN  CONFIG_SCENEHUB_I2S_BCK_PIN
#define I2S_WS_PIN   CONFIG_SCENEHUB_I2S_WS_PIN
#define I2S_DATA_PIN CONFIG_SCENEHUB_I2S_DATA_PIN

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BITS        I2S_DATA_BIT_WIDTH_16BIT
#define AUDIO_CHANNELS    2
#define AUDIO_FRAME_BYTES (AUDIO_CHANNELS * sizeof(int16_t))
#define AUDIO_SILENCE_DRAIN_MS 50
#define AUDIO_SILENCE_FRAMES 256

static const char *TAG = "audio_player";

static i2s_chan_handle_t s_tx_chan = NULL;
static bool s_tx_enabled = false;
static SemaphoreHandle_t s_output_lock = NULL;
static SemaphoreHandle_t s_tone_lock = NULL;
static portMUX_TYPE s_tone_lock_init_lock = portMUX_INITIALIZER_UNLOCKED;
static DMA_ATTR int16_t s_silence_buf[AUDIO_SILENCE_FRAMES * AUDIO_CHANNELS];
static DMA_ATTR int16_t s_tone_buf[256 * AUDIO_CHANNELS];

static esp_err_t output_ensure_lock(void)
{
    if (!s_output_lock) {
        s_output_lock = xSemaphoreCreateMutex();
    }
    return s_output_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

static bool output_lock(void)
{
    return s_output_lock && xSemaphoreTake(s_output_lock, portMAX_DELAY) == pdTRUE;
}

static void output_unlock(void)
{
    if (s_output_lock) {
        xSemaphoreGive(s_output_lock);
    }
}

static esp_err_t tone_ensure_lock(void)
{
    SemaphoreHandle_t mutex = NULL;
    if (s_tone_lock) {
        return ESP_OK;
    }
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&s_tone_lock_init_lock);
    if (!s_tone_lock) {
        s_tone_lock = mutex;
        mutex = NULL;
    }
    portEXIT_CRITICAL(&s_tone_lock_init_lock);
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
    return s_tone_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t output_enable_locked(void)
{
    if (!s_tx_chan) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_tx_enabled) {
        return ESP_OK;
    }
    esp_err_t err = i2s_channel_enable(s_tx_chan);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_tx_enabled = true;
        return ESP_OK;
    }
    return err;
}

static void output_disable_locked(void)
{
    if (!s_tx_chan || !s_tx_enabled) {
        return;
    }
    esp_err_t err = i2s_channel_disable(s_tx_chan);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_tx_enabled = false;
    } else {
        ESP_LOGW(TAG, "i2s disable failed: %s", esp_err_to_name(err));
    }
}

void audio_player_output_drain_silence(int duration_ms)
{
    int remaining_frames = (AUDIO_SAMPLE_RATE * duration_ms) / 1000;

    if (!s_tx_chan || duration_ms <= 0) {
        return;
    }
    if (audio_player_output_enable() != ESP_OK) {
        return;
    }

    memset(s_silence_buf, 0, sizeof(s_silence_buf));
    while (remaining_frames > 0) {
        int frames = remaining_frames > AUDIO_SILENCE_FRAMES ? AUDIO_SILENCE_FRAMES : remaining_frames;
        size_t bytes_written = 0;
        esp_err_t err = audio_player_output_write(s_silence_buf,
                                                  frames * AUDIO_CHANNELS * sizeof(int16_t),
                                                  &bytes_written,
                                                  pdMS_TO_TICKS(100));
        if (err != ESP_OK || bytes_written == 0) {
            break;
        }
        remaining_frames -= frames;
    }
}

static esp_err_t audio_player_output_setup(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL), TAG, "i2s new channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(AUDIO_BITS, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_DATA_PIN,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg), TAG, "i2s std init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "i2s enable failed");
    s_tx_enabled = true;
    return ESP_OK;
}

esp_err_t audio_player_output_init(void)
{
    ESP_RETURN_ON_ERROR(output_ensure_lock(), TAG, "output lock init failed");
    if (s_tx_chan) {
        return ESP_OK;
    }
    return audio_player_output_setup();
}

esp_err_t audio_player_output_enable(void)
{
    esp_err_t err = output_ensure_lock();
    if (err != ESP_OK) {
        return err;
    }
    if (!output_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    err = output_enable_locked();
    output_unlock();
    return err;
}

void audio_player_output_disable(void)
{
    if (output_ensure_lock() != ESP_OK || !output_lock()) {
        return;
    }
    output_disable_locked();
    output_unlock();
}

void audio_player_output_pause(void)
{
    audio_player_output_disable();
}

void audio_player_output_resume(void)
{
    esp_err_t err = audio_player_output_enable();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s resume enable failed: %s", esp_err_to_name(err));
    }
}

void audio_player_output_reset(void)
{
    audio_player_output_drain_silence(AUDIO_SILENCE_DRAIN_MS);
    if (output_ensure_lock() != ESP_OK || !output_lock()) {
        return;
    }
    if (s_tx_chan) {
        output_disable_locked();
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
    }
    s_tx_enabled = false;
    esp_err_t err = audio_player_output_setup();
    output_unlock();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s reset failed: %s", esp_err_to_name(err));
        error_monitor_report_audio_fault();
    }
}

void audio_player_output_play_tone(int freq_hz, int duration_ms, int volume_percent)
{
    if (!s_tx_chan) {
        return;
    }

    const int total_samples = (AUDIO_SAMPLE_RATE * duration_ms) / 1000;
    const int buf_samples = sizeof(s_tone_buf) / (AUDIO_CHANNELS * sizeof(s_tone_buf[0]));
    if (tone_ensure_lock() != ESP_OK ||
        xSemaphoreTake(s_tone_lock, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "tone buffer lock failed");
        error_monitor_report_audio_fault();
        return;
    }

    float phase = 0.0f;
    const float step = 2.0f * (float)M_PI * (float)freq_hz / (float)AUDIO_SAMPLE_RATE;
    const int32_t amplitude = (int32_t)(32767 * (volume_percent / 100.0f));

    int produced = 0;
    while (produced < total_samples) {
        int chunk = buf_samples;
        if (produced + chunk > total_samples) {
            chunk = total_samples - produced;
        }
        for (int i = 0; i < chunk; ++i) {
            int16_t sample = (int16_t)(sinf(phase) * amplitude);
            phase += step;
            if (phase > 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
            s_tone_buf[2 * i] = sample;
            s_tone_buf[2 * i + 1] = sample;
        }
        size_t bytes_written = 0;
        esp_err_t err = audio_player_output_write(s_tone_buf,
                                                  chunk * AUDIO_CHANNELS * sizeof(int16_t),
                                                  &bytes_written,
                                                  pdMS_TO_TICKS(1000));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s write err %s", esp_err_to_name(err));
            break;
        }
        produced += chunk;
    }

    xSemaphoreGive(s_tone_lock);
}

esp_err_t audio_player_output_write(const void *data, size_t len, size_t *bytes_written, TickType_t timeout)
{
    esp_err_t err = ESP_OK;
    size_t total_written = 0;

    if (bytes_written) {
        *bytes_written = 0;
    }

    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // ВАЖНО: I2S stereo 16-bit должен получать только целые фреймы.
    len -= len % AUDIO_FRAME_BYTES;
    if (len == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    err = output_ensure_lock();
    if (err != ESP_OK) {
        return err;
    }

    if (!output_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    err = output_enable_locked();

    const uint8_t *src = (const uint8_t *)data;

    while (err == ESP_OK && total_written < len) {
        size_t written_now = 0;
        size_t remaining = len - total_written;

        // Пишем только frame-aligned размер.
        remaining -= remaining % AUDIO_FRAME_BYTES;
        if (remaining == 0) {
            break;
        }

        err = i2s_channel_write(
            s_tx_chan,
            src + total_written,
            remaining,
            &written_now,
            timeout
        );

        if (written_now > 0) {
            // Самая важная защита.
            // Если драйвер вернул запись не кратную 4 байтам,
            // значит аудиопоток может съехать по границам сэмплов.
            if ((written_now % AUDIO_FRAME_BYTES) != 0) {
                ESP_LOGE(TAG,
                         "unaligned i2s write: %u bytes, frame=%u. Resetting output.",
                         (unsigned)written_now,
                         (unsigned)AUDIO_FRAME_BYTES);

                output_disable_locked();

                if (s_tx_chan) {
                    i2s_del_channel(s_tx_chan);
                    s_tx_chan = NULL;
                }

                s_tx_enabled = false;
                esp_err_t reset_err = audio_player_output_setup();
                if (reset_err != ESP_OK) {
                    ESP_LOGE(TAG, "i2s recover failed: %s", esp_err_to_name(reset_err));
                    error_monitor_report_audio_fault();
                }

                err = ESP_FAIL;
                break;
            }

            total_written += written_now;
        }

        if (err != ESP_OK) {
            break;
        }

        if (written_now == 0) {
            err = ESP_ERR_TIMEOUT;
            break;
        }
    }

    output_unlock();

    if (bytes_written) {
        *bytes_written = total_written;
    }

    return err;
}

i2s_chan_handle_t audio_player_output_channel(void)
{
    return s_tx_chan;
}
