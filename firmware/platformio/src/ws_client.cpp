/**
 * WebSocket Client for Wake Word Service
 * Using ESP-IDF native esp_websocket_client
 */

#include "ws_client.h"
#include <esp_websocket_client.h>
#include <esp_log.h>
#include <cstring>
#include <cstdlib>

static const char* TAG = "WsClient";

// WebSocket client handle
static esp_websocket_client_handle_t ws_client = nullptr;

// Connection settings
static char ws_host[64] = "";
static uint16_t ws_port = 8765;

// State
static bool ws_connected = false;

// Callbacks
static WsMessageCallback message_callback = nullptr;
static WsEventCallback event_callback = nullptr;

// Forward declarations
static void websocket_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);

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

    // Build WebSocket URI
    char uri[128];
    snprintf(uri, sizeof(uri), "ws://%s:%d", ws_host, ws_port);

    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri = uri;
    ws_cfg.disable_auto_reconnect = false;
    ws_cfg.reconnect_timeout_ms = 5000;
    ws_cfg.network_timeout_ms = 10000;

    ESP_LOGI(TAG, "Connecting to %s", uri);

    ws_client = esp_websocket_client_init(&ws_cfg);
    if (!ws_client) {
        ESP_LOGE(TAG, "Failed to init WebSocket client");
        return false;
    }

    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, nullptr);

    esp_err_t err = esp_websocket_client_start(ws_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(ws_client);
        ws_client = nullptr;
        return false;
    }

    return true;
}

void ws_disconnect() {
    if (ws_client) {
        esp_websocket_client_stop(ws_client);
        esp_websocket_client_destroy(ws_client);
        ws_client = nullptr;
        ws_connected = false;
    }
}

bool ws_is_connected() {
    return ws_connected && ws_client && esp_websocket_client_is_connected(ws_client);
}

void ws_loop() {
    // ESP-IDF WebSocket client runs in its own task, no manual loop needed
}

bool ws_send_text(const char* text) {
    if (!ws_is_connected()) {
        return false;
    }

    int len = strlen(text);
    int sent = esp_websocket_client_send_text(ws_client, text, len, portMAX_DELAY);
    return sent == len;
}

bool ws_send_binary(const uint8_t* data, size_t length) {
    if (!ws_is_connected()) {
        return false;
    }

    int sent = esp_websocket_client_send_bin(ws_client, (const char*)data, length, portMAX_DELAY);
    return sent == (int)length;
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
static void websocket_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            ws_connected = true;
            if (event_callback) {
                event_callback(WS_EVENT_CONNECTED);
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket disconnected");
            ws_connected = false;
            if (event_callback) {
                event_callback(WS_EVENT_DISCONNECTED);
            }
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x01) {
                // Text frame
                if (message_callback && data->data_len > 0) {
                    // Null-terminate the text
                    char* text = (char*)malloc(data->data_len + 1);
                    if (text) {
                        memcpy(text, data->data_ptr, data->data_len);
                        text[data->data_len] = '\0';
                        message_callback(WS_MSG_TEXT, (const uint8_t*)text, data->data_len);
                        free(text);
                    }
                }
            } else if (data->op_code == 0x02) {
                // Binary frame
                if (message_callback && data->data_len > 0) {
                    message_callback(WS_MSG_BINARY, (const uint8_t*)data->data_ptr, data->data_len);
                }
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            if (event_callback) {
                event_callback(WS_EVENT_ERROR);
            }
            break;

        default:
            break;
    }
}
