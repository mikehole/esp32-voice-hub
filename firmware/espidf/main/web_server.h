#pragma once

#include "esp_err.h"

// Start the HTTP server (call after WiFi connected)
esp_err_t web_server_start(void);

// Stop the HTTP server
void web_server_stop(void);

// Set the authentication token for protected endpoints
void web_server_set_token(const char* token);
