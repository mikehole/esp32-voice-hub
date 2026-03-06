/**
 * LVGL Port - Thread-safe LVGL access
 * 
 * LVGL is NOT thread-safe. All LVGL calls must be protected by this mutex.
 * Call lvgl_port_lock() before any lv_* function, and lvgl_port_unlock() after.
 */

#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include <stdint.h>
#include <stdbool.h>

// Initialize the LVGL port (creates mutex)
void lvgl_port_init();

// Lock LVGL for exclusive access
// Returns true if lock acquired, false on timeout
// timeout_ms: max time to wait (0 = try once, portMAX_DELAY = forever)
bool lvgl_port_lock(uint32_t timeout_ms);

// Unlock LVGL after use
void lvgl_port_unlock();

// Run LVGL timer handler (acquires lock internally)
// Returns recommended delay until next call in ms
uint32_t lvgl_port_task_handler();

#endif // LVGL_PORT_H
