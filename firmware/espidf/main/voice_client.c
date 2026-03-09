/**
 * Voice Client - WebSocket connection to OpenClaw plugin
 * 
 * Supports two modes:
 * 1. Tap-to-talk: Touch to start, release to stop
 * 2. Wake word: "Hi ESP" to start, silence to stop (VAD)
 */

#include "voice_client.h"
#include "touch.h"
#include "audio.h"
#include "display.h"
#include "wifi_manager.h"
#include "wakeword.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_heap_caps.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "voice";

// Configuration
static char ws_uri[128] = {0};
static bool configured = false;
static bool wakeword_mode = true;  // true = wake word, false = tap only

// WebSocket client
static esp_websocket_client_handle_t ws_client = NULL;
static volatile bool ws_connected = false;

// Recording state
static volatile bool is_recording = false;
static volatile bool stop_requested = false;
static uint8_t *record_buffer = NULL;
static size_t record_size = 0;
static size_t record_pos = 0;
static SemaphoreHandle_t record_mutex = NULL;

#define MAX_RECORD_SIZE (16000 * 2 * 10)  // 10 seconds max at 16kHz 16-bit
#define SILENCE_THRESHOLD 500             // ~31ms of silence at 16kHz
#define SILENCE_COUNT_STOP 20             // Stop after ~600ms of silence

// Forward declarations
static void start_recording(const char *trigger);
static void stop_recording(void);

// WebSocket event handler
static void ws_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            ws_connected = true;
            if (wakeword_mode) {
                display_set_state(DISPLAY_STATE_IDLE);
            } else {
                display_set_state(DISPLAY_STATE_IDLE);
            }
            break;
            
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected");
            ws_connected = false;
            display_set_state(DISPLAY_STATE_CONNECTING);
            break;
            
        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x01) {  // Text frame
                ESP_LOGI(TAG, "Received: %.*s", data->data_len, (char*)data->data_ptr);
            }
            break;
            
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            break;
    }
}

// Check if audio chunk is silence (simple energy-based VAD)
static bool is_silence(const int16_t *samples, size_t count)
{
    int32_t energy = 0;
    for (size_t i = 0; i < count; i++) {
        energy += abs(samples[i]);
    }
    energy /= count;
    return energy < SILENCE_THRESHOLD;
}

