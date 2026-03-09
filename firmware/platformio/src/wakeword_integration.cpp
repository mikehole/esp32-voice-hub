/**
 * Wake Word Integration
 * Using ESP-IDF WebSocket client
 */

#include "wakeword_integration.h"
#include "ws_client.h"
#include "audio_capture.h"
#include "avatar.h"
#include "status_ring.h"
#include "openai_client.h"
#include "esp_heap_caps.h"
#include <ArduinoJson.h>
#include <cstring>

static const char* TAG = "WakeWord";

// State
static bool enabled = true;
static bool initialized = false;

// TTS buffering
static uint8_t* tts_buffer = nullptr;
static size_t tts_buffer_size = 0;
static size_t tts_buffer_pos = 0;
static uint32_t tts_sample_rate = 16000;

// Forward declarations
static void on_ws_message(WsMessageType type, const uint8_t* data, size_t length);
static void on_ws_event(WsEventType event);
static void on_idle_audio(const uint8_t* data, size_t length);
static void handle_json_message(const char* json);

void wakeword_integration_init() {
    if (initialized) return;
    
    Serial.println("WakeWord: Initializing integration...");
    
    // Configure connection from stored settings
    ws_set_host(wakeword_get_host());
    ws_set_port(wakeword_get_port());
    
    // Set up callbacks
    ws_set_message_callback(on_ws_message);
    ws_set_event_callback(on_ws_event);
    
    // Set up idle audio callback (sends to WebSocket)
    audio_set_idle_callback(on_idle_audio);
    
    // Connect to server
    if (!ws_init()) {
        Serial.println("WakeWord: Failed to connect to server");
        return;
    }
    
    initialized = true;
    Serial.println("WakeWord: Integration initialized");
}

// Reconnect state
static unsigned long last_reconnect_attempt = 0;
static const unsigned long RECONNECT_INTERVAL = 5000;

void wakeword_integration_loop() {
    if (!initialized) return;
    
    // Run WebSocket loop
    ws_loop();
    
    // Auto-reconnect if disconnected
    if (enabled && !ws_is_connected()) {
        unsigned long now = millis();
        if (now - last_reconnect_attempt > RECONNECT_INTERVAL) {
            last_reconnect_attempt = now;
            Serial.println("WakeWord: Attempting to reconnect...");
            if (ws_init()) {
                Serial.println("WakeWord: Reconnected!");
            }
        }
    }
}

bool wakeword_integration_is_active() {
    return initialized && enabled && ws_is_connected();
}

void wakeword_start_recording() {
    if (!initialized) return;
    
    // Stop idle streaming, start recording
    audio_stop_idle_stream();
    
    if (audio_start_recording()) {
        avatar_set_state(STATE_RECORDING);
        status_ring_show(STATE_RECORDING);
    }
}

void wakeword_stop_recording() {
    if (!initialized || !audio_is_recording()) return;
    
    size_t audio_size = 0;
    const uint8_t* audio_data = audio_stop_recording(&audio_size);
    
    if (audio_size < 3200) {
        Serial.println("WakeWord: Recording too short");
        // Resume idle streaming
        audio_start_idle_stream();
        status_ring_hide();
        avatar_set_state(STATE_IDLE);
        return;
    }
    
    // Show thinking state
    avatar_set_state(STATE_THINKING);
    status_ring_show(STATE_THINKING);
    
    // Send audio_start message
    ws_send_audio_start(16000);
    
    // Send audio data in chunks
    const size_t CHUNK_SIZE = 1024;
    for (size_t i = 0; i < audio_size; i += CHUNK_SIZE) {
        size_t len = (i + CHUNK_SIZE < audio_size) ? CHUNK_SIZE : (audio_size - i);
        ws_send_binary(audio_data + i, len);
    }
    
    // Send audio_end message
    ws_send_audio_end();
    
    Serial.printf("WakeWord: Sent %u bytes to server\n", audio_size);
}

void wakeword_set_enabled(bool en) {
    enabled = en;
    if (en && initialized && ws_is_connected()) {
        // Resume idle streaming
        audio_start_idle_stream();
    } else {
        audio_stop_idle_stream();
    }
}

bool wakeword_is_enabled() {
    return enabled;
}

// ============================================================================
// WebSocket Callbacks
// ============================================================================

