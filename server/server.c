// Server main file
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>  // ADD THIS at the top for isalnum() function
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#define inet_pton InetPton
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif
#include "server.h"
#include "../common/protocol.h"

int main() {
    printf("Chat server starting...\n");
    
#ifdef _WIN32
    // Initialize Winsock for Windows
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        printf("WSAStartup failed: %d\n", result);
        return 1;
    }
    printf("Winsock initialized\n");
#endif

    server_t server;

    if (server_init(&server) != 0) {
        fprintf(stderr, "Failed to initialize server\n");
        server_cleanup(&server);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    printf("Server initialized successfully\n");
    printf("Press Ctrl+C to stop the server\n");

    if (server_run(&server) != 0) {
        fprintf(stderr, "Server encountered an error\n");
        server_cleanup(&server);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    server_cleanup(&server);
    
#ifdef _WIN32
    WSACleanup();
    printf("Winsock cleaned up\n");
#endif
    
    printf("Server stopped\n");
    return 0;
}

// Function to initialize the server
int server_init(server_t *server) {
    printf("Initializing server...\n");
    // Initialize server structure
    memset(server, 0, sizeof(server_t)); // Clear the server structure
    server->running = 1;

    // Create welcome socket
    server->welcome_socket = socket(AF_INET, SOCK_STREAM, 0);  // Address family -IPv4, socket type - TCP, protocol - 0 (default)
    if (server->welcome_socket < 0) {
        perror("Failed to create welcome socket");
        return -1;
    }

    printf("Welcome socket created\n");

    //Configure socket to address and port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // Clear the address structure
    server_addr.sin_family = AF_INET; // Address family - IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any available interface
    server_addr.sin_port = htons(DEFAULT_TCP_PORT); // Port number in network byte order

    //bind the socket to the address and port
    if (bind(server->welcome_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to bind welcome socket");
        close(server->welcome_socket);
        return -1;
    }
    printf("Welcome socket bound to port %d\n", DEFAULT_TCP_PORT);

    // Set the socket to listen for incoming connections
    if (listen(server->welcome_socket, MAX_CLIENTS) < 0) {
        perror("Failed to listen on welcome socket");
        close(server->welcome_socket);
        return -1;
    }
    printf("Server listening on port %d\n", DEFAULT_TCP_PORT);    // Initialize file descriptor set
    FD_ZERO(&server->master_fds); // Clear the master file descriptor set
    FD_SET(server->welcome_socket, &server->master_fds); // Add the welcome socket to the set
    server->max_fd = server->welcome_socket; // Set the maximum file descriptor to the welcome socket
    
    // Initialize multicast socket
    if (init_multicast_socket(server) != 0) {
        printf("Failed to initialize multicast socket\n");
        close(server->welcome_socket);
        return -1;
    }
    
    // Initialize threading
    if (init_threading(server) != 0) {
        printf("Failed to initialize threading\n");
        close(server->welcome_socket);
        close(server->multicast_socket);
        return -1;
    }
    
    printf("Server initialization complete (TCP + UDP + Threading)\n");
    return 0;
}

// Cleanup function to close sockets and free resources
void server_cleanup(server_t *server) {
    printf("Cleaning up server...\n");

    // Stop server
    server->running = 0;
    
    // Cleanup threading
    cleanup_threading(server);

    // Close multicast socket
    if (server->multicast_socket >= 0) {
        close(server->multicast_socket);
        printf("Multicast socket closed\n");
    }

    // Close welcome socket
    if (server->welcome_socket >= 0) {
        close(server->welcome_socket);
        printf("Welcome socket closed\n");
    }
    // Close all client sockets
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i].is_active && server->clients[i].socket_fd >= 0) {
            close(server->clients[i].socket_fd);
            printf("Closed client socket %d\n", server->clients[i].socket_fd);
        }
    }
    printf("Server cleanup complete\n");
}

// Function to run the server and handle incoming connections
int server_run(server_t *server) {
    printf("Server is running, wating for connections...\n");

    while (server->running) {
        server->read_fds = server->master_fds;// Copy the master set to read_fds

        // Wait for activity on any socket
        int activity = select(server->max_fd + 1, &server->read_fds, NULL, NULL, NULL);//field: check from 0 to max_fd + 1,socket to check, write check,errors check, timeout

        if (activity < 0) {
            perror("select error");
            return -1;
        }

        // Check if there is activity on the welcome socket
        if (FD_ISSET(server->welcome_socket, &server->read_fds)) {
            handle_new_connection(server); 
        }
        // Check for activity on client sockets
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (server->clients[i].is_active && FD_ISSET(server->clients[i].socket_fd, &server->read_fds)) {
                // Handle client message
                if (handle_client_message(server, i) < 0) {// If handling fails, mark client as inactive
                    printf("Client %d disconnected\n", i);
                    close(server->clients[i].socket_fd); // Close the client socket
                    FD_CLR(server->clients[i].socket_fd, &server->master_fds); // Remove from master set
                    server->clients[i].is_active = 0; // Mark client as inactive
                    memset(&server->clients[i], 0, sizeof(client_t)); // Clear client structure
                }
            }
        }

        // --- Timeout check for all clients ---
        time_t current_time = time(NULL);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (server->clients[i].is_active && 
                difftime(current_time, server->clients[i].last_activity) > CONNECTION_TIMEOUT_SEC) {
                // Client has timed out
                printf("Client %d timed out\n", i);
                close(server->clients[i].socket_fd); // Close the client socket
                FD_CLR(server->clients[i].socket_fd, &server->master_fds); // Remove from master set
                server->clients[i].is_active = 0; // Mark client as inactive
                memset(&server->clients[i], 0, sizeof(client_t)); // Clear client structure
            }
        }

    }
    printf("Server is stopping...\n");
    return 0;   
}

