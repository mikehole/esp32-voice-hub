/**
 * Audio Module Header
 * I2S PDM Microphone + PCM5100A DAC
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Initialize audio hardware (mic + DAC)
void audio_init(void);

// Recording functions
bool audio_start_recording(size_t max_bytes);
size_t audio_record_chunk(uint8_t *buffer, size_t max_size);
void audio_stop_recording(void);
bool audio_is_recording(void);

// Playback functions
// data: raw PCM audio (16-bit mono)
// sample_rate: e.g., 24000 for OpenAI TTS
// take_ownership: if true, audio module will free the buffer when done
bool audio_play(const uint8_t *data, size_t size, uint32_t sample_rate, bool take_ownership);
void audio_stop_playback(void);
bool audio_is_playing(void);

// Streaming playback functions (for real-time audio)
bool audio_start_streaming_playback(uint32_t sample_rate);
void audio_write_streaming(const uint8_t *data, size_t size);
void audio_stop_streaming_playback(void);

// Volume control (0-100)
void audio_set_volume(int volume);
int audio_get_volume(void);

// Convenience wrapper that copies data (doesn't take ownership)
// For short sounds like attention chimes
void audio_play_pcm(const uint8_t *data, size_t size, uint32_t sample_rate);
