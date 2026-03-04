/**
 * OpenAI API Client
 * Whisper transcription via multipart form POST
 */

#include "openai_client.h"
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
