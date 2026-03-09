/**
 * Wedge UI - Radial menu with avatar center
 * 
 * 8 wedge segments around a center circle containing
 * the Minerva avatar with state animations.
 */

#ifndef WEDGE_UI_H
#define WEDGE_UI_H

#include <stdbool.h>

// Avatar states (matches display states)
typedef enum {
    AVATAR_IDLE = 0,
    AVATAR_LISTENING,
    AVATAR_THINKING,
    AVATAR_SPEAKING,
    AVATAR_NOTIFICATION
} avatar_state_t;

// Initialize wedge UI (call after LVGL init)
bool wedge_ui_init(void);

// Set avatar state (updates center image)
void wedge_ui_set_avatar_state(avatar_state_t state);

// Set selected wedge (0-7, highlights that segment)
void wedge_ui_set_selection(int wedge);

// Get current selection
int wedge_ui_get_selection(void);

// Rotate selection (delta = +1 or -1)
void wedge_ui_rotate(int delta);

#endif // WEDGE_UI_H
