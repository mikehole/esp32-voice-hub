/**
 * Wake Word Detection - ESP-SR WakeNet
 * 
 * Continuously listens for wake word, then triggers recording.
 * Uses "Hi ESP" built-in model initially (can train custom later).
 */

#include "wakeword.h"
#include "audio.h"
#include "display.h"
#include "voice_client.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_config.h"
#include "model_path.h"

static const char *TAG = "wakeword";

// AFE (Audio Front-End) handle
static esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t *afe_data = NULL;

// Wake word state
static volatile bool wakeword_enabled = false;
static volatile bool wakeword_detected = false;
static TaskHandle_t wakeword_task_handle = NULL;
static TaskHandle_t feed_task_handle = NULL;

// Audio buffer for AFE feed
static int feed_chunk_size = 0;  // Set from AFE after init
static int16_t *feed_buffer = NULL;

// Callback when wake word detected
static wakeword_callback_t wakeword_callback = NULL;

// Feed task - continuously sends audio to AFE
static void feed_task(void *arg)
{
    ESP_LOGI(TAG, "Feed task started, chunk size: %d samples (%d bytes)", 
             feed_chunk_size, feed_chunk_size * sizeof(int16_t));
    
    size_t bytes_to_read = feed_chunk_size * sizeof(int16_t);
    int feed_count = 0;
    int32_t max_amplitude = 0;
    
    while (wakeword_enabled) {
        // Read from mic - blocks until we have enough data
        size_t bytes_read = audio_record_chunk((uint8_t *)feed_buffer, bytes_to_read);
        
        if (bytes_read == bytes_to_read) {
            // Check audio levels periodically
            feed_count++;
            if (feed_count % 100 == 0) {
                // Find max amplitude in this chunk
                max_amplitude = 0;
                for (int i = 0; i < feed_chunk_size; i++) {
                    int16_t sample = feed_buffer[i];
                    if (sample < 0) sample = -sample;
                    if (sample > max_amplitude) max_amplitude = sample;
                }
                ESP_LOGI(TAG, "Feed #%d, max amplitude: %ld", feed_count, (long)max_amplitude);
            }
            
            // Feed to AFE
            afe_handle->feed(afe_data, feed_buffer);
        } else if (bytes_read > 0) {
            ESP_LOGW(TAG, "Partial read: %d/%d bytes", bytes_read, bytes_to_read);
        } else {
            ESP_LOGW(TAG, "No audio data read");
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        // Small yield to prevent watchdog
        taskYIELD();
    }
    
    ESP_LOGI(TAG, "Feed task stopped");
    vTaskDelete(NULL);
}

// Detection task - processes AFE output and detects wake word
static void detect_task(void *arg)
{
    ESP_LOGI(TAG, "Detection task started");
    
    while (wakeword_enabled) {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        
        if (res && res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "*** WAKE WORD DETECTED! ***");
            wakeword_detected = true;
            
            // Notify via callback
            if (wakeword_callback) {
                wakeword_callback();
            }
            
            // Brief pause before listening again
            vTaskDelay(pdMS_TO_TICKS(500));
            wakeword_detected = false;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "Detection task stopped");
    vTaskDelete(NULL);
}

bool wakeword_init(void)
{
    ESP_LOGI(TAG, "Initializing wake word detection...");
    
    // Load models from the "model" partition (packed binary format)
    ESP_LOGI(TAG, "Loading SR models from partition...");
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models || models->num == 0) {
        ESP_LOGE(TAG, "Failed to load SR models - check if partition is flashed");
        return false;
    }
    ESP_LOGI(TAG, "Loaded %d SR models", models->num);
    for (int i = 0; i < models->num; i++) {
        ESP_LOGI(TAG, "  Model %d: %s", i, models->model_name[i]);
    }
    
    // Find the wake word model
    char *wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
    if (!wn_name) {
        ESP_LOGE(TAG, "No wake word model found");
        return false;
    }
    ESP_LOGI(TAG, "Using wake word model: %s", wn_name);
    
    // Use the default config macro and customize for single mic
    afe_config_t afe_config = AFE_CONFIG_DEFAULT();
    afe_config.aec_init = false;              // No echo cancellation (no speaker feedback during listen)
    afe_config.se_init = true;                // Speech enhancement
    afe_config.vad_init = true;               // Voice activity detection
    afe_config.wakenet_init = true;           // Enable wake word
    afe_config.wakenet_model_name = wn_name;  // Use discovered model
    afe_config.wakenet_mode = DET_MODE_95;    // Higher sensitivity (lower threshold)
    afe_config.afe_mode = SR_MODE_LOW_COST;   // Single mic mode
    afe_config.afe_perferred_core = 0;        // Run on core 0
    afe_config.afe_perferred_priority = 5;
    afe_config.pcm_config.total_ch_num = 1;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.ref_num = 0;
    afe_config.pcm_config.sample_rate = 16000;
    
    afe_handle = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
    afe_data = afe_handle->create_from_config(&afe_config);
    
    if (!afe_data) {
        ESP_LOGE(TAG, "Failed to create AFE");
        return false;
    }
    
    // Get required chunk size from AFE
    feed_chunk_size = afe_handle->get_feed_chunksize(afe_data);
    ESP_LOGI(TAG, "AFE feed chunk size: %d samples", feed_chunk_size);
    
    // Allocate feed buffer (use PSRAM for larger buffer)
    feed_buffer = heap_caps_malloc(feed_chunk_size * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!feed_buffer) {
        // Fall back to internal RAM
        feed_buffer = heap_caps_malloc(feed_chunk_size * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    }
    if (!feed_buffer) {
        ESP_LOGE(TAG, "Failed to allocate feed buffer");
        return false;
    }
    
    ESP_LOGI(TAG, "Wake word detection initialized (say 'Hi ESP')");
    return true;
}

bool wakeword_start(void)
{
    if (wakeword_enabled) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }
    
    if (!afe_handle || !afe_data) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    wakeword_enabled = true;
    
    // Start mic recording for continuous feed
    if (!audio_start_recording(320000)) {  // 10 seconds buffer
        ESP_LOGE(TAG, "Failed to start recording");
        wakeword_enabled = false;
        return false;
    }
    
    // Start tasks (ESP-SR needs larger stacks)
    xTaskCreatePinnedToCore(feed_task, "ww_feed", 8192, NULL, 5, &feed_task_handle, 0);
    xTaskCreatePinnedToCore(detect_task, "ww_detect", 8192, NULL, 5, &wakeword_task_handle, 1);
    
    display_set_state(DISPLAY_STATE_IDLE);
    ESP_LOGI(TAG, "Wake word detection started");
    return true;
}

void wakeword_stop(void)
{
    if (!wakeword_enabled) return;
    
    wakeword_enabled = false;
    
    // Wait for tasks to finish
    vTaskDelay(pdMS_TO_TICKS(100));
    
    audio_stop_recording();
    
    ESP_LOGI(TAG, "Wake word detection stopped");
}

void wakeword_set_callback(wakeword_callback_t callback)
{
    wakeword_callback = callback;
}

bool wakeword_is_detected(void)
{
    return wakeword_detected;
}
