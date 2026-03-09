/**
 * Audio Module - I2S Microphone + DAC
 * Stub implementation - will add full I2S support
 */

#include "audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio";

static bool playing = false;
static bool recording = false;

esp_err_t audio_init(void)
{
    // TODO: Initialize I2S for PDM microphone
    // TODO: Initialize I2S for DAC output
    ESP_LOGI(TAG, "Audio init (stub)");
    return ESP_OK;
}

esp_err_t audio_play(uint8_t* data, size_t len, uint32_t sample_rate)
{
    ESP_LOGI(TAG, "Playing %d bytes at %lu Hz", len, sample_rate);
    playing = true;
    
    // TODO: Actually play audio via I2S
    // For now, just simulate playback time
    vTaskDelay(pdMS_TO_TICKS(len / (sample_rate * 2 / 1000)));
    
    playing = false;
    free(data);
    return ESP_OK;
}

bool audio_is_playing(void)
{
    return playing;
}

void audio_stop(void)
{
    playing = false;
}

esp_err_t audio_start_recording(void)
{
    ESP_LOGI(TAG, "Start recording (stub)");
    recording = true;
    return ESP_OK;
}

const uint8_t* audio_stop_recording(size_t* out_len)
{
    ESP_LOGI(TAG, "Stop recording (stub)");
    recording = false;
    *out_len = 0;
    return NULL;
}

bool audio_is_recording(void)
{
    return recording;
}

uint8_t audio_get_level(void)
{
    return 0;
}

void audio_set_stream_callback(audio_stream_callback_t cb)
{
    // TODO: Store callback
}

void audio_start_streaming(void)
{
    ESP_LOGI(TAG, "Start streaming (stub)");
}

void audio_stop_streaming(void)
{
    ESP_LOGI(TAG, "Stop streaming (stub)");
}
