# Multi-Room Chat Server Project

A comprehensive C implementation of a multi-room chat server with TCP control connections and UDP multicast messaging, featuring authentication, room management, and multi-threading support.

## 📋 Project Overview

This project implements a robust network programming solution that demonstrates:
- **TCP Server with select()** for handling up to 50 concurrent client connections
- **UDP Multicast Communication** for efficient room-based messaging with dynamic address allocation (239.1.1.x)
- **Multi-threading Support** for concurrent client handling and thread-safe operations
- **User Authentication System** with session tokens and secure login/logout
- **Dynamic Room Management** with password protection and automatic cleanup
- **Real-time Chat Features** including private messaging and user notifications
- **Comprehensive Protocol** supporting 20+ message types for all chat operations
## 🏗️ Architecture

### Server Architecture
```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   TCP Server    │    │  Room Manager   │    │ Thread Manager  │
│   (Control)     │◄──►│   (Multicast)   │◄──►│  (Concurrent)   │
└─────────────────┘    └─────────────────┘    └─────────────────┘
         │                       │                       │
         ▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│ Authentication  │    │ UDP Multicast   │    │ Client Threads  │
│    System       │    │   Sockets       │    │   (Optional)    │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```


### Client Architecture
```
                    ┌─────────────────┐
                    │   Main Client   │
                    │   (User I/O)    │
                    └─────────┬───────┘
                              │
                              ▼
    ┌─────────────────┐    ┌─────────────────┐
    │  Command Thread │    │  Receiver Thread│
    │ (TCP to Server) │◄──►│ (UDP Multicast) │
    └─────────┬───────┘    └─────────┬───────┘
              │                      │
              ▼                      ▼
    ┌─────────────────┐    ┌─────────────────┐
    │   TCP Socket    │    │   UDP Socket    │
    │ (Login, Rooms,  │    │  (Room Chat &   │
    │ Private msgs)   │    │  Notifications) │
    └─────────────────┘    └─────────────────┘
```

## 🚀 Features

### Core Networking Features
- [x] TCP server with `select()` multiplexing for up to 50 concurrent clients
- [x] UDP multicast communication with dynamic address allocation (239.1.1.x)
- [x] Multi-threading support with thread-safe operations
- [x] Robust I/O handling with non-blocking sockets

### User Management
- [x] User authentication with login/logout
- [x] Session token-based security system
- [x] Unique username enforcement
- [x] Secure password validation
- [x] Active user session tracking

### Room Management
- [x] Create/join/leave rooms dynamically
- [x] Password-protected room access
- [x] Real-time room list generation
- [x] Room-scoped user lists and status
- [x] Automatic multicast address assignment (239.1.1.1-20)
- [x] Automatic room cleanup when empty

### Messaging Features
- [x] Room-based multicast chat
- [x] Private messaging between users via TCP
- [x] Message validation and error handling
- [x] Real-time message delivery

### Additional Features
- [x] Connection keepalive
- [x] Graceful disconnect handling
- [x] Error handling and reporting
- [x] Memory management
- [x] Thread-safe operations

```

## 🛠️ Building the Project

### Prerequisites

#### Windows
- MinGW-w64 or Visual Studio with GCC
- Windows Sockets 2 (ws2_32.lib)
- Pthread library

#### Linux/Unix
- GCC compiler
- POSIX threads (pthread)
- Standard socket libraries

### Build Commands

```bash
# Build everything
make all

# Build server only
make server

# Build client only
make client

# Clean build files
make clean

# Complete cleanup
make distclean

# Show help
make help
```

### Manual Compilation

#### Windows
```cmd
gcc -o build/server.exe server/server.c -lws2_32 -lpthread
gcc -o build/client.exe client/client.c -lws2_32 -lpthread
```

#### Linux
```bash
gcc -o build/server server/server.c -lpthread
gcc -o build/client client/client.c -lpthread
```

## 🚀 Running the Application

### Start the Server
```bash
# Using make
make test-server

