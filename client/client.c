#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <process.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    typedef int socklen_t;
    #define pthread_t HANDLE
    #define pthread_create(thread, attr, func, arg) \
        ((*thread = (HANDLE)_beginthreadex(NULL, 0, (unsigned (__stdcall *)(void *))func, arg, 0, NULL)) != 0 ? 0 : -1)
    #define pthread_join(thread, retval) \
        (WaitForSingleObject(thread, INFINITE), CloseHandle(thread))
    #define ssize_t int
#else
    #include <unistd.h>
    #include <pthread.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <sys/select.h>    
    #include <sys/time.h>   
    struct ip_mreq {
        struct in_addr imr_multiaddr;
        struct in_addr imr_interface;
    };

    #ifndef IP_ADD_MEMBERSHIP
    #define IP_ADD_MEMBERSHIP 35
    #define IP_DROP_MEMBERSHIP 36
    #endif
#endif

#include "client.h"
#include <ctype.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <server_ip> <server_port>\n", argv[0]);
        return 1;
    }    client_t client;
    memset(&client, 0, sizeof(client));
    client.running = 1;
    client.connected = 0;
    client.in_room = 0;
    client.current_room_id = 0;
    client.last_keepalive = 0;
    #ifdef _WIN32
    client.tcp_socket = INVALID_SOCKET;
    client.udp_socket = INVALID_SOCKET;
    #else
    client.tcp_socket = -1;
    client.udp_socket = -1;
    #endif

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }
#endif

    if (init_client(&client, argv[1], atoi(argv[2])) != 0) {
        printf("Failed to initialize client\n");
        cleanup_client(&client);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("Connected to server at %s:%s\n", argv[1], argv[2]);
    printf("Type 'help' for available commands\n");

    // Start UDP receiver thread
    pthread_t udp_thread;
    if (pthread_create(&udp_thread, NULL, udp_receiver_thread, &client) != 0) {
        printf("Failed to create UDP receiver thread\n");
        cleanup_client(&client);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }    // Handle user input in main thread
    handle_enhanced_user_input(&client);

    // Cleanup
    client.running = 0;
    pthread_join(udp_thread, NULL);
    cleanup_client(&client);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}