// Function to handle new client connections
int handle_new_connection(server_t *server) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Accept the new connection
    int client_socket = accept(server->welcome_socket, (struct sockaddr *)&client_addr, &client_len);// feild: welcome socket, address of client, size of client address

    if (client_socket < 0) {
        perror("Failed to accept new connection");
        return -1;
    }

    printf("New connection accepted: socket %s: %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));// Print client IP and port
      // Find an available slot for the new client
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!server->clients[i].is_active) {
            // Initialize the new client
            server->clients[i].socket_fd = client_socket;
            server->clients[i].is_active = 1;// Mark client as active
            server->clients[i].state = CLIENT_AUTHENTICATING; // Set initial state
            server->clients[i].current_room_id = -1; // Not in a room
            server->clients[i].last_activity = time(NULL); // Set last activity time

            FD_SET(client_socket, &server->master_fds); // Add client socket to the master set
            if (client_socket > server->max_fd) {
                server->max_fd = client_socket; // Update max_fd if necessary
            }

            // Create thread for this client
            if (create_client_thread(server, i) != 0) {
                printf("Failed to create thread for client %d, using select() mode\n", i);
                // Continue with select() mode for this client
            }

            printf("Client connected: socket %d, index %d\n", client_socket, i);
            return 0; 
        }
    }
    printf("Server is full, rejecting new connection\n");
    close(client_socket); // Close the socket if no slots are available
    return -1; 
}

// Function to handle messages from a client
int handle_client_message(server_t *server, int client_index) {
    printf("Handling message from client %d\n", client_index);
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer)); // Clear the buffer

    // Read message from the client
    int bytes_received = recv(server->clients[client_index].socket_fd, buffer, sizeof(buffer) - 1, 0);// field: socket, buffer to store data, size of buffer - 1, flags (0 for no flags)
    printf("Client %d: recv() returned %d bytes\n", client_index, bytes_received);

    if (bytes_received <= 0) {
        if (bytes_received < 0) {
            perror("recv error");
            printf("Client %d: recv() error: errno=%d (%s)\n", client_index, errno, strerror(errno));
        } else {
           printf("Client %d: Connection closed by client (recv=0)\n", client_index);
        }
        return -1; // Client disconnected or error
    }

    server->clients[client_index].last_activity = time(NULL); // Update last activity time

    struct message_header *header = (struct message_header *)buffer; // Cast buffer to message header
    printf("Received message from client %d: type=%d, length=%d\n", client_index, header->msg_type, header->msg_length);
    printf("Processing message type %d for client %d\n", header->msg_type, client_index);

    // Handle different message types based on the header
    switch (header->msg_type) {
    case LOGIN_REQUEST:
        return handle_login_request(server, client_index, (struct login_request*)buffer);
    
    case JOIN_ROOM_REQUEST:
        return handle_join_room_request(server, client_index, (struct join_room_request*)buffer);
    
    case LEAVE_ROOM_REQUEST:
        return handle_leave_room_request(server, client_index);
    
    case CREATE_ROOM_REQUEST:
        return handle_create_room_request(server, client_index, (struct create_room_request*)buffer);
    
    case CHAT_MESSAGE:
        return handle_chat_message(server, client_index, (struct chat_message*)buffer);
    
    case PRIVATE_MESSAGE:
        return handle_private_message(server, client_index, (struct private_message*)buffer);
    
    case KEEPALIVE:
        return handle_keepalive(server, client_index);
    
    case DISCONNECT_REQUEST:
        return handle_disconnect_request(server, client_index);
    
    case ROOM_LIST_REQUEST:
        return handle_room_list_request(server, client_index);
    
    case USER_LIST_REQUEST:
        return handle_user_list_request(server, client_index);
    
    default:
        printf("Unknown message type: 0x%04X\n", header->msg_type);
        return 0;
    
    }
}

// Helper: Send a create room error response
void send_create_room_error(server_t *server, int client_index, uint16_t error_code, const char *msg) {
    struct create_room_response response;
    memset(&response, 0, sizeof(response));
    response.msg_type = CREATE_ROOM_FAILED;
    response.timestamp = time(NULL);
    response.session_token = server->clients[client_index].session_token;
    response.error_code = error_code;
    snprintf(response.error_msg, sizeof(response.error_msg), "%s", msg);
    response.error_msg_len = strlen(response.error_msg);
    response.msg_length = sizeof(response);
    send(server->clients[client_index].socket_fd, &response, sizeof(response), 0);
}

// Helper: Validate room name
int is_valid_room_name(const char *name, int len) {
    if (len <= 0 || len >= 32) return 0;
    for (int i = 0; i < len; ++i) {
        if (name[i] < 32 || name[i] > 126) return 0; // printable ASCII
    }
    return 1;
}

int is_valid_password(const char *pw, int len) {
    // Check length
    if (len < 3 || len > 20) {
        return 0;
    }
    
    // Check for null or empty
    if (pw == NULL || len == 0) {
        return 0;
    }
    
    // Check for valid characters (letters, numbers, basic symbols)
    for (int i = 0; i < len; i++) {
        if (!isalnum(pw[i]) && pw[i] != '_' && pw[i] != '-' && pw[i] != '.') {
            return 0;
        }
    }
    
    return 1; // Valid password
}

