/**
 * Wake Word Detection Header
 */

#pragma once

#include <stdbool.h>

// Callback type for wake word detection
typedef void (*wakeword_callback_t)(void);

// Initialize wake word detection (loads model)
bool wakeword_init(void);

// Start continuous listening for wake word
bool wakeword_start(void);

// Stop listening
void wakeword_stop(void);

// Set callback for when wake word is detected
void wakeword_set_callback(wakeword_callback_t callback);

// Check if wake word was just detected
bool wakeword_is_detected(void);

// Check if wakeword detection is currently running
bool wakeword_is_running(void);
