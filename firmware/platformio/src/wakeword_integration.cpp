/**
 * Wake Word Integration
 */

#include "wakeword_integration.h"
#include "ws_client.h"
#include "audio_capture.h"
#include "avatar.h"
#include "status_ring.h"
#include "openai_client.h"
#include "esp_heap_caps.h"

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
static void on_wake_detected();
static void on_transcript(const char* text);
static void on_tts_start(uint32_t sampleRate, uint32_t byteLength);
static void on_tts_chunk(const uint8_t* data, size_t length);
static void on_tts_end();
static void on_error(const char* message);
static void on_connected();
static void on_disconnected();
static void on_idle_audio(const uint8_t* data, size_t length);

void wakeword_integration_init() {
    if (initialized) return;
    
    Serial.println("WakeWord: Initializing integration...");
    
    // Initialize WebSocket client
    ws_client_init();
    
    // Configure connection from stored settings
    ws_client_set_host(wakeword_get_host());
    ws_client_set_port(wakeword_get_port());
    
    // Set up callbacks
    ws_client_set_on_wake_detected(on_wake_detected);
    ws_client_set_on_transcript(on_transcript);
    ws_client_set_on_tts_start(on_tts_start);
    ws_client_set_on_tts_chunk(on_tts_chunk);
    ws_client_set_on_tts_end(on_tts_end);
    ws_client_set_on_error(on_error);
    ws_client_set_on_connected(on_connected);
    ws_client_set_on_disconnected(on_disconnected);
    
    // Set up idle audio callback (sends to WebSocket)
    audio_set_idle_callback(on_idle_audio);
    
    // Connect to server
    ws_client_connect();
    
    initialized = true;
    Serial.println("WakeWord: Integration initialized");
}

void wakeword_integration_loop() {
    if (!initialized) return;
    
    // Pump WebSocket events
    ws_client_loop();
}

bool wakeword_integration_is_active() {
    return initialized && enabled && ws_client_is_connected();
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
    
    // Send to server via WebSocket
    ws_client_start_recording(16000, audio_size);
    
    // Send in chunks
    const size_t CHUNK_SIZE = 1024;
    for (size_t i = 0; i < audio_size; i += CHUNK_SIZE) {
        size_t len = (i + CHUNK_SIZE < audio_size) ? CHUNK_SIZE : (audio_size - i);
        ws_client_send_audio_chunk(audio_data + i, len);
    }
    
    ws_client_end_recording();
    Serial.printf("WakeWord: Sent %u bytes to server\n", audio_size);
}

void wakeword_set_enabled(bool en) {
    enabled = en;
    if (en && initialized && ws_client_is_connected()) {
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
// Callbacks
// ============================================================================

static void on_wake_detected() {
    Serial.println("WakeWord: Wake detected - starting recording");
    
    // Play acknowledgment beep
    audio_play_ack_beep();
    
    // Start recording
    wakeword_start_recording();
}

static void on_transcript(const char* text) {
    Serial.printf("WakeWord: Transcript: %s\n", text);
    // Avatar is already in thinking state, transcript is informational
}

static void on_tts_start(uint32_t sampleRate, uint32_t byteLength) {
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
    
    // Switch to speaking state
    avatar_set_state(STATE_SPEAKING);
    status_ring_show(STATE_SPEAKING);
}

static void on_tts_chunk(const uint8_t* data, size_t length) {
    if (!tts_buffer || tts_buffer_pos + length > tts_buffer_size) {
        Serial.println("WakeWord: TTS buffer overflow or not allocated");
        return;
    }
    
    memcpy(tts_buffer + tts_buffer_pos, data, length);
    tts_buffer_pos += length;
}

static void on_tts_end() {
    Serial.printf("WakeWord: TTS end - received %u bytes\n", tts_buffer_pos);
    
    if (tts_buffer && tts_buffer_pos > 0) {
        // Play the TTS audio
        audio_play(tts_buffer, tts_buffer_pos, tts_sample_rate);
        
        // Wait for playback to finish
        while (audio_is_playing()) {
            ws_client_loop();  // Keep WebSocket alive
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
    
    // Resume idle streaming for next wake word
    if (enabled) {
        audio_start_idle_stream();
    }
}

static void on_error(const char* message) {
    Serial.printf("WakeWord: Error - %s\n", message);
    
    // Return to idle state
    status_ring_hide();
    avatar_set_state(STATE_IDLE);
    
    // Resume idle streaming
    if (enabled) {
        audio_start_idle_stream();
    }
}

static void on_connected() {
    Serial.println("WakeWord: Connected to server");
    
    // Start idle streaming for wake word detection
    if (enabled) {
        audio_start_idle_stream();
    }
}

static void on_disconnected() {
    Serial.println("WakeWord: Disconnected from server");
    
    // Stop idle streaming (will auto-reconnect)
    audio_stop_idle_stream();
}

static void on_idle_audio(const uint8_t* data, size_t length) {
    // Forward idle audio chunks to WebSocket server
    if (ws_client_is_connected()) {
        ws_client_send_idle_audio(data, length);
    }
}
