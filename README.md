# Chat Room Project
Multi-Room Chat System for Network Programming Course

## Project Status: 🚧 In Development

### Branch: `fix-room-management`
Currently working on room management functionality and protocol improvements.

## Server Development Progress

### ✅ Completed Features
- **TCP Server Setup**: Welcome socket with select() for multiple connections
- **Client Management**: Connection handling, authentication state tracking  
- **Authentication System**: Login with session tokens and validation
- **Connection Management**: Keepalive, graceful disconnect handling, timeout detection
- **Room Management**: Create, join, and leave room functionality
- **Utility Functions**: Room/client search and management helpers
- **Protocol Improvements**: Proper struct handling and null-termination fixes

### 🔄 Currently Working On
- **Chat Message Handling**: Implementing multicast message distribution
- **Private Messaging**: Direct client-to-client messaging through server

### ⏳ Next Steps
- Complete chat message handling and multicast implementation
- Add private messaging functionality  
- Implement room/user list requests
- Test and validate all room management features
- Merge back to server-development branch

### 🔧 Implemented Functions
**Authentication & Connection:**
- `handle_login_request()` - User authentication with session tokens
- `handle_keepalive()` - Connection maintenance
- `handle_disconnect_request()` - Graceful disconnection

**Room Management:**
- `handle_create_room_request()` - Create new chat rooms
- `handle_join_room_request()` - Join existing rooms with password support
- `handle_leave_room_request()` - Leave rooms (auto-closes empty rooms)

**Utility Functions:**
- `find_free_room_slot()`, `find_room_by_name()`, `find_client_by_socket()`
- Input validation and error handling helpers

### 🎯 Remaining Functions to Implement
- `handle_chat_message()` - Multicast chat messages to room members
- `handle_private_message()` - Direct messaging between users
- `handle_room_list_request()` - List available rooms
- `handle_user_list_request()` - List users in current room

### 🏗️ Architecture
- **Server**: Single-threaded with select() for I/O multiplexing
- **Protocol**: TCP for control messages, UDP multicast for chat messages
- **Authentication**: Session token-based security
- **Room System**: Dynamic room creation with password protection