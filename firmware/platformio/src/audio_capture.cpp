/**
 * Audio Capture Module
 * Records audio from PDM microphone, plays through PCM5100A DAC
 */

#include "audio_capture.h"
#include "audio_config.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "audio";

// I2S channel handles
static i2s_chan_handle_t rx_chan = NULL;  // PDM mic input
static i2s_chan_handle_t tx_chan = NULL;  // DAC output

// Recording state
static volatile bool recording = false;
static volatile bool playing = false;
static uint8_t* record_buffer = NULL;
static size_t record_buffer_size = 0;
static size_t record_position = 0;
static uint32_t record_start_time = 0;
static volatile uint8_t current_audio_level = 0;

// Recording task handle
static TaskHandle_t record_task_handle = NULL;

// Playback task handle and state
static TaskHandle_t playback_task_handle = NULL;
static const uint8_t* playback_data = NULL;
static size_t playback_size = 0;
static uint32_t playback_sample_rate = 0;
static bool playback_owns_buffer = false;  // If true, task will free playback_data

// Pre-allocated volume buffer for playback (avoid malloc during audio)
#define PLAYBACK_CHUNK_SIZE 2048
static int16_t* playback_vol_buf = NULL;

// Volume control (0-100, default 30)
static int playback_volume = 30;

// Initialize PDM microphone (I2S RX)
static bool init_pdm_mic() {
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    rx_chan_cfg.dma_desc_num = 6;
    rx_chan_cfg.dma_frame_num = 240;
    
    esp_err_t err = i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);
    if (err != ESP_OK) {
        Serial.printf("Audio: Failed to create RX channel: %d\n", err);
        return false;
    }
    
    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = AUDIO_PDM_CLK_PIN,
            .din = AUDIO_PDM_DATA_PIN,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    
    err = i2s_channel_init_pdm_rx_mode(rx_chan, &pdm_rx_cfg);
    if (err != ESP_OK) {
        Serial.printf("Audio: Failed to init PDM RX: %d\n", err);
        return false;
    }
    
    Serial.println("Audio: PDM microphone initialized");
    return true;
}

// Initialize I2S DAC output
static bool init_dac_output() {
    // Enable PCM5100A (GPIO 0 HIGH)
    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << AUDIO_DAC_ENABLE_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio_conf);
    gpio_set_level(AUDIO_DAC_ENABLE_PIN, 1);
    
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    
    esp_err_t err = i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL);
    if (err != ESP_OK) {
        Serial.printf("Audio: Failed to create TX channel: %d\n", err);
        return false;
    }
    
    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_BCLK_PIN,
            .ws = AUDIO_I2S_WS_PIN,
            .dout = AUDIO_I2S_DOUT_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    err = i2s_channel_init_std_mode(tx_chan, &tx_std_cfg);
    if (err != ESP_OK) {
        Serial.printf("Audio: Failed to init STD TX: %d\n", err);
        return false;
    }
    
    // Don't enable channel here - enable only when playing
    Serial.println("Audio: DAC output initialized");
    return true;
}

// Recording task - runs in background
static void recording_task(void* arg) {
    int16_t* read_buf = (int16_t*)heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_INTERNAL);
    if (!read_buf) {
        Serial.println("Audio: Failed to allocate read buffer");
        recording = false;
        vTaskDelete(NULL);
        return;
    }
    
    size_t bytes_read = 0;
    
    while (recording) {
        // Check if we've hit max duration
        if (record_position >= record_buffer_size) {
            Serial.println("Audio: Buffer full, stopping");
            break;
        }
        
        // Read from microphone
        esp_err_t err = i2s_channel_read(rx_chan, read_buf, AUDIO_BUFFER_SIZE, &bytes_read, pdMS_TO_TICKS(100));
        if (err == ESP_OK && bytes_read > 0) {
            // Calculate audio level for visualization
            int32_t sum = 0;
            int16_t max_sample = 0;
            int16_t* samples = read_buf;
            size_t sample_count = bytes_read / sizeof(int16_t);
            for (size_t i = 0; i < sample_count; i++) {
                int16_t s = abs(samples[i]);
                sum += s;
                if (s > max_sample) max_sample = s;
            }
            int32_t avg = sum / sample_count;
            // Use peak level, scaled to 0-100 (32767 max for 16-bit)
            current_audio_level = (uint8_t)min(100, (max_sample * 100) / 16000);
            
            // Debug every ~1 second
            static uint32_t last_level_debug = 0;
            if (millis() - last_level_debug > 1000) {
                Serial.printf("Audio: avg=%d max=%d level=%d\n", avg, max_sample, current_audio_level);
                last_level_debug = millis();
            }
            
            // Copy to record buffer
            size_t to_copy = min(bytes_read, record_buffer_size - record_position);
            memcpy(record_buffer + record_position, read_buf, to_copy);
            record_position += to_copy;
        }
    }
    
    recording = false;
    heap_caps_free(read_buf);
    vTaskDelete(NULL);
}

