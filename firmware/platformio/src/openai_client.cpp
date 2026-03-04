/**
 * OpenAI API Client
 * Whisper transcription via multipart form POST
 */

#include "openai_client.h"
#include "conversation.h"
#include <Preferences.h>
#include <WiFiClientSecure.h>

static const char* TAG = "openai";
static const char* PREF_NAMESPACE = "openai";
static const char* PREF_KEY = "api_key";

static Preferences prefs;
static char api_key[256] = {0};  // OpenAI project keys can be ~180 chars
static char last_error[256] = {0};

// OpenAI API endpoint
static const char* WHISPER_URL = "https://api.openai.com/v1/audio/transcriptions";

// Forward declaration
static void load_openclaw_endpoint();

void openai_init() {
    prefs.begin(PREF_NAMESPACE, true);  // Read-only
    String key = prefs.getString(PREF_KEY, "");
    if (key.length() > 0) {
        strncpy(api_key, key.c_str(), sizeof(api_key) - 1);
        Serial.println("OpenAI: API key loaded from NVS");
    } else {
        Serial.println("OpenAI: No API key configured");
    }
    prefs.end();
    
    // Also load OpenClaw endpoint
    load_openclaw_endpoint();
}

void openai_set_api_key(const char* key) {
    strncpy(api_key, key, sizeof(api_key) - 1);
    api_key[sizeof(api_key) - 1] = '\0';
    
    prefs.begin(PREF_NAMESPACE, false);  // Read-write
    prefs.putString(PREF_KEY, api_key);
    prefs.end();
    
    Serial.println("OpenAI: API key saved");
}

bool openai_has_api_key() {
    return strlen(api_key) > 10;  // Basic sanity check
}

const char* openai_get_api_key() {
    static char masked[32];
    if (!openai_has_api_key()) {
        return "(not set)";
    }
    // Show first 4 and last 4 chars
    size_t len = strlen(api_key);
    snprintf(masked, sizeof(masked), "%.4s...%s", api_key, api_key + len - 4);
    return masked;
}

const char* openai_get_last_error() {
    return last_error;
}

// Build WAV header for raw PCM data
static void build_wav_header(uint8_t* header, size_t audio_size) {
    uint32_t sample_rate = 16000;
    uint16_t bits_per_sample = 16;
    uint16_t channels = 1;
    uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    uint16_t block_align = channels * bits_per_sample / 8;
    uint32_t wav_size = 44 + audio_size;
    uint32_t chunk_size = wav_size - 8;
    
    memcpy(header, "RIFF", 4);
    header[4] = chunk_size & 0xFF;
    header[5] = (chunk_size >> 8) & 0xFF;
    header[6] = (chunk_size >> 16) & 0xFF;
    header[7] = (chunk_size >> 24) & 0xFF;
    memcpy(header + 8, "WAVE", 4);
    
    memcpy(header + 12, "fmt ", 4);
    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
    header[20] = 1; header[21] = 0;  // PCM
    header[22] = channels; header[23] = 0;
    header[24] = sample_rate & 0xFF;
    header[25] = (sample_rate >> 8) & 0xFF;
    header[26] = (sample_rate >> 16) & 0xFF;
    header[27] = (sample_rate >> 24) & 0xFF;
    header[28] = byte_rate & 0xFF;
    header[29] = (byte_rate >> 8) & 0xFF;
    header[30] = (byte_rate >> 16) & 0xFF;
    header[31] = (byte_rate >> 24) & 0xFF;
    header[32] = block_align; header[33] = 0;
    header[34] = bits_per_sample; header[35] = 0;
    
    memcpy(header + 36, "data", 4);
    header[40] = audio_size & 0xFF;
    header[41] = (audio_size >> 8) & 0xFF;
    header[42] = (audio_size >> 16) & 0xFF;
    header[43] = (audio_size >> 24) & 0xFF;
}

// Transcription state
static volatile bool transcribing = false;
static volatile bool speaking = false;

// OpenAI TTS endpoint
static const char* TTS_URL = "api.openai.com";