int init_client(client_t *client, const char *server_ip, int server_port) {
    // Create TCP socket
    client->tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    #ifdef _WIN32
    if (client->tcp_socket == INVALID_SOCKET) {
    #else
    if (client->tcp_socket == -1) {
    #endif
        perror("TCP socket creation failed");
        return -1;
    }

    // Connect to server
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        printf("Invalid server IP address\n");
        return -1;
    }    if (connect(client->tcp_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Connection to server failed");
        return -1;
    }

    // Store server address and set connected flag
    client->server_addr = server_addr;
    client->connected = 1;

    // Create UDP socket for multicast
    client->udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    #ifdef _WIN32
    if (client->udp_socket == INVALID_SOCKET) {
    #else
    if (client->udp_socket == -1) {
    #endif
        perror("UDP socket creation failed");
        return -1;
    }

    // Set socket options for multicast
    int reuse = 1;
    if (setsockopt(client->udp_socket, SOL_SOCKET, SO_REUSEADDR, 
                   (const char*)&reuse, sizeof(reuse)) == -1) {
        perror("Failed to set SO_REUSEADDR");
        return -1;
    }

    return 0;
}

void cleanup_client(client_t *client) {
    #ifdef _WIN32
    if (client->tcp_socket != INVALID_SOCKET) {
        closesocket(client->tcp_socket);
        client->tcp_socket = INVALID_SOCKET;
    }
    if (client->udp_socket != INVALID_SOCKET) {
        closesocket(client->udp_socket);
        client->udp_socket = INVALID_SOCKET;
    }
    #else
    if (client->tcp_socket != -1) {
        close(client->tcp_socket);
        client->tcp_socket = -1;
    }    if (client->udp_socket != -1) {
        close(client->udp_socket);
        client->udp_socket = -1;
    }
    #endif
    
    // Reset connection state
    client->connected = 0;
    client->in_room = 0;
    client->session_token = 0;
    client->current_room_id = 0;
    memset(client->current_room, 0, sizeof(client->current_room));
    memset(client->username, 0, sizeof(client->username));
}

#ifdef _WIN32
unsigned __stdcall udp_receiver_thread(void *arg) {
#else
void *udp_receiver_thread(void *arg) {
#endif
    client_t *client = (client_t*)arg;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    while (client->running) {
        fd_set read_fds;
        struct timeval timeout;
        
        FD_ZERO(&read_fds);
        FD_SET(client->udp_socket, &read_fds);
        
        // ALSO monitor TCP socket for private messages
        //FD_SET(client->tcp_socket, &read_fds);
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        #ifdef _WIN32
        int max_fd = (client->udp_socket > client->tcp_socket) ? client->udp_socket : client->tcp_socket;
        int result = select(0, &read_fds, NULL, NULL, &timeout);
        #else
        int max_fd = (client->udp_socket > client->tcp_socket) ? client->udp_socket : client->tcp_socket;
        int result = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        #endif
        
        // Handle UDP multicast messages (chat, notifications)
        if (result > 0 && FD_ISSET(client->udp_socket, &read_fds)) {
            ssize_t bytes_received = recvfrom(client->udp_socket, buffer, 
                                            sizeof(buffer) - 1, 0,
                                            (struct sockaddr*)&sender_addr, &addr_len);
            if (bytes_received > 0 && bytes_received >= (ssize_t)sizeof(struct message_header)) {
                buffer[bytes_received] = '\0';
                
                // Parse multicast message with proper validation
                struct message_header *header = (struct message_header*)buffer;
                if (header->msg_type == CHAT_MESSAGE && bytes_received >= (ssize_t)sizeof(struct chat_message)) {
                    struct chat_message *chat_msg = (struct chat_message*)buffer;
                    // Ensure strings are null-terminated and valid
                    if (chat_msg->sender_username_len < MAX_USERNAME_LEN && 
                        chat_msg->message_len < 512 && 
                        chat_msg->sender_username_len > 0 && 
                        chat_msg->message_len > 0) {
                        
                        printf("\n[%.*s]: %.*s\n> ", 
                               (int)chat_msg->sender_username_len, chat_msg->sender_username,
                               (int)chat_msg->message_len, chat_msg->message);
                        fflush(stdout);
                    }
                } else if (header->msg_type == USER_JOINED_ROOM && bytes_received >= (ssize_t)sizeof(struct user_notification)) {
                    struct user_notification *notif = (struct user_notification*)buffer;
                    if (notif->username_len < 32 && notif->username_len > 0) {
                        printf("\n*** %.*s joined the room ***\n> ", 
                               (int)notif->username_len, notif->username);
                        fflush(stdout);
                    }
                } else if (header->msg_type == USER_LEFT_ROOM && bytes_received >= (ssize_t)sizeof(struct user_notification)) {
                    struct user_notification *notif = (struct user_notification*)buffer;
                    if (notif->username_len < 32 && notif->username_len > 0) {
                        printf("\n*** %.*s left the room ***\n> ", 
                               (int)notif->username_len, notif->username);
                        fflush(stdout);
                    }
                } else {
                    printf("\n[INFO] Received message (type: 0x%04x)\n> ", header->msg_type);
                    fflush(stdout);
                }
            }
        }
        
        // // Handle TCP private messages
        // if (result > 0 && FD_ISSET(client->tcp_socket, &read_fds)) {
        //     ssize_t bytes_received = recv(client->tcp_socket, buffer, sizeof(buffer) - 1, 0);
        //     if (bytes_received > 0 && bytes_received >= (ssize_t)sizeof(struct message_header)) {
        //         buffer[bytes_received] = '\0';
                
        //         struct message_header *header = (struct message_header*)buffer;
        //         if (header->msg_type == PRIVATE_MESSAGE && bytes_received >= (ssize_t)sizeof(struct private_message)) {
        //             struct private_message *priv_msg = (struct private_message*)buffer;
        //             if (priv_msg->target_username_len < 32 && 
        //                 priv_msg->message_len < 512 && 
        //                 priv_msg->message_len > 0) {
        //                 // Show private messages received via TCP
        //                 printf("\n[PRIVATE from %.*s]: %.*s\n> ", 
        //                        (int)priv_msg->target_username_len, priv_msg->target_username,
        //                        (int)priv_msg->message_len, priv_msg->message);
        //                 fflush(stdout);
        //             }
        //         }
        //     } else if (bytes_received < 0) {
        //         // Server closed TCP connection
        //         printf("\nServer disconnected\n");
        //         break;
        //     }
        // }
        
        #ifdef _WIN32
        else if (result == SOCKET_ERROR) {
            if (WSAGetLastError() != WSAETIMEDOUT) {
                break;
            }
        }
        #endif
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}


int send_login_request(client_t *client, const char *username, const char *password) {
    struct login_request req;
    struct login_response resp;
    
    memset(&req, 0, sizeof(req));
    req.msg_type = LOGIN_REQUEST;
    req.msg_length = sizeof(req);
    req.timestamp = time(NULL);
    req.username_len = strlen(username);
    strncpy(req.username, username, MAX_USERNAME_LEN - 1);
    req.password_len = strlen(password);
    strncpy(req.password, password, MAX_PASSWORD_LEN - 1);
    
    int bytes_sent = send(client->tcp_socket, (char*)&req, sizeof(req), 0);
    #ifdef _WIN32
    if (bytes_sent == SOCKET_ERROR || bytes_sent != sizeof(req)) {
        printf("Failed to send login request: %d\n", WSAGetLastError());
        return -1;
    }
    #else
    if (bytes_sent != sizeof(req)) {
        perror("Failed to send login request");
        return -1;
    }
    #endif
    
    // Receive response with proper error handling
    int bytes_received = recv(client->tcp_socket, (char*)&resp, sizeof(resp), 0);
    #ifdef _WIN32
    if (bytes_received == SOCKET_ERROR || bytes_received != sizeof(resp)) {
        printf("Failed to receive login response: %d\n", WSAGetLastError());
        return -1;
    }
    #else
    if (bytes_received != sizeof(resp)) {
        perror("Failed to receive login response");
        return -1;
    }
    #endif
    
    if (resp.msg_type == LOGIN_SUCCESS) {
        client->session_token = resp.session_token;
        strncpy(client->username, username, sizeof(client->username) - 1);
        printf("Login successful! Welcome %s\n", username);
        return 0;
    } else if (resp.msg_type == LOGIN_FAILED) {
        if (resp.error_msg_len > 0 && resp.error_msg_len < sizeof(resp.error_msg)) {
            printf("Login failed: %.*s\n", resp.error_msg_len, resp.error_msg);
        } else {
            printf("Login failed: Invalid username or password\n");
        }
    }
    
    return -1;
}

int send_chat_message(client_t *client, const char *message) {
    struct chat_message msg;
    
    if (client->current_room_id == 0) {
        printf("Error: You must join a room before sending messages\n");
        return -1;
    }
    
    memset(&msg, 0, sizeof(msg));
    msg.msg_type = CHAT_MESSAGE;
    msg.msg_length = sizeof(msg);
    msg.timestamp = time(NULL);
    msg.session_token = client->session_token;
    msg.room_id = client->current_room_id;
    msg.sender_username_len = strlen(client->username);
    strncpy(msg.sender_username, client->username, MAX_USERNAME_LEN - 1);
    msg.message_len = strlen(message);
    strncpy(msg.message, message, 512 - 1);
    
    int result = send(client->tcp_socket, (char*)&msg, sizeof(msg), 0);
    if (result == sizeof(msg)) {
        // Remove the annoying success message
        return 0;
    } else {
        printf("Failed to send message\n");
        return -1;
    }
}

int send_create_room_request(client_t *client, const char *room_name, const char *password) {
    struct create_room_request req;
    struct create_room_response resp;
    
    memset(&req, 0, sizeof(req));
    req.msg_type = CREATE_ROOM_REQUEST;
    req.msg_length = sizeof(req);
    req.timestamp = time(NULL);
    req.session_token = client->session_token;
    req.room_name_len = strlen(room_name);
    strncpy(req.room_name, room_name, 64 - 1);
    if (strlen(password) > 0) {
        req.password_len = strlen(password);
        strncpy(req.room_password, password, 32 - 1);
    } else {
        req.password_len = 0;
    }
    req.max_users = 20; // Default max users, can be changed later
    
    if (send(client->tcp_socket, (char*)&req, sizeof(req), 0) != sizeof(req)) {
        return -1;
    }
    
    if (recv(client->tcp_socket, (char*)&resp, sizeof(resp), 0) != sizeof(resp)) {
        return -1;
    }
    
    if (resp.msg_type == CREATE_ROOM_SUCCESS) {
        client->current_room_id = resp.room_id;
        strncpy(client->current_room, room_name, MAX_ROOM_NAME_LEN - 1);
        
        // Setup multicast for the created room
        memset(&client->multicast_addr, 0, sizeof(client->multicast_addr));
        client->multicast_addr.sin_family = AF_INET;
        client->multicast_addr.sin_port = htons(resp.multicast_port);
        inet_pton(AF_INET, resp.multicast_addr, &client->multicast_addr.sin_addr);
        
        printf("Room '%s' created successfully! Use 'join_room %s' to start chatting.\n", room_name, room_name);
        return 0;
    } else if (resp.msg_type == CREATE_ROOM_FAILED) {
        if (resp.error_msg_len > 0 && resp.error_msg_len < sizeof(resp.error_msg)) {
            printf("Create room failed: %.*s\n", resp.error_msg_len, resp.error_msg);
        } else {
            printf("Create room failed\n");
        }
    }
    
    return -1;
}

int send_join_room_request(client_t *client, const char *room_name, const char *password) {
    struct join_room_request req;
    struct join_room_response resp;
    
    memset(&req, 0, sizeof(req));
    req.msg_type = JOIN_ROOM_REQUEST;
    req.msg_length = sizeof(req);
    req.timestamp = time(NULL);
    req.session_token = client->session_token;
    req.room_name_len = strlen(room_name);
    strncpy(req.room_name, room_name, 64 - 1);
    if (strlen(password) > 0) {
        req.password_len = strlen(password);
        strncpy(req.room_password, password, 32 - 1);
    } else {
        req.password_len = 0;
    }
    
    if (send(client->tcp_socket, (char*)&req, sizeof(req), 0) != sizeof(req)) {
        return -1;
    }
    
    if (recv(client->tcp_socket, (char*)&resp, sizeof(resp), 0) != sizeof(resp)) {
        return -1;
    }
    
    if (resp.msg_type == JOIN_ROOM_SUCCESS) {
        client->current_room_id = resp.room_id;
        // Close existing UDP socket and create new one for multicast
        // #ifdef _WIN32
        // if (client->udp_socket != INVALID_SOCKET) {
        //     closesocket(client->udp_socket);
        // }
        // #else
        // if (client->udp_socket != -1) {
        //     close(client->udp_socket);
        // }
        // #endif
        
        client->udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
        #ifdef _WIN32
        if (client->udp_socket == INVALID_SOCKET) {
        #else
        if (client->udp_socket == -1) {
        #endif
            return -1;
        }
        
        // Set socket options
        int reuse = 1;
        setsockopt(client->udp_socket, SOL_SOCKET, SO_REUSEADDR, 
                  (const char*)&reuse, sizeof(reuse));
        
        // Setup multicast receiving
        memset(&client->multicast_addr, 0, sizeof(client->multicast_addr));
        client->multicast_addr.sin_family = AF_INET;
        client->multicast_addr.sin_port = htons(resp.multicast_port);
        inet_pton(AF_INET, resp.multicast_addr, &client->multicast_addr.sin_addr);
        
        // Bind to multicast port
        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = INADDR_ANY;
        bind_addr.sin_port = htons(resp.multicast_port);
        
        if (bind(client->udp_socket, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == 0) {
            // Join multicast group
            struct ip_mreq mreq;
            inet_pton(AF_INET, resp.multicast_addr, &mreq.imr_multiaddr);
            mreq.imr_interface.s_addr = INADDR_ANY;
            if (setsockopt(client->udp_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                         (const char*)&mreq, sizeof(mreq)) != 0) {
                perror("Failed to join multicast group");
            } else {
                printf("Successfully joined multicast group %s:%d for room %s\n", 
                       resp.multicast_addr, resp.multicast_port, room_name);
            }
        } else {
            perror("Failed to bind UDP socket");
        }
        strncpy(client->current_room, room_name, MAX_ROOM_NAME_LEN - 1);
        printf("Successfully joined room '%s'!\n", room_name);
        return 0;
    } else if (resp.msg_type == JOIN_ROOM_FAILED) {
        if (resp.error_msg_len > 0 && resp.error_msg_len < sizeof(resp.error_msg)) {
            printf("Join room failed: %.*s\n", resp.error_msg_len, resp.error_msg);
        } else {
            printf("Join room failed\n");
        }
    }
    
    return -1;
}

int send_private_message(client_t *client, const char *target_username, const char *message) {
    struct private_message msg;
    
    memset(&msg, 0, sizeof(msg));
    msg.msg_type = PRIVATE_MESSAGE;
    msg.msg_length = sizeof(msg);
    msg.timestamp = time(NULL);
    msg.session_token = client->session_token;
    msg.target_username_len = strlen(target_username);
    strncpy(msg.target_username, target_username, 32 - 1);
    msg.message_len = strlen(message);
    strncpy(msg.message, message, 512 - 1);
    
    int result = send(client->tcp_socket, (char*)&msg, sizeof(msg), 0);
    if (result == sizeof(msg)) {
        // Show sent private message to sender for confirmation
        printf("[PRIVATE to %s]: %s\n", target_username, message);
        return 0;
    } else {
        printf("Failed to send private message\n");
        return -1;
    }
}

void print_menu() {
    printf("\nAvailable commands:\n");
    printf("  login <username> <password>       - Login to your account\n");
    printf("  create_room <name> [password]     - Create a new chat room\n");
    printf("  join_room <name> [password]       - Join an existing room\n");
    printf("  chat <message>                    - Send a message to current room\n");
    printf("  private <username> <message>      - Send a private message\n");
    printf("  help                              - Show this menu\n");
    printf("  quit/exit                         - Exit the application\n\n");
}

// ================================
// INFORMATION REQUEST FUNCTIONS
// ================================

int send_room_list_request(client_t *client) {
    struct room_list_request req;
    
    memset(&req, 0, sizeof(req));
    req.msg_type = ROOM_LIST_REQUEST;
    req.msg_length = sizeof(req);
    req.timestamp = time(NULL);
    req.session_token = client->session_token;
    
    ssize_t sent = send(client->tcp_socket, (char*)&req, sizeof(req), 0);
    if (sent != sizeof(req)) {
        printf("Failed to send room list request\n");
        return -1;
    }
    
    // Receive response immediately
    char response_buffer[2048]; // Large enough for room list
    ssize_t received = recv(client->tcp_socket, response_buffer, sizeof(response_buffer), 0);
    if (received <= 0) {
        printf("Failed to receive room list response\n");
        return -1;
    }
    
    // Process the response
    handle_room_list_response(client, response_buffer, received);
    return 0;
}

// Update the existing function to use the proper struct

int send_user_list_request(client_t *client) {
    if (!IS_IN_ROOM(client)) {
        printf("Error: You must be in a room to list users\n");
        return -1;
    }
    
    struct user_list_request req;
    memset(&req, 0, sizeof(req));
    req.msg_type = USER_LIST_REQUEST;
    req.msg_length = sizeof(req);
    req.timestamp = time(NULL);
    req.session_token = client->session_token;
    req.room_id = client->current_room_id;
    
    ssize_t sent = send(client->tcp_socket, (char*)&req, sizeof(req), 0);
    if (sent != sizeof(req)) {
        printf("Failed to send user list request\n");
        return -1;
    }
    
    // Receive response immediately
    char response_buffer[1024]; // Large enough for user list
    ssize_t received = recv(client->tcp_socket, response_buffer, sizeof(response_buffer), 0);
    if (received <= 0) {
        printf("Failed to receive user list response\n");
        return -1;
    }
    
    // Process the response
    handle_user_list_response(client, response_buffer, received);
    return 0;
}

// Add this new function

void handle_user_list_response(client_t *client, char *buffer, size_t buffer_size) {
    if (buffer_size < sizeof(struct user_list_response)) {
        printf("Invalid user list response size\n");
        return;
    }
    
    struct user_list_response *response = (struct user_list_response*)buffer;
    
    printf("\n=== Users in Room ===\n");
    printf("Found %d user(s) in room %d:\n", response->user_count, client->current_room_id);
    
    // Parse variable-length user data
    char *data_ptr = buffer + sizeof(struct user_list_response);
    char *buffer_end = buffer + buffer_size;
    
    for (int i = 0; i < response->user_count && data_ptr < buffer_end; i++) {
        // Read username length
        if (data_ptr + sizeof(uint8_t) > buffer_end) break;
        uint8_t username_len = *(uint8_t*)data_ptr;
        data_ptr += sizeof(uint8_t);
        
        // Read username
        if (data_ptr + username_len > buffer_end) break;
        char username[MAX_USERNAME_LEN + 1] = {0};
        memcpy(username, data_ptr, username_len);
        data_ptr += username_len;
        
        printf("  %d. %s\n", i + 1, username);
    }
    printf("=====================\n\n");
}

int send_leave_room_request(client_t *client) {
    if (client->current_room_id == 0) {
        printf("You are not in any room\n");
        return -1;
    }
    
    struct leave_room_request req;
    
    memset(&req, 0, sizeof(req));
    req.msg_type = LEAVE_ROOM_REQUEST;
    req.msg_length = sizeof(req);
    req.timestamp = time(NULL);
    req.session_token = client->session_token;
    
    ssize_t sent = send(client->tcp_socket, (char*)&req, sizeof(req), 0);
    if (sent != sizeof(req)) {
        printf("Failed to send leave room request\n");
        return -1;
    }
    
    // Receive response immediately
    struct leave_room_response resp;
    ssize_t received = recv(client->tcp_socket, (char*)&resp, sizeof(resp), 0);
    if (received != sizeof(resp)) {
        printf("Failed to receive leave room response\n");
        return -1;
    }
    
    // Process the response
    handle_leave_room_response(client, &resp);
    return 0;
}

// ================================
// RESPONSE HANDLERS
// ================================

void handle_room_list_response(client_t *client, char *buffer, size_t buffer_size) {
    (void)client; // Unused parameter, but could be used for logging
    if (buffer_size < 9) { // Minimum: 2+2+4+1 = 9 bytes
        printf("Invalid room list response size\n");
        return;
    }
    
    // Parse the dynamic response format that server sends
    char *ptr = buffer;
    char *buffer_end = buffer + buffer_size;
    
    // Read header
    uint16_t msg_type = *(uint16_t*)ptr;
    (void)msg_type; // We don't use msg_type here, but could log it
    ptr += sizeof(uint16_t);
    
    uint16_t msg_length = *(uint16_t*)ptr;
    (void)msg_length; // We don't use msg_length here, but could log it
    ptr += sizeof(uint16_t);
    
    uint32_t timestamp = *(uint32_t*)ptr;
    (void)timestamp; // We don't use timestamp here, but could log it
    ptr += sizeof(uint32_t);
    
    uint8_t room_count = *(uint8_t*)ptr;
    ptr += sizeof(uint8_t);
    
    printf("\n=== Available Rooms ===\n");
    if (room_count == 0) {
        printf("No rooms available\n");
    } else {
        printf("Found %d room(s):\n", room_count);
        
        // Parse each room's data
        for (int i = 0; i < room_count && ptr < buffer_end; i++) {
            // Read room_id
            if (ptr + sizeof(uint16_t) > buffer_end) break;
            uint16_t room_id = *(uint16_t*)ptr;
            ptr += sizeof(uint16_t);
            
            // Read room_name_len
            if (ptr + sizeof(uint8_t) > buffer_end) break;
            uint8_t room_name_len = *(uint8_t*)ptr;
            ptr += sizeof(uint8_t);
            
            // Read room_name
            if (ptr + room_name_len > buffer_end) break;
            char room_name[MAX_ROOM_NAME_LEN + 1] = {0};
            memcpy(room_name, ptr, room_name_len);
            ptr += room_name_len;
            
            // Read user_count
            if (ptr + sizeof(uint8_t) > buffer_end) break;
            uint8_t user_count = *(uint8_t*)ptr;
            ptr += sizeof(uint8_t);
            
            // Read has_password
            if (ptr + sizeof(uint8_t) > buffer_end) break;
            uint8_t has_password = *(uint8_t*)ptr;
            ptr += sizeof(uint8_t);
            
            // Display room info
            printf("  %d. %s (Room ID: %d, %d users) %s\n", 
                   i + 1,
                   room_name,
                   room_id,
                   user_count,
                   has_password ? "[Password Protected]" : "");
        }
    }
    printf("=======================\n\n");
}

int handle_leave_room_response(client_t *client, void *response_data) {
    struct leave_room_response *resp = (struct leave_room_response*)response_data;
    
    if (resp->error_code == ROOM_SUCCESS_CODE) {
        printf("Successfully left room '%s'\n", client->current_room);
        
        // Leave multicast group
        leave_multicast_group(client);
        
        // Reset client room state
        client->current_room_id = 0;
        memset(client->current_room, 0, sizeof(client->current_room));
        client->in_room = 0;
        
        return 0;
    } else {
        if (resp->error_msg_len > 0 && resp->error_msg_len < sizeof(resp->error_msg)) {
            printf("Failed to leave room: %.*s\n", resp->error_msg_len, resp->error_msg);
        } else {
            printf("Failed to leave room\n");
        }
        return -1;
    }
}

// ================================
// CONNECTION MANAGEMENT FUNCTIONS
// ================================

int send_keepalive(client_t *client) {
    struct keepalive msg;
    
    memset(&msg, 0, sizeof(msg));
    msg.msg_type = KEEPALIVE;
    msg.msg_length = sizeof(msg);
    msg.timestamp = time(NULL);
    msg.session_token = client->session_token;
    
    ssize_t sent = send(client->tcp_socket, (char*)&msg, sizeof(msg), 0);
    if (sent != sizeof(msg)) {
        printf("Failed to send keepalive\n");
        return -1;
    }
    
    client->last_keepalive = time(NULL);
    return 0;
}

int send_disconnect_request(client_t *client) {
    struct disconnect_request req;
    
    memset(&req, 0, sizeof(req));
    req.msg_type = DISCONNECT_REQUEST;
    req.msg_length = sizeof(req);
    req.timestamp = time(NULL);
    req.session_token = client->session_token;
    
    ssize_t sent = send(client->tcp_socket, (char*)&req, sizeof(req), 0);
    if (sent != sizeof(req)) {
        printf("Failed to send disconnect request\n");
        return -1;
    }
    
    // Receive response immediately (with short timeout since we're disconnecting)
    struct disconnect_response resp;
    ssize_t received = recv(client->tcp_socket, (char*)&resp, sizeof(resp), 0);
    if (received == sizeof(resp)) {
        if (resp.msg_type == DISCONNECT_SUCCESS) {
            printf("Disconnected successfully. Goodbye!\n");
        } else {
            printf("Disconnect acknowledged\n");
        }
    } else {
        // Server might close connection immediately, so this is normal
        printf("Disconnecting...\n");
    }
    
    return 0;
}

// ================================
// MULTICAST FUNCTIONS
// ================================

int leave_multicast_group(client_t *client) {
    if (client->udp_socket == -1 || client->current_room_id == 0) {
        return 0; // Not in multicast group
    }
    
    struct ip_mreq mreq;
    mreq.imr_multiaddr = client->multicast_addr.sin_addr;
    mreq.imr_interface.s_addr = INADDR_ANY;
    
    if (setsockopt(client->udp_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                   (const char*)&mreq, sizeof(mreq)) != 0) {
        perror("Failed to leave multicast group");
        return -1;
    }
    
    return 0;
}

// ================================
// INPUT VALIDATION FUNCTIONS
// ================================

int validate_username(const char *username) {
    if (!username || strlen(username) == 0) {
        printf("Username cannot be empty\n");
        return 0;
    }
    
    if (strlen(username) >= MAX_USERNAME_LEN) {
        printf("Username too long (max %d characters)\n", MAX_USERNAME_LEN - 1);
        return 0;
    }
    
    // Check for valid characters (alphanumeric and underscore)
    for (const char *p = username; *p; p++) {
        if (!isalnum(*p) && *p != '_') {
            printf("Username can only contain letters, numbers, and underscores\n");
            return 0;
        }
    }
    
    return 1;
}

int validate_password(const char *password) {
    if (!password) {
        return 1; // Password can be empty
    }
    
    if (strlen(password) >= MAX_PASSWORD_LEN) {
        printf("Password too long (max %d characters)\n", MAX_PASSWORD_LEN - 1);
        return 0;
    }
    
    return 1;
}

int validate_room_name(const char *room_name) {
    if (!room_name || strlen(room_name) == 0) {
        printf("Room name cannot be empty\n");
        return 0;
    }
    
    if (strlen(room_name) >= MAX_ROOM_NAME_LEN) {
        printf("Room name too long (max %d characters)\n", MAX_ROOM_NAME_LEN - 1);
        return 0;
    }
    
    // Check for valid characters
    for (const char *p = room_name; *p; p++) {
        if (!isalnum(*p) && *p != '_' && *p != '-' && *p != ' ') {
            printf("Room name can only contain letters, numbers, spaces, hyphens, and underscores\n");
            return 0;
        }
    }
    
    return 1;
}

int validate_message(const char *message) {
    if (!message || strlen(message) == 0) {
        printf("Message cannot be empty\n");
        return 0;
    }
    
    if (strlen(message) >= MAX_MESSAGE_LEN) {
        printf("Message too long (max %d characters)\n", MAX_MESSAGE_LEN - 1);
        return 0;
    }
    
    return 1;
}

// ================================
// UTILITY FUNCTIONS
// ================================

char* trim_whitespace(char *str) {
    if (!str) return NULL;
    
    // Trim leading whitespace
    while (isspace(*str)) str++;
    
    // If all whitespace
    if (*str == 0) return str;
    
    // Trim trailing whitespace
    char *end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    
    // Write new null terminator
    *(end + 1) = 0;
    
    return str;
}

int parse_command(char *input, char **command, char **args) {
    if (!input || !command || !args) return -1;
    
    // Trim input
    input = trim_whitespace(input);
    if (strlen(input) == 0) return -1;
    
    // Find first space
    char *space = strchr(input, ' ');
    if (space) {
        *space = '\0';
        *command = input;
        *args = trim_whitespace(space + 1);
    } else {
        *command = input;
        *args = NULL;
    }
    
    return 0;
}

void print_timestamp() {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    printf("[%02d:%02d:%02d] ", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
}

void print_error(const char *error_msg) {
    print_timestamp();
    printf("ERROR: %s\n", error_msg);
}

void print_info(const char *info_msg) {
    print_timestamp();
    printf("INFO: %s\n", info_msg);
}

void print_chat_message(const char *sender, const char *message, time_t timestamp) {
    struct tm *tm_info = localtime(&timestamp);
    printf("[%02d:%02d:%02d] %s: %s\n", 
           tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
           sender, message);
}

void print_help() {
    printf("\n=== Chat Client Help ===\n");
    printf("Available commands:\n");
    printf("  login <username> <password>       - Login to your account\n");
    printf("  create_room <name> [password]     - Create a new chat room\n");
    printf("  join_room <name> [password]       - Join an existing room\n");
    printf("  leave_room                        - Leave current room\n");
    printf("  chat <message>                    - Send a message to current room\n");
    printf("  private <username> <message>      - Send a private message\n");
    printf("  room_list                         - List all available rooms\n");
    printf("  user_list                         - List users in current room\n");
    printf("  help                              - Show this help\n");
    printf("  quit/exit                         - Exit the application\n");
    printf("========================\n\n");
}

// ================================
// ENHANCED USER INPUT HANDLING
// ================================

void handle_enhanced_user_input(client_t *client) {
    char input[MAX_INPUT_SIZE];
    char *command, *args;
    time_t last_keepalive = time(NULL);  // ← Track last keepalive time
    
    while (client->running) {
        printf("> ");
        fflush(stdout);
        printf("DEBUG: client->running=%d, waiting for input...\n", client->running); 
        
        // Send keepalive every 20 seconds ← AUTOMATIC KEEPALIVE
        time_t now = time(NULL);
        if (client->session_token != 0 && difftime(now, last_keepalive) > 20) {
            send_keepalive(client);
            last_keepalive = now;
        }
        
        if (!fgets(input, sizeof(input), stdin)) {
            printf("DEBUG: fgets() failed or EOF\n");  // ← ADD THIS
            break;
        }
        printf("DEBUG: User entered: '%s'\n", input);  // ← ADD THIS
        // Remove newline
        input[strcspn(input, "\n")] = 0;
        
        if (parse_command(input, &command, &args) != 0) {
            printf("DEBUG: parse_command failed\n");  // ← ADD THIS
            continue;
        }
        printf("DEBUG: Command: '%s', Args: '%s'\n", command, args ? args : "NULL");  // ← ADD THIS
        // Handle commands
        if (strcmp(command, "login") == 0) {
            if (client->session_token != 0) {
                printf("Already logged in as %s\n", client->username);
                continue;
            }
            
            if (!args) {
                printf("Usage: login <username> <password>\n");
                continue;
            }
            
            char *username = strtok(args, " ");
            char *password = strtok(NULL, " ");
            
            if (!username || !password) {
                printf("Usage: login <username> <password>\n");
                continue;
            }
            
            if (!validate_username(username) || !validate_password(password)) {
                continue;
            }
            
            send_login_request(client, username, password);
            
        } else if (strcmp(command, "create_room") == 0) {
            if (client->session_token == 0) {
                printf("You must login first\n");
                continue;
            }
            
            if (!args) {
                printf("Usage: create_room <name> [password]\n");
                continue;
            }
            
            char *room_name = strtok(args, " ");
            char *password = strtok(NULL, " ");
            
            if (!room_name) {
                printf("Usage: create_room <name> [password]\n");
                continue;
            }
            
            if (!validate_room_name(room_name) || 
                (password && !validate_password(password))) {
                continue;
            }
            
            send_create_room_request(client, room_name, password ? password : "");
            
        } else if (strcmp(command, "join_room") == 0) {
            if (client->session_token == 0) {
                printf("You must login first\n");
                continue;
            }
            
            if (!args) {
                printf("Usage: join_room <name> [password]\n");
                continue;
            }
            
            char *room_name = strtok(args, " ");
            char *password = strtok(NULL, " ");
            
            if (!room_name) {
                printf("Usage: join_room <name> [password]\n");
                continue;
            }
            
            if (!validate_room_name(room_name) || 
                (password && !validate_password(password))) {
                continue;
            }
            
            send_join_room_request(client, room_name, password ? password : "");
            
        } else if (strcmp(command, "leave_room") == 0) {
            if (client->session_token == 0) {
                printf("You must login first\n");
                continue;
            }
            
            send_leave_room_request(client);
            
        } else if (strcmp(command, "chat") == 0) {
            if (client->session_token == 0) {
                printf("You must login first\n");
                continue;
            }
            
            if (client->current_room_id == 0) {
                printf("You must join a room first\n");
                continue;
            }
            
            if (!args || !validate_message(args)) {
                continue;
            }
            
            send_chat_message(client, args);
            
        } else if (strcmp(command, "private") == 0) {
            if (client->session_token == 0) {
                printf("You must login first\n");
                continue;
            }
            
            if (!args) {
                printf("Usage: private <username> <message>\n");
                continue;
            }
            
            char *username = strtok(args, " ");
            char *message = strtok(NULL, "");
            
            if (!username || !message) {
                printf("Usage: private <username> <message>\n");
                continue;
            }
            
            if (!validate_username(username) || !validate_message(message)) {
                continue;
            }
            
            send_private_message(client, username, message);
            
        } else if (strcmp(command, "room_list") == 0) {
            if (client->session_token == 0) {
                printf("You must login first\n");
                continue;
            }
            
            send_room_list_request(client);
            
        } else if (strcmp(command, "user_list") == 0) {
            if (client->session_token == 0) {
                printf("You must login first\n");
                continue;
            }
            
            if (client->current_room_id == 0) {
                printf("You must join a room first\n");
                continue;
            }
            
            send_user_list_request(client);
            
        } else if (strcmp(command, "help") == 0) {
            print_help();
            
        } else if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
            printf("Disconnecting...\n");
            if (client->session_token != 0) {
                send_disconnect_request(client);
            }
            client->running = 0;
            break;
            
        } else {
            printf("Unknown command: %s\n", command);
            printf("Type 'help' for available commands\n");
        }
    }
}