bool audio_init() {
    Serial.println("Audio: Initializing...");
    
    if (!init_pdm_mic()) {
        return false;
    }
    
    if (!init_dac_output()) {
        return false;
    }
    
    // Allocate recording buffer in PSRAM (enough for max duration)
    record_buffer_size = AUDIO_SAMPLE_RATE * 2 * AUDIO_RECORD_SECONDS;  // 16-bit mono
    record_buffer = (uint8_t*)heap_caps_malloc(record_buffer_size, MALLOC_CAP_SPIRAM);
    if (!record_buffer) {
        Serial.println("Audio: Failed to allocate record buffer in PSRAM");
        return false;
    }
    
    // Pre-allocate playback volume buffer (avoid malloc during playback)
    playback_vol_buf = (int16_t*)heap_caps_malloc(PLAYBACK_CHUNK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!playback_vol_buf) {
        Serial.println("Audio: Failed to allocate playback buffer");
        return false;
    }
    
    Serial.printf("Audio: Initialized (buffer: %u KB in PSRAM)\n", record_buffer_size / 1024);
    return true;
}

bool audio_start_recording() {
    if (recording || playing) {
        return false;
    }
    
    // Enable mic channel
    esp_err_t err = i2s_channel_enable(rx_chan);
    if (err != ESP_OK) {
        Serial.printf("Audio: Failed to enable RX channel: %d\n", err);
        return false;
    }
    
    record_position = 0;
    record_start_time = millis();
    recording = true;
    current_audio_level = 0;
    
    // Start recording task
    xTaskCreatePinnedToCore(recording_task, "audio_rec", 4096, NULL, 5, &record_task_handle, 0);
    
    Serial.println("Audio: Recording started");
    return true;
}

const uint8_t* audio_stop_recording(size_t* out_size) {
    if (!recording) {
        *out_size = 0;
        return NULL;
    }
    
    recording = false;
    
    // Wait for task to finish
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Disable mic channel
    i2s_channel_disable(rx_chan);
    
    *out_size = record_position;
    Serial.printf("Audio: Recording stopped (%u bytes, %u ms)\n", 
                  record_position, millis() - record_start_time);
    
    return record_buffer;
}

bool audio_is_recording() {
    return recording;
}

uint32_t audio_get_recording_duration_ms() {
    if (!recording) return 0;
    return millis() - record_start_time;
}

bool audio_play(const uint8_t* data, size_t size, uint32_t sample_rate) {
    if (playing || recording) {
        return false;
    }
    
    Serial.printf("Audio: Playing %u bytes at %u Hz\n", size, sample_rate);
    
    // Reconfigure I2S clock for requested sample rate (must be done while disabled)
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    esp_err_t err = i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);
    if (err != ESP_OK) {
        Serial.printf("Audio: Failed to reconfig clock: %d\n", err);
    }
    
    err = i2s_channel_enable(tx_chan);
    if (err != ESP_OK) {
        Serial.printf("Audio: Failed to enable TX channel: %d\n", err);
        return false;
    }
    
    playing = true;
    
    // Convert mono to stereo first, then use the working stereo playback path
    // Input: 16-bit mono samples (little-endian)
    // Output: 16-bit stereo interleaved (L, R, L, R, ...)
    
    const int16_t* mono_samples = (const int16_t*)data;
    size_t total_mono_samples = size / 2;
    size_t stereo_size = size * 2;
    
    Serial.printf("Audio: Converting %u mono samples to stereo (%u bytes)\n", total_mono_samples, stereo_size);
    
    // Allocate stereo buffer in PSRAM
    uint8_t* stereo_buf = (uint8_t*)heap_caps_malloc(stereo_size, MALLOC_CAP_SPIRAM);
    if (!stereo_buf) {
        Serial.println("Audio: Failed to allocate stereo buffer");
        i2s_channel_disable(tx_chan);
        playing = false;
        return false;
    }
    
    // Convert mono to stereo (no volume adjustment here - let stereo path handle it)
    int16_t* stereo_samples = (int16_t*)stereo_buf;
    for (size_t i = 0; i < total_mono_samples; i++) {
        stereo_samples[i * 2] = mono_samples[i];      // Left
        stereo_samples[i * 2 + 1] = mono_samples[i];  // Right
    }
    
    // Disable channel (stereo playback will re-enable)
    i2s_channel_disable(tx_chan);
    playing = false;
    
    // Use the working stereo playback function
    // Mark buffer as owned so the playback task will free it when done
    playback_owns_buffer = true;
    bool result = audio_play_stereo(stereo_buf, stereo_size, sample_rate);
    
    // Don't wait or free here - task will free when complete
    return result;
}

