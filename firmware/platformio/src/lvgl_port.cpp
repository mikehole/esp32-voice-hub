/**
 * LVGL Port - Thread-safe LVGL access implementation
 */

#include "lvgl_port.h"
#include "lvgl.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Arduino.h>

static SemaphoreHandle_t lvgl_mutex = NULL;

void lvgl_port_init() {
    if (!lvgl_mutex) {
        // Use recursive mutex so same thread can lock multiple times
        // This allows nested calls like: create_ui() -> update_selection()
        lvgl_mutex = xSemaphoreCreateRecursiveMutex();
        if (lvgl_mutex) {
            Serial.println("LVGL port: recursive mutex created");
        } else {
            Serial.println("LVGL port: ERROR - failed to create mutex!");
        }
    }
}

bool lvgl_port_lock(uint32_t timeout_ms) {
    if (!lvgl_mutex) {
        Serial.println("LVGL port: WARNING - mutex not initialized!");
        return false;
    }
    
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mutex, ticks) == pdTRUE;
}

void lvgl_port_unlock() {
    if (lvgl_mutex) {
        xSemaphoreGiveRecursive(lvgl_mutex);
    }
}

uint32_t lvgl_port_task_handler() {
    uint32_t delay_ms = 10;  // Default
    
    if (lvgl_port_lock(50)) {  // 50ms timeout
        delay_ms = lv_timer_handler();
        lvgl_port_unlock();
        
        // Clamp delay to reasonable range
        if (delay_ms > 50) delay_ms = 50;
        if (delay_ms < 5) delay_ms = 5;
    }
    
    return delay_ms;
}
