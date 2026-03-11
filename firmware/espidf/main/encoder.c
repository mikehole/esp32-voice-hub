/**
 * Rotary Encoder Module
 * 
 * Accumulates encoder ticks and delivers them when UI is ready,
 * rather than trying to update on every tick.
 */

#include "encoder.h"
#include "bidi_switch_knob.h"
#include "esp_log.h"

static const char *TAG = "encoder";

#define ENCODER_PIN_A    8
#define ENCODER_PIN_B    7

static knob_handle_t knob_handle = NULL;
static volatile int accumulated_delta = 0;  // Accumulated ticks since last read
static encoder_callback_t user_callback = NULL;

static void knob_left_cb(void *arg, void *data) {
    accumulated_delta--;
}

static void knob_right_cb(void *arg, void *data) {
    accumulated_delta++;
}

void encoder_init(void)
{
    knob_config_t knob_cfg = {
        .gpio_encoder_a = ENCODER_PIN_A,
        .gpio_encoder_b = ENCODER_PIN_B,
    };
    
    knob_handle = iot_knob_create(&knob_cfg);
    if (knob_handle != NULL) {
        iot_knob_register_cb(knob_handle, KNOB_LEFT, knob_left_cb, NULL);
        iot_knob_register_cb(knob_handle, KNOB_RIGHT, knob_right_cb, NULL);
        ESP_LOGI(TAG, "Rotary encoder initialized (GPIO %d/%d)", ENCODER_PIN_A, ENCODER_PIN_B);
    } else {
        ESP_LOGE(TAG, "Failed to create knob handle");
    }
}

void encoder_set_callback(encoder_callback_t callback)
{
    user_callback = callback;
}

// Get accumulated delta since last call (clears accumulator)
int encoder_get_delta(void)
{
    int delta = accumulated_delta;
    accumulated_delta = 0;
    return delta;
}

// Check if there are pending encoder ticks
bool encoder_has_pending(void)
{
    return accumulated_delta != 0;
}

