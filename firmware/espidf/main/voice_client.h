/**
 * Voice Client Header
 * WebSocket client for OpenClaw plugin communication
 */

#pragma once

#include <stdbool.h>

// Initialize voice client (touch + wake word + WebSocket)
void voice_client_init(void);

// Connect to OpenClaw plugin WebSocket
void voice_client_connect(const char *uri);

// Called when WebSocket connects (starts wake word)
void voice_client_on_connected(void);

// Disconnect from plugin
void voice_client_disconnect(void);

// Check if connected
bool voice_client_is_connected(void);