int handle_create_room_request(server_t *server, int client_index, struct create_room_request *req) {
    printf("Client %d wants to create room: %.*s\n", client_index, req->room_name_len, req->room_name);

    // Check if client is logged in
    if (server->clients[client_index].state != CLIENT_CONNECTED) {
        send_create_room_error(server, client_index, ROOM_NOT_FOUND, "Not logged in");
        return 0;
    }

    // Validate room name
    if (!is_valid_room_name(req->room_name, req->room_name_len)) {
        send_create_room_error(server, client_index, ROOM_NAME_EXISTS, "Invalid room name");
        return 0;
    }

    // Validate password
    if (!is_valid_password(req->room_password, req->password_len)) {
        send_create_room_error(server, client_index, ROOM_WRONG_PASSWORD, "Invalid password length");
        return 0;
    }

    // Validate max users
    if (req->max_users <= 0 || req->max_users > MAX_CLIENTS) {
        send_create_room_error(server, client_index, ROOM_FULL, "Invalid max users");
        return 0;
    }

    // Find free room slot
    int room_slot = find_free_room_slot(server);
    if (room_slot == -1) {
        send_create_room_error(server, client_index, ROOM_FULL, "Server room limit reached");
        return 0;
    }

    // Check if room name already exists
    char clean_name[MAX_ROOM_NAME_LEN + 1];
    memcpy(clean_name, req->room_name, req->room_name_len);
    clean_name[req->room_name_len] = '\0';

    if (find_room_by_name(server, clean_name) != -1) {
        send_create_room_error(server, client_index, ROOM_NAME_EXISTS, "Room name already exists");
        return 0;
    }

    // Create the room
    room_t *room = &server->rooms[room_slot];
    room->room_id = room_slot + 1;
    strncpy(room->room_name, req->room_name, req->room_name_len);
    room->room_name[req->room_name_len] = '\0';

    if (req->password_len > 0) {
        strncpy(room->password, req->room_password, req->password_len);
        room->password[req->password_len] = '\0';
    } else {
        room->password[0] = '\0';
    }    room->max_clients = req->max_users;
    room->client_count = 0;
    room->is_active = 1;

    // Generate multicast address
    sprintf(room->multicast_addr, "%s%d", MULTICAST_BASE_IP, room->room_id);
    room->multicast_port = MULTICAST_PORT_START + room->room_id;

    // Fill response with room info
    struct create_room_response response;
    memset(&response, 0, sizeof(response));
    response.msg_type = CREATE_ROOM_SUCCESS;
    response.timestamp = time(NULL);
    response.session_token = server->clients[client_index].session_token;
    response.room_id = room->room_id;
    strncpy(response.room_name, room->room_name, sizeof(response.room_name));
    strncpy(response.multicast_addr, room->multicast_addr, sizeof(response.multicast_addr));
    response.multicast_port = room->multicast_port;
    response.msg_length = sizeof(response);
    response.error_code = ROOM_SUCCESS_CODE;
    response.error_msg_len = 0;

    send(server->clients[client_index].socket_fd, &response, sizeof(response), 0);

    printf("Room '%s' created with ID %d\n", room->room_name, room->room_id);
    return 0;
}

int find_free_room_slot(server_t *server) {
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!server->rooms[i].is_active) {
            return i;
        }
    }
    return -1;  // No free slots
}


int find_room_by_name(server_t *server, const char *room_name) {
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (server->rooms[i].is_active && 
            strcmp(server->rooms[i].room_name, room_name) == 0) {
            return i;
        }
    }
    return -1;  // Room not found
}

int find_client_by_socket(server_t *server, int socket_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i].is_active && 
            server->clients[i].socket_fd == socket_fd) {
            return i;
        }
    }
    return -1;  // Client not found
}


// Helper: Send a join room error response
void send_join_room_error(server_t *server, int client_index, uint16_t error_code, const char *msg) {
    struct join_room_response response;
    memset(&response, 0, sizeof(response));
    response.msg_type = JOIN_ROOM_FAILED;
    response.timestamp = time(NULL);
    response.session_token = server->clients[client_index].session_token;
    response.error_code = error_code;
    snprintf(response.error_msg, sizeof(response.error_msg), "%s", msg);
    response.error_msg_len = strlen(response.error_msg);
    response.msg_length = sizeof(response);
    send(server->clients[client_index].socket_fd, &response, sizeof(response), 0);
}

int handle_join_room_request(server_t *server, int client_index, struct join_room_request *req) {
    printf("Client %d wants to join room: %.*s\n", client_index, req->room_name_len, req->room_name);

    // Check if client is logged in
    if (server->clients[client_index].state != CLIENT_CONNECTED) {
        send_join_room_error(server, client_index, ROOM_NOT_FOUND, "Not logged in");
        return 0;
    }

    // Validate room name
    if (!is_valid_room_name(req->room_name, req->room_name_len)) {
        send_join_room_error(server, client_index, ROOM_NOT_FOUND, "Invalid room name");
        return 0;
    }

    // Lock room access for thread safety
#ifdef _WIN32
    WaitForSingleObject(server->room_mutex, INFINITE);
#else
    pthread_mutex_lock(&server->room_mutex);
#endif

    // Find the room
    char clean_name[MAX_ROOM_NAME_LEN + 1];
    memcpy(clean_name, req->room_name, req->room_name_len);
    clean_name[req->room_name_len] = '\0';
    int room_index = find_room_by_name(server, clean_name);
    if (room_index == -1) {
#ifdef _WIN32
        ReleaseMutex(server->room_mutex);
#else
        pthread_mutex_unlock(&server->room_mutex);
#endif
        send_join_room_error(server, client_index, ROOM_NOT_FOUND, "Room not found");
        return 0;
    }

    room_t *room = &server->rooms[room_index];

    // Check password if room has one
    if (strlen(room->password) > 0) {
        if (req->password_len != strlen(room->password) ||
            strncmp(room->password, req->room_password, req->password_len) != 0) {
#ifdef _WIN32
            ReleaseMutex(server->room_mutex);
#else
            pthread_mutex_unlock(&server->room_mutex);
#endif
            send_join_room_error(server, client_index, ROOM_WRONG_PASSWORD, "Wrong room password");
            return 0;
        }
    }

    // Check room capacity
    if (room->max_clients > 0 && room->client_count >= room->max_clients) {
#ifdef _WIN32
        ReleaseMutex(server->room_mutex);
#else
        pthread_mutex_unlock(&server->room_mutex);
#endif
        send_join_room_error(server, client_index, ROOM_FULL, "Room is full");
        return 0;
    }

    // Join the room
    server->clients[client_index].state = CLIENT_IN_ROOM;
    server->clients[client_index].current_room_id = room->room_id;
    room->client_count++;

    // Send success response
    struct join_room_response response;
    memset(&response, 0, sizeof(response));
    response.msg_type = JOIN_ROOM_SUCCESS;
    response.msg_length = sizeof(response);
    response.timestamp = time(NULL);
    response.session_token = server->clients[client_index].session_token;
    response.room_id = room->room_id;
    strncpy(response.multicast_addr, room->multicast_addr, sizeof(response.multicast_addr));
    response.multicast_port = room->multicast_port;
    response.error_code = ROOM_SUCCESS_CODE;
    response.error_msg_len = 0;

    // Unlock room access
#ifdef _WIN32
    ReleaseMutex(server->room_mutex);
#else
    pthread_mutex_unlock(&server->room_mutex);
#endif

    send(server->clients[client_index].socket_fd, &response, sizeof(response), 0);

    printf("Client %d joined room %s (ID: %d)\n", client_index, room->room_name, room->room_id);

    return 0;
}

