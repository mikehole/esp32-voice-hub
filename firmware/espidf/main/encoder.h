/**
 * Rotary Encoder Module
 */

#pragma once

#include <stdbool.h>

// Callback for encoder rotation
typedef void (*encoder_callback_t)(int delta);  // +1 = right, -1 = left

// Initialize encoder on GPIO 7 & 8
void encoder_init(void);

// Set callback for rotation events
void encoder_set_callback(encoder_callback_t callback);

// Get accumulated delta since last call (clears accumulator)
// Returns total ticks: negative = left, positive = right
int encoder_get_delta(void);

// Check if there are pending encoder ticks
bool encoder_has_pending(void);
