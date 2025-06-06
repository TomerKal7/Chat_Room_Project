// Server main file
#include <stdio.h>
#include "server.h"

int main() {
    printf("Chat server starting...\n");
    server_t server;

    if (server_init(&server) != 0) {
        fprintf(stderr, "Failed to initialize server\n");
        server_cleanup(&server);
        return 1;
    }
    printf("Server initialized successfully\n");
    printf("Press Ctrl+C to stop the server\n");

    if (server_run(&server) != 0) {
        fprintf(stderr, "Server encountered an error\n");
        server_cleanup(&server);
        return 1;
    }
    server_cleanup(&server);
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
    printf("Server listening on port %d\n", DEFAULT_TCP_PORT);

    // Initialize file descriptor set
    FD_ZERO(&server->master_fds); // Clear the master file descriptor set
    FD_SET(server->welcome_socket, &server->master_fds); // Add the welcome socket to the set
    server->max_fd = server->welcome_socket; // Set the maximum file descriptor to the welcome socket
    return 0;
}

// Cleanup function to close sockets and free resources
void server_cleanup(server_t *server) {
    printf("Cleaning up server...\n");

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

            FD_SET(client_socket, &server->master_fds); // Add client socket to the master set
            if (client_socket > server->max_fd) {
                server->max_fd = client_socket; // Update max_fd if necessary
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
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer)); // Clear the buffer

    // Read message from the client
    int bytes_received = recv(server->clients[client_index].socket_fd, buffer, sizeof(buffer) - 1, 0);// field: socket, buffer to store data, size of buffer - 1, flags (0 for no flags)

    if (bytes_received <= 0) {
        if (bytes_received < 0) {
            perror("recv error");
        } else {
            printf("Client %d disconnected\n", client_index);
        }
        return -1; // Client disconnected or error
    }

    struct message_header *header = (struct message_header *)buffer; // Cast buffer to message header
    printf("Received message from client %d: type=%d, length=%d\n", client_index, header->msg_type, header->msg_length);

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

int handle_login_request(server_t *server, int client_index, struct login_request *req) {
    printf("Login request from client %d, username: %.*s\n", 
           client_index, req->username_len, req->username);
    
    // For now, accept all logins (we'll add real validation later)
    struct login_response response;
    memset(&response, 0, sizeof(response));
    
    response.msg_type = LOGIN_SUCCESS;
    response.msg_length = sizeof(response);
    response.timestamp = time(NULL);
    response.session_token = generate_session_token();
    response.error_code = LOGIN_SUCCESS_CODE;
    response.error_msg_len = 0;
    
    // Update client state
    server->clients[client_index].state = CLIENT_CONNECTED;
    server->clients[client_index].session_token = response.session_token;
    strncpy(server->clients[client_index].username, req->username, req->username_len);
    server->clients[client_index].username[req->username_len] = '\0';
    
    // Send response
    send(server->clients[client_index].socket_fd, &response, sizeof(response), 0);
    
    printf("Client %d logged in as: %s\n", client_index, server->clients[client_index].username);
    return 0;
}

uint32_t generate_session_token(void) {
    static uint32_t counter = 1000;  // Starting value
    
    // Simple token generation: current time + counter
    uint32_t token = (uint32_t)time(NULL) + counter;
    counter++;
    
    // Make sure we never return 0 (invalid token)
    if (token == 0) {
        token = 1;
    }
    
    return token;
}

int handle_keepalive(server_t *server, int client_index) {
    printf("Keepalive from client %d\n", client_index);
    
    // Update last activity time (we'll add this field later if needed)
    // For now, just send keepalive back
    struct keepalive response;
    response.msg_type = KEEPALIVE;
    response.msg_length = sizeof(response);
    response.timestamp = time(NULL);
    response.session_token = server->clients[client_index].session_token;
    
    send(server->clients[client_index].socket_fd, &response, sizeof(response), 0);
    
    return 0;
}

int handle_disconnect_request(server_t *server, int client_index) {
    printf("Client %d requested disconnect\n", client_index);
    
    // Send acknowledgment
    struct disconnect_request ack;
    ack.msg_type = DISCONNECT_ACK;
    ack.msg_length = sizeof(ack);
    ack.timestamp = time(NULL);
    ack.session_token = server->clients[client_index].session_token;
    
    send(server->clients[client_index].socket_fd, &ack, sizeof(ack), 0);
    
    // Return -1 to signal disconnection
    return -1;
}

// int handle_create_room_request(server_t *server, int client_index, struct create_room_request *req) {
//     printf("Client %d wants to create room: %.*s\n", 
//            client_index, req->room_name_len, req->room_name);
    
//     // Check if client is logged in
//     if (server->clients[client_index].state != CLIENT_CONNECTED) {
//         // Send error - not logged in
//         return 0;
//     }
    
