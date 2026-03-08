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

// Text-to-Speech using OpenAI TTS API
// Returns audio data (caller must free) or NULL on error
// out_size receives the audio data size
// Audio is PCM 24kHz 16-bit mono
uint8_t* openai_tts(const char* text, size_t* out_size);

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

// Send message with full conversation history
// Uses conversation.h for context
char* openclaw_send_with_history(const char* message);

// Voice hook: fire-and-forget voice command
// Sends audio_url to OpenClaw hook, which fetches audio, transcribes,
// processes, and POSTs TTS response back to callback_url
// Returns true if hook was triggered successfully (202 Accepted)
bool openclaw_voice_hook(const char* audio_url, const char* callback_url);

// Wake word server settings (WebSocket)
void wakeword_set_host(const char* host);  // e.g., "192.168.1.100"
void wakeword_set_port(uint16_t port);     // e.g., 8765
bool wakeword_has_config();
const char* wakeword_get_host();
uint16_t wakeword_get_port();

#endif // OPENAI_CLIENT_H