// Helper: Send a leave room response (success or error)
void send_leave_room_response(server_t *server, int client_index, uint16_t error_code, const char *msg) {
    struct leave_room_response response;
    memset(&response, 0, sizeof(response));
    response.msg_type = LEAVE_ROOM_RESPONSE;
    response.timestamp = time(NULL);
    response.session_token = server->clients[client_index].session_token;
    response.error_code = error_code;
    if (msg) {
        snprintf(response.error_msg, sizeof(response.error_msg), "%s", msg);
        response.error_msg_len = strlen(response.error_msg);
    } else {
        response.error_msg_len = 0;
    }
    response.msg_length = sizeof(response);
    send(server->clients[client_index].socket_fd, &response, sizeof(response), 0);
}

int handle_leave_room_request(server_t *server, int client_index) {
    client_t *client = &server->clients[client_index];

    // Check if client is in a room
    if (client->state != CLIENT_IN_ROOM || client->current_room_id < 0) {
        send_leave_room_response(server, client_index, ROOM_NOT_FOUND, "Not in a room");
        return 0;
    }

    // Lock room access for thread safety
#ifdef _WIN32
    WaitForSingleObject(server->room_mutex, INFINITE);
#else
    pthread_mutex_lock(&server->room_mutex);
#endif

    // Find the room by ID
    int room_index = -1;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (server->rooms[i].is_active && server->rooms[i].room_id == client->current_room_id) {
            room_index = i;
            break;
        }
    }
    if (room_index == -1) {
        client->state = CLIENT_CONNECTED;
        client->current_room_id = -1;
#ifdef _WIN32
        ReleaseMutex(server->room_mutex);
#else
        pthread_mutex_unlock(&server->room_mutex);
#endif
        send_leave_room_response(server, client_index, ROOM_NOT_FOUND, "Room not found");
        return 0;
    }

    room_t *room = &server->rooms[room_index];

    // Remove client from room
    if (room->client_count > 0) {
        room->client_count--;
    }
    client->state = CLIENT_CONNECTED;
    client->current_room_id = -1;

    // Optionally, deactivate room if empty
    if (room->client_count == 0) {
        room->is_active = 0;
        printf("Room %s (ID: %d) deactivated (empty)\n", room->room_name, room->room_id);
    }

    // Unlock room access
#ifdef _WIN32
    ReleaseMutex(server->room_mutex);
#else
    pthread_mutex_unlock(&server->room_mutex);
#endif

    send_leave_room_response(server, client_index, ROOM_SUCCESS_CODE, NULL);

    printf("Client %d left room %s (ID: %d)\n", client_index, room->room_name, room->room_id);
    return 0;
}

// Add these missing functions to the end of your server.c file:

int handle_login_request(server_t *server, int client_index, struct login_request *req) {
    printf("Login request from client %d, username: %.*s\n", 
           client_index, req->username_len, req->username);
    
    client_t *client = &server->clients[client_index];

    // Check if client is in the correct state for login
    if (client->state != CLIENT_AUTHENTICATING) {
        printf("Client %d not in authenticating state\n", client_index);
        return 0;
    }

    // Basic validation
    if (req->username_len <= 0 || req->username_len >= MAX_USERNAME_LEN) {
        printf("Invalid username length from client %d\n", client_index);
        return 0;
    }

    // Update client state
    strncpy(client->username, req->username, req->username_len);
    client->username[req->username_len] = '\0';
    client->state = CLIENT_CONNECTED;
    client->session_token = generate_session_token();
    client->current_room_id = -1;
    client->last_activity = time(NULL);

    // Send success response
    struct login_response response;
    memset(&response, 0, sizeof(response));
    response.msg_type = LOGIN_SUCCESS;
    response.msg_length = sizeof(response);
    response.timestamp = time(NULL);
    response.session_token = client->session_token;
    response.error_code = LOGIN_SUCCESS_CODE;
    response.error_msg_len = 0;

    send(client->socket_fd, &response, sizeof(response), 0);
    printf("Client %d logged in as: %s\n", client_index, client->username);
    return 0;
}

// Function to generate a unique session token for each client
uint32_t generate_session_token(void) {
    static uint32_t counter = 1000;
    uint32_t token = (uint32_t)time(NULL) + counter;
    counter++;
    if (token == 0) token = 1;
    return token;
}

