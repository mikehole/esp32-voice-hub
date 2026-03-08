/**
 * WebSocket Client for Wake Word Service
 * Using arduinoWebSockets library
 */

#include "ws_client.h"

// Must include WiFi before WebSockets
#include <WiFi.h>
#include <WebSocketsClient.h>

#include <esp_log.h>
#include <cstring>

static const char* TAG = "WsClient";

// WebSocket client
static WebSocketsClient ws;

// Connection settings
static char ws_host[64] = "";
static uint16_t ws_port = 8765;

// State
static bool ws_connected = false;
static bool ws_initialized = false;

// Callbacks
static WsMessageCallback message_callback = nullptr;
static WsEventCallback event_callback = nullptr;

// Forward declaration
static void websocket_event(WStype_t type, uint8_t* payload, size_t length);

void ws_set_host(const char* host) {
    strncpy(ws_host, host, sizeof(ws_host) - 1);
    ws_host[sizeof(ws_host) - 1] = '\0';
}

void ws_set_port(uint16_t port) {
    ws_port = port;
}

const char* ws_get_host() {
    return ws_host;
}

uint16_t ws_get_port() {
    return ws_port;
}

void ws_set_message_callback(WsMessageCallback callback) {
    message_callback = callback;
}

void ws_set_event_callback(WsEventCallback callback) {
    event_callback = callback;
}

bool ws_init() {
    if (strlen(ws_host) == 0) {
        ESP_LOGW(TAG, "No host configured");
        return false;
    }

    ESP_LOGI(TAG, "Connecting to ws://%s:%d", ws_host, ws_port);

    ws.begin(ws_host, ws_port, "/");
    ws.onEvent(websocket_event);
    ws.setReconnectInterval(5000);
    ws.enableHeartbeat(15000, 3000, 2);

    ws_initialized = true;
    return true;
}

void ws_disconnect() {
    if (ws_initialized) {
        ws.disconnect();
        ws_connected = false;
        ws_initialized = false;
    }
}

bool ws_is_connected() {
    return ws_connected;
}

void ws_loop() {
    if (ws_initialized) {
        ws.loop();
    }
}

bool ws_send_text(const char* text) {
    if (!ws_connected) {
        return false;
    }
    return ws.sendTXT(text);
}

bool ws_send_binary(const uint8_t* data, size_t length) {
    if (!ws_connected) {
        return false;
    }
    return ws.sendBIN(data, length);
}

bool ws_send_json(const char* type) {
    char json[128];
    snprintf(json, sizeof(json), "{\"type\":\"%s\"}", type);
    return ws_send_text(json);
}

bool ws_send_audio_start(int sampleRate) {
    char json[128];
    snprintf(json, sizeof(json), "{\"type\":\"audio_start\",\"sampleRate\":%d}", sampleRate);
    return ws_send_text(json);
}

bool ws_send_audio_end() {
    return ws_send_json("audio_end");
}

// Event handler
static void websocket_event(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket disconnected");
            ws_connected = false;
            if (event_callback) {
                event_callback(WS_EVENT_DISCONNECTED);
            }
            break;

        case WStype_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected to %s", payload);
            ws_connected = true;
            if (event_callback) {
                event_callback(WS_EVENT_CONNECTED);
            }
            break;

        case WStype_TEXT:
            if (message_callback && length > 0) {
                message_callback(WS_MSG_TEXT, payload, length);
            }
            break;

        case WStype_BIN:
            if (message_callback && length > 0) {
                message_callback(WS_MSG_BINARY, payload, length);
            }
            break;

        case WStype_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            if (event_callback) {
                event_callback(WS_EVENT_ERROR);
            }
            break;

        case WStype_PING:
        case WStype_PONG:
            // Heartbeat, ignore
            break;

        default:
            break;
    }
}
