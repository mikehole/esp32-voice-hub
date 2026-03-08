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

// Volume control (0-100)
void audio_set_volume(int vol);
int audio_get_volume();

// Idle streaming for wake word detection
// Callback receives 80ms chunks (1280 samples = 2560 bytes at 16kHz)
typedef void (*IdleAudioCallback)(const uint8_t* data, size_t length);
void audio_set_idle_callback(IdleAudioCallback cb);
void audio_start_idle_stream();
void audio_stop_idle_stream();
bool audio_is_idle_streaming();

// Play a short acknowledgment beep (100ms, 880Hz)
void audio_play_ack_beep();

#endif // AUDIO_CAPTURE_H
