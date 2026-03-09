/**
 * Voice Streaming - Simple WebSocket audio streaming
 * 
 * Flow:
 * 1. User taps center → connect WebSocket, start streaming audio
 * 2. User taps again → stop streaming, send "done" message, disconnect
 * 3. Server processes audio, POSTs TTS response to /api/play
 * 4. Orb plays response, returns to idle
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Initialize voice streaming (call once at startup)
void voice_stream_init();

// Start a voice session (connect + stream)
// Returns true if connection successful
bool voice_stream_start();

// Stop the current voice session (stop streaming + disconnect)
void voice_stream_stop();

// Check if currently streaming
bool voice_stream_is_active();

// Check if waiting for server response
bool voice_stream_is_waiting();

// Called when /api/play receives audio
void voice_stream_response_received();

// Called when playback finishes
void voice_stream_response_done();

// Call from main loop to pump WebSocket events
void voice_stream_loop();
