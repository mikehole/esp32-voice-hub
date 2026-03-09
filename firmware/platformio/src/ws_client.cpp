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

// WiFi client - use pointer so we can fully reset it
static WiFiClient* client = nullptr;

// Connection settings
static char ws_host[64] = "";
static uint16_t ws_port = 8765;

// State
static bool ws_connected = false;

// Callbacks
static WsMessageCallback message_callback = nullptr;
static WsEventCallback event_callback = nullptr;

// Buffer for incoming frames (increased for potential larger messages)
static uint8_t rx_buffer[8192];
static size_t rx_pos = 0;

// Keepalive
static unsigned long last_ping_time = 0;
static const unsigned long PING_INTERVAL_MS = 30000;  // Send ping every 30s
static unsigned long last_pong_time = 0;
static const unsigned long PONG_TIMEOUT_MS = 10000;   // Expect pong within 10s

// Forward declarations
static bool ws_handshake();
static void ws_process_frame();
static bool ws_send_frame(uint8_t opcode, const uint8_t* data, size_t length);
static void ws_cleanup();

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

// Clean up existing connection completely
static void ws_cleanup() {
    if (client) {
        client->stop();
        delete client;
        client = nullptr;
    }
    ws_connected = false;
    rx_pos = 0;
    last_ping_time = 0;
    last_pong_time = 0;
}

bool ws_init() {
    if (strlen(ws_host) == 0) {
        ESP_LOGW(TAG, "No host configured");
        return false;
    }

    // Always clean up any existing connection first
    ws_cleanup();

    // Create fresh client
    client = new WiFiClient();
    if (!client) {
        ESP_LOGE(TAG, "Failed to allocate WiFiClient");
        return false;
    }

    // Set timeouts
    client->setTimeout(10);  // 10 second timeout for operations

    ESP_LOGI(TAG, "Connecting to %s:%d", ws_host, ws_port);

    if (!client->connect(ws_host, ws_port)) {
        ESP_LOGE(TAG, "TCP connection failed");
        ws_cleanup();
        return false;
    }

    if (!ws_handshake()) {
        ESP_LOGE(TAG, "WebSocket handshake failed");
        ws_cleanup();
        return false;
    }

    ws_connected = true;
    last_ping_time = millis();
    last_pong_time = millis();  // Assume connection is healthy at start
    
    ESP_LOGI(TAG, "WebSocket connected");

    if (event_callback) {
        event_callback(WS_EVENT_CONNECTED);
    }

    return true;
}

void ws_disconnect() {
    if (ws_connected) {
        // Try to send close frame (best effort)
        ws_send_frame(0x08, nullptr, 0);
    }
    
    bool was_connected = ws_connected;
    ws_cleanup();
    
    if (was_connected && event_callback) {
        event_callback(WS_EVENT_DISCONNECTED);
    }
}

bool ws_is_connected() {
    if (!client) {
        ws_connected = false;
        return false;
    }
    
    if (ws_connected && !client->connected()) {
        ESP_LOGW(TAG, "Connection lost (client disconnected)");
        bool was_connected = ws_connected;
        ws_cleanup();
        if (was_connected && event_callback) {
            event_callback(WS_EVENT_DISCONNECTED);
        }
    }
    return ws_connected;
}