// Recording task - handles both tap and wake word modes
static void recording_task(void *arg)
{
    const char *trigger = (const char *)arg;
    ESP_LOGI(TAG, "Recording started (trigger: %s)", trigger);
    display_set_state(DISPLAY_STATE_LISTENING);
    
    // Allocate buffer in PSRAM
    record_buffer = heap_caps_malloc(MAX_RECORD_SIZE, MALLOC_CAP_SPIRAM);
    if (!record_buffer) {
        ESP_LOGE(TAG, "Failed to allocate recording buffer");
        is_recording = false;
        vTaskDelete(NULL);
        return;
    }
    
    record_pos = 0;
    int silence_count = 0;
    bool use_vad = (strcmp(trigger, "wakeword") == 0);  // VAD only for wake word
    
    audio_start_recording(MAX_RECORD_SIZE);
    
    // Record until stopped or silence detected
    while (is_recording && !stop_requested && record_pos < MAX_RECORD_SIZE) {
        size_t chunk = audio_record_chunk(record_buffer + record_pos, 
                                          MIN(4096, MAX_RECORD_SIZE - record_pos));
        if (chunk > 0) {
            // VAD: check for silence (wake word mode only)
            if (use_vad && record_pos > 16000) {  // After 0.5s of audio
                if (is_silence((int16_t *)(record_buffer + record_pos), chunk / 2)) {
                    silence_count++;
                    if (silence_count >= SILENCE_COUNT_STOP) {
                        ESP_LOGI(TAG, "Silence detected - stopping");
                        break;
                    }
                } else {
                    silence_count = 0;
                }
            }
            record_pos += chunk;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    audio_stop_recording();
    record_size = record_pos;
    
    ESP_LOGI(TAG, "Recording stopped: %u bytes (%.1fs)", record_size, record_size / 32000.0f);
    
    // Send to WebSocket if connected
    if (ws_connected && record_size > 0) {
        display_set_state(DISPLAY_STATE_THINKING);
        
        ESP_LOGI(TAG, "Sending audio to plugin...");
        
        // Send audio_start message
        const char *start_msg = "{\"type\":\"audio_start\",\"sampleRate\":16000}";
        esp_websocket_client_send_text(ws_client, start_msg, strlen(start_msg), pdMS_TO_TICKS(5000));
        
        // Send audio data in chunks
        size_t chunk_size = 4096;
        size_t sent_total = 0;
        while (sent_total < record_size) {
            size_t to_send = MIN(chunk_size, record_size - sent_total);
            int sent = esp_websocket_client_send_bin(ws_client, 
                                                      (const char*)(record_buffer + sent_total), 
                                                      to_send, pdMS_TO_TICKS(5000));
            if (sent < 0) {
                ESP_LOGE(TAG, "Failed to send chunk at offset %u", sent_total);
                break;
            }
            sent_total += to_send;
        }
        
        // Send audio_end message
        const char *end_msg = "{\"type\":\"audio_end\"}";
        esp_websocket_client_send_text(ws_client, end_msg, strlen(end_msg), pdMS_TO_TICKS(5000));
        
        ESP_LOGI(TAG, "Sent %u bytes in chunks", sent_total);
    } else {
        ESP_LOGW(TAG, "Not connected or no audio");
        display_set_state(DISPLAY_STATE_IDLE);
    }
    
    // Free buffer
    if (record_buffer) {
        free(record_buffer);
        record_buffer = NULL;
    }
    
    is_recording = false;
    stop_requested = false;
    vTaskDelete(NULL);
}

static void start_recording(const char *trigger)
{
    if (is_recording) {
        ESP_LOGW(TAG, "Already recording");
        return;
    }
    is_recording = true;
    stop_requested = false;
    xTaskCreate(recording_task, "record", 8192, (void *)trigger, 5, NULL);
}

static void stop_recording(void)
{
    stop_requested = true;
}

// Wake word callback
static void on_wakeword_detected(void)
{
    ESP_LOGI(TAG, "*** WAKE WORD! Starting recording ***");
    
    // Stop wake word detection while recording
    wakeword_stop();
    
    // Start recording with VAD
    start_recording("wakeword");
    
    // Wait for recording to complete, then restart wake word
    while (is_recording) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Small delay then restart wake word detection
    vTaskDelay(pdMS_TO_TICKS(500));
    if (wakeword_mode && ws_connected) {
        wakeword_start();
    }
}

// Touch monitoring task
static void touch_task(void *arg)
{
    bool was_pressed = false;
    
    while (1) {
        bool pressed = touch_is_pressed();
        
        // Detect press (touch down)
        if (pressed && !was_pressed && !is_recording) {
            ESP_LOGI(TAG, "Touch detected - starting recording");
            if (wakeword_mode) {
                wakeword_stop();  // Pause wake word while touch recording
            }
            start_recording("touch");
        }
        
        // Detect release (touch up) - only stops touch-triggered recording
        if (!pressed && was_pressed && is_recording) {
            ESP_LOGI(TAG, "Touch released - stopping recording");
            stop_recording();
        }
        
        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void voice_client_init(void)
{
    ESP_LOGI(TAG, "Voice client initializing...");
    
    record_mutex = xSemaphoreCreateMutex();
    
    // Initialize touch
    touch_init();
    
    // Start touch monitoring (always available)
    xTaskCreate(touch_task, "touch", 4096, NULL, 3, NULL);
    
    // Initialize wake word detection
    if (wakeword_mode) {
        if (wakeword_init()) {
            wakeword_set_callback(on_wakeword_detected);
            ESP_LOGI(TAG, "Wake word detection ready");
        } else {
            ESP_LOGW(TAG, "Wake word init failed - tap-to-talk only");
            wakeword_mode = false;
        }
    }
    
    ESP_LOGI(TAG, "Voice client ready (mode: %s)", wakeword_mode ? "wake word + tap" : "tap only");
}

void voice_client_connect(const char *uri)
{
    if (ws_client) {
        esp_websocket_client_destroy(ws_client);
        ws_client = NULL;
    }
    
    strncpy(ws_uri, uri, sizeof(ws_uri) - 1);
    configured = true;
    
    ESP_LOGI(TAG, "Connecting to: %s", ws_uri);
    display_set_state(DISPLAY_STATE_CONNECTING);
    
    esp_websocket_client_config_t ws_cfg = {
        .uri = ws_uri,
        .buffer_size = 8192,
    };
    
    ws_client = esp_websocket_client_init(&ws_cfg);
    if (!ws_client) {
        ESP_LOGE(TAG, "Failed to create WebSocket client");
        return;
    }
    
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    esp_websocket_client_start(ws_client);
    
    // Start wake word detection once connected
    if (wakeword_mode) {
        // Will be started after WebSocket connects
    }
}

// Called when WebSocket connects
void voice_client_on_connected(void)
{
    if (wakeword_mode) {
        ESP_LOGI(TAG, "Starting wake word detection...");
        wakeword_start();
    }
}

void voice_client_disconnect(void)
{
    if (wakeword_mode) {
        wakeword_stop();
    }
    
    if (ws_client) {
        esp_websocket_client_stop(ws_client);
        esp_websocket_client_destroy(ws_client);
        ws_client = NULL;
        ws_connected = false;
    }
}

bool voice_client_is_connected(void)
{
    return ws_connected;
}
