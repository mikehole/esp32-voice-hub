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
#include "wedge_ui.h"
#include "encoder.h"
#include "update_checker.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

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
// NOTE: Recording audio does NOT have mic gain applied (unlike wakeword feed)
// Raw mic noise floor ~200-400, speech ~1000-3000
#define SILENCE_THRESHOLD 800             // Avg amplitude (no gain applied to recording!)
#define SILENCE_COUNT_STOP 40             // Stop after ~1.2s of continuous silence

// Streaming audio state
typedef enum {
    STREAM_IDLE,
    STREAM_BUFFERING,
    STREAM_PLAYING
} stream_state_t;

static stream_state_t stream_state = STREAM_IDLE;
static uint8_t *stream_buffer = NULL;
static size_t stream_buffer_size = 0;
static size_t stream_write_pos = 0;
static uint32_t stream_sample_rate = 24000;
#define STREAM_BUFFER_SIZE (24000 * 2 * 5)  // 5 seconds max at 24kHz 16-bit
#define STREAM_PLAYBACK_THRESHOLD (24000 * 2 / 20)  // Start after 50ms buffered (2400 bytes)

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
                
                // Parse JSON message
                char *json_str = strndup((char*)data->data_ptr, data->data_len);
                if (json_str) {
                    cJSON *json = cJSON_Parse(json_str);
                    if (json) {
                        cJSON *type = cJSON_GetObjectItem(json, "type");
                        if (type && cJSON_IsString(type)) {
                            if (strcmp(type->valuestring, "audio_stream_start") == 0) {
                                // Start streaming audio
                                cJSON *rate = cJSON_GetObjectItem(json, "sampleRate");
                                stream_sample_rate = (rate && cJSON_IsNumber(rate)) ? rate->valueint : 24000;
                                
                                // Allocate stream buffer in PSRAM
                                if (!stream_buffer) {
                                    stream_buffer = heap_caps_malloc(STREAM_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
                                }
                                if (stream_buffer) {
                                    stream_write_pos = 0;
                                    stream_state = STREAM_BUFFERING;
                                    display_set_state(DISPLAY_STATE_SPEAKING);
                                    ESP_LOGI(TAG, "Audio stream started @ %lu Hz", stream_sample_rate);
                                } else {
                                    ESP_LOGE(TAG, "Failed to allocate stream buffer");
                                }
                            } else if (strcmp(type->valuestring, "audio_stream_end") == 0) {
                                // End of stream - play the full buffer now
                                if (stream_state != STREAM_IDLE && stream_buffer && stream_write_pos > 0) {
                                    ESP_LOGI(TAG, "Audio stream ended, playing %u bytes @ %lu Hz", stream_write_pos, stream_sample_rate);
                                    audio_play(stream_buffer, stream_write_pos, stream_sample_rate, false);
                                    stream_state = STREAM_PLAYING;
                                    // Display stays in SPEAKING until playback done
                                    // TODO: Add callback from audio module when playback finishes
                                } else {
                                    stream_state = STREAM_IDLE;
                                    display_set_state(DISPLAY_STATE_IDLE);
                                }
                            }
                        }
                        cJSON_Delete(json);
                    }
                    free(json_str);
                }
            } else if (data->op_code == 0x02) {  // Binary frame
                // Streaming audio data - just buffer it, play on stream_end
                if (stream_state != STREAM_IDLE && stream_buffer) {
                    size_t space = STREAM_BUFFER_SIZE - stream_write_pos;
                    size_t to_copy = (data->data_len < space) ? data->data_len : space;
                    
                    if (to_copy > 0) {
                        memcpy(stream_buffer + stream_write_pos, data->data_ptr, to_copy);
                        stream_write_pos += to_copy;
                    }
                    
                    if (to_copy < (size_t)data->data_len) {
                        ESP_LOGW(TAG, "Stream buffer full, dropped %d bytes", data->data_len - to_copy);
                    }
                }
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
    
    // Debug: log energy periodically
    static int check_count = 0;
    if (++check_count % 50 == 0) {
        ESP_LOGI(TAG, "VAD energy: %ld (threshold: %d)", (long)energy, SILENCE_THRESHOLD);
    }
    
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

// Update check callback
static void on_update_check_complete(bool available, const char* version)
{
    ESP_LOGI(TAG, "Update check complete: %s (version: %s)", 
             available ? "update available" : "up to date",
             version ? version : "unknown");
    
    wedge_ui_set_update_available(available);
    
    // Resume wakeword if it was running
    if (wakeword_mode && !wakeword_is_running()) {
        wakeword_start();
    }
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
// Screen geometry for touch detection
#define SCREEN_SIZE     360
#define CENTER_X        (SCREEN_SIZE / 2)
#define CENTER_Y        (SCREEN_SIZE / 2)
#define CENTER_RADIUS   65   // Touch within this = center tap
#define OUTER_RADIUS    165
#define INNER_RADIUS    85

// Check if touch is in center circle
static bool is_center_touch(uint16_t x, uint16_t y)
{
    // Invert coordinates to match display orientation
    x = SCREEN_SIZE - x;
    y = SCREEN_SIZE - y;
    
    int dx = x - CENTER_X;
    int dy = y - CENTER_Y;
    float distance = sqrtf(dx * dx + dy * dy);
    
    return distance < CENTER_RADIUS;
}

// Calculate which wedge was touched (-1 if not in wedge ring)
static int get_touched_wedge(uint16_t x, uint16_t y)
{
    // Invert coordinates to match display orientation
    x = SCREEN_SIZE - x;
    y = SCREEN_SIZE - y;
    
    int dx = x - CENTER_X;
    int dy = y - CENTER_Y;
    float distance = sqrtf(dx * dx + dy * dy);
    
    // Check if touch is in the wedge ring
    if (distance < INNER_RADIUS || distance > OUTER_RADIUS) {
        return -1;
    }
    
    // Calculate angle (0 = right, counter-clockwise)
    float angle = atan2f(dy, dx) * 180.0f / M_PI;
    
    // Convert to our coordinate system (0 = top, clockwise)
    angle = angle + 90;
    if (angle < 0) angle += 360;
    
    // Calculate wedge index (each wedge is 45 degrees)
    return (int)(angle / 45.0f) % 8;
}

static void touch_task(void *arg)
{
    bool was_pressed = false;
    
    while (1) {
        uint16_t x, y;
        bool pressed = touch_read(&x, &y);
        
        // Detect new press (touch down)
        if (pressed && !was_pressed) {
            // Check what was touched
            if (is_center_touch(x, y)) {
                // Center tap - let wedge_ui handle the action
                wedge_action_t action = wedge_ui_center_tap();
                
                switch (action) {
                    case ACTION_VOICE_START:
                        if (!is_recording) {
                            ESP_LOGI(TAG, "Center tap - starting recording");
                            if (wakeword_mode) {
                                wakeword_stop();
                            }
                            start_recording("touch");
                        }
                        break;
                    case ACTION_OTA_MODE:
                        // Pause wakeword for OTA
                        ESP_LOGI(TAG, "Entering OTA mode - pausing wakeword");
                        if (wakeword_mode) {
                            wakeword_stop();
                            wakeword_mode = false;
                        }
                        break;
                    case ACTION_TOGGLE_WAKEWORD:
                        // Toggle wake word detection
                        if (wakeword_mode) {
                            ESP_LOGI(TAG, "Disabling wakeword");
                            wakeword_stop();
                            wakeword_mode = false;
                        } else {
                            ESP_LOGI(TAG, "Enabling wakeword");
                            wakeword_start();
                            wakeword_mode = true;
                        }
                        break;
                    case ACTION_OTA_CHECK:
                        // Check for updates in background
                        ESP_LOGI(TAG, "Checking for OTA updates...");
                        if (wakeword_mode) {
                            wakeword_stop();  // Pause during check
                        }
                        update_checker_check(on_update_check_complete);
                        break;
                    case ACTION_OTA_INSTALL:
                        // Install the update
                        ESP_LOGI(TAG, "Installing OTA update...");
                        if (wakeword_mode) {
                            wakeword_stop();
                        }
                        update_checker_install();  // Will restart on success
                        break;
                    default:
                        // ACTION_NONE, ACTION_SUBMENU, ACTION_BACK - UI handled it
                        break;
                }
            } else {
                // Check for wedge touch
                int wedge = get_touched_wedge(x, y);
                if (wedge >= 0) {
                    ESP_LOGI(TAG, "Wedge %d touched", wedge);
                    wedge_ui_set_selection(wedge);
                }
            }
        }
        
        // Detect release (touch up) - stops touch-triggered recording
        if (!pressed && was_pressed && is_recording) {
            ESP_LOGI(TAG, "Touch released - stopping recording");
            stop_recording();
        }
        
        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Encoder polling task - runs at controlled rate to allow screen to render
static void encoder_task(void *arg)
{
    while (1) {
        // Wait for screen to be ready (fixed interval)
        vTaskDelay(pdMS_TO_TICKS(80));  // ~12 updates/sec max
        
        // Get accumulated delta
        int delta = encoder_get_delta();
        if (delta != 0) {
            // Check if we're in adjustment mode
            if (wedge_ui_is_adjusting()) {
                wedge_ui_adjust_value(delta * 5);  // 5% per click
            } else {
                // Normal mode - rotate wedge selection
                // Clamp to single step to avoid jumping too far
                wedge_ui_rotate(delta > 0 ? 1 : -1);
            }
        }
    }
}

void voice_client_init(void)
{
    ESP_LOGI(TAG, "Voice client initializing...");
    
    record_mutex = xSemaphoreCreateMutex();
    
    // Initialize touch
    touch_init();
    
    // Initialize encoder
    encoder_init();
    
    // Start encoder polling task (controlled rate for smooth rendering)
    xTaskCreate(encoder_task, "encoder", 2048, NULL, 3, NULL);
    
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
