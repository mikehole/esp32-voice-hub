/**
 * Wake Word Integration
 * 
 * High-level integration layer for the wake word WebSocket client.
 * Handles connecting to the server, streaming idle audio, and processing
 * wake word detections and TTS responses.
 * 
 * Usage:
 *   1. Call wakeword_integration_init() after WiFi connects
 *   2. Call wakeword_integration_loop() in loop()
 *   3. The module handles everything automatically:
 *      - Streams idle audio for wake word detection
 *      - Plays ack beep on wake word detection
 *      - Starts recording automatically
 *      - Sends recording to server when done
 *      - Plays TTS response
 *      - Returns to idle streaming
 */

#ifndef WAKEWORD_INTEGRATION_H
#define WAKEWORD_INTEGRATION_H

#include <Arduino.h>

// Initialize wake word integration (call after WiFi connects)
void wakeword_integration_init();

// Must be called in loop()
void wakeword_integration_loop();

// Check if wake word mode is enabled and connected
bool wakeword_integration_is_active();

// Manually start recording (can be used for tap-to-talk fallback)
void wakeword_start_recording();

// Manually stop recording (triggers processing)
void wakeword_stop_recording();

// Enable/disable wake word mode (tap-to-talk fallback)
void wakeword_set_enabled(bool enabled);
bool wakeword_is_enabled();

#endif // WAKEWORD_INTEGRATION_H
