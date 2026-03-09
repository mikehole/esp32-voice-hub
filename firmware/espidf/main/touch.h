/**
 * Touch Input Module Header
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Initialize touch controller
void touch_init(void);

// Read touch position (returns true if touched)
bool touch_read(uint16_t *x, uint16_t *y);

// Check if screen is currently pressed
bool touch_is_pressed(void);
