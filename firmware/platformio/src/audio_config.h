/**
 * Audio Configuration for ESP32-S3-Knob-Touch-LCD-1.8
 * 
 * Hardware:
 * - PCM5100A DAC for audio output (I2S standard mode)
 * - Digital PDM microphone for audio input
 */

#ifndef AUDIO_CONFIG_H
#define AUDIO_CONFIG_H

#include "driver/gpio.h"

// I2S Standard Mode - Audio Output (PCM5100A DAC)
#define AUDIO_I2S_BCLK_PIN      (gpio_num_t)39  // Bit clock
#define AUDIO_I2S_WS_PIN        (gpio_num_t)40  // Word select (LRCLK)
#define AUDIO_I2S_DOUT_PIN      (gpio_num_t)41  // Data out to DAC

// I2S PDM Mode - Audio Input (Digital Microphone)
#define AUDIO_PDM_DATA_PIN      (gpio_num_t)46  // PDM data from mic
#define AUDIO_PDM_CLK_PIN       (gpio_num_t)45  // PDM clock to mic

// PCM5100A enable pin (active HIGH)
#define AUDIO_DAC_ENABLE_PIN    (gpio_num_t)0

// Audio parameters
#define AUDIO_SAMPLE_RATE       16000   // 16kHz for voice (Whisper compatible)
#define AUDIO_BIT_DEPTH         16      // 16-bit samples
#define AUDIO_CHANNELS          1       // Mono

// Buffer sizes
#define AUDIO_BUFFER_SIZE       4096    // Bytes per buffer
#define AUDIO_RECORD_SECONDS    10      // Max recording duration

#endif // AUDIO_CONFIG_H
