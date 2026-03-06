/**
 * Avatar state management - swap avatar images based on processing state
 */

#ifndef AVATAR_H
#define AVATAR_H

#include "status_ring.h"  // For ProcessingState enum

// Initialize avatar display
void avatar_init(lv_obj_t* parent);

// Set avatar state (swaps image)
void avatar_set_state(ProcessingState state);

// Get current avatar image object
lv_obj_t* avatar_get_obj();

// Set avatar based on selected wedge (for menu navigation)
void avatar_set_wedge(int wedge_index);

// Set custom avatar image from RGB565 data (130x130 = 33800 bytes)
// Data is copied to internal buffer. Pass NULL to reset to normal avatar.
bool avatar_set_custom(const uint16_t* rgb565_data, size_t size);

// Check if custom avatar is active
bool avatar_is_custom();

// Reset to normal avatar mode
void avatar_reset_custom();

#endif // AVATAR_H