void ws_loop() {
    if (!ws_connected || !client) return;

    // Check connection
    if (!client->connected()) {
        ESP_LOGW(TAG, "Connection lost in loop");
        bool was_connected = ws_connected;
        ws_cleanup();
        if (was_connected && event_callback) {
            event_callback(WS_EVENT_DISCONNECTED);
        }
        return;
    }

    unsigned long now = millis();

    // Send periodic ping to keep connection alive
    if (now - last_ping_time > PING_INTERVAL_MS) {
        ESP_LOGD(TAG, "Sending ping");
        ws_send_frame(0x09, nullptr, 0);  // Ping
        last_ping_time = now;
    }

    // Check for pong timeout (only if we've sent a ping and are waiting)
    if (last_ping_time > last_pong_time && 
        now - last_ping_time > PONG_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Pong timeout - connection may be dead");
        // Don't immediately disconnect, but mark as potentially unhealthy
        // The next send failure will trigger reconnect
    }

    // Read available data
    int available = client->available();
    while (available > 0 && rx_pos < sizeof(rx_buffer)) {
        int to_read = min((size_t)available, sizeof(rx_buffer) - rx_pos);
        int read = client->read(rx_buffer + rx_pos, to_read);
        if (read > 0) {
            rx_pos += read;
            available -= read;
        } else {
            break;
        }
    }
    
    // Process any complete frames
    ws_process_frame();
    
    // Warn if buffer is getting full
    if (rx_pos > sizeof(rx_buffer) * 3 / 4) {
        ESP_LOGW(TAG, "RX buffer %d%% full", (int)(rx_pos * 100 / sizeof(rx_buffer)));
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
    if (!client) return false;
    
    // Generate random key
    uint8_t key_bytes[16];
    for (int i = 0; i < 16; i++) {
        key_bytes[i] = random(256);
    }
    String key = base64::encode(key_bytes, 16);

    // Build request as single string to avoid fragmentation issues
    String request = "GET / HTTP/1.1\r\n";
    request += "Host: ";
    request += ws_host;
    request += ":";
    request += String(ws_port);
    request += "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: ";
    request += key;
    request += "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "\r\n";
    
    client->print(request);

    // Wait for response with timeout
    unsigned long start = millis();
    while (!client->available() && millis() - start < 5000) {
        delay(10);
    }

    if (!client->available()) {
        ESP_LOGE(TAG, "Handshake timeout");
        return false;
    }

    // Read response (just check for 101)
    String response = client->readStringUntil('\n');
    if (response.indexOf("101") == -1) {
        ESP_LOGE(TAG, "Bad handshake response: %s", response.c_str());
        return false;
    }

    // Skip rest of headers
    while (client->available()) {
        String line = client->readStringUntil('\n');
        if (line == "\r" || line.length() == 0) {
            break;
        }
    }

    return true;
}

static void ws_process_frame() {
    // Need at least 2 bytes for header
    while (rx_pos >= 2) {
        uint8_t opcode = rx_buffer[0] & 0x0F;
        bool fin = (rx_buffer[0] & 0x80) != 0;
        bool masked = (rx_buffer[1] & 0x80) != 0;
        size_t payload_len = rx_buffer[1] & 0x7F;
        size_t header_len = 2;

        // Extended payload length
        if (payload_len == 126) {
            if (rx_pos < 4) return;  // Need more data
            payload_len = (rx_buffer[2] << 8) | rx_buffer[3];
            header_len = 4;
        } else if (payload_len == 127) {
            if (rx_pos < 10) return;  // Need more data
            // 64-bit length - we only support up to our buffer size
            payload_len = ((size_t)rx_buffer[6] << 24) | 
                          ((size_t)rx_buffer[7] << 16) |
                          ((size_t)rx_buffer[8] << 8) | 
                          rx_buffer[9];
            header_len = 10;
            
            // Sanity check
            if (payload_len > sizeof(rx_buffer)) {
                ESP_LOGE(TAG, "Frame too large: %u bytes", payload_len);
                rx_pos = 0;  // Reset buffer
                return;
            }
        }

        // Mask key (server shouldn't send masked, but handle it)
        if (masked) {
            header_len += 4;
        }

        // Check if we have full frame
        size_t frame_len = header_len + payload_len;
        if (rx_pos < frame_len) return;  // Need more data

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
                ESP_LOGI(TAG, "Received close frame");
                ws_disconnect();
                return;  // Exit loop - we're disconnected

            case 0x09:  // Ping
                ESP_LOGD(TAG, "Received ping, sending pong");
                ws_send_frame(0x0A, payload, payload_len);  // Pong
                break;

            case 0x0A:  // Pong
                ESP_LOGD(TAG, "Received pong");
                last_pong_time = millis();
                break;
                
            default:
                ESP_LOGW(TAG, "Unknown opcode: 0x%02x", opcode);
                break;
        }

        // Remove processed frame from buffer
        if (rx_pos > frame_len) {
            memmove(rx_buffer, rx_buffer + frame_len, rx_pos - frame_len);
        }
        rx_pos -= frame_len;
    }
}

static bool ws_send_frame(uint8_t opcode, const uint8_t* data, size_t length) {
    if (!ws_connected || !client || !client->connected()) {
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
    size_t written = client->write(header, header_len);
    if (written != header_len) {
        ESP_LOGE(TAG, "Failed to write header (%d/%d)", written, header_len);
        return false;
    }

    // Send masked payload
    if (data && length > 0) {
        // For large payloads, send in chunks to avoid memory issues
        const size_t CHUNK_SIZE = 1024;
        uint8_t chunk[CHUNK_SIZE];
        
        for (size_t offset = 0; offset < length; offset += CHUNK_SIZE) {
            size_t chunk_len = min(CHUNK_SIZE, length - offset);
            
            for (size_t i = 0; i < chunk_len; i++) {
                chunk[i] = data[offset + i] ^ mask[(offset + i) % 4];
            }
            
            written = client->write(chunk, chunk_len);
            if (written != chunk_len) {
                ESP_LOGE(TAG, "Failed to write payload chunk (%d/%d)", written, chunk_len);
                return false;
            }
        }
    }

    return true;
}
