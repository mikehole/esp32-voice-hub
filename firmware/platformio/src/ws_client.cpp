/**
 * WebSocket Client for Wake Word Service
 */

#include "ws_client.h"
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "esp_log.h"

static const char* TAG = "WsClient";

// WebSocket client
static WebSocketsClient ws;

// Connection settings
static String ws_host = "192.168.1.100";
static uint16_t ws_port = 8765;

// Callbacks
static OnWakeDetectedCallback on_wake_detected = nullptr;
static OnTranscriptCallback on_transcript = nullptr;
static OnTtsStartCallback on_tts_start = nullptr;
static OnTtsChunkCallback on_tts_chunk = nullptr;
static OnTtsEndCallback on_tts_end = nullptr;
static OnErrorCallback on_error = nullptr;
static OnConnectedCallback on_connected = nullptr;
static OnDisconnectedCallback on_disconnected = nullptr;

// TTS state
static bool tts_streaming = false;

// Forward declarations
static void ws_event_handler(WStype_t type, uint8_t* payload, size_t length);

void ws_client_init() {
    ESP_LOGI(TAG, "Initializing WebSocket client");
}

void ws_client_set_host(const char* host) {
    ws_host = String(host);
}

void ws_client_set_port(uint16_t port) {
    ws_port = port;
}

const char* ws_client_get_host() {
    return ws_host.c_str();
}

uint16_t ws_client_get_port() {
    return ws_port;
}

bool ws_client_connect() {
    ESP_LOGI(TAG, "Connecting to %s:%d", ws_host.c_str(), ws_port);
    
    ws.begin(ws_host.c_str(), ws_port, "/");
    ws.onEvent(ws_event_handler);
    ws.setReconnectInterval(3000);
    
    return true;
}

void ws_client_disconnect() {
    ws.disconnect();
}

bool ws_client_is_connected() {
    return ws.isConnected();
}

void ws_client_loop() {
    ws.loop();
}

void ws_client_send_idle_audio(const uint8_t* data, size_t length) {
    if (ws.isConnected() && length > 0) {
        ws.sendBIN(data, length);
    }
}

void ws_client_start_recording(uint32_t sampleRate, uint32_t expectedBytes) {
    if (!ws.isConnected()) {
        ESP_LOGW(TAG, "Cannot start recording - not connected");
        return;
    }
    
    StaticJsonDocument<128> doc;
    doc["type"] = "audio_start";
    doc["sampleRate"] = sampleRate;
    doc["byteLength"] = expectedBytes;
    
    String json;
    serializeJson(doc, json);
    ws.sendTXT(json);
    
    ESP_LOGI(TAG, "Recording started: %u bytes expected", expectedBytes);
}

void ws_client_send_audio_chunk(const uint8_t* data, size_t length) {
    if (ws.isConnected() && length > 0) {
        ws.sendBIN(data, length);
    }
}

void ws_client_end_recording() {
    if (!ws.isConnected()) return;
    
    ws.sendTXT("{\"type\":\"audio_end\"}");
    ESP_LOGI(TAG, "Recording ended");
}

// Callback setters
void ws_client_set_on_wake_detected(OnWakeDetectedCallback cb) { on_wake_detected = cb; }
void ws_client_set_on_transcript(OnTranscriptCallback cb) { on_transcript = cb; }
void ws_client_set_on_tts_start(OnTtsStartCallback cb) { on_tts_start = cb; }
void ws_client_set_on_tts_chunk(OnTtsChunkCallback cb) { on_tts_chunk = cb; }
void ws_client_set_on_tts_end(OnTtsEndCallback cb) { on_tts_end = cb; }
void ws_client_set_on_error(OnErrorCallback cb) { on_error = cb; }
void ws_client_set_on_connected(OnConnectedCallback cb) { on_connected = cb; }
void ws_client_set_on_disconnected(OnDisconnectedCallback cb) { on_disconnected = cb; }

// WebSocket event handler
static void ws_event_handler(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            ESP_LOGI(TAG, "Connected to wake word server");
            if (on_connected) on_connected();
            break;
            
        case WStype_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from wake word server");
            tts_streaming = false;
            if (on_disconnected) on_disconnected();
            break;
            
        case WStype_TEXT: {
            // Parse JSON message
            StaticJsonDocument<256> doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err) {
                ESP_LOGW(TAG, "JSON parse error: %s", err.c_str());
                break;
            }
            
            const char* msg_type = doc["type"] | "";
            
            if (strcmp(msg_type, "wake_detected") == 0) {
                ESP_LOGI(TAG, "Wake word detected!");
                if (on_wake_detected) on_wake_detected();
            }
            else if (strcmp(msg_type, "transcript") == 0) {
                const char* text = doc["text"] | "";
                ESP_LOGI(TAG, "Transcript: %s", text);
                if (on_transcript) on_transcript(text);
            }
            else if (strcmp(msg_type, "tts_start") == 0) {
                uint32_t sampleRate = doc["sampleRate"] | 16000;
                uint32_t byteLength = doc["byteLength"] | 0;
                ESP_LOGI(TAG, "TTS start: %u bytes @ %u Hz", byteLength, sampleRate);
                tts_streaming = true;
                if (on_tts_start) on_tts_start(sampleRate, byteLength);
            }
            else if (strcmp(msg_type, "tts_end") == 0) {
                ESP_LOGI(TAG, "TTS end");
                tts_streaming = false;
                if (on_tts_end) on_tts_end();
            }
            else if (strcmp(msg_type, "status") == 0) {
                const char* state = doc["state"] | "";
                ESP_LOGI(TAG, "Status: %s", state);
            }
            else if (strcmp(msg_type, "error") == 0) {
                const char* message = doc["message"] | "Unknown error";
                ESP_LOGE(TAG, "Error: %s", message);
                if (on_error) on_error(message);
            }
            else if (strcmp(msg_type, "pong") == 0) {
                // Heartbeat response, ignore
            }
            else {
                ESP_LOGW(TAG, "Unknown message type: %s", msg_type);
            }
            break;
        }
        
        case WStype_BIN:
            // TTS audio chunk
            if (tts_streaming && on_tts_chunk) {
                on_tts_chunk(payload, length);
            }
            break;
            
        case WStype_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            break;
            
        case WStype_PING:
        case WStype_PONG:
            // Handled automatically
            break;
            
        default:
            break;
    }
}
