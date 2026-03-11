/**
 * Notification System Implementation for ESP-IDF
 */

#include "notification.h"
#include "attention_sound.h"
#include "audio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "notification";

// Notification state
static char notification_text[NOTIFICATION_MAX_LEN] = {0};
static notify_type_t notify_type = NOTIFY_NONE;
static uint32_t last_sound_time = 0;
static const uint32_t SOUND_INTERVAL_MS = 3000;  // Play sound every 3 seconds

// Audio notification state
static uint8_t* notification_audio = NULL;
static size_t notification_audio_size = 0;
static uint32_t notification_audio_rate = 24000;

// Silent mode (no attention chime)
static bool notification_silent = false;

void notification_init(void)
{
    notification_text[0] = '\0';
    notify_type = NOTIFY_NONE;
    last_sound_time = 0;
    notification_audio = NULL;
    notification_audio_size = 0;
    notification_audio_rate = 24000;
    notification_silent = false;
    ESP_LOGI(TAG, "Notification system initialized");
}

bool notification_queue(const char* text)
{
    return notification_queue_ex(text, false);
}

bool notification_queue_ex(const char* text, bool silent)
{
    if (!text || strlen(text) == 0) {
        ESP_LOGW(TAG, "Empty text, ignoring");
        return false;
    }
    
    if (strlen(text) >= NOTIFICATION_MAX_LEN) {
        ESP_LOGW(TAG, "Text too long (%d chars)", strlen(text));
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
    notification_silent = silent;
    last_sound_time = 0;  // Play sound immediately (unless silent)
    
    ESP_LOGI(TAG, "Queued text notification (silent=%d): %.50s%s", 
             silent, notification_text, strlen(notification_text) > 50 ? "..." : "");
    return true;
}

bool notification_queue_audio(const uint8_t* audio_data, size_t audio_size, 
                              uint32_t sample_rate, const char* display_text)
{
    return notification_queue_audio_ex(audio_data, audio_size, sample_rate, display_text, false);
}

bool notification_queue_audio_ex(const uint8_t* audio_data, size_t audio_size, 
                                  uint32_t sample_rate, const char* display_text, bool silent)
{
    if (!audio_data || audio_size == 0) {
        ESP_LOGW(TAG, "No audio data");
        return false;
    }
    
    if (audio_size > NOTIFICATION_MAX_AUDIO_SIZE) {
        ESP_LOGW(TAG, "Audio too large (%u > %u)", audio_size, NOTIFICATION_MAX_AUDIO_SIZE);
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
        ESP_LOGE(TAG, "Failed to allocate audio buffer in PSRAM");
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
    notification_silent = silent;
    last_sound_time = 0;  // Play sound immediately (unless silent)
    
    ESP_LOGI(TAG, "Queued audio notification (%u bytes @ %u Hz, silent=%d)", 
             audio_size, sample_rate, silent);
    return true;
}

bool notification_is_silent(void)
{
    return notification_silent;
}

void notification_free_audio(void)
{
    if (notification_audio) {
        heap_caps_free(notification_audio);
        notification_audio = NULL;
        notification_audio_size = 0;
        ESP_LOGI(TAG, "Audio buffer freed");
    }
}

bool notification_pending(void)
{
    return notify_type != NOTIFY_NONE;
}

notify_type_t notification_get_type(void)
{
    return notify_type;
}

const char* notification_get_text(void)
{
    return notification_text;
}

const uint8_t* notification_get_audio(size_t* out_size, uint32_t* out_sample_rate)
{
    if (notify_type != NOTIFY_AUDIO || !notification_audio) {
        if (out_size) *out_size = 0;
        if (out_sample_rate) *out_sample_rate = 0;
        return NULL;
    }
    if (out_size) *out_size = notification_audio_size;
    if (out_sample_rate) *out_sample_rate = notification_audio_rate;
    return notification_audio;
}

const char* notification_acknowledge(void)
{
    if (notify_type == NOTIFY_NONE) {
        return NULL;
    }
    
    notify_type_t type = notify_type;
    notify_type = NOTIFY_NONE;
    
    if (type == NOTIFY_TEXT) {
        ESP_LOGI(TAG, "Acknowledged text notification");
        return notification_text;
    } else {
        ESP_LOGI(TAG, "Acknowledged audio notification (%u bytes)", notification_audio_size);
        // Don't free audio yet - caller will play it
        return NULL;
    }
}

void notification_cancel(void)
{
    notify_type = NOTIFY_NONE;
    notification_text[0] = '\0';
    if (notification_audio) {
        heap_caps_free(notification_audio);
        notification_audio = NULL;
        notification_audio_size = 0;
    }
    ESP_LOGI(TAG, "Notification cancelled");
}

void notification_play_attention(void)
{
    // Convert 16-bit signed mono to format expected by audio system
    // Attention sound is 9600 samples @ 24kHz = 400ms
    size_t pcm_bytes = ATTENTION_SOUND_SAMPLES * sizeof(int16_t);
    
    // Allocate temporary buffer for conversion (16-bit signed to raw bytes)
    uint8_t* buffer = (uint8_t*)heap_caps_malloc(pcm_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate attention sound buffer");
        return;
    }
    
    // Copy samples (already 16-bit signed, just copy as bytes)
    memcpy(buffer, attention_sound, pcm_bytes);
    
    // Play at 24kHz, 16-bit mono
    audio_play_pcm(buffer, pcm_bytes, ATTENTION_SOUND_RATE);
    
    heap_caps_free(buffer);
}

bool notification_update(void)
{
    if (notify_type == NOTIFY_NONE) {
        return false;
    }
    
    // Don't play sound if something else is playing
    if (audio_is_playing()) {
        return false;
    }
    
    // Skip sound if silent mode
    if (notification_silent) {
        return false;
    }
    
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Play attention sound periodically
    if (now - last_sound_time >= SOUND_INTERVAL_MS) {
        last_sound_time = now;
        notification_play_attention();
        return true;
    }
    
    return false;
}