// Function to handle keepalive messages from clients
int handle_keepalive(server_t *server, int client_index) {
    printf("Keepalive from client %d\n", client_index);
    server->clients[client_index].last_activity = time(NULL);
    return 0;
}

// Function to handle disconnect requests from clients
int handle_disconnect_request(server_t *server, int client_index) {
    printf("Client %d requested disconnect\n", client_index);
    
    client_t *client = &server->clients[client_index];
    
    // Send disconnect response first
    struct disconnect_response resp;
    memset(&resp, 0, sizeof(resp));
    resp.msg_type = DISCONNECT_SUCCESS;
    resp.msg_length = sizeof(resp);
    resp.timestamp = time(NULL);
    resp.session_token = client->session_token;
    resp.status_code = 0; // Success
    resp.status_msg_len = 0;
    
    send(client->socket_fd, &resp, sizeof(resp), 0);
    
    // If client is in a room, remove them from it first
    if (client->state == CLIENT_IN_ROOM && client->current_room_id >= 0) {
        printf("Client %d leaving room %d before disconnect\n", client_index, client->current_room_id);
        
        // Find the room and decrement client count
        for (int i = 0; i < MAX_ROOMS; i++) {
            if (server->rooms[i].is_active && server->rooms[i].room_id == client->current_room_id) {
                if (server->rooms[i].client_count > 0) {
                    server->rooms[i].client_count--;
                }
                
                // Deactivate room if empty
                if (server->rooms[i].client_count == 0) {
                    server->rooms[i].is_active = 0;
                    printf("Room %d deactivated (empty)\n", server->rooms[i].room_id);
                }
                break;
            }
        }
    }
    
    printf("Client %d (%s) disconnected gracefully\n", 
           client_index, 
           (strlen(client->username) > 0) ? client->username : "unknown");
    
    // Return -1 to signal disconnection to the main loop
    return -1;
}

// Function to handle chat messages from clients
int handle_chat_message(server_t *server, int client_index, struct chat_message *msg) {
    client_t *sender = &server->clients[client_index];
    
    printf("Chat message from client %d in room %d: %.*s\n", 
           client_index, sender->current_room_id, msg->message_len, msg->message);

    // Check if client is in a room
    if (sender->state != CLIENT_IN_ROOM || sender->current_room_id < 0) {
        printf("Client %d not in a room, ignoring chat message\n", client_index);
        return 0;
    }

    // Lock room access for thread safety
#ifdef _WIN32
    WaitForSingleObject(server->room_mutex, INFINITE);
#else
    pthread_mutex_lock(&server->room_mutex);
#endif

    // Create multicast message with sender info
    struct chat_message multicast_msg;
    memset(&multicast_msg, 0, sizeof(multicast_msg));
    multicast_msg.msg_type = CHAT_MESSAGE;
    multicast_msg.timestamp = time(NULL);
    multicast_msg.room_id = sender->current_room_id;
    
    // Add sender username
    strncpy(multicast_msg.sender_username, sender->username, sizeof(multicast_msg.sender_username) - 1);
    multicast_msg.sender_username_len = strlen(sender->username);
    
    // Copy message
    size_t safe_msg_len = (msg->message_len < sizeof(multicast_msg.message)) ? 
                         msg->message_len : sizeof(multicast_msg.message) - 1;
    strncpy(multicast_msg.message, msg->message, safe_msg_len);
    multicast_msg.message_len = safe_msg_len;
    multicast_msg.msg_length = sizeof(multicast_msg);

    // Send via UDP multicast to room
    int result = send_multicast_message(server, sender->current_room_id, 
                                       (char*)&multicast_msg, sizeof(multicast_msg));

    // Unlock room access
#ifdef _WIN32
    ReleaseMutex(server->room_mutex);
#else
    pthread_mutex_unlock(&server->room_mutex);
#endif

    if (result == 0) {
        printf("Chat message sent via multicast to room %d\n", sender->current_room_id);
    } else {
        printf("Failed to send multicast message to room %d\n", sender->current_room_id);
    }
    
    return result;
}

int handle_private_message(server_t *server, int client_index, struct private_message *msg) {
    client_t *sender = &server->clients[client_index];
    
    // Validate sender authentication
    if (sender->state == CLIENT_DISCONNECTED || 
        sender->session_token != msg->session_token) {
        printf("Invalid session token for private message from client %d\n", client_index);
        send_error_response(sender->socket_fd, "Invalid session");
        return -1;
    }
    
    // Null-terminate the target username and message for safety
    char target_username[33] = {0};
    char message_content[513] = {0};
    
    size_t username_len = (msg->target_username_len < 32) ? msg->target_username_len : 32;
    size_t message_len = (msg->message_len < 512) ? msg->message_len : 512;
    
    memcpy(target_username, msg->target_username, username_len);
    memcpy(message_content, msg->message, message_len);
    
    printf("Private message from %s to %s: %s\n", 
           sender->username, target_username, message_content);
    
    // Lock client access for thread safety
#ifdef _WIN32
    WaitForSingleObject(server->client_mutex, INFINITE);
#else
    pthread_mutex_lock(&server->client_mutex);
#endif
    
    // Find target client by username
    int target_index = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i].is_active && 
            server->clients[i].state != CLIENT_DISCONNECTED &&
            strcmp(server->clients[i].username, target_username) == 0) {
            target_index = i;
            break;
        }
    }
    
    if (target_index == -1) {
#ifdef _WIN32
        ReleaseMutex(server->client_mutex);
#else
        pthread_mutex_unlock(&server->client_mutex);
#endif
        printf("Target user '%s' not found or not online\n", target_username);
        send_error_response(sender->socket_fd, "User not found or offline");
        return -1;
    }
    
    client_t *target = &server->clients[target_index];
    
    // ALWAYS send private messages via direct TCP (unicast only!)
    struct private_message forward_msg;
    memset(&forward_msg, 0, sizeof(forward_msg));
    forward_msg.msg_type = PRIVATE_MESSAGE;
    forward_msg.timestamp = time(NULL);
    forward_msg.session_token = 0;
    forward_msg.target_username_len = strlen(sender->username);
    strncpy(forward_msg.target_username, sender->username, sizeof(forward_msg.target_username) - 1);
    forward_msg.message_len = message_len;
    memcpy(forward_msg.message, message_content, message_len);
    forward_msg.msg_length = sizeof(forward_msg);
    
    // Send directly to target via TCP (unicast)
    int sent = send(target->socket_fd, &forward_msg, sizeof(forward_msg), 0);
    
