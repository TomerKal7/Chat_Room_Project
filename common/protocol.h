#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#include <stdint.h>

#ifdef __GNUC__
    #define PACKED __attribute__((packed))
#elif defined(_MSC_VER)
    #define PACKED
#else
    #define PACKED
#endif

// For MSVC, use pragma pack
#ifdef _MSC_VER
    #pragma pack(push, 1)
#endif

// ================================
// NETWORK CONFIGURATION
// ================================

#define DEFAULT_TCP_PORT        8080
#define MULTICAST_PORT_START    9000
#define MULTICAST_BASE_IP       "224.1.1."  // Global scope for multi-hop

// Multicast TTL for lab environment (configurable)
#define MULTICAST_TTL_DEFAULT   32   // Allow 32 hops for lab topology
#define MULTICAST_TTL_LAN       1    // For single LAN testing
#define MULTICAST_TTL_LAB       64   // For complex lab topologies

// ================================
// TIMING CONFIGURATION
// ================================

#define KEEPALIVE_INTERVAL_SEC  10   
#define CONNECTION_TIMEOUT_SEC  30 
#define RESPONSE_TIMEOUT_SEC    10
#define SESSION_TIMEOUT_SEC     30

// ================================
// HELPER MACROS AND CONSTANTS
// ================================

#define MAX_USERNAME_LEN    32
#define MAX_PASSWORD_LEN    64
#define MAX_ROOM_NAME_LEN   64
#define MAX_MESSAGE_LEN     512
#define MAX_ERROR_MSG_LEN   256

// Session token validation
#define INVALID_SESSION_TOKEN 0

// ================================
// MESSAGE TYPES DEFINITIONS
// ================================


typedef enum {
    // Authentication messages
    LOGIN_REQUEST       = 0x0001,
    LOGIN_SUCCESS       = 0x0002,  
    LOGIN_FAILED        = 0x0003,

    // Room management messages
    JOIN_ROOM_REQUEST   = 0x0010,
    JOIN_ROOM_SUCCESS   = 0x0011,
    JOIN_ROOM_FAILED    = 0x0012,
    JOIN_ROOM_IN_PROGRESS = 0x0013,  // Server confirms join request received
    LEAVE_ROOM_REQUEST  = 0x0020,
    LEAVE_ROOM_RESPONSE  = 0x0021,
    CREATE_ROOM_REQUEST = 0x0030,
    CREATE_ROOM_RESPONSE = 0x0031, // Response to create room request
    CREATE_ROOM_SUCCESS = 0x0032,
    CREATE_ROOM_FAILED  = 0x0033,

    // Chat messages
    CHAT_MESSAGE        = 0x0040,
    PRIVATE_MESSAGE     = 0x0050,
    USER_JOINED_ROOM    = 0x0060,  // Notification when someone joins
    USER_LEFT_ROOM      = 0x0061,  // Notification when someone leaves

    // Connection management
    KEEPALIVE           = 0x0070,
    DISCONNECT_REQUEST  = 0x0080,
    DISCONNECT_SUCCESS  = 0x0081,
    DISCONNECT_ACK      = 0x0082,
    CLIENT_KICKED       = 0x0083,  // Server kicks client
    FORCE_DISCONNECT    = 0x0084,  // Server forces disconnection
    CONNECTION_LOST     = 0x0085,  // Notification of lost connection

    // Error and status messages
    ERROR_MESSAGE       = 0x0090,
    RETRY_CONNECTION    = 0x0091,  // Client requests to retry connection
    ROOM_LIST_REQUEST   = 0x00A0,
    ROOM_LIST_RESPONSE  = 0x00A1,
    USER_LIST_REQUEST   = 0x00B0,
    USER_LIST_RESPONSE  = 0x00B1
} message_type_t;

// ================================
// ERROR CODE ENUMS
// ================================

typedef enum {
    LOGIN_SUCCESS_CODE      = 0,
    LOGIN_WRONG_PASSWORD    = 1,
    LOGIN_USER_EXISTS       = 2,
    LOGIN_SERVER_FULL       = 3
} login_error_t;