static void on_ws_message(WsMessageType type, const uint8_t* data, size_t length) {
    if (type == WS_MSG_TEXT) {
        // JSON message
        char* json = (char*)malloc(length + 1);
        if (json) {
            memcpy(json, data, length);
            json[length] = '\0';
            handle_json_message(json);
            free(json);
        }
    } else if (type == WS_MSG_BINARY) {
        // TTS audio chunk
        if (tts_buffer && tts_buffer_pos + length <= tts_buffer_size) {
            memcpy(tts_buffer + tts_buffer_pos, data, length);
            tts_buffer_pos += length;
        }
    }
}

static void on_ws_event(WsEventType event) {
    switch (event) {
        case WS_EVENT_CONNECTED:
            Serial.println("WakeWord: Connected to server");
            if (enabled) {
                audio_start_idle_stream();
            }
            break;
            
        case WS_EVENT_DISCONNECTED:
            Serial.println("WakeWord: Disconnected from server");
            audio_stop_idle_stream();
            // Reset to idle state
            status_ring_hide();
            avatar_set_state(STATE_IDLE);
            // Try to reconnect after a delay
            Serial.println("WakeWord: Will reconnect in 5 seconds...");
            break;
            
        case WS_EVENT_ERROR:
            Serial.println("WakeWord: WebSocket error");
            audio_stop_idle_stream();
            status_ring_hide();
            avatar_set_state(STATE_IDLE);
            break;
    }
}

static void handle_json_message(const char* json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("WakeWord: JSON parse error: %s\n", err.c_str());
        return;
    }
    
    const char* type = doc["type"];
    if (!type) return;
    
    if (strcmp(type, "wake_detected") == 0) {
        Serial.println("WakeWord: Wake detected - starting recording");
        audio_play_ack_beep();
        wakeword_start_recording();
        
    } else if (strcmp(type, "transcript") == 0) {
        const char* text = doc["text"];
        if (text) {
            Serial.printf("WakeWord: Transcript: %s\n", text);
        }
        
    } else if (strcmp(type, "tts_start") == 0) {
        uint32_t sampleRate = doc["sampleRate"] | 16000;
        uint32_t byteLength = doc["byteLength"] | 0;
        
        Serial.printf("WakeWord: TTS start - %u bytes @ %u Hz\n", byteLength, sampleRate);
        
        // Allocate TTS buffer in PSRAM
        if (tts_buffer) {
            heap_caps_free(tts_buffer);
        }
        
        tts_buffer = (uint8_t*)heap_caps_malloc(byteLength, MALLOC_CAP_SPIRAM);
        if (!tts_buffer) {
            Serial.println("WakeWord: Failed to allocate TTS buffer");
            return;
        }
        
        tts_buffer_size = byteLength;
        tts_buffer_pos = 0;
        tts_sample_rate = sampleRate;
        
        avatar_set_state(STATE_SPEAKING);
        status_ring_show(STATE_SPEAKING);
        
    } else if (strcmp(type, "tts_end") == 0) {
        Serial.printf("WakeWord: TTS end - received %u bytes\n", tts_buffer_pos);
        
        if (tts_buffer && tts_buffer_pos > 0) {
            // Play the TTS audio
            audio_play(tts_buffer, tts_buffer_pos, tts_sample_rate);
            
            // Wait for playback to finish
            while (audio_is_playing()) {
                delay(50);
            }
            
            // Free buffer
            heap_caps_free(tts_buffer);
            tts_buffer = nullptr;
            tts_buffer_size = 0;
            tts_buffer_pos = 0;
        }
        
        // Return to idle
        status_ring_hide();
        avatar_set_state(STATE_IDLE);
        
        // Resume idle streaming
        if (enabled) {
            audio_start_idle_stream();
        }
        
    } else if (strcmp(type, "error") == 0) {
        const char* message = doc["message"];
        Serial.printf("WakeWord: Error - %s\n", message ? message : "unknown");
        
        status_ring_hide();
        avatar_set_state(STATE_IDLE);
        
        if (enabled) {
            audio_start_idle_stream();
        }
        
    } else if (strcmp(type, "pong") == 0) {
        // Heartbeat response, ignore
    }
}

static void on_idle_audio(const uint8_t* data, size_t length) {
    // Forward idle audio chunks to WebSocket server for wake word detection
    if (ws_is_connected()) {
        ws_send_binary(data, length);
    }
}