uint8_t* openai_tts(const char* text, size_t* out_size) {
    *out_size = 0;
    
    if (!openai_has_api_key()) {
        strcpy(last_error, "No API key configured");
        return NULL;
    }
    
    if (!text || strlen(text) == 0) {
        strcpy(last_error, "Empty text");
        return NULL;
    }
    
    if (speaking) {
        strcpy(last_error, "TTS already in progress");
        return NULL;
    }
    
    speaking = true;
    Serial.printf("OpenAI TTS: Converting %u chars...\n", strlen(text));
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30);
    
    if (!client.connect(TTS_URL, 443)) {
        strcpy(last_error, "TTS connection failed");
        speaking = false;
        return NULL;
    }
    
    // Build JSON body - request PCM format for direct playback
    String body = "{\"model\":\"tts-1\",\"input\":\"";
    for (size_t i = 0; i < strlen(text); i++) {
        char c = text[i];
        if (c == '"') body += "\\\"";
        else if (c == '\\') body += "\\\\";
        else if (c == '\n') body += " ";
        else if (c == '\r') continue;
        else body += c;
    }
    body += "\",\"voice\":\"shimmer\",\"response_format\":\"pcm\"}";
    
    // Send request
    client.print("POST /v1/audio/speech HTTP/1.1\r\n");
    client.print("Host: api.openai.com\r\n");
    client.print("Authorization: Bearer ");
    client.print(api_key);
    client.print("\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: ");
    client.print(body.length());
    client.print("\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(body);
    
    Serial.println("OpenAI TTS: Waiting for response...");
    
    // Wait for response headers
    unsigned long start = millis();
    while (!client.available() && millis() - start < 30000) {
        delay(10);
    }
    
    if (!client.available()) {
        strcpy(last_error, "TTS response timeout");
        client.stop();
        speaking = false;
        return NULL;
    }
    
    // Read headers
    int httpCode = 0;
    int contentLength = 0;
    bool chunked = false;
    while (client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.1")) {
            httpCode = line.substring(9, 12).toInt();
        }
        if (line.startsWith("content-length:") || line.startsWith("Content-Length:")) {
            contentLength = line.substring(16).toInt();
        }
        if (line.indexOf("chunked") >= 0) {
            chunked = true;
        }
        if (line.length() == 0) break;  // Empty line = end of headers
    }
    
    Serial.printf("OpenAI TTS: HTTP %d, Content-Length: %d, Chunked: %d\n", httpCode, contentLength, chunked);
    

    
    if (httpCode != 200) {
        String error = client.readString();
        snprintf(last_error, sizeof(last_error), "TTS HTTP %d: %s", httpCode, error.c_str());
        client.stop();
        speaking = false;
        return NULL;
    }
    
    // Allocate buffer in PSRAM for audio data
    // PCM format is 24kHz 16-bit mono = 48KB/sec, ~30 seconds max
    size_t bufferSize = contentLength > 0 ? contentLength : 1536 * 1024;  // 1.5MB max if unknown
    uint8_t* audioBuffer = (uint8_t*)heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM);
    if (!audioBuffer) {
        strcpy(last_error, "Failed to allocate audio buffer");
        client.stop();
        speaking = false;
        return NULL;
    }
    
    // Read audio data (handle chunked encoding if present)
    size_t totalRead = 0;
    
    if (chunked) {
        // Chunked transfer encoding: read chunk size, then chunk data, repeat
        while (client.connected() && totalRead < bufferSize) {
            // Read chunk size line
            String chunkLine = client.readStringUntil('\n');
            chunkLine.trim();
            if (chunkLine.length() == 0) continue;
            
            size_t chunkSize = strtoul(chunkLine.c_str(), NULL, 16);
            if (chunkSize == 0) break;  // Final chunk
            
            // Read chunk data
            size_t chunkRead = 0;
            while (chunkRead < chunkSize && totalRead < bufferSize) {
                if (client.available()) {
                    int toRead = min((size_t)4096, min(chunkSize - chunkRead, bufferSize - totalRead));
                    int bytesRead = client.read(audioBuffer + totalRead, toRead);
                    if (bytesRead > 0) {
                        totalRead += bytesRead;
                        chunkRead += bytesRead;
                    }
                }
                if (millis() - start > 60000) break;
                yield();
            }
            
            // Read trailing \r\n after chunk data
            client.readStringUntil('\n');
            
            if (millis() - start > 60000) break;
        }
    } else {
        // Content-Length mode: just read all data
        while ((client.available() || client.connected()) && totalRead < bufferSize) {
            if (client.available()) {
                int toRead = min((size_t)4096, bufferSize - totalRead);
                int bytesRead = client.read(audioBuffer + totalRead, toRead);
                if (bytesRead > 0) {
                    totalRead += bytesRead;
                }
            }
            if (millis() - start > 60000) break;  // 60 second total timeout
            yield();
        }
    }
    
    client.stop();
    speaking = false;
    
    if (totalRead == 0) {
        strcpy(last_error, "No audio data received");
        heap_caps_free(audioBuffer);
        return NULL;
    }
    
    Serial.printf("OpenAI TTS: Received %u bytes of PCM audio\n", totalRead);
    *out_size = totalRead;
    return audioBuffer;
}

