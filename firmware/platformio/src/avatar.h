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

#endif // AVATAR_H