#ifdef _WIN32
    ReleaseMutex(server->client_mutex);
#else
    pthread_mutex_unlock(&server->client_mutex);
#endif
    
    if (sent != -1) {
        printf("Private message delivered via TCP unicast from %s to %s\n", sender->username, target_username);
        return 0;
    } else {
        printf("Failed to send private message via TCP\n");
        send_error_response(sender->socket_fd, "Failed to deliver message");
        return -1;
    }
}

// Helper function to send error responses
void send_error_response(int socket_fd, const char *error_msg) {
    struct error_message response;
    memset(&response, 0, sizeof(response));
    response.msg_type = ERROR_MESSAGE;
    response.timestamp = time(NULL);
    response.error_code = 1; // Generic error code
    
    size_t msg_len = strlen(error_msg);
    if (msg_len > MAX_ERROR_MSG_LEN - 1) {
        msg_len = MAX_ERROR_MSG_LEN - 1;
    }
    
    response.error_msg_len = (uint8_t)msg_len;
    memcpy(response.error_msg, error_msg, msg_len);
    response.error_msg[msg_len] = '\0';
    response.msg_length = sizeof(response);
    
    send(socket_fd, &response, sizeof(response), 0);
    printf("Error response sent: %s\n", error_msg);
}

// ================================
// MULTICAST IMPLEMENTATION
// ================================

// Initialize UDP socket for multicast communication
int init_multicast_socket(server_t *server) {
    printf("Initializing multicast socket...\n");
    
    // Create UDP socket
    server->multicast_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server->multicast_socket < 0) {
        perror("Failed to create multicast socket");
        return -1;
    }
    
    // Enable broadcast for the socket
    int broadcast_enable = 1;
    if (setsockopt(server->multicast_socket, SOL_SOCKET, SO_BROADCAST, 
                   (char*)&broadcast_enable, sizeof(broadcast_enable)) < 0) {
        perror("Failed to enable broadcast on multicast socket");
        close(server->multicast_socket);
        return -1;
    }
    
    // Set multicast TTL for lab environment (multi-hop)
    int ttl = MULTICAST_TTL_DEFAULT; // 32 hops for lab topology
    if (setsockopt(server->multicast_socket, IPPROTO_IP, IP_MULTICAST_TTL,
                   (char*)&ttl, sizeof(ttl)) < 0) {
        perror("Failed to set multicast TTL");
        close(server->multicast_socket);
        return -1;
    }
    
    printf("Multicast socket initialized with TTL=%d for lab environment\n", ttl);
    return 0;
}

// Send multicast message to a specific room
int send_multicast_message(server_t *server, int room_id, const char *message, size_t message_len) {
    // Find the room
    int room_index = -1;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (server->rooms[i].is_active && server->rooms[i].room_id == room_id) {
            room_index = i;
            break;
        }
    }
    
    if (room_index == -1) {
        printf("Room %d not found for multicast\n", room_id);
        return -1;
    }
    
    room_t *room = &server->rooms[room_index];
    
    // Setup multicast address
    struct sockaddr_in multicast_addr;
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_port = htons(room->multicast_port);
    
    if (inet_pton(AF_INET, room->multicast_addr, &multicast_addr.sin_addr) <= 0) {
        printf("Invalid multicast address: %s\n", room->multicast_addr);
        return -1;
    }
    
    // Send the message
    int sent = sendto(server->multicast_socket, message, message_len, 0,
                     (struct sockaddr*)&multicast_addr, sizeof(multicast_addr));
    
    if (sent < 0) {
        perror("Failed to send multicast message");
        return -1;
    }
    
    printf("Multicast message sent to room %d (%s:%d): %d bytes\n", 
           room_id, room->multicast_addr, room->multicast_port, sent);
    return 0;
}

// ================================
// THREADING IMPLEMENTATION
// ================================

// Initialize threading components
int init_threading(server_t *server) {
    printf("Initializing threading...\n");
    
#ifdef _WIN32
    // Initialize mutexes for Windows
    server->client_mutex = CreateMutex(NULL, FALSE, NULL);
    if (server->client_mutex == NULL) {
        printf("Failed to create client mutex\n");
        return -1;
    }
    
    server->room_mutex = CreateMutex(NULL, FALSE, NULL);
    if (server->room_mutex == NULL) {
        printf("Failed to create room mutex\n");
        CloseHandle(server->client_mutex);
        return -1;
    }
    
    // Initialize thread pool
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        server->thread_pool[i] = NULL;
    }
#else
    // Initialize mutexes for POSIX
    if (pthread_mutex_init(&server->client_mutex, NULL) != 0) {
        printf("Failed to initialize client mutex\n");
        return -1;
    }
    
    if (pthread_mutex_init(&server->room_mutex, NULL) != 0) {
        printf("Failed to initialize room mutex\n");
        pthread_mutex_destroy(&server->client_mutex);
        return -1;
    }
    
    // Initialize thread pool
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        server->thread_pool[i] = 0;
    }