bool openai_is_transcribing() {
    return transcribing;
}

char* openai_transcribe(const uint8_t* audio_data, size_t audio_size) {
    if (transcribing) {
        strcpy(last_error, "Transcription already in progress");
        return NULL;
    }
    
    if (!openai_has_api_key()) {
        strcpy(last_error, "No API key configured");
        return NULL;
    }
    
    if (!audio_data || audio_size == 0) {
        strcpy(last_error, "No audio data");
        return NULL;
    }
    
    transcribing = true;
    Serial.printf("OpenAI: Transcribing %u bytes...\n", audio_size);
    
    WiFiClientSecure client;
    client.setInsecure();  // Skip cert verification (for simplicity)
    client.setTimeout(30);  // 30 second timeout
    
    // Build multipart form data
    String boundary = "----ESP32Boundary" + String(millis());
    
    // Calculate content length
    String part1 = "--" + boundary + "\r\n"
                   "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
                   "Content-Type: audio/wav\r\n\r\n";
    String part2 = "\r\n--" + boundary + "\r\n"
                   "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
                   "whisper-1\r\n"
                   "--" + boundary + "--\r\n";
    
    size_t wav_size = 44 + audio_size;
    size_t content_length = part1.length() + wav_size + part2.length();
    
    // Connect to OpenAI
    Serial.println("OpenAI: Connecting to api.openai.com:443...");
    int retries = 3;
    while (retries > 0 && !client.connect("api.openai.com", 443)) {
        Serial.printf("OpenAI: Connection attempt failed, %d retries left\n", retries - 1);
        retries--;
        delay(1000);
    }
    if (!client.connected()) {
        strcpy(last_error, "Connection failed after retries");
        transcribing = false;
        return NULL;
    }
    Serial.println("OpenAI: Connected!");
    
    // Send headers manually
    client.print("POST /v1/audio/transcriptions HTTP/1.1\r\n");
    client.print("Host: api.openai.com\r\n");
    client.print("Authorization: Bearer ");
    client.print(api_key);
    client.print("\r\n");
    client.print("Content-Type: multipart/form-data; boundary=");
    client.print(boundary);
    client.print("\r\n");
    client.print("Content-Length: ");
    client.print(content_length);
    client.print("\r\n");
    client.print("Connection: close\r\n\r\n");
    
    // Send part 1 (headers for file)
    client.print(part1);
    
    // Send WAV header
    uint8_t wav_header[44];
    build_wav_header(wav_header, audio_size);
    client.write(wav_header, 44);
    
    // Send audio data in chunks
    size_t sent = 0;
    const size_t chunk_size = 4096;
    while (sent < audio_size) {
        size_t to_send = min(chunk_size, audio_size - sent);
        size_t written = client.write(audio_data + sent, to_send);
        if (written == 0) {
            strcpy(last_error, "Write failed");
            client.stop();
            transcribing = false;
            return NULL;
        }
        sent += written;
        yield();  // Let WiFi stack breathe
    }
    
    // Send part 2 (model field + boundary end)
    client.print(part2);
    
    Serial.println("OpenAI: Waiting for response...");
    
    // Read response
    unsigned long start = millis();
    while (!client.available() && millis() - start < 30000) {
        delay(100);
    }
    
    if (!client.available()) {
        strcpy(last_error, "Response timeout");
        client.stop();
        transcribing = false;
        return NULL;
    }
    
    // Skip HTTP headers
    while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
        
        // Check for error status
        if (line.startsWith("HTTP/1.1")) {
            int code = line.substring(9, 12).toInt();
            if (code != 200) {
                snprintf(last_error, sizeof(last_error), "HTTP %d", code);
                // Continue to read error body
            }
        }
    }
    
    // Read response body
    String response = client.readString();
    client.stop();
    
    Serial.printf("OpenAI: Response: %s\n", response.c_str());
    
    // Parse JSON response - look for "text" field
    int textStart = response.indexOf("\"text\"");
    if (textStart < 0) {
        // Check for error
        int errorStart = response.indexOf("\"error\"");
        if (errorStart >= 0) {
            int msgStart = response.indexOf("\"message\"", errorStart);
            if (msgStart >= 0) {
                int colonPos = response.indexOf(":", msgStart);
                int quoteStart = response.indexOf("\"", colonPos);
                int quoteEnd = response.indexOf("\"", quoteStart + 1);
                if (quoteStart >= 0 && quoteEnd > quoteStart) {
                    String errMsg = response.substring(quoteStart + 1, quoteEnd);
                    strncpy(last_error, errMsg.c_str(), sizeof(last_error) - 1);
                }
            }
        } else {
            strcpy(last_error, "No transcript in response");
        }
        transcribing = false;
        return NULL;
    }
    
    // Extract text value
    int colonPos = response.indexOf(":", textStart);
    int quoteStart = response.indexOf("\"", colonPos);
    int quoteEnd = response.indexOf("\"", quoteStart + 1);
    
    if (quoteStart < 0 || quoteEnd <= quoteStart) {
        strcpy(last_error, "Failed to parse response");
        transcribing = false;
        return NULL;
    }
    
    String text = response.substring(quoteStart + 1, quoteEnd);
    
    // Allocate and return result
    char* result = (char*)malloc(text.length() + 1);
    if (result) {
        strcpy(result, text.c_str());
        Serial.printf("OpenAI: Transcript: %s\n", result);
    }
    
    transcribing = false;
    return result;
}