typedef enum {
    ROOM_SUCCESS_CODE       = 0,
    ROOM_NOT_FOUND          = 1,
    ROOM_WRONG_PASSWORD     = 2,
    ROOM_FULL               = 3,
    ROOM_NAME_EXISTS        = 4,
    ROOM_JOIN_TIMEOUT       = 5
} room_error_t;

typedef enum {
    CONNECTION_NETWORK_ERROR    = 1,
    CONNECTION_TIMEOUT          = 2,
    CONNECTION_SERVER_SHUTDOWN  = 3,
    CONNECTION_KICKED_BY_ADMIN  = 4
} connection_error_t;

// ================================
// BASIC MESSAGE STRUCTURES
// ================================

// Common header for all messages
struct message_header {
    uint16_t msg_type;
    uint16_t msg_length;    // Total message length including header
    uint32_t timestamp;     // Unix timestamp
} PACKED;

// ================================
// AUTHENTICATION MESSAGES
// ================================

// Client -> Server: Login request with credentials
struct login_request {
    uint16_t msg_type;        // LOGIN_REQUEST
    uint16_t msg_length;      
    uint32_t timestamp;
    uint8_t username_len;     // Max 32 chars
    char username[32];        
    uint8_t password_len;     // Max 64 chars
    char password[64];        
} PACKED;

// Server -> Client: Login response with session token or error
struct login_response {
    uint16_t msg_type;        // LOGIN_SUCCESS/LOGIN_FAILED
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;   // 0 if login failed
    uint8_t error_code;       // 0=success, 1=wrong_pass, 2=user_exists, 3=server_full
    uint8_t error_msg_len;    
    char error_msg[128];      
} PACKED;

// ================================
// ROOM MANAGEMENT MESSAGES
// ================================

// Client -> Server: Request to join existing room
struct join_room_request {
    uint16_t msg_type;        // JOIN_ROOM_REQUEST
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;   // Must be valid
    uint8_t room_name_len;
    char room_name[64];
    uint8_t password_len;     // 0 if no password
    char room_password[32];   
} PACKED;

// Server -> Client: Response to join room request
struct join_room_response {
    uint16_t msg_type;        // JOIN_ROOM_SUCCESS/JOIN_ROOM_FAILED
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;
    uint16_t room_id;         // Unique room identifier
    char multicast_addr[16];  // IP address for multicast (e.g., "239.1.1.5")
    uint16_t multicast_port;  // Port for multicast
    uint8_t error_code;       // 0=success, 1=room_not_found, 2=wrong_password, 3=room_full
    uint8_t error_msg_len;
    char error_msg[128];
} PACKED;

// Client -> Server: Request to create new room
struct create_room_request {
    uint16_t msg_type;        // CREATE_ROOM_REQUEST
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;
    uint8_t room_name_len;
    char room_name[64];
    uint8_t password_len;     // 0 for public room
    char room_password[32];
    uint8_t max_users;        // 0 = unlimited
} PACKED;

// Server -> Client: Response to create room request
struct create_room_response {
    uint16_t msg_type;        // CREATE_ROOM_SUCCESS/CREATE_ROOM_FAILED
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;
    uint16_t room_id;         // Unique room identifier
    char room_name[32];    // Name of the created room
    char multicast_addr[16];  // IP address for multicast (e.g., "239.1.1.5")
    uint16_t multicast_port;  // Port for multicast
    uint8_t error_code;       // 0=success, 1=room_name_exists, 2=server_room_limit_reached
    uint8_t error_msg_len;    // Length of the error message
    char error_msg[128];      // Error message if any
} PACKED;


// Client -> Server: Request to leave current room
struct leave_room_request {
    uint16_t msg_type;        // LEAVE_ROOM_REQUEST
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;
} PACKED;

// Server -> Client: Response to leave room request
struct leave_room_response {
    uint16_t msg_type;        //LEAVE_ROOM_RESPONSE
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;
    uint8_t error_code;       // 0=success, 1=not_in_room, 2=room_not_found
    uint8_t error_msg_len;    // Length of the error message
    char error_msg[128];      // Error message if any
} PACKED;

// Server -> Client: Confirmation of join request received (JOINING ROOM state)
struct join_room_in_progress {
    uint16_t msg_type;        // JOIN_ROOM_IN_PROGRESS
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;
    uint8_t status_msg_len;
    char status_msg[128];     // e.g., "Processing room join..."
} PACKED;

