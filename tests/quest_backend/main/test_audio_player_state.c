#include <string.h>

#include "unity.h"

#include "audio_player.h"
#include "audio_player_internal.h"

static audio_player_status_t s_audio_status;

static void audio_state_get(void)
{
    memset(&s_audio_status, 0, sizeof(s_audio_status));
    audio_player_status_get(&s_audio_status);
}

static void test_audio_player_volume_clamps_and_updates_status(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, audio_player_status_init());
    audio_player_status_reset(70);
    TEST_ASSERT_EQUAL(ESP_OK, audio_player_set_volume(1));

    TEST_ASSERT_EQUAL(ESP_OK, audio_player_set_volume(-10));
    TEST_ASSERT_EQUAL(0, audio_player_get_volume());
    audio_state_get();
    TEST_ASSERT_EQUAL(0, s_audio_status.volume);

    TEST_ASSERT_EQUAL(ESP_OK, audio_player_set_volume(140));
    TEST_ASSERT_EQUAL(100, audio_player_get_volume());
    audio_state_get();
    TEST_ASSERT_EQUAL(100, s_audio_status.volume);

    TEST_ASSERT_EQUAL(ESP_OK, audio_player_set_volume(55));
    TEST_ASSERT_EQUAL(55, audio_player_get_volume());
    audio_state_get();
    TEST_ASSERT_EQUAL(55, s_audio_status.volume);
}

static void test_audio_player_status_runtime_state_transitions(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, audio_player_status_init());
    audio_player_status_reset(33);

    audio_player_status_set_play("/sdcard/effect.wav", AUDIO_FMT_WAV, 33);
    audio_state_get();
    TEST_ASSERT_TRUE(s_audio_status.playing);
    TEST_ASSERT_FALSE(s_audio_status.paused);
    TEST_ASSERT_EQUAL(AUDIO_PLAYER_FMT_WAV, s_audio_status.fmt);
    TEST_ASSERT_EQUAL_STRING("/sdcard/effect.wav", s_audio_status.path);

    audio_player_status_set_runtime_state(AUDIO_RUNTIME_PAUSED);
    audio_state_get();
    TEST_ASSERT_TRUE(s_audio_status.playing);
    TEST_ASSERT_TRUE(s_audio_status.paused);

    audio_player_status_set_runtime_state(AUDIO_RUNTIME_PLAYING);
    audio_state_get();
    TEST_ASSERT_TRUE(s_audio_status.playing);
    TEST_ASSERT_FALSE(s_audio_status.paused);

    audio_player_status_set_runtime_state(AUDIO_RUNTIME_IDLE);
    audio_state_get();
    TEST_ASSERT_FALSE(s_audio_status.playing);
    TEST_ASSERT_FALSE(s_audio_status.paused);
    TEST_ASSERT_EQUAL(AUDIO_PLAYER_FMT_UNKNOWN, s_audio_status.fmt);
    TEST_ASSERT_EQUAL_STRING("", s_audio_status.path);
}

static void test_audio_player_status_progress_bitrate_and_message_are_stable(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, audio_player_status_init());
    audio_player_status_reset(70);

    audio_player_status_set_play("/sdcard/music.mp3", AUDIO_FMT_MP3, 70);
    audio_player_status_update_progress(250, 1000, 1200, 4800);
    audio_player_status_update_bitrate(192);
    audio_player_status_set_message("buffering");
    audio_state_get();
    TEST_ASSERT_EQUAL(25, s_audio_status.progress);
    TEST_ASSERT_EQUAL(1200, s_audio_status.pos_ms);
    TEST_ASSERT_EQUAL(4800, s_audio_status.dur_ms);
    TEST_ASSERT_EQUAL(192, s_audio_status.bitrate_kbps);
    TEST_ASSERT_EQUAL_STRING("buffering", s_audio_status.message);

    audio_player_status_update_progress(1500, 1000, 0, 0);
    audio_state_get();
    TEST_ASSERT_EQUAL(100, s_audio_status.progress);
    TEST_ASSERT_EQUAL(1200, s_audio_status.pos_ms);
    TEST_ASSERT_EQUAL(4800, s_audio_status.dur_ms);
}

static void test_audio_player_seek_state_requires_active_path(void)
{
    char path[32] = {0};
    int dur_ms = -1;

    TEST_ASSERT_EQUAL(ESP_OK, audio_player_status_init());
    audio_player_status_reset(70);
    TEST_ASSERT_FALSE(audio_player_status_prepare_seek(path, sizeof(path), &dur_ms));

    audio_player_status_set_play("/sdcard/long.wav", AUDIO_FMT_WAV, 70);
    audio_player_status_update_progress(100, 1000, 500, 5000);
    TEST_ASSERT_TRUE(audio_player_status_prepare_seek(path, sizeof(path), &dur_ms));
    TEST_ASSERT_EQUAL_STRING("/sdcard/long.wav", path);
    TEST_ASSERT_EQUAL(5000, dur_ms);

    audio_player_status_set_seek_position(2500, 50);
    audio_state_get();
    TEST_ASSERT_EQUAL(2500, s_audio_status.pos_ms);
    TEST_ASSERT_EQUAL(50, s_audio_status.progress);
}

static void test_audio_player_format_mapping_handles_known_and_unknown_formats(void)
{
    TEST_ASSERT_EQUAL(AUDIO_PLAYER_FMT_UNKNOWN, audio_player_to_public_format(AUDIO_FMT_UNKNOWN));
    TEST_ASSERT_EQUAL(AUDIO_PLAYER_FMT_WAV, audio_player_to_public_format(AUDIO_FMT_WAV));
    TEST_ASSERT_EQUAL(AUDIO_PLAYER_FMT_MP3, audio_player_to_public_format(AUDIO_FMT_MP3));
    TEST_ASSERT_EQUAL(AUDIO_PLAYER_FMT_OGG, audio_player_to_public_format(AUDIO_FMT_OGG));
}

void register_audio_player_state_tests(void)
{
    RUN_TEST(test_audio_player_volume_clamps_and_updates_status);
    RUN_TEST(test_audio_player_status_runtime_state_transitions);
    RUN_TEST(test_audio_player_status_progress_bitrate_and_message_are_stable);
    RUN_TEST(test_audio_player_seek_state_requires_active_path);
    RUN_TEST(test_audio_player_format_mapping_handles_known_and_unknown_formats);
}
