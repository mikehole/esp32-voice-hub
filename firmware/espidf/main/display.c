/**
 * Display Module - LCD + LVGL
 * Stub implementation - will add full LVGL support
 */

#include "display.h"
#include "esp_log.h"

static const char *TAG = "display";

void display_init(void)
{
    // TODO: Initialize LCD hardware (SPI)
    // TODO: Initialize LVGL
    // TODO: Create UI widgets
    ESP_LOGI(TAG, "Display init (stub)");
}

void display_loop(void)
{
    // TODO: Call lv_timer_handler()
}

void display_set_state(display_state_t state)
{
    ESP_LOGI(TAG, "Display state: %d", state);
    // TODO: Update avatar and status ring
}

void display_show_notification(const char* title, const char* message)
{
    ESP_LOGI(TAG, "Notification: %s - %s", title, message);
    // TODO: Show notification overlay
}
