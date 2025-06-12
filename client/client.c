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
#endif

#include "../common/protocol.h"

// Define missing constants
#define BUFFER_SIZE 1024

typedef struct {
    #ifdef _WIN32
    SOCKET tcp_socket;
    SOCKET udp_socket;
    #else
    int tcp_socket;
    int udp_socket;
    #endif
    uint32_t session_token;
    uint16_t current_room_id;
    char current_room[MAX_ROOM_NAME_LEN];
    char username[MAX_USERNAME_LEN];
    struct sockaddr_in multicast_addr;
    int running;
} client_t;

// Function prototypes
int init_client(client_t *client, const char *server_ip, int server_port);
void cleanup_client(client_t *client);
#ifdef _WIN32
unsigned __stdcall udp_receiver_thread(void *arg);
#else
void *udp_receiver_thread(void *arg);
#endif
void handle_user_input(client_t *client);
int send_login_request(client_t *client, const char *username, const char *password);
int send_chat_message(client_t *client, const char *message);
int send_create_room_request(client_t *client, const char *room_name, const char *password);
int send_join_room_request(client_t *client, const char *room_name, const char *password);
int send_private_message(client_t *client, const char *target_username, const char *message);
void print_menu();

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <server_ip> <server_port>\n", argv[0]);
        return 1;
    }    client_t client;
    memset(&client, 0, sizeof(client));
    client.running = 1;
    client.current_room_id = 0;
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
    }

    // Handle user input in main thread
    handle_user_input(&client);

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
    }

    if (connect(client->tcp_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Connection to server failed");
        return -1;
    }

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
    }
    if (client->udp_socket != -1) {
        close(client->udp_socket);
        client->udp_socket = -1;
    }
    #endif
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
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        #ifdef _WIN32
        int result = select(0, &read_fds, NULL, NULL, &timeout);
        #else
        int result = select(client->udp_socket + 1, &read_fds, NULL, NULL, &timeout);
        #endif
        
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
                        
                        // Don't display our own messages
                        if (strncmp(chat_msg->sender_username, client->username, chat_msg->sender_username_len) != 0) {
                            printf("\n[%.*s]: %.*s\n> ", 
                                   (int)chat_msg->sender_username_len, chat_msg->sender_username,
                                   (int)chat_msg->message_len, chat_msg->message);
                            fflush(stdout);
                        }
                    }
                } else if (header->msg_type == PRIVATE_MESSAGE && bytes_received >= (ssize_t)sizeof(struct private_message)) {
                    struct private_message *priv_msg = (struct private_message*)buffer;
                    if (priv_msg->target_username_len < 32 && 
                        priv_msg->message_len < 512 && 
                        priv_msg->message_len > 0) {
                        printf("\n[PRIVATE]: %.*s\n> ", 
                               (int)priv_msg->message_len, priv_msg->message);
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

void handle_user_input(client_t *client) {
    char input[BUFFER_SIZE];
    char command[64];

    while (client->running) {
        printf("> ");
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        // Remove newline
        input[strcspn(input, "\n")] = 0;
        
        if (strlen(input) == 0) {
            continue;
        }

        // Parse command
        sscanf(input, "%s", command);        if (strcmp(command, "help") == 0) {
            print_menu();
        }
        else if (strcmp(command, "login") == 0) {
            char username[MAX_USERNAME_LEN], password[MAX_PASSWORD_LEN];
            if (sscanf(input, "login %31s %63s", username, password) == 2) {
                if (send_login_request(client, username, password) == 0) {
                    strncpy(client->username, username, MAX_USERNAME_LEN - 1);
                    printf("Login successful!\n");
                } else {
                    printf("Login failed\n");
                }
            } else {
                printf("Usage: login <username> <password>\n");
            }
        }
        else if (strcmp(command, "create_room") == 0) {
            char room_name[MAX_ROOM_NAME_LEN], room_password[MAX_PASSWORD_LEN] = {0};
            int args = sscanf(input, "create_room %63s %63s", room_name, room_password);
            if (args >= 1) {
                if (send_create_room_request(client, room_name, args == 2 ? room_password : "") == 0) {
                    printf("Room '%s' created successfully!\n", room_name);
                } else {
                    printf("Failed to create room\n");
                }
            } else {
                printf("Usage: create_room <room_name> [password]\n");
            }
        }        else if (strcmp(command, "join_room") == 0) {
            char room_name[MAX_ROOM_NAME_LEN], room_password[MAX_PASSWORD_LEN] = {0};
            int args = sscanf(input, "join_room %63s %63s", room_name, room_password);
            if (args >= 1) {
                if (send_join_room_request(client, room_name, args == 2 ? room_password : "") == 0) {
                    strncpy(client->current_room, room_name, MAX_ROOM_NAME_LEN - 1);
                    printf("Joined room '%s' successfully!\n", room_name);
                } else {
                    printf("Failed to join room\n");
                }
            } else {
                printf("Usage: join_room <room_name> [password]\n");
            }
        }        else if (strcmp(command, "chat") == 0) {
            char *chat_message = input + 5; // Skip "chat "
            if (strlen(chat_message) > 0 && client->session_token != 0 && client->current_room_id != 0) {
                send_chat_message(client, chat_message);
            } else {
                printf("Usage: chat <message>\nNote: You must be logged in and in a room\n");
            }
        }
        else if (strcmp(command, "private") == 0) {
            char target_user[MAX_USERNAME_LEN];
            char *private_message = NULL;
            if (sscanf(input, "private %31s", target_user) == 1) {
                private_message = strchr(input + 8, ' '); // Find message after username
                if (private_message && strlen(private_message + 1) > 0) {
                    private_message++; // Skip space
                    if (send_private_message(client, target_user, private_message) == 0) {
                        printf("Private message sent to %s\n", target_user);
                    } else {
                        printf("Failed to send private message\n");
                    }
                } else {
                    printf("Usage: private <username> <message>\n");
                }
            } else {
                printf("Usage: private <username> <message>\n");
            }
        }
        else if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
            client->running = 0;
            break;
        }
        else {
            printf("Unknown command. Type 'help' for available commands.\n");
        }
    }
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
    
    // Send request with proper error handling
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
    
    return (send(client->tcp_socket, (char*)&msg, sizeof(msg), 0) == sizeof(msg)) ? 0 : -1;
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
    req.max_users = 0; // unlimited
    
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
        #ifdef _WIN32
        if (client->udp_socket != INVALID_SOCKET) {
            closesocket(client->udp_socket);
        }
        #else
        if (client->udp_socket != -1) {
            close(client->udp_socket);
        }
        #endif
        
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
            }        } else {
            perror("Failed to bind UDP socket");
        }
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
    
    return (send(client->tcp_socket, (char*)&msg, sizeof(msg), 0) == sizeof(msg)) ? 0 : -1;
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