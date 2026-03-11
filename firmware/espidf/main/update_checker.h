/**
 * Update Checker - Check GitHub releases for firmware updates
 */

#pragma once

#include <stdbool.h>

// Initialize update checker
void update_checker_init(void);

// Check for updates (non-blocking, runs in background)
// Calls callback when done: update_available, new_version string
void update_checker_check(void (*callback)(bool update_available, const char* new_version));

// Get the download URL for the latest firmware
const char* update_checker_get_url(void);

// Download and flash the update (blocks, restarts on success)
bool update_checker_install(void);

// Check if an update is available (from last check)
bool update_checker_has_update(void);

// Get the latest version string (from last check)
const char* update_checker_get_latest_version(void);