// Playback task - runs on core 1 for smooth audio
static void playback_task(void* param) {
    const float volume = playback_volume / 100.0f;
    
    // Use pre-allocated buffer (avoid malloc during playback)
    if (!playback_vol_buf) {
        Serial.println("Audio: Playback buffer not allocated!");
        playing = false;
        playback_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    int16_t* vol_buf = playback_vol_buf;
    
    size_t bytes_written = 0;
    size_t offset = 0;
    const int16_t* samples = (const int16_t*)playback_data;
    size_t total_samples = playback_size / 2;  // 16-bit samples
    
    while (offset < total_samples && playing) {
        size_t samples_to_process = min((size_t)(PLAYBACK_CHUNK_SIZE / 2), total_samples - offset);
        
        // Calculate audio level for this chunk (for ring animation)
        int32_t sum = 0;
        int16_t max_sample = 0;
        
        for (size_t i = 0; i < samples_to_process; i++) {
            int16_t sample = samples[offset + i];
            vol_buf[i] = (int16_t)(sample * volume);
            
            // Track peak amplitude
            int16_t abs_sample = sample < 0 ? -sample : sample;
            if (abs_sample > max_sample) max_sample = abs_sample;
        }
        
        // Update audio level (0-100 scale) - use peak for more responsive animation
        current_audio_level = (uint8_t)((max_sample * 100) / 32768);
        
        size_t bytes_to_write = samples_to_process * 2;
        esp_err_t err = i2s_channel_write(tx_chan, vol_buf, bytes_to_write, &bytes_written, pdMS_TO_TICKS(1000));
        if (err == ESP_OK) {
            offset += samples_to_process;
        } else {
            Serial.printf("Audio: Write error at offset %u: %d\n", offset, err);
            break;
        }
        
        // Yield to other tasks on this core
        taskYIELD();
    }
    
    // Reset audio level when done
    current_audio_level = 0;
    
    // Flush with silence
    memset(vol_buf, 0, PLAYBACK_CHUNK_SIZE);
    i2s_channel_write(tx_chan, vol_buf, PLAYBACK_CHUNK_SIZE, &bytes_written, pdMS_TO_TICKS(100));
    
    // Don't free vol_buf - it's pre-allocated
    i2s_channel_disable(tx_chan);
    
    // Free the playback buffer if we own it (mono->stereo conversion case)
    if (playback_owns_buffer && playback_data) {
        heap_caps_free((void*)playback_data);
        playback_data = NULL;
        playback_owns_buffer = false;
    }
    
    playing = false;
    playback_task_handle = NULL;
    
    Serial.printf("Audio: Playback task complete (%u samples)\n", offset);
    vTaskDelete(NULL);
}

// Play stereo audio directly (no conversion needed)
// Now runs in a separate task on core 1 for smooth playback
bool audio_play_stereo(const uint8_t* data, size_t size, uint32_t sample_rate) {
    if (playing || recording) {
        return false;
    }
    
    Serial.printf("Audio: Playing %u bytes stereo at %u Hz (threaded)\n", size, sample_rate);
    
    // Reconfigure I2S clock for requested sample rate
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    esp_err_t err = i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);
    if (err != ESP_OK) {
        Serial.printf("Audio: Failed to reconfig clock: %d\n", err);
    }
    
    err = i2s_channel_enable(tx_chan);
    if (err != ESP_OK) {
        Serial.printf("Audio: Failed to enable TX channel: %d\n", err);
        // If we own the buffer, free it on failure
        if (playback_owns_buffer && data) {
            heap_caps_free((void*)data);
            playback_owns_buffer = false;
        }
        return false;
    }
    
    // Store playback params for the task
    playback_data = data;
    playback_size = size;
    playback_sample_rate = sample_rate;
    playing = true;
    
    // Create playback task on core 1 (UI runs on core 0)
    // Priority 10 = high priority for smooth audio
    BaseType_t result = xTaskCreatePinnedToCore(
        playback_task,
        "audio_play",
        4096,
        NULL,
        10,  // High priority
        &playback_task_handle,
        1    // Core 1 (separate from UI on core 0)
    );
    
    if (result != pdPASS) {
        Serial.println("Audio: Failed to create playback task");
        i2s_channel_disable(tx_chan);
        playing = false;
        return false;
    }
    
    return true;
}

bool audio_is_playing() {
    return playing;
}

void audio_stop_playback() {
    if (playing) {
        playing = false;  // Signal task to stop
        
        // Wait for task to finish (with timeout)
        int timeout = 50;  // 500ms max
        while (playback_task_handle != NULL && timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            timeout--;
        }
        
        // Force cleanup if task didn't exit
        if (playback_task_handle != NULL) {
            Serial.println("Audio: Force stopping playback task");
            vTaskDelete(playback_task_handle);
            playback_task_handle = NULL;
            i2s_channel_disable(tx_chan);
        }
    }
}

uint8_t audio_get_level() {
    return current_audio_level;
}

const uint8_t* audio_get_last_recording(size_t* out_size) {
    if (recording) {
        *out_size = 0;
        return NULL;  // Still recording
    }
    *out_size = record_position;
    return record_buffer;
}

void audio_set_volume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    playback_volume = vol;
    Serial.printf("Audio: Volume set to %d%%\n", vol);
}

int audio_get_volume() {
    return playback_volume;
}
