/**
 * @file command_server.c
 * @brief WebSocket server for remote control commands
 */

#include "command_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>

static const char* TAG = "cmd_srv";

#define MAX_CLIENTS 4
#define CMD_PORT 81

static httpd_handle_t server = NULL;
static int client_fds[MAX_CLIENTS] = {-1, -1, -1, -1};
static int client_count = 0;

// Track client connections
static void add_client(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] == -1) {
            client_fds[i] = fd;
            client_count++;
            ESP_LOGI(TAG, "Client connected (fd=%d, total=%d)", fd, client_count);
            return;
        }
    }
    ESP_LOGW(TAG, "Max clients reached, rejecting fd=%d", fd);
}

static void remove_client(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] == fd) {
            client_fds[i] = -1;
            client_count--;
            ESP_LOGI(TAG, "Client disconnected (fd=%d, total=%d)", fd, client_count);
            return;
        }
    }
}

// WebSocket handler
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        // New WebSocket connection
        add_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    
    // Receive frame (we don't expect commands from clients, but handle gracefully)
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (ws_pkt.len > 0) {
        uint8_t *buf = malloc(ws_pkt.len + 1);
        if (buf) {
            ws_pkt.payload = buf;
            ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            if (ret == ESP_OK) {
                buf[ws_pkt.len] = 0;
                ESP_LOGI(TAG, "Received: %s", buf);
                // Could handle incoming commands here if needed
            }
            free(buf);
        }
    }
    
    return ESP_OK;
}

// Send message to all connected clients
static void broadcast(const char* msg) {
    if (!server || client_count == 0) return;
    
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = (uint8_t*)msg;
    ws_pkt.len = strlen(msg);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] >= 0) {
            esp_err_t ret = httpd_ws_send_frame_async(server, client_fds[i], &ws_pkt);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send to fd=%d: %d", client_fds[i], ret);
                remove_client(client_fds[i]);
            }
        }
    }
}

bool command_server_start(void) {
    if (server) {
        ESP_LOGW(TAG, "Server already running");
        return true;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CMD_PORT;
    config.ctrl_port = CMD_PORT + 1000;  // Control port must be different
    config.max_open_sockets = MAX_CLIENTS + 1;
    
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %d", ret);
        return false;
    }
    
    // Register WebSocket endpoint
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
        .handle_ws_control_frames = true,
    };
    httpd_register_uri_handler(server, &ws_uri);
    
    // Also register at root for convenience
    httpd_uri_t ws_root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
        .handle_ws_control_frames = true,
    };
    httpd_register_uri_handler(server, &ws_root_uri);
    
    ESP_LOGI(TAG, "Command server started on port %d", CMD_PORT);
    return true;
}

void command_server_stop(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
        client_count = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            client_fds[i] = -1;
        }
        ESP_LOGI(TAG, "Command server stopped");
    }
}

bool command_server_has_clients(void) {
    return client_count > 0;
}

int command_server_client_count(void) {
    return client_count;
}

void command_send(const char* cmd, const char* arg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", cmd);
    if (arg) {
        cJSON_AddStringToObject(root, "arg", arg);
    }
    
    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        ESP_LOGI(TAG, "Sending: %s", json);
        broadcast(json);
        free(json);
    }
    cJSON_Delete(root);
}

void command_send_play_pause(void) {
    command_send("play_pause", NULL);
}

void command_send_next_track(void) {
    command_send("next_track", NULL);
}

void command_send_prev_track(void) {
    command_send("prev_track", NULL);
}

void command_send_volume_up(void) {
    command_send("volume_up", NULL);
}

void command_send_volume_down(void) {
    command_send("volume_down", NULL);
}

void command_send_mute(void) {
    command_send("mute", NULL);
}