// ============== OpenClaw Integration ==============

static char openclaw_endpoint[128] = {0};
static char openclaw_token[128] = {0};
static const char* PREF_OPENCLAW_KEY = "openclaw_url";
static const char* PREF_OPENCLAW_TOKEN = "openclaw_tok";

void openclaw_set_endpoint(const char* url) {
    strncpy(openclaw_endpoint, url, sizeof(openclaw_endpoint) - 1);
    openclaw_endpoint[sizeof(openclaw_endpoint) - 1] = '\0';
    
    // Remove trailing slash if present
    size_t len = strlen(openclaw_endpoint);
    if (len > 0 && openclaw_endpoint[len - 1] == '/') {
        openclaw_endpoint[len - 1] = '\0';
    }
    
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString(PREF_OPENCLAW_KEY, openclaw_endpoint);
    prefs.end();
    
    Serial.printf("OpenClaw: Endpoint saved: %s\n", openclaw_endpoint);
}

void openclaw_set_token(const char* token) {
    strncpy(openclaw_token, token, sizeof(openclaw_token) - 1);
    openclaw_token[sizeof(openclaw_token) - 1] = '\0';
    
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString(PREF_OPENCLAW_TOKEN, openclaw_token);
    prefs.end();
    
    Serial.println("OpenClaw: Token saved");
}

bool openclaw_has_endpoint() {
    return strlen(openclaw_endpoint) > 5;
}

bool openclaw_has_token() {
    return strlen(openclaw_token) > 5;
}

const char* openclaw_get_endpoint() {
    return openclaw_endpoint;
}

const char* openclaw_get_token() {
    static char masked[32];
    if (!openclaw_has_token()) {
        return "(not set)";
    }
    size_t len = strlen(openclaw_token);
    if (len <= 8) {
        return "****";
    }
    snprintf(masked, sizeof(masked), "%.4s...%s", openclaw_token, openclaw_token + len - 4);
    return masked;
}

