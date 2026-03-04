/**
 * Web Admin Interface
 * Status, settings, and screenshot endpoints
 */

#ifndef WEB_ADMIN_H
#define WEB_ADMIN_H

#include "esp_http_server.h"

// Register admin endpoints with the HTTP server
void web_admin_register(httpd_handle_t server);

// Callback to get current brightness (0-100)
typedef int (*brightness_getter_t)();
typedef void (*brightness_setter_t)(int);

// Set brightness callbacks
void web_admin_set_brightness_callbacks(brightness_getter_t getter, brightness_setter_t setter);

#endif