// ================================
// CHAT MESSAGES
// ================================

// Client -> Server: Regular chat message (server forwards to room)
struct chat_message {
    uint16_t msg_type;        // CHAT_MESSAGE
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;
    uint32_t room_id;         // Which room this message belongs to
    uint8_t sender_username_len;
    char sender_username[MAX_USERNAME_LEN];
    uint16_t message_len;
    char message[512];        // Max message length
} PACKED;

// Client -> Server: Private message to specific user
struct private_message {
    uint16_t msg_type;        // PRIVATE_MESSAGE
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;
    uint8_t target_username_len;
    char target_username[32];
    uint16_t message_len;
    char message[512];
} PACKED;

// Server -> Clients: Notification when user joins/leaves room
struct user_notification {
    uint16_t msg_type;        // USER_JOINED_ROOM/USER_LEFT_ROOM
    uint16_t msg_length;
    uint32_t timestamp;
    uint8_t username_len;
    char username[32];
    uint16_t room_id;
} PACKED;

// ================================
// CONNECTION MANAGEMENT
// ================================

// Client <-> Server: Keep connection alive
struct keepalive {
    uint16_t msg_type;        // KEEPALIVE
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;
} PACKED;

// Client -> Server: Request to disconnect gracefully
struct disconnect_request {
    uint16_t msg_type;        // DISCONNECT_REQUEST
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;
} PACKED;

// Server -> Client: Response to disconnect request
struct disconnect_response {
    uint16_t msg_type;        // DISCONNECT_SUCCESS/DISCONNECT_ACK
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;
    uint8_t status_code;      // 0=success, 1=already_disconnected
    uint8_t status_msg_len;
    char status_msg[64];      // Optional goodbye message
} PACKED;

// Server -> Client: Connection status notification
struct connection_status {
    uint16_t msg_type;        // CONNECTION_LOST/CLIENT_KICKED/FORCE_DISCONNECT
    uint16_t msg_length;
    uint32_t timestamp;
    uint8_t reason_code;      // 1=network_error, 2=timeout, 3=server_shutdown, 4=kicked_by_admin
    uint8_t reason_msg_len;
    char reason_msg[128];
} PACKED;

// Client -> Server: Request to retry connection after error
struct retry_connection {
    uint16_t msg_type;        // RETRY_CONNECTION
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;   // Previous session token if available
} PACKED;

// ========================================
// INFORMATION REQUESTS
// ================================

// Client -> Server: Request list of available rooms
struct room_list_request {
    uint16_t msg_type;        // ROOM_LIST_REQUEST
    uint16_t msg_length;
    uint32_t timestamp;
    uint32_t session_token;
} PACKED;

// Server -> Client: Response with room list
struct room_list_response {
    uint16_t msg_type;        // ROOM_LIST_RESPONSE
    uint16_t msg_length;
    uint32_t timestamp;
    uint8_t room_count;       // Count of rooms available
    // Followed by room_count entries of:
    // uint16_t room_id + uint8_t room_name_len + char room_name[] + uint8_t user_count + uint8_t has_password
} PACKED;

// Client -> Server: Request list of users in current room
struct user_list_request {  
    uint16_t msg_type;        // USER_LIST_REQUEST
    uint16_t msg_length;
    uint32_t timestamp;       // Room to get user list from
    uint32_t session_token;
    uint16_t room_id;         // Room to get user list from
} PACKED;

// Server -> Client: Response with user list in room
struct user_list_response { 
    uint16_t msg_type;        // USER_LIST_RESPONSE
    uint16_t msg_length;
    uint32_t timestamp;
    uint8_t user_count;       // Number of users in the room
    // Followed by user_count entries of:
    // uint8_t username_len + char username[]
} PACKED;

// ================================
// ERROR HANDLING
// ================================

// Server -> Client: General error message
struct error_message {
    uint16_t msg_type;        // ERROR_MESSAGE
    uint16_t msg_length;
    uint32_t timestamp;
    uint8_t error_code;       // Custom error codes
    uint8_t error_msg_len;
    char error_msg[256];      // Error message
} PACKED;

#endif // CHAT_PROTOCOL_H