char* openclaw_send_message(const char* message) {
    if (!openclaw_has_endpoint()) {
        strcpy(last_error, "No OpenClaw endpoint configured");
        return NULL;
    }
    
    if (!openclaw_has_token()) {
        strcpy(last_error, "No OpenClaw token configured");
        return NULL;
    }
    
    if (!message || strlen(message) == 0) {
        strcpy(last_error, "Empty message");
        return NULL;
    }
    
    Serial.printf("OpenClaw: Sending message: %s\n", message);
    
    WiFiClientSecure client;
    client.setInsecure();  // Skip cert verification for local endpoints
    client.setTimeout(60);  // 60 second timeout for AI response
    
    // Parse host from endpoint URL
    String url = String(openclaw_endpoint);
    String host;
    int port = 443;
    
    if (url.startsWith("https://")) {
        url = url.substring(8);
    } else if (url.startsWith("http://")) {
        url = url.substring(7);
        port = 80;
    }
    
    int slashPos = url.indexOf('/');
    if (slashPos > 0) {
        host = url.substring(0, slashPos);
    } else {
        host = url;
    }
    
    int colonPos = host.indexOf(':');
    if (colonPos > 0) {
        port = host.substring(colonPos + 1).toInt();
        host = host.substring(0, colonPos);
    }
    
    Serial.printf("OpenClaw: Connecting to %s:%d\n", host.c_str(), port);
    
    if (!client.connect(host.c_str(), port)) {
        strcpy(last_error, "Connection to OpenClaw failed");
        return NULL;
    }
    
    // Build OpenAI Chat Completions format JSON body
    String body = "{\"model\":\"default\",\"messages\":[{\"role\":\"user\",\"content\":\"";
    // Escape the message for JSON
    for (size_t i = 0; i < strlen(message); i++) {
        char c = message[i];
        if (c == '"') body += "\\\"";
        else if (c == '\\') body += "\\\\";
        else if (c == '\n') body += "\\n";
        else if (c == '\r') body += "\\r";
        else body += c;
    }
    body += "\"}]}";
    
    // Send HTTP request to /v1/chat/completions
    client.print("POST /v1/chat/completions HTTP/1.1\r\n");
    client.print("Host: ");
    client.print(host);
    client.print("\r\n");
    client.print("Authorization: Bearer ");
    client.print(openclaw_token);  // Uses gateway token
    client.print("\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: ");
    client.print(body.length());
    client.print("\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(body);
    
    Serial.println("OpenClaw: Waiting for response...");
    Serial.printf("OpenClaw: Body: %s\n", body.c_str());
    
    // Wait for response
    unsigned long start = millis();
    while (!client.available() && millis() - start < 60000) {
        delay(100);
    }
    
    if (!client.available()) {
        strcpy(last_error, "OpenClaw response timeout");
        client.stop();
        return NULL;
    }
    
    // Read response
    String response = "";
    bool headersEnded = false;
    int httpCode = 0;
    while (client.available() || client.connected()) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (!headersEnded) {
                if (line.startsWith("HTTP/1.1")) {
                    httpCode = line.substring(9, 12).toInt();
                }
                if (line == "\r" || line == "") {
                    headersEnded = true;
                }
            } else {
                response += line;
            }
        }
        if (!client.available() && millis() - start > 60000) break;
    }
    client.stop();
    
    Serial.printf("OpenClaw: HTTP %d, Response: %s\n", httpCode, response.c_str());
    
    if (httpCode != 200) {
        snprintf(last_error, sizeof(last_error), "HTTP %d: %s", httpCode, response.c_str());
        return NULL;
    }
    
    // Parse OpenAI response format: {"choices":[{"message":{"content":"..."}}]}
    int contentStart = response.indexOf("\"content\"");
    if (contentStart < 0) {
        strcpy(last_error, "No content in response");
        return NULL;
    }
    
    int colonPos2 = response.indexOf(":", contentStart);
    int quoteStart = response.indexOf("\"", colonPos2 + 1);
    int quoteEnd = quoteStart + 1;
    
    // Find end quote, handling escaped quotes
    while (quoteEnd < (int)response.length()) {
        if (response[quoteEnd] == '"' && response[quoteEnd - 1] != '\\') {
            break;
        }
        quoteEnd++;
    }
    
    if (quoteStart < 0 || quoteEnd <= quoteStart) {
        strcpy(last_error, "Failed to parse response content");
        return NULL;
    }
    
    String content = response.substring(quoteStart + 1, quoteEnd);
    
    // Unescape JSON string
    content.replace("\\n", "\n");
    content.replace("\\\"", "\"");
    content.replace("\\\\", "\\");
    
    char* result = (char*)malloc(content.length() + 1);
    if (result) {
        strcpy(result, content.c_str());
        Serial.printf("OpenClaw: Parsed response: %s\n", result);
    }
    
    return result;
}

// Load OpenClaw settings from NVS (call in openai_init)
static void load_openclaw_endpoint() {
    prefs.begin(PREF_NAMESPACE, true);
    String url = prefs.getString(PREF_OPENCLAW_KEY, "");
    if (url.length() > 0) {
        strncpy(openclaw_endpoint, url.c_str(), sizeof(openclaw_endpoint) - 1);
        Serial.printf("OpenClaw: Endpoint loaded: %s\n", openclaw_endpoint);
    }
    String token = prefs.getString(PREF_OPENCLAW_TOKEN, "");
    if (token.length() > 0) {
        strncpy(openclaw_token, token.c_str(), sizeof(openclaw_token) - 1);
        Serial.println("OpenClaw: Token loaded from NVS");
    }
    prefs.end();
}

