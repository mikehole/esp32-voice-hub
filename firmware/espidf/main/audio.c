/**
 * Audio Module - I2S PDM Microphone + PCM5100A DAC
 * ESP-IDF implementation for ESP32-S3-Knob-Touch-LCD-1.8
 */

#include "audio.h"
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "audio";

// Pin definitions
#define AUDIO_I2S_BCLK_PIN      GPIO_NUM_39   // Bit clock
#define AUDIO_I2S_WS_PIN        GPIO_NUM_40   // Word select (LRCLK)
#define AUDIO_I2S_DOUT_PIN      GPIO_NUM_41   // Data out to DAC
#define AUDIO_PDM_DATA_PIN      GPIO_NUM_46   // PDM data from mic
#define AUDIO_PDM_CLK_PIN       GPIO_NUM_45   // PDM clock to mic
#define AUDIO_DAC_ENABLE_PIN    GPIO_NUM_0    // PCM5100A enable (active HIGH)

// Audio parameters
#define AUDIO_SAMPLE_RATE       16000   // 16kHz for voice
#define AUDIO_BUFFER_SIZE       4096    // Bytes per buffer
#define PLAYBACK_CHUNK_SIZE     2048

// I2S channel handles
static i2s_chan_handle_t rx_chan = NULL;  // PDM mic input
static i2s_chan_handle_t tx_chan = NULL;  // DAC output

// Recording state
static volatile bool recording = false;
static uint8_t *record_buffer = NULL;
static size_t record_buffer_size = 0;
static size_t record_position = 0;

// Playback state
static volatile bool playing = false;
static TaskHandle_t playback_task_handle = NULL;
static const uint8_t *playback_data = NULL;
static size_t playback_size = 0;
static uint32_t playback_sample_rate = 0;
static bool playback_owns_buffer = false;

// Volume (0-100)
static int playback_volume = 30;

// Pre-allocated buffer for volume processing
static int16_t *playback_vol_buf = NULL;

// Initialize PDM microphone (I2S RX)
static bool init_pdm_mic(void)
{
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    rx_chan_cfg.dma_desc_num = 6;
    rx_chan_cfg.dma_frame_num = 240;
    
    esp_err_t err = i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RX channel: %s", esp_err_to_name(err));
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
        ESP_LOGE(TAG, "Failed to init PDM RX: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "PDM microphone initialized");
    return true;
}

// Initialize I2S DAC output
static bool init_dac_output(void)
{
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
        ESP_LOGE(TAG, "Failed to create TX channel: %s", esp_err_to_name(err));
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
        ESP_LOGE(TAG, "Failed to init STD TX: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "DAC output initialized");
    return true;
}

// Playback task
static void playback_task(void *arg)
{
    ESP_LOGI(TAG, "Playback starting: %u bytes @ %lu Hz", playback_size, playback_sample_rate);
    
    // Reconfigure sample rate if needed
    if (playback_sample_rate != AUDIO_SAMPLE_RATE) {
        i2s_channel_disable(tx_chan);
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(playback_sample_rate);
        i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);
    }
    
    i2s_channel_enable(tx_chan);
    
    const int16_t *src = (const int16_t *)playback_data;
    size_t samples = playback_size / 2;  // 16-bit samples
    size_t pos = 0;
    
    float vol_scale = playback_volume / 100.0f;
    
    while (pos < samples && playing) {
        size_t chunk_samples = (samples - pos > PLAYBACK_CHUNK_SIZE/4) ? 
                               PLAYBACK_CHUNK_SIZE/4 : samples - pos;
        
        // Convert mono to stereo with volume
        for (size_t i = 0; i < chunk_samples; i++) {
            int32_t sample = (int32_t)(src[pos + i] * vol_scale);
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            playback_vol_buf[i * 2] = (int16_t)sample;      // Left
            playback_vol_buf[i * 2 + 1] = (int16_t)sample;  // Right
        }
        
        size_t bytes_written = 0;
        i2s_channel_write(tx_chan, playback_vol_buf, chunk_samples * 4, &bytes_written, portMAX_DELAY);
        pos += chunk_samples;
    }
    
    i2s_channel_disable(tx_chan);
    
    // Restore default sample rate
    if (playback_sample_rate != AUDIO_SAMPLE_RATE) {
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE);
        i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);
    }
    
    if (playback_owns_buffer && playback_data) {
        free((void *)playback_data);
    }
    
    playing = false;
    playback_data = NULL;
    playback_task_handle = NULL;
    
    ESP_LOGI(TAG, "Playback complete");
    vTaskDelete(NULL);
}

void audio_init(void)
{
    ESP_LOGI(TAG, "Initializing audio...");
    
    // Allocate playback volume buffer
    playback_vol_buf = heap_caps_malloc(PLAYBACK_CHUNK_SIZE, MALLOC_CAP_INTERNAL);
    if (!playback_vol_buf) {
        ESP_LOGE(TAG, "Failed to allocate playback buffer");
        return;
    }
    
    if (!init_pdm_mic()) {
        ESP_LOGE(TAG, "PDM mic init failed");
    }
    
    if (!init_dac_output()) {
        ESP_LOGE(TAG, "DAC init failed");
    }
    
    ESP_LOGI(TAG, "Audio initialized");
}

bool audio_start_recording(size_t max_bytes)
{
    if (recording) {
        ESP_LOGW(TAG, "Already recording");
        return false;
    }
    
    // Allocate recording buffer in PSRAM
    record_buffer = heap_caps_malloc(max_bytes, MALLOC_CAP_SPIRAM);
    if (!record_buffer) {
        ESP_LOGE(TAG, "Failed to allocate recording buffer");
        return false;
    }
    
    record_buffer_size = max_bytes;
    record_position = 0;
    recording = true;
    
    i2s_channel_enable(rx_chan);
    
    ESP_LOGI(TAG, "Recording started (max %u bytes)", max_bytes);
    return true;
}

size_t audio_record_chunk(uint8_t *buffer, size_t max_size)
{
    if (!recording || !rx_chan) return 0;
    
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(rx_chan, buffer, max_size, &bytes_read, pdMS_TO_TICKS(100));
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read error: %s", esp_err_to_name(err));
        return 0;
    }
    
    return bytes_read;
}

void audio_stop_recording(void)
{
    if (!recording) return;
    
    recording = false;
    i2s_channel_disable(rx_chan);
    
    if (record_buffer) {
        free(record_buffer);
        record_buffer = NULL;
    }
    
    ESP_LOGI(TAG, "Recording stopped");
}

bool audio_is_recording(void)
{
    return recording;
}

bool audio_play(const uint8_t *data, size_t size, uint32_t sample_rate, bool take_ownership)
{
    if (playing) {
        ESP_LOGW(TAG, "Already playing");
        return false;
    }
    
    if (!tx_chan) {
        ESP_LOGE(TAG, "TX channel not initialized");
        return false;
    }
    
    playback_data = data;
    playback_size = size;
    playback_sample_rate = sample_rate;
    playback_owns_buffer = take_ownership;
    playing = true;
    
    xTaskCreate(playback_task, "audio_play", 4096, NULL, 5, &playback_task_handle);
    
    return true;
}

void audio_stop_playback(void)
{
    playing = false;
    if (playback_task_handle) {
        // Task will clean up and delete itself
    }
}

bool audio_is_playing(void)
{
    return playing;
}

void audio_set_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    playback_volume = volume;
    ESP_LOGI(TAG, "Volume set to %d%%", volume);
}

int audio_get_volume(void)
{
    return playback_volume;
}
