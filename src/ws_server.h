#ifndef WS_SERVER_H
#define WS_SERVER_H

#include "esp_http_server.h"

// Initialize and start the WebSocket server
httpd_handle_t ws_server_start(void);

// Broadcast a message to all connected clients
void ws_server_broadcast(const char *msg);

#endif // WS_SERVER_H