#endif
    
    printf("Threading initialized successfully\n");
    return 0;
}

// Cleanup threading components
void cleanup_threading(server_t *server) {
    printf("Cleaning up threading...\n");
    
#ifdef _WIN32
    // Wait for all threads to complete
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        if (server->thread_pool[i] != NULL) {
            WaitForSingleObject(server->thread_pool[i], 5000); // 5 second timeout
            CloseHandle(server->thread_pool[i]);
            server->thread_pool[i] = NULL;
        }
    }
    
    // Cleanup mutexes
    if (server->client_mutex != NULL) {
        CloseHandle(server->client_mutex);
        server->client_mutex = NULL;
    }
    if (server->room_mutex != NULL) {
        CloseHandle(server->room_mutex);
        server->room_mutex = NULL;
    }
#else
    // Wait for all threads to complete
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        if (server->thread_pool[i] != 0) {
            pthread_join(server->thread_pool[i], NULL);
            server->thread_pool[i] = 0;
        }
    }
    
    // Cleanup mutexes
    pthread_mutex_destroy(&server->client_mutex);
    pthread_mutex_destroy(&server->room_mutex);
#endif
    
    printf("Threading cleanup complete\n");
}

// Thread handler for client processing
#ifdef _WIN32
unsigned __stdcall client_thread_handler(void *arg) {
#else
void* client_thread_handler(void *arg) {
#endif
    client_thread_data_t *data =  (client_thread_data_t*)arg;
    server_t *server = data->server;
    int client_index = data->client_index;
    
    printf("Thread started for client %d\n", client_index);
    
    // Process client messages in a loop
    while (server->running && server->clients[client_index].is_active) {
        // Use select with timeout to check for messages
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server->clients[client_index].socket_fd, &read_fds);
        
        struct timeval timeout;
        // Check client state and set appropriate timeout
        if (server->clients[client_index].state == CLIENT_AUTHENTICATING) {
            timeout.tv_sec = 300;  // Increase to 5 minutes for manual login
        } else {
            timeout.tv_sec = 120;  // Increase to 2 minutes for normal operations (was 30)
        }
        timeout.tv_usec = 0;
        
        int activity = select(server->clients[client_index].socket_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity > 0 && FD_ISSET(server->clients[client_index].socket_fd, &read_fds)) {
            // Lock client access
#ifdef _WIN32
            WaitForSingleObject(server->client_mutex, INFINITE);
#else
            pthread_mutex_lock(&server->client_mutex);
#endif
            
            // Handle client message
            if (handle_client_message(server, client_index) < 0) {
                printf("Client %d disconnected in thread\n", client_index);
                
                // Store socket fd before clearing
                int socket_fd = server->clients[client_index].socket_fd;
                
                close(server->clients[client_index].socket_fd);
                
                // Remove from master_fds (THIS WAS MISSING!)
                FD_CLR(socket_fd, &server->master_fds);
                
                server->clients[client_index].is_active = 0;
                memset(&server->clients[client_index], 0, sizeof(client_t));
            }
            
            // Unlock client access
#ifdef _WIN32
            ReleaseMutex(server->client_mutex);
#else
            pthread_mutex_unlock(&server->client_mutex);
#endif
        }
        
        // Check for client timeout
        time_t current_time = time(NULL);
        if (server->clients[client_index].is_active &&
            difftime(current_time, server->clients[client_index].last_activity) > CONNECTION_TIMEOUT_SEC) {
            
#ifdef _WIN32
            WaitForSingleObject(server->client_mutex, INFINITE);
#else
            pthread_mutex_lock(&server->client_mutex);
#endif
            
            printf("Client %d timed out in thread\n", client_index);
            
            // Store socket fd before clearing
            int socket_fd = server->clients[client_index].socket_fd;
            
            close(server->clients[client_index].socket_fd);
            
            // Remove from master_fds (THIS WAS MISSING!)
            FD_CLR(socket_fd, &server->master_fds);
            
            server->clients[client_index].is_active = 0;
            memset(&server->clients[client_index], 0, sizeof(client_t));
            
#ifdef _WIN32
            ReleaseMutex(server->client_mutex);
#else
            pthread_mutex_unlock(&server->client_mutex);
#endif
            break;
        }
    }
    
    // Clean up thread data
    free(data);
    printf("Thread ended for client %d\n", client_index);
    
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

// Create a new thread for handling a client
int create_client_thread(server_t *server, int client_index) {
    // Find available thread slot
    int thread_slot = -1;
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
#ifdef _WIN32
        if (server->thread_pool[i] == NULL) {
#else
        if (server->thread_pool[i] == 0) {
#endif
            thread_slot = i;
            break;
        }
    }
    
    if (thread_slot == -1) {
        printf("No available thread slots for client %d\n", client_index);
        return -1;
    }
    
    // Prepare thread data
    client_thread_data_t *thread_data = malloc(sizeof(client_thread_data_t));
    if (!thread_data) {
        printf("Failed to allocate thread data for client %d\n", client_index);
        return -1;
    }
    
    thread_data->server = server;
    thread_data->client_index = client_index;
    
    // Create the thread
#ifdef _WIN32
    server->thread_pool[thread_slot] = (HANDLE)_beginthreadex(
        NULL, 0, client_thread_handler, thread_data, 0, NULL);
    if (server->thread_pool[thread_slot] == NULL) {
        printf("Failed to create thread for client %d\n", client_index);
        free(thread_data);
        return -1;
    }
#else
    if (pthread_create(&server->thread_pool[thread_slot], NULL, 
                      client_thread_handler, thread_data) != 0) {
        printf("Failed to create thread for client %d\n", client_index);
        free(thread_data);
        return -1;
    }
#endif
    
    printf("Thread created for client %d in slot %d\n", client_index, thread_slot);
    return 0;
}

int handle_room_list_request(server_t *server, int client_index) {
    client_t *client = &server->clients[client_index];
    
    // Validate client authentication (though room list might be allowed for any connected client)
    if (client->state == CLIENT_DISCONNECTED) {
        printf("Room list request from disconnected client %d\n", client_index);
        send_error_response(client->socket_fd, "Not connected");
        return -1;
    }
    
    printf("Room list request from client %d\n", client_index);
    
    // Count active rooms first
    uint8_t active_room_count = 0;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (server->rooms[i].is_active) {
            active_room_count++;
        }
    }
    
    // Calculate total message size
    size_t base_size = sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint8_t);
    size_t rooms_data_size = 0;
    
    // Calculate room data size
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (server->rooms[i].is_active) {
            rooms_data_size += sizeof(uint16_t) + // room_id
                              sizeof(uint8_t) +   // room_name_len
                              strlen(server->rooms[i].room_name) + // room_name
                              sizeof(uint8_t) +   // user_count
                              sizeof(uint8_t);    // has_password
        }
    }
    
    size_t total_size = base_size + rooms_data_size;
    
    // Allocate buffer for response
    char *response_buffer = malloc(total_size);
    if (!response_buffer) {
        printf("Memory allocation failed for room list response\n");
        send_error_response(client->socket_fd, "Server error");
        return -1;
    }
    
    // Build response
    char *ptr = response_buffer;
    
    // Header
    *(uint16_t*)ptr = ROOM_LIST_RESPONSE;
    ptr += sizeof(uint16_t);
    
    *(uint16_t*)ptr = (uint16_t)total_size;
    ptr += sizeof(uint16_t);
    
    *(uint32_t*)ptr = time(NULL);
    ptr += sizeof(uint32_t);
    
    *(uint8_t*)ptr = active_room_count;
    ptr += sizeof(uint8_t);
    
    // Add room data
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (server->rooms[i].is_active) {
            // room_id
            *(uint16_t*)ptr = (uint16_t)server->rooms[i].room_id;
            ptr += sizeof(uint16_t);
            
            // room_name_len and room_name
            uint8_t name_len = strlen(server->rooms[i].room_name);
            *(uint8_t*)ptr = name_len;
            ptr += sizeof(uint8_t);
            
            memcpy(ptr, server->rooms[i].room_name, name_len);
            ptr += name_len;
            
            // user_count
            *(uint8_t*)ptr = (uint8_t)server->rooms[i].client_count;
            ptr += sizeof(uint8_t);
            
            // has_password
            *(uint8_t*)ptr = (strlen(server->rooms[i].password) > 0) ? 1 : 0;
            ptr += sizeof(uint8_t);
        }
    }
    
    // Send response
    int sent = send(client->socket_fd, response_buffer, total_size, 0);
    free(response_buffer);
    
    if (sent == -1) {
        printf("Failed to send room list to client %d\n", client_index);
        return -1;
    }
    
    printf("Room list sent to client %d (%d active rooms)\n", client_index, active_room_count);
    return 0;
}

