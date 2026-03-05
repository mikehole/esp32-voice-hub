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
#include "lvgl.h"
#include "status_ring.h"

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
    bool result = audio_play_stereo(stereo_buf, stereo_size, sample_rate);
    
    heap_caps_free(stereo_buf);
    return result;
}

// Play stereo audio directly (no conversion needed)
bool audio_play_stereo(const uint8_t* data, size_t size, uint32_t sample_rate) {
    if (playing || recording) {
        return false;
    }
    
    Serial.printf("Audio: Playing %u bytes stereo at %u Hz\n", size, sample_rate);
    
    // Reconfigure I2S clock for requested sample rate
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
    
    // Direct write - data is already stereo 16-bit
    // Apply volume scaling
    const float volume = 0.3f;
    const size_t CHUNK_SIZE = 2048;
    int16_t* vol_buf = (int16_t*)heap_caps_malloc(CHUNK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!vol_buf) {
        Serial.println("Audio: Failed to allocate volume buffer");
        i2s_channel_disable(tx_chan);
        playing = false;
        return false;
    }
    
    size_t bytes_written = 0;
    size_t offset = 0;
    const int16_t* samples = (const int16_t*)data;
    size_t total_samples = size / 2;  // 16-bit samples
    
    while (offset < total_samples && playing) {
        size_t samples_to_process = min((size_t)(CHUNK_SIZE / 2), total_samples - offset);
        
        for (size_t i = 0; i < samples_to_process; i++) {
            vol_buf[i] = (int16_t)(samples[offset + i] * volume);
        }
        
        size_t bytes_to_write = samples_to_process * 2;
        err = i2s_channel_write(tx_chan, vol_buf, bytes_to_write, &bytes_written, pdMS_TO_TICKS(1000));
        if (err == ESP_OK) {
            offset += samples_to_process;
        } else {
            Serial.printf("Audio: Write error at offset %u: %d\n", offset, err);
            break;
        }
        
        // Update UI during playback (runs on main thread, so LVGL is safe)
        static size_t last_ui_update = 0;
        if (offset - last_ui_update > sample_rate / 10) {  // ~100ms
            lv_timer_handler();
            status_ring_update();
            last_ui_update = offset;
        }
        
        yield();  // Let other tasks run
    }
    
    // Flush with silence
    memset(vol_buf, 0, CHUNK_SIZE);
    i2s_channel_write(tx_chan, vol_buf, CHUNK_SIZE, &bytes_written, pdMS_TO_TICKS(100));
    
    heap_caps_free(vol_buf);
    i2s_channel_disable(tx_chan);
    playing = false;
    
    Serial.printf("Audio: Stereo playback complete (%u samples)\n", offset);
    return true;
}

bool audio_is_playing() {
    return playing;
}

void audio_stop_playback() {
    if (playing) {
        playing = false;
        i2s_channel_disable(tx_chan);
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
