/**
 * Minimal WebSocket Client
 * Uses WiFiClient directly (no external library)
 * Only implements what we need for wake word service
 */

#include "ws_client.h"
#include <WiFiClient.h>
#include <esp_log.h>
#include <cstring>
#include <cstdlib>
#include <base64.h>

static const char* TAG = "WsClient";

// WiFi client
static WiFiClient client;

// Connection settings
static char ws_host[64] = "";
static uint16_t ws_port = 8765;

// State
static bool ws_connected = false;

// Callbacks
static WsMessageCallback message_callback = nullptr;
static WsEventCallback event_callback = nullptr;

// Buffer for incoming frames
static uint8_t rx_buffer[4096];
static size_t rx_pos = 0;

// Forward declarations
static bool ws_handshake();
static void ws_process_frame();
static bool ws_send_frame(uint8_t opcode, const uint8_t* data, size_t length);

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

    ESP_LOGI(TAG, "Connecting to %s:%d", ws_host, ws_port);

    if (!client.connect(ws_host, ws_port)) {
        ESP_LOGE(TAG, "TCP connection failed");
        return false;
    }

    if (!ws_handshake()) {
        ESP_LOGE(TAG, "WebSocket handshake failed");
        client.stop();
        return false;
    }

    ws_connected = true;
    ESP_LOGI(TAG, "WebSocket connected");

    if (event_callback) {
        event_callback(WS_EVENT_CONNECTED);
    }

    return true;
}

void ws_disconnect() {
    if (ws_connected) {
        // Send close frame
        ws_send_frame(0x08, nullptr, 0);
        client.stop();
        ws_connected = false;
        
        if (event_callback) {
            event_callback(WS_EVENT_DISCONNECTED);
        }
    }
}

bool ws_is_connected() {
    if (ws_connected && !client.connected()) {
        ws_connected = false;
        if (event_callback) {
            event_callback(WS_EVENT_DISCONNECTED);
        }
    }
    return ws_connected;
}

void ws_loop() {
    if (!ws_connected) return;

    // Check connection
    if (!client.connected()) {
        ws_connected = false;
        if (event_callback) {
            event_callback(WS_EVENT_DISCONNECTED);
        }
        return;
    }

    // Read available data
    while (client.available() && rx_pos < sizeof(rx_buffer)) {
        rx_buffer[rx_pos++] = client.read();
        ws_process_frame();
    }
}

bool ws_send_text(const char* text) {
    return ws_send_frame(0x01, (const uint8_t*)text, strlen(text));
}

bool ws_send_binary(const uint8_t* data, size_t length) {
    return ws_send_frame(0x02, data, length);
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

// ============================================================================
// Internal Functions
// ============================================================================

static bool ws_handshake() {
    // Generate random key
    uint8_t key_bytes[16];
    for (int i = 0; i < 16; i++) {
        key_bytes[i] = random(256);
    }
    String key = base64::encode(key_bytes, 16);

    // Send HTTP upgrade request
    client.print("GET / HTTP/1.1\r\n");
    client.print("Host: ");
    client.print(ws_host);
    client.print(":");
    client.print(ws_port);
    client.print("\r\n");
    client.print("Upgrade: websocket\r\n");
    client.print("Connection: Upgrade\r\n");
    client.print("Sec-WebSocket-Key: ");
    client.print(key);
    client.print("\r\n");
    client.print("Sec-WebSocket-Version: 13\r\n");
    client.print("\r\n");

    // Wait for response
    unsigned long start = millis();
    while (!client.available() && millis() - start < 5000) {
        delay(10);
    }

    if (!client.available()) {
        return false;
    }

    // Read response (just check for 101)
    String response = client.readStringUntil('\n');
    if (response.indexOf("101") == -1) {
        ESP_LOGE(TAG, "Bad handshake response: %s", response.c_str());
        return false;
    }

    // Skip rest of headers
    while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.length() == 0) {
            break;
        }
    }

    return true;
}

static void ws_process_frame() {
    // Need at least 2 bytes for header
    if (rx_pos < 2) return;

    uint8_t opcode = rx_buffer[0] & 0x0F;
    bool masked = (rx_buffer[1] & 0x80) != 0;
    size_t payload_len = rx_buffer[1] & 0x7F;
    size_t header_len = 2;

    // Extended payload length
    if (payload_len == 126) {
        if (rx_pos < 4) return;
        payload_len = (rx_buffer[2] << 8) | rx_buffer[3];
        header_len = 4;
    } else if (payload_len == 127) {
        if (rx_pos < 10) return;
        // We don't support >64KB frames
        payload_len = 0;
        header_len = 10;
    }

    // Mask key (server shouldn't send masked, but handle it)
    if (masked) {
        header_len += 4;
    }

    // Check if we have full frame
    size_t frame_len = header_len + payload_len;
    if (rx_pos < frame_len) return;

    // Extract payload
    uint8_t* payload = rx_buffer + header_len;

    // Unmask if needed
    if (masked) {
        uint8_t* mask = rx_buffer + header_len - 4;
        for (size_t i = 0; i < payload_len; i++) {
            payload[i] ^= mask[i % 4];
        }
    }

    // Handle frame
    switch (opcode) {
        case 0x01:  // Text
            if (message_callback) {
                message_callback(WS_MSG_TEXT, payload, payload_len);
            }
            break;

        case 0x02:  // Binary
            if (message_callback) {
                message_callback(WS_MSG_BINARY, payload, payload_len);
            }
            break;

        case 0x08:  // Close
            ws_disconnect();
            break;

        case 0x09:  // Ping
            ws_send_frame(0x0A, payload, payload_len);  // Pong
            break;

        case 0x0A:  // Pong
            // Ignore
            break;
    }

    // Remove processed frame from buffer
    if (rx_pos > frame_len) {
        memmove(rx_buffer, rx_buffer + frame_len, rx_pos - frame_len);
    }
    rx_pos -= frame_len;
}

static bool ws_send_frame(uint8_t opcode, const uint8_t* data, size_t length) {
    if (!ws_connected || !client.connected()) {
        return false;
    }

    // Frame header
    uint8_t header[14];
    size_t header_len = 2;

    header[0] = 0x80 | opcode;  // FIN + opcode

    // Payload length + mask bit (client must mask)
    if (length < 126) {
        header[1] = 0x80 | length;
    } else if (length < 65536) {
        header[1] = 0x80 | 126;
        header[2] = (length >> 8) & 0xFF;
        header[3] = length & 0xFF;
        header_len = 4;
    } else {
        header[1] = 0x80 | 127;
        // 64-bit length (we only use lower 32 bits)
        header[2] = 0;
        header[3] = 0;
        header[4] = 0;
        header[5] = 0;
        header[6] = (length >> 24) & 0xFF;
        header[7] = (length >> 16) & 0xFF;
        header[8] = (length >> 8) & 0xFF;
        header[9] = length & 0xFF;
        header_len = 10;
    }

    // Mask key
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) {
        mask[i] = random(256);
        header[header_len++] = mask[i];
    }

    // Send header
    client.write(header, header_len);

    // Send masked payload
    if (data && length > 0) {
        uint8_t* masked = (uint8_t*)malloc(length);
        if (!masked) return false;
        
        for (size_t i = 0; i < length; i++) {
            masked[i] = data[i] ^ mask[i % 4];
        }
        client.write(masked, length);
        free(masked);
    }

    return true;
}
