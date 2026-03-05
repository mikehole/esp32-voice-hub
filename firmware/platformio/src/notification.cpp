/**
 * Notification System Implementation
 */

#include "notification.h"
#include "attention_sound.h"
#include "audio_capture.h"
#include <Arduino.h>

// Notification state
static char notification_text[NOTIFICATION_MAX_LEN] = {0};
static bool has_notification = false;
static uint32_t last_sound_time = 0;
static const uint32_t SOUND_INTERVAL_MS = 3000;  // Play sound every 3 seconds

void notification_init() {
    notification_text[0] = '\0';
    has_notification = false;
    last_sound_time = 0;
    Serial.println("Notification: initialized");
}

bool notification_queue(const char* text) {
    if (!text || strlen(text) == 0) {
        Serial.println("Notification: empty text, ignoring");
        return false;
    }
    
    if (strlen(text) >= NOTIFICATION_MAX_LEN) {
        Serial.println("Notification: text too long");
        return false;
    }
    
    // Store notification text
    strncpy(notification_text, text, NOTIFICATION_MAX_LEN - 1);
    notification_text[NOTIFICATION_MAX_LEN - 1] = '\0';
    has_notification = true;
    last_sound_time = 0;  // Play sound immediately
    
    Serial.printf("Notification: queued '%s'\n", notification_text);
    return true;
}

bool notification_pending() {
    return has_notification;
}

const char* notification_get_text() {
    return notification_text;
}

const char* notification_acknowledge() {
    if (!has_notification) {
        return NULL;
    }
    
    has_notification = false;
    Serial.printf("Notification: acknowledged '%s'\n", notification_text);
    return notification_text;
}

void notification_cancel() {
    has_notification = false;
    notification_text[0] = '\0';
    Serial.println("Notification: cancelled");
}

void notification_play_attention() {
    // Convert 16-bit signed mono to 16-bit signed mono PCM buffer
    // The attention_sound is already in the right format, just need to cast
    size_t pcm_size = attention_sound_size;
    
    // Play as mono audio at 24kHz
    // Note: audio_play expects unsigned 8-bit or needs adjustment
    // For 16-bit signed, we need to convert or use raw playback
    
    // Quick solution: convert to unsigned 8-bit on the fly
    // Better solution would be to add 16-bit playback support
    uint8_t* buffer = (uint8_t*)heap_caps_malloc(pcm_size / 2, MALLOC_CAP_SPIRAM);
    if (!buffer) {
        Serial.println("Notification: failed to allocate sound buffer");
        return;
    }
    
    // Convert 16-bit signed to 8-bit unsigned
    for (size_t i = 0; i < pcm_size / 2; i++) {
        int16_t sample = attention_sound[i];
        buffer[i] = (uint8_t)((sample + 32768) >> 8);
    }
    
    // Play at 24kHz
    audio_play(buffer, pcm_size / 2, 24000);
    
    heap_caps_free(buffer);
}

void notification_update() {
    if (!has_notification) {
        return;
    }
    
    // Don't play sound if something else is playing
    if (audio_is_playing()) {
        return;
    }
    
    uint32_t now = millis();
    
    // Play attention sound periodically
    if (now - last_sound_time >= SOUND_INTERVAL_MS) {
        last_sound_time = now;
        notification_play_attention();
    }
}
