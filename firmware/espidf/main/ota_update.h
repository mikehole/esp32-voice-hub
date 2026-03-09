#pragma once

#include "esp_err.h"
#include <stddef.h>

// Initialize OTA subsystem
void ota_init(void);

// Start OTA update from URL
esp_err_t ota_update_from_url(const char* url);

// Start OTA update from direct upload (streaming)
// Call ota_begin, then ota_write repeatedly, then ota_end
esp_err_t ota_begin(void);
esp_err_t ota_write(const uint8_t* data, size_t len);
esp_err_t ota_end(void);
esp_err_t ota_abort(void);

// Get current firmware version
const char* ota_get_version(void);

// Get current firmware SHA256 (first 8 chars)
const char* ota_get_sha256_short(void);
