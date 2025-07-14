#ifndef CLIENT_H
#define CLIENT_H

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

// ================================
// CONSTANTS AND CONFIGURATION
// ================================

#define BUFFER_SIZE 1024
#define MAX_INPUT_SIZE 1024
//#define KEEPALIVE_INTERVAL 10
#define RECONNECT_ATTEMPTS 3
#define RECONNECT_DELAY 5
#define IS_IN_ROOM(client) ((client)->current_room_id != -1)

// ================================
// CLIENT STRUCTURE
// ================================

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
    struct sockaddr_in server_addr;
    struct sockaddr_in multicast_addr;
    int running;
    int connected;
    int in_room;
    time_t last_keepalive;
} client_t;

// ================================
// CORE CLIENT FUNCTIONS
// ================================

// Initialization and cleanup
int init_client(client_t *client, const char *server_ip, int server_port);
void cleanup_client(client_t *client);
int connect_to_server(client_t *client, const char *server_ip, int server_port);
int disconnect_from_server(client_t *client);

// Threading functions
#ifdef _WIN32
unsigned __stdcall udp_receiver_thread(void *arg);
unsigned __stdcall keepalive_thread(void *arg);
#else
void *udp_receiver_thread(void *arg);
void *keepalive_thread(void *arg);
#endif

// Main client loop
void handle_user_input(client_t *client);
void print_menu();
void print_help();

// ================================
// AUTHENTICATION FUNCTIONS
// ================================

int send_login_request(client_t *client, const char *username, const char *password);
int handle_login_response(client_t *client, void *response_data);

// ================================
// ROOM MANAGEMENT FUNCTIONS
// ================================

int send_create_room_request(client_t *client, const char *room_name, const char *password);
int send_join_room_request(client_t *client, const char *room_name, const char *password);
int send_leave_room_request(client_t *client);

int handle_create_room_response(client_t *client, void *response_data);
int handle_join_room_response(client_t *client, void *response_data);
int handle_leave_room_response(client_t *client, void *response_data);

// ================================
// CHAT MESSAGING FUNCTIONS
// ================================

int send_chat_message(client_t *client, const char *message);
int send_private_message(client_t *client, const char *target_username, const char *message);
int handle_incoming_chat_message(client_t *client, void *message_data);
int handle_private_message(client_t *client, void *message_data);

// ================================
// INFORMATION REQUEST FUNCTIONS
// ================================

int send_room_list_request(client_t *client);
int send_user_list_request(client_t *client);
void handle_room_list_response(client_t *client, char *buffer, size_t buffer_size);
void handle_user_list_response(client_t *client, char *buffer, size_t buffer_size);

// ================================
// CONNECTION MANAGEMENT FUNCTIONS
// ================================

int send_keepalive(client_t *client);
int send_disconnect_request(client_t *client);
int handle_keepalive_response(client_t *client);
int handle_disconnect_ack(client_t *client);
int handle_force_disconnect(client_t *client);

// ================================
// MULTICAST FUNCTIONS
// ================================

int setup_multicast_socket(client_t *client, const char *multicast_addr, uint16_t port);
int join_multicast_group(client_t *client, const char *multicast_addr);
int leave_multicast_group(client_t *client);
int handle_multicast_message(client_t *client, void *message_data, size_t data_len);

// ================================
// MESSAGE HANDLING FUNCTIONS
// ================================

int handle_server_response(client_t *client, void *response_data, size_t data_len);
void handle_enhanced_user_input(client_t *client);
int handle_user_joined_notification(client_t *client, void *notification_data);
int handle_user_left_notification(client_t *client, void *notification_data);
int handle_error_message(client_t *client, void *error_data);

// ================================
// INPUT VALIDATION FUNCTIONS
// ================================

int validate_username(const char *username);
int validate_password(const char *password);
int validate_room_name(const char *room_name);
int validate_message(const char *message);
int validate_session_token(uint32_t token);

// ================================
// UTILITY FUNCTIONS
// ================================

char* trim_whitespace(char *str);
int parse_command(char *input, char **command, char **args);
void print_timestamp();
void print_error(const char *error_msg);
void print_info(const char *info_msg);
void print_chat_message(const char *sender, const char *message, time_t timestamp);

// ================================
// SESSION MANAGEMENT FUNCTIONS
// ================================

int save_session_info(client_t *client);
int load_session_info(client_t *client);
void clear_session_info(client_t *client);
int is_session_active(client_t *client);

// ================================
// ERROR HANDLING FUNCTIONS
// ================================

void handle_socket_error(const char *operation);
void handle_network_error(client_t *client, const char *operation);
int attempt_reconnection(client_t *client);

#endif // CLIENT_H