# Or directly
./build/server 8080
```

### Connect Clients
```bash
# Using make
make test-client

# Or directly
./build/client 127.0.0.1 8080
```

## 🧪 Testing

### Automated Testing

#### Linux/Unix
```bash
# Make script executable
chmod +x test.sh

# Run all tests
./test.sh

# Run specific tests
./test.sh build    # Build only
./test.sh server   # Start server for manual testing
./test.sh client   # Start client for manual testing
./test.sh clean    # Cleanup
```

#### Windows
```cmd
# Run all tests
test.bat

# Or double-click test.bat in Explorer
```

### Manual Testing

1. **Start the server:**
   ```bash
   ./build/server 8080
   ```

2. **Connect multiple clients:**
   ```bash
   ./build/client 127.0.0.1 8080
   ```

3. **Test basic functionality:**
   ```
   > help
   > register alice password123
   > login alice password123
   > create_room general
   > join_room general
   > chat Hello everyone!
   > list_users
   > list_rooms
   > quit
   ```

## 📡 Protocol Reference


### Network Configuration
- **TCP Port:** Command and control (configurable, default 8080)
- **UDP Multicast Base:** 224.0.0.1 (room-specific addresses)
- **UDP Port Range:** 8001+ (auto-assigned per room)
- **Max Clients:** 50 (configurable)
- **Max Rooms:** 20 (configurable)

## 🔧 Configuration

### Server Configuration (`server.h`)
```c
#define MAX_CLIENTS 100
#define MAX_ROOMS 50
#define MAX_USERS 1000
#define THREAD_POOL_SIZE 10
#define MULTICAST_BASE_ADDR "224.0.0.1"
#define MULTICAST_BASE_PORT 8001
```

### Buffer Sizes (`protocol.h`)
```c
#define BUFFER_SIZE 1024
#define USERNAME_SIZE 32
#define PASSWORD_SIZE 32
#define ROOM_NAME_SIZE 32
#define MESSAGE_SIZE 512
#define TOKEN_SIZE 64
```

## 🔐 Security Features

1. **Authentication System**
   - Password-based user accounts
   - Session token validation
   - Secure session management

2. **Room Security**
   - Password-protected rooms
   - Room-scoped access control
   - Owner privileges

3. **Input Validation**
   - Buffer overflow protection
   - Message size limits
   - Command validation

## 🧵 Threading Model

### Server Threading
- **Main Thread:** Accept connections, select() multiplexing
- **Client Threads:** Optional per-client thread handling
- **Multicast Thread:** UDP message distribution
- **Cleanup Thread:** Resource management

### Client Threading
- **Main Thread:** User input and TCP commands
- **UDP Receiver Thread:** Multicast message reception

### Thread Safety
- Mutex protection for shared data structures
- Atomic operations for counters
- Safe memory management

## 🔍 Debugging and Logging

### Debug Mode
Compile with debug flags:
```bash
make CFLAGS="-DDEBUG -g" all
```

### Log Files
- `server.log` - Server operations and errors
- `client.log` - Client connection logs
- `build.log` - Compilation output
- `valgrind.log` - Memory leak detection (if available)

### Common Issues

1. **Port Already in Use**
   ```bash
   # Find and kill process
   netstat -an | grep :8080
   kill $(lsof -t -i:8080)
   ```

2. **Permission Denied**
   ```bash
   # Check port permissions (Linux)
   sudo setcap 'cap_net_bind_service=+ep' ./build/server
   ```

3. **Multicast Not Working**
   - Check firewall settings
   - Verify multicast routing
   - Test with `ping 224.0.0.1`

## 📈 Performance Considerations

### Scalability
- **Clients:** Supports up to 100 concurrent clients
- **Rooms:** Up to 50 simultaneous rooms
- **Messages:** Non-blocking multicast delivery
- **Threads:** Configurable thread pool

### Optimization
- Use `select()` for I/O multiplexing
- Multicast reduces server load for chat messages
- Thread pool prevents resource exhaustion
- Efficient memory management
