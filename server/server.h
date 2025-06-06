#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#endif
#include "../common/protocol.h"
#include <errno.h>
#include <time.h>    


// Server configuration
#define MAX_CLIENTS 50
#define MAX_ROOMS 20

// Client states - state machine
typedef enum {
    CLIENT_DISCONNECTED,
    CLIENT_AUTHENTICATING,
    CLIENT_CONNECTED,
    CLIENT_JOINING_ROOM,
    CLIENT_IN_ROOM,
} client_state_t;

// Client structure
typedef struct {
    int socket_fd;                // Client socket file descriptor
    client_state_t state;         // Current state of the client
    uint32_t session_token;       // Unique session token for the client
    char username[MAX_USERNAME_LEN]; // Username of the client
    int current_room_id;                  // Current room ID, -1 if not in a room
    int is_active;               // 1 if client is active, 0 if disconnected
} client_t;


// Room structure
typedef struct {
    int id;                     // Unique room identifier
    char room_name[MAX_ROOM_NAME_LEN]; // Name of the room
    char password[MAX_PASSWORD_LEN]; // Password for the room, if any
    char multicast_addr[16]; // Multicast address for the room
    uint16_t multicast_port; // Port for multicast
    int max_client;              // Maximum number of users allowed in the room
    int client_count;          // Current number of users in the room
    int is_active;           // 1 if room is active, 0 if closed
} room_t;


// Server structure
typedef struct {
    int welcome_socket; // Socket for accepting new connections
    client_t clients[MAX_CLIENTS]; // Array of connected clients
    room_t rooms[MAX_ROOMS]; // Array of available rooms
    fd_set master_fds; // Master file descriptor set for select()
    int max_fd; // Maximum file descriptor value in the master_fds set
    int running; // 1 if server is running, 0 if stopped
} server_t;


// Function declarations
int server_init(server_t *server);
int server_run(server_t *server);
void server_cleanup(server_t *server);
int handle_new_connection(server_t *server);
int handle_client_message(server_t *server, int client_index);

#endif // SERVER_H