//     // Find free room slot
//     int room_slot = find_free_room_slot(server);
//     if (room_slot == -1) {
//         // Send room creation failed - server full
//         printf("No free room slots\n");
//         return 0;
//     }
    
//     // Check if room name already exists
//     if (find_room_by_name(server, req->room_name) != -1) {
//         // Send room creation failed - name exists
//         printf("Room name already exists\n");
//         return 0;
//     }
    
//     // Create the room
//     room_t *room = &server->rooms[room_slot];
//     room->room_id = room_slot + 1;  // Room IDs start from 1
//     strncpy(room->room_name, req->room_name, req->room_name_len);
//     room->room_name[req->room_name_len] = '\0';
    
//     if (req->password_len > 0) {
//         strncpy(room->password, req->room_password, req->password_len);
//         room->password[req->password_len] = '\0';
//     } else {
//         room->password[0] = '\0';  // No password
//     }
    
//     room->max_clients = req->max_users;
//     room->client_count = 0;
//     room->is_active = 1;
    
//     // Generate multicast address
//     sprintf(room->multicast_addr, "%s%d", MULTICAST_BASE_IP, room->room_id);
//     room->multicast_port = MULTICAST_PORT_START + room->room_id;
    
//     // Send success response
//     struct create_room_request response;  // Reusing struct for response
//     response.msg_type = CREATE_ROOM_SUCCESS;
//     response.msg_length = sizeof(response);
//     response.timestamp = time(NULL);
//     response.session_token = server->clients[client_index].session_token;
    
//     send(server->clients[client_index].socket_fd, &response, sizeof(response), 0);
    
//     printf("Room '%s' created with ID %d\n", room->room_name, room->room_id);
//     return 0;
// }

// int find_free_room_slot(server_t *server) {
//     for (int i = 0; i < MAX_ROOMS; i++) {
//         if (!server->rooms[i].is_active) {
//             return i;
//         }
//     }
//     return -1;  // No free slots
// }


// int find_room_by_name(server_t *server, const char *room_name) {
//     for (int i = 0; i < MAX_ROOMS; i++) {
//         if (server->rooms[i].is_active && 
//             strcmp(server->rooms[i].room_name, room_name) == 0) {
//             return i;
//         }
//     }
//     return -1;  // Room not found
// }

// int find_client_by_socket(server_t *server, int socket_fd) {
//     for (int i = 0; i < MAX_CLIENTS; i++) {
//         if (server->clients[i].is_active && 
//             server->clients[i].socket_fd == socket_fd) {
//             return i;
//         }
//     }
//     return -1;  // Client not found
// }


// int handle_join_room_request(server_t *server, int client_index, struct join_room_request *req) {
//     printf("Client %d wants to join room: %.*s\n", 
//            client_index, req->room_name_len, req->room_name);
    
//     // Check if client is logged in
//     if (server->clients[client_index].state != CLIENT_CONNECTED) {
//         printf("Client not logged in\n");
//         return 0;
//     }
    
//     // Find the room
//     int room_index = find_room_by_name(server, req->room_name);
//     if (room_index == -1) {
//         printf("Room not found\n");
//         // Send JOIN_ROOM_FAILED
//         return 0;
//     }
    
//     room_t *room = &server->rooms[room_index];
    
//     // Check password if room has one
//     if (strlen(room->password) > 0) {
//         if (strncmp(room->password, req->room_password, req->password_len) != 0) {
//             printf("Wrong room password\n");
//             // Send JOIN_ROOM_FAILED - wrong password
//             return 0;
//         }
//     }
    
//     // Check room capacity
//     if (room->max_clients > 0 && room->client_count >= room->max_clients) {
//         printf("Room is full\n");
//         // Send JOIN_ROOM_FAILED - room full
//         return 0;
//     }
    
//     // Join the room
//     server->clients[client_index].state = CLIENT_IN_ROOM;
//     server->clients[client_index].current_room_id = room->room_id;
//     room->client_count++;
    
//     // Send success response
//     struct join_room_response response;
//     response.msg_type = JOIN_ROOM_SUCCESS;
//     response.msg_length = sizeof(response);
//     response.timestamp = time(NULL);
//     response.session_token = server->clients[client_index].session_token;
//     response.room_id = room->room_id;
//     strcpy(response.multicast_addr, room->multicast_addr);
//     response.multicast_port = room->multicast_port;
//     response.error_code = ROOM_SUCCESS_CODE;
//     response.error_msg_len = 0;
    
//     send(server->clients[client_index].socket_fd, &response, sizeof(response), 0);
    
//     printf("Client %d joined room %s (ID: %d)\n", 
//            client_index, room->room_name, room->room_id);
    
//     return 0;
// }