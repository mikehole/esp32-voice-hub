#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// Initialize audio subsystem (I2S mic + DAC)
esp_err_t audio_init(void);

// Play PCM audio (takes ownership of buffer, will free when done)
esp_err_t audio_play(uint8_t* data, size_t len, uint32_t sample_rate);

// Check if currently playing
bool audio_is_playing(void);

// Stop playback
void audio_stop(void);

// Start recording for voice command
esp_err_t audio_start_recording(void);

// Stop recording and get buffer
const uint8_t* audio_stop_recording(size_t* out_len);

// Check if recording
bool audio_is_recording(void);

// Get current audio level (0-100)
uint8_t audio_get_level(void);

// Callback for streaming audio (for wake word / voice streaming)
typedef void (*audio_stream_callback_t)(const uint8_t* data, size_t len);
void audio_set_stream_callback(audio_stream_callback_t cb);
void audio_start_streaming(void);
void audio_stop_streaming(void);
