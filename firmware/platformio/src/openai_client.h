/**
 * OpenAI API Client
 * Whisper transcription + Chat completions
 */

#ifndef OPENAI_CLIENT_H
#define OPENAI_CLIENT_H

#include <Arduino.h>

// Initialize the OpenAI client
void openai_init();

// Set/get API key (stored in NVS)
void openai_set_api_key(const char* key);
bool openai_has_api_key();
const char* openai_get_api_key();  // Returns masked version for display

// Check if transcription is in progress
bool openai_is_transcribing();

// Transcribe audio using Whisper API
// Returns transcript text (caller must free) or NULL on error
char* openai_transcribe(const uint8_t* audio_data, size_t audio_size);

// Get last error message
const char* openai_get_last_error();

// OpenClaw integration
void openclaw_set_endpoint(const char* url);  // e.g., "https://mikesdocker"
void openclaw_set_token(const char* token);   // hooks.token for auth
bool openclaw_has_endpoint();
bool openclaw_has_token();
const char* openclaw_get_endpoint();
const char* openclaw_get_token();  // Returns masked version

// Send message to OpenClaw and get response
// Returns response text (caller must free) or NULL on error
char* openclaw_send_message(const char* message);

#endif // OPENAI_CLIENT_H