int handle_user_list_request(server_t *server, int client_index) {
    client_t *client = &server->clients[client_index];
    
    // Validate client authentication and that they're in a room
    if (client->state != CLIENT_IN_ROOM || client->current_room_id < 0) {
        printf("User list request from client %d not in a room\n", client_index);
        send_error_response(client->socket_fd, "Not in a room");
        return -1;
    }
    
    printf("User list request from client %d for room %d\n", client_index, client->current_room_id);
    
    // Count users in the same room
    uint8_t user_count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i].is_active && 
            server->clients[i].state == CLIENT_IN_ROOM &&
            server->clients[i].current_room_id == client->current_room_id) {
            user_count++;
        }
    }
    
    // Calculate total message size
    size_t base_size = sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint8_t);
    size_t users_data_size = 0;
    
    // Calculate user data size
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i].is_active && 
            server->clients[i].state == CLIENT_IN_ROOM &&
            server->clients[i].current_room_id == client->current_room_id) {
            users_data_size += sizeof(uint8_t) + // username_len
                              strlen(server->clients[i].username); // username
        }
    }
    
    size_t total_size = base_size + users_data_size;
    
    // Allocate buffer for response
    char *response_buffer = malloc(total_size);
    if (!response_buffer) {
        printf("Memory allocation failed for user list response\n");
        send_error_response(client->socket_fd, "Server error");
        return -1;
    }
    
    // Build response
    char *ptr = response_buffer;
    
    // Header
    *(uint16_t*)ptr = USER_LIST_RESPONSE;
    ptr += sizeof(uint16_t);
    
    *(uint16_t*)ptr = (uint16_t)total_size;
    ptr += sizeof(uint16_t);
    
    *(uint32_t*)ptr = time(NULL);
    ptr += sizeof(uint32_t);
    
    *(uint8_t*)ptr = user_count;
    ptr += sizeof(uint8_t);
    
    // Add user data (format: username_len + username)
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i].is_active && 
            server->clients[i].state == CLIENT_IN_ROOM &&
            server->clients[i].current_room_id == client->current_room_id) {
            
            uint8_t username_len = strlen(server->clients[i].username);
            *(uint8_t*)ptr = username_len;
            ptr += sizeof(uint8_t);
            
            memcpy(ptr, server->clients[i].username, username_len);
            ptr += username_len;
        }
    }
    
    // Send response
    int sent = send(client->socket_fd, response_buffer, total_size, 0);
    free(response_buffer);
    
    if (sent == -1) {
        printf("Failed to send user list to client %d\n", client_index);
        return -1;
    }
    
    printf("User list sent to client %d (%d users in room %d)\n", 
           client_index, user_count, client->current_room_id);
    return 0;
}