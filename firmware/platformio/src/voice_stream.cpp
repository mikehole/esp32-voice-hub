/**
 * Voice Streaming - Simple WebSocket audio streaming
 * 
 * Flow:
 * 1. Tap center → Connect WebSocket → Start streaming mic audio
 * 2. Tap again → Stop streaming → Send "audio_end" → Show thinking
 * 3. Server: STT → Agent → TTS → POST to /api/play
 * 4. /api/play handler plays audio → Return to idle
 */

#include "voice_stream.h"
#include "ws_client.h"
#include "audio_capture.h"
#include "avatar.h"
#include "status_ring.h"
#include "openai_client.h"  // For wakeword_get_host/port settings
#include <Arduino.h>

static const char* TAG = "VoiceStream";

// State - use volatile for variables accessed from callback
static bool initialized = false;
static volatile bool streaming = false;
static bool waiting_for_response = false;

// Audio streaming callback - forwards mic audio to WebSocket
static void on_stream_audio(const uint8_t* data, size_t length);

// WebSocket callbacks
static void on_ws_message(WsMessageType type, const uint8_t* data, size_t length);
static void on_ws_event(WsEventType event);

void voice_stream_init() {
    if (initialized) return;
    
    // Set up WebSocket callbacks
    ws_set_message_callback(on_ws_message);
    ws_set_event_callback(on_ws_event);
    
    initialized = true;
    Serial.println("VoiceStream: Initialized");
}

bool voice_stream_start() {
    if (!initialized) {
        Serial.println("VoiceStream: Not initialized");
        return false;
    }
    
    if (streaming) {
        Serial.println("VoiceStream: Already streaming");
        return false;
    }
    
    if (waiting_for_response) {
        Serial.println("VoiceStream: Still waiting for response");
        return false;
    }
    
    Serial.println("VoiceStream: Starting...");
    
    // Configure WebSocket connection
    ws_set_host(wakeword_get_host());
    ws_set_port(wakeword_get_port());
    
    // Connect
    if (!ws_init()) {
        Serial.println("VoiceStream: Failed to connect");
        return false;
    }
    
    // Send start message with sample rate
    ws_send_audio_start(16000);
    
    // Mark as streaming BEFORE starting audio
    streaming = true;
    
    // Set up audio streaming callback
    audio_set_idle_callback(on_stream_audio);
    
    // Start capturing and streaming
    audio_start_idle_stream();
    
    // Update UI
    avatar_set_state(STATE_RECORDING);
    status_ring_show(STATE_RECORDING);
    
    Serial.println("VoiceStream: Streaming started");
    return true;
}

void voice_stream_stop() {
    if (!streaming) return;
    
    Serial.println("VoiceStream: Stopping stream...");
    
    // CRITICAL: Set streaming to false FIRST to prevent callback from sending more data
    streaming = false;
    
    // Stop audio capture and clear callback
    audio_stop_idle_stream();
    audio_set_idle_callback(nullptr);
    
    // Small delay to ensure any in-flight callbacks complete
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Send end message while still connected
    if (ws_is_connected()) {
        ws_send_audio_end();
        Serial.println("VoiceStream: Sent audio_end");
    }
    
    // Disconnect WebSocket - server will process and POST to /api/play
    ws_disconnect();
    
    waiting_for_response = true;
    
    // Update UI to thinking (waiting for /api/play response)
    avatar_set_state(STATE_THINKING);
    status_ring_show(STATE_THINKING);
    
    Serial.println("VoiceStream: Waiting for response via /api/play");
}

bool voice_stream_is_active() {
    return streaming;
}

bool voice_stream_is_waiting() {
    return waiting_for_response;
}

void voice_stream_response_received() {
    // Called by /api/play handler when audio arrives
    waiting_for_response = false;
    Serial.println("VoiceStream: Response received");
}

void voice_stream_response_done() {
    // Called when playback finishes
    waiting_for_response = false;
    avatar_set_state(STATE_IDLE);
    status_ring_hide();
    Serial.println("VoiceStream: Playback done, returning to idle");
}

void voice_stream_loop() {
    if (!initialized) return;
    
    // Pump WebSocket events while connected
    if (ws_is_connected()) {
        ws_loop();
    }
}

// ============================================================================
// Callbacks
// ============================================================================

static void on_stream_audio(const uint8_t* data, size_t length) {
    // Forward audio to WebSocket (with safety checks)
    // Check streaming flag first (volatile, set to false before disconnect)
    if (!streaming) return;
    if (!initialized) return;
    if (!ws_is_connected()) return;
    
    if (!ws_send_binary(data, length)) {
        Serial.println("VoiceStream: Failed to send audio chunk");
    }
}

static void on_ws_message(WsMessageType type, const uint8_t* data, size_t length) {
    if (type == WS_MSG_TEXT) {
        // Parse JSON response
        char* json = (char*)malloc(length + 1);
        if (json) {
            memcpy(json, data, length);
            json[length] = '\0';
            Serial.printf("VoiceStream: Server: %s\n", json);
            free(json);
        }
    }
}

static void on_ws_event(WsEventType event) {
    switch (event) {
        case WS_EVENT_CONNECTED:
            Serial.println("VoiceStream: Connected");
            break;
            
        case WS_EVENT_DISCONNECTED:
            Serial.println("VoiceStream: Disconnected");
            if (streaming) {
                // Unexpected disconnect during streaming
                streaming = false;
                audio_stop_idle_stream();
                audio_set_idle_callback(nullptr);
                avatar_set_state(STATE_IDLE);
                status_ring_hide();
            }
            break;
            
        case WS_EVENT_ERROR:
            Serial.println("VoiceStream: Error");
            streaming = false;
            waiting_for_response = false;
            audio_stop_idle_stream();
            audio_set_idle_callback(nullptr);
            avatar_set_state(STATE_IDLE);
            status_ring_hide();
            break;
    }
}
