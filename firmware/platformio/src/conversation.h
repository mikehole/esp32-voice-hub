/**
 * Conversation History Manager
 * Stores conversation on SD card for context continuity
 */

#ifndef CONVERSATION_H
#define CONVERSATION_H

#include <Arduino.h>

// Maximum messages to keep in history
#define MAX_CONVERSATION_MESSAGES 20

// Initialize SD card and load conversation history
bool conversation_init();

// Check if SD card is available
bool conversation_sd_available();

// Add a message to the conversation
// role: "user", "assistant", or "system"
void conversation_add_message(const char* role, const char* content);

// Get conversation history as JSON array string for API call
// Returns allocated string (caller must free)
// Format: [{"role":"user","content":"..."},{"role":"assistant","content":"..."}]
char* conversation_get_messages_json();

// Get message count
int conversation_get_count();

// Clear conversation history
void conversation_clear();

// Save conversation to SD card
bool conversation_save();

// Get SD card info string
const char* conversation_get_sd_info();

#endif // CONVERSATION_H
