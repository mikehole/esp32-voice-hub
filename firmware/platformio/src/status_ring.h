/**
 * Status Ring - Animated ring around avatar for visual feedback
 */

#ifndef STATUS_RING_H
#define STATUS_RING_H

#include <Arduino.h>
#include "lvgl.h"

// Processing states
enum ProcessingState {
    STATE_IDLE,
    STATE_RECORDING,
    STATE_THINKING,
    STATE_SPEAKING
};

// Initialize the status ring (call after UI is created)
void status_ring_init(lv_obj_t* parent);

// Show the status ring with specified state
void status_ring_show(ProcessingState state);

// Hide the status ring
void status_ring_hide();

// Update animation (call in loop)
void status_ring_update();

// Get current state
ProcessingState status_ring_get_state();

#endif // STATUS_RING_H
