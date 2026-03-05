/**
 * Notification System for ESP32 Voice Hub
 * Handles push notifications from external sources (e.g., OpenClaw)
 * 
 * Two modes:
 * 1. POST /api/notify with text → device does TTS on acknowledge
 * 2. POST /api/notify-audio with audio → device plays pre-loaded audio on acknowledge
 * 
 * Flow:
 * 1. POST notification (text or audio)
 * 2. Device shows notification avatar, plays attention sound
 * 3. User taps center to acknowledge
 * 4. Device speaks/plays the announcement
 * 5. Returns to idle state
 */

#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <Arduino.h>

// Maximum length of notification text
#define NOTIFICATION_MAX_LEN 1024

// Maximum audio size (512KB in PSRAM)
#define NOTIFICATION_MAX_AUDIO_SIZE (512 * 1024)

// Notification type
typedef enum {
    NOTIFY_NONE,
    NOTIFY_TEXT,    // Text to be spoken via TTS
    NOTIFY_AUDIO    // Pre-loaded audio to play directly
} NotifyType;

// Initialize notification system
void notification_init();

// Queue a text notification (device will do TTS)
// Returns true if queued successfully
bool notification_queue(const char* text);

// Queue an audio notification (pre-loaded audio to play)
// Audio is copied to internal buffer. Sample rate for playback.
// Returns true if queued successfully
bool notification_queue_audio(const uint8_t* audio_data, size_t audio_size, 
                              uint32_t sample_rate, const char* display_text);

// Check if a notification is pending
bool notification_pending();

// Get the notification type
NotifyType notification_get_type();

// Get the pending notification text (for display or TTS)
const char* notification_get_text();

// Get the pending notification audio (for NOTIFY_AUDIO type)
// Returns NULL if no audio notification pending
const uint8_t* notification_get_audio(size_t* out_size, uint32_t* out_sample_rate);

// Acknowledge and clear the notification (called when user taps)
// For NOTIFY_TEXT: returns the text (caller should speak it)
// For NOTIFY_AUDIO: returns NULL (caller should use notification_get_audio first)
const char* notification_acknowledge();

// Cancel current notification without playing
void notification_cancel();

// Play attention sound once
void notification_play_attention();

// Update notification state (call in main loop)
// Handles periodic sound playback
void notification_update();

#endif // NOTIFICATION_H
