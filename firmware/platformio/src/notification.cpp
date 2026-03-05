/**
 * Notification System Implementation
 */

#include "notification.h"
#include "attention_sound.h"
#include "audio_capture.h"
#include "esp_heap_caps.h"
#include <Arduino.h>

// Notification state
static char notification_text[NOTIFICATION_MAX_LEN] = {0};
static NotifyType notify_type = NOTIFY_NONE;
static uint32_t last_sound_time = 0;
static const uint32_t SOUND_INTERVAL_MS = 3000;  // Play sound every 3 seconds

// Audio notification state
static uint8_t* notification_audio = NULL;
static size_t notification_audio_size = 0;
static uint32_t notification_audio_rate = 24000;

void notification_init() {
    notification_text[0] = '\0';
    notify_type = NOTIFY_NONE;
    last_sound_time = 0;
    notification_audio = NULL;
    notification_audio_size = 0;
    notification_audio_rate = 24000;
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
    
    // Clear any existing audio notification
    if (notification_audio) {
        heap_caps_free(notification_audio);
        notification_audio = NULL;
        notification_audio_size = 0;
    }
    
    // Store notification text
    strncpy(notification_text, text, NOTIFICATION_MAX_LEN - 1);
    notification_text[NOTIFICATION_MAX_LEN - 1] = '\0';
    notify_type = NOTIFY_TEXT;
    last_sound_time = 0;  // Play sound immediately
    
    Serial.printf("Notification: queued text '%s'\n", notification_text);
    return true;
}

bool notification_queue_audio(const uint8_t* audio_data, size_t audio_size, 
                              uint32_t sample_rate, const char* display_text) {
    if (!audio_data || audio_size == 0) {
        Serial.println("Notification: no audio data");
        return false;
    }
    
    if (audio_size > NOTIFICATION_MAX_AUDIO_SIZE) {
        Serial.printf("Notification: audio too large (%u > %u)\n", 
                      audio_size, NOTIFICATION_MAX_AUDIO_SIZE);
        return false;
    }
    
    // Clear any existing audio
    if (notification_audio) {
        heap_caps_free(notification_audio);
        notification_audio = NULL;
    }
    
    // Allocate buffer in PSRAM
    notification_audio = (uint8_t*)heap_caps_malloc(audio_size, MALLOC_CAP_SPIRAM);
    if (!notification_audio) {
        Serial.println("Notification: failed to allocate audio buffer");
        return false;
    }
    
    // Copy audio data
    memcpy(notification_audio, audio_data, audio_size);
    notification_audio_size = audio_size;
    notification_audio_rate = sample_rate;
    
    // Store display text (optional)
    if (display_text && strlen(display_text) < NOTIFICATION_MAX_LEN) {
        strncpy(notification_text, display_text, NOTIFICATION_MAX_LEN - 1);
        notification_text[NOTIFICATION_MAX_LEN - 1] = '\0';
    } else {
        notification_text[0] = '\0';
    }
    
    notify_type = NOTIFY_AUDIO;
    last_sound_time = 0;  // Play sound immediately
    
    Serial.printf("Notification: queued audio (%u bytes @ %u Hz)\n", 
                  audio_size, sample_rate);
    return true;
}

bool notification_pending() {
    return notify_type != NOTIFY_NONE;
}

NotifyType notification_get_type() {
    return notify_type;
}

const char* notification_get_text() {
    return notification_text;
}

const uint8_t* notification_get_audio(size_t* out_size, uint32_t* out_sample_rate) {
    if (notify_type != NOTIFY_AUDIO || !notification_audio) {
        if (out_size) *out_size = 0;
        if (out_sample_rate) *out_sample_rate = 0;
        return NULL;
    }
    if (out_size) *out_size = notification_audio_size;
    if (out_sample_rate) *out_sample_rate = notification_audio_rate;
    return notification_audio;
}

const char* notification_acknowledge() {
    if (notify_type == NOTIFY_NONE) {
        return NULL;
    }
    
    NotifyType type = notify_type;
    notify_type = NOTIFY_NONE;
    
    if (type == NOTIFY_TEXT) {
        Serial.printf("Notification: acknowledged text '%s'\n", notification_text);
        return notification_text;
    } else {
        Serial.printf("Notification: acknowledged audio (%u bytes)\n", notification_audio_size);
        // Don't free audio yet - caller will play it
        return NULL;
    }
}

void notification_cancel() {
    notify_type = NOTIFY_NONE;
    notification_text[0] = '\0';
    if (notification_audio) {
        heap_caps_free(notification_audio);
        notification_audio = NULL;
        notification_audio_size = 0;
    }
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
    if (notify_type == NOTIFY_NONE) {
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
