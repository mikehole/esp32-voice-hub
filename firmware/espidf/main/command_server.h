/**
 * @file command_server.h
 * @brief WebSocket server for remote control commands
 * 
 * Provides a WebSocket endpoint on port 81 that companion apps can connect to.
 * When the Music wedge sends commands, they're broadcast to all connected clients.
 */

#ifndef COMMAND_SERVER_H
#define COMMAND_SERVER_H

#include <stdbool.h>

/**
 * @brief Initialize and start the command WebSocket server
 * @return true on success
 */
bool command_server_start(void);

/**
 * @brief Stop the command server
 */
void command_server_stop(void);

/**
 * @brief Check if any clients are connected
 * @return true if at least one client is connected
 */
bool command_server_has_clients(void);

/**
 * @brief Get number of connected clients
 * @return Number of connected WebSocket clients
 */
int command_server_client_count(void);

// Media control commands - send to all connected clients
void command_send_play_pause(void);
void command_send_next_track(void);
void command_send_prev_track(void);
void command_send_volume_up(void);
void command_send_volume_down(void);
void command_send_mute(void);

// Generic command sender
void command_send(const char* cmd, const char* arg);

#endif // COMMAND_SERVER_H
