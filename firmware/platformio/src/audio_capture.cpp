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
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
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
            int16_t* samples = read_buf;
            size_t sample_count = bytes_read / sizeof(int16_t);
            for (size_t i = 0; i < sample_count; i++) {
                sum += abs(samples[i]);
            }
            int32_t avg = sum / sample_count;
            current_audio_level = (uint8_t)min(100L, avg / 200);  // Scale to 0-100
            
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
    
    // TODO: Implement playback with sample rate conversion if needed
    // For now, just enable and write
    
    esp_err_t err = i2s_channel_enable(tx_chan);
    if (err != ESP_OK) {
        Serial.printf("Audio: Failed to enable TX channel: %d\n", err);
        return false;
    }
    
    playing = true;
    
    size_t bytes_written = 0;
    size_t offset = 0;
    
    while (offset < size && playing) {
        size_t chunk = min((size_t)AUDIO_BUFFER_SIZE, size - offset);
        err = i2s_channel_write(tx_chan, data + offset, chunk, &bytes_written, pdMS_TO_TICKS(1000));
        if (err == ESP_OK) {
            offset += bytes_written;
        } else {
            break;
        }
    }
    
    i2s_channel_disable(tx_chan);
    playing = false;
    
    Serial.printf("Audio: Playback complete (%u bytes)\n", offset);
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
