/**
 * Audio Capture Module
 * Records audio from PDM microphone for voice recognition
 */

#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <Arduino.h>

// Initialize audio hardware (mic + DAC)
bool audio_init();

// Start recording audio (non-blocking, fills buffer in background)
bool audio_start_recording();

// Stop recording and return captured audio
// Returns pointer to audio buffer (caller must not free)
// Sets out_size to number of bytes captured
const uint8_t* audio_stop_recording(size_t* out_size);

// Check if currently recording
bool audio_is_recording();

// Get recording duration in milliseconds
uint32_t audio_get_recording_duration_ms();

// Play mono audio through DAC (converts to stereo internally)
bool audio_play(const uint8_t* data, size_t size, uint32_t sample_rate);

// Play stereo audio through DAC (direct playback, no conversion)
bool audio_play_stereo(const uint8_t* data, size_t size, uint32_t sample_rate);

// Check if currently playing
bool audio_is_playing();

// Stop playback
void audio_stop_playback();

// Get audio level (0-100) for visualization
uint8_t audio_get_level();

// Get last recorded audio buffer (for download)
const uint8_t* audio_get_last_recording(size_t* out_size);

#endif // AUDIO_CAPTURE_H
