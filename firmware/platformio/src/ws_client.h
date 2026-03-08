/**
 * WebSocket Client for Wake Word Service
 * Using ESP-IDF native esp_websocket_client
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

// Message types
enum WsMessageType {
    WS_MSG_TEXT,
    WS_MSG_BINARY
};

// Event types
enum WsEventType {
    WS_EVENT_CONNECTED,
    WS_EVENT_DISCONNECTED,
    WS_EVENT_ERROR
};

// Callbacks
typedef void (*WsMessageCallback)(WsMessageType type, const uint8_t* data, size_t length);
typedef void (*WsEventCallback)(WsEventType event);

// Configuration
void ws_set_host(const char* host);
void ws_set_port(uint16_t port);
const char* ws_get_host();
uint16_t ws_get_port();

// Callbacks
void ws_set_message_callback(WsMessageCallback callback);
void ws_set_event_callback(WsEventCallback callback);

// Connection
bool ws_init();
void ws_disconnect();
bool ws_is_connected();
void ws_loop();

// Sending
bool ws_send_text(const char* text);
bool ws_send_binary(const uint8_t* data, size_t length);
bool ws_send_json(const char* type);
bool ws_send_audio_start(int sampleRate);
bool ws_send_audio_end();