char* openclaw_send_with_history(const char* message) {
    if (!openclaw_has_endpoint()) {
        strcpy(last_error, "No OpenClaw endpoint configured");
        return NULL;
    }
    
    if (!openclaw_has_token()) {
        strcpy(last_error, "No OpenClaw token configured");
        return NULL;
    }
    
    if (!message || strlen(message) == 0) {
        strcpy(last_error, "Empty message");
        return NULL;
    }
    
    // Add user message to conversation history
    conversation_add_message("user", message);
    
    Serial.printf("OpenClaw: Sending with history (%d messages)\n", conversation_get_count());
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(60);
    
    // Parse host from endpoint URL
    String url = String(openclaw_endpoint);
    String host;
    int port = 443;
    
    if (url.startsWith("https://")) {
        url = url.substring(8);
    } else if (url.startsWith("http://")) {
        url = url.substring(7);
        port = 80;
    }
    
    int slashPos = url.indexOf('/');
    if (slashPos > 0) {
        host = url.substring(0, slashPos);
    } else {
        host = url;
    }
    
    int colonPos = host.indexOf(':');
    if (colonPos > 0) {
        port = host.substring(colonPos + 1).toInt();
        host = host.substring(0, colonPos);
    }
    
    Serial.printf("OpenClaw: Connecting to %s:%d\n", host.c_str(), port);
    
    if (!client.connect(host.c_str(), port)) {
        strcpy(last_error, "Connection to OpenClaw failed");
        return NULL;
    }
    
    // Build body with full conversation history
    char* messages_json = conversation_get_messages_json();
    if (!messages_json) {
        strcpy(last_error, "Failed to build messages JSON");
        client.stop();
        return NULL;
    }
    
    String body = "{\"model\":\"default\",\"messages\":";
    body += messages_json;
    body += "}";
    free(messages_json);
    
    Serial.printf("OpenClaw: Body length: %d\n", body.length());
    
    // Send HTTP request
    client.print("POST /v1/chat/completions HTTP/1.1\r\n");
    client.print("Host: ");
    client.print(host);
    client.print("\r\n");
    client.print("Authorization: Bearer ");
    client.print(openclaw_token);
    client.print("\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: ");
    client.print(body.length());
    client.print("\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(body);
    
    Serial.println("OpenClaw: Waiting for response...");
    
    // Wait for response
    unsigned long start = millis();
    while (!client.available() && millis() - start < 60000) {
        delay(100);
    }
    
    if (!client.available()) {
        strcpy(last_error, "OpenClaw response timeout");
        client.stop();
        return NULL;
    }
    
    // Read response
    String response = "";
    bool headersEnded = false;
    int httpCode = 0;
    while (client.available() || client.connected()) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (!headersEnded) {
                if (line.startsWith("HTTP/1.1")) {
                    httpCode = line.substring(9, 12).toInt();
                }
                if (line == "\r" || line == "") {
                    headersEnded = true;
                }
            } else {
                response += line;
            }
        }
        if (!client.available() && millis() - start > 60000) break;
    }
    client.stop();
    
    Serial.printf("OpenClaw: HTTP %d\n", httpCode);
    
    if (httpCode != 200) {
        snprintf(last_error, sizeof(last_error), "HTTP %d: %s", httpCode, response.c_str());
        return NULL;
    }
    
    // Parse response
    int contentStart = response.indexOf("\"content\"");
    if (contentStart < 0) {
        strcpy(last_error, "No content in response");
        return NULL;
    }
    
    int colonPos2 = response.indexOf(":", contentStart);
    int quoteStart = response.indexOf("\"", colonPos2 + 1);
    int quoteEnd = quoteStart + 1;
    
    while (quoteEnd < (int)response.length()) {
        if (response[quoteEnd] == '"' && response[quoteEnd - 1] != '\\') {
            break;
        }
        quoteEnd++;
    }
    
    if (quoteStart < 0 || quoteEnd <= quoteStart) {
        strcpy(last_error, "Failed to parse response content");
        return NULL;
    }
    
    String content = response.substring(quoteStart + 1, quoteEnd);
    content.replace("\\n", "\n");
    content.replace("\\\"", "\"");
    content.replace("\\\\", "\\");
    
    // Add assistant response to conversation history
    conversation_add_message("assistant", content.c_str());
    
    char* result = (char*)malloc(content.length() + 1);
    if (result) {
        strcpy(result, content.c_str());
        Serial.printf("OpenClaw: Response: %s\n", result);
    }
    
    return result;
}
