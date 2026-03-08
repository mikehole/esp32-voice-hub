/**
 * WebSocket Client for Wake Word Service
 * 
 * Connects to the wake-word-service server and handles:
 * - Idle audio streaming for wake word detection
 * - Recording audio transmission
 * - TTS response reception
 */

#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <Arduino.h>

// Callback types
typedef void (*OnWakeDetectedCallback)(void);
typedef void (*OnTranscriptCallback)(const char* text);
typedef void (*OnTtsStartCallback)(uint32_t sampleRate, uint32_t byteLength);
typedef void (*OnTtsChunkCallback)(const uint8_t* data, size_t length);
typedef void (*OnTtsEndCallback)(void);
typedef void (*OnErrorCallback)(const char* message);
typedef void (*OnConnectedCallback)(void);
typedef void (*OnDisconnectedCallback)(void);

// Initialize WebSocket client
void ws_client_init();

// Set connection parameters (call before connect)
void ws_client_set_host(const char* host);
void ws_client_set_port(uint16_t port);
const char* ws_client_get_host();
uint16_t ws_client_get_port();

// Connection control
bool ws_client_connect();
void ws_client_disconnect();
bool ws_client_is_connected();

// Must be called in loop()
void ws_client_loop();

// Send idle audio chunk (2560 bytes, 80ms at 16kHz)
void ws_client_send_idle_audio(const uint8_t* data, size_t length);

// Send recording (full utterance)
void ws_client_start_recording(uint32_t sampleRate, uint32_t expectedBytes);
void ws_client_send_audio_chunk(const uint8_t* data, size_t length);
void ws_client_end_recording();

// Callbacks
void ws_client_set_on_wake_detected(OnWakeDetectedCallback cb);
void ws_client_set_on_transcript(OnTranscriptCallback cb);
void ws_client_set_on_tts_start(OnTtsStartCallback cb);
void ws_client_set_on_tts_chunk(OnTtsChunkCallback cb);
void ws_client_set_on_tts_end(OnTtsEndCallback cb);
void ws_client_set_on_error(OnErrorCallback cb);
void ws_client_set_on_connected(OnConnectedCallback cb);
void ws_client_set_on_disconnected(OnDisconnectedCallback cb);

#endif // WS_CLIENT_H
