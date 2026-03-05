/**
 * Notification System for ESP32 Voice Hub
 * Handles push notifications from external sources (e.g., OpenClaw)
 * 
 * Flow:
 * 1. POST /api/notify with text body
 * 2. Device shows notification avatar, plays attention sound
 * 3. User taps center to acknowledge
 * 4. Device speaks the announcement via TTS
 * 5. Returns to idle state
 */

#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <Arduino.h>

// Maximum length of notification text
#define NOTIFICATION_MAX_LEN 1024

// Initialize notification system
void notification_init();

// Queue a new notification (called from web endpoint)
// Returns true if queued successfully
bool notification_queue(const char* text);

// Check if a notification is pending
bool notification_pending();

// Get the pending notification text
const char* notification_get_text();

// Acknowledge and clear the notification (called when user taps)
// Returns the notification text (caller should speak it)
const char* notification_acknowledge();

// Cancel current notification without speaking
void notification_cancel();

// Play attention sound once
void notification_play_attention();

// Update notification state (call in main loop)
// Handles periodic sound playback
void notification_update();

#endif // NOTIFICATION_H
