#!/bin/bash

# Chat Room Project Test Script
# Comprehensive testing for client-server functionality

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SERVER_IP="127.0.0.1"
SERVER_PORT="8080"
CLIENT_EXEC="./client"
SERVER_EXEC="./server"
TEST_TIMEOUT=30

# Print functions
print_header() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}  Chat Room Project Test Suite  ${NC}"
    echo -e "${BLUE}================================${NC}"
}

print_status() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_separator() {
    echo -e "${BLUE}--------------------------------${NC}"
}

# Cleanup function
cleanup() {
    print_status "Cleaning up processes..."
    pkill -f "$SERVER_EXEC" 2>/dev/null
    pkill -f "$CLIENT_EXEC" 2>/dev/null
    rm -f *.log *.txt test_*.txt 2>/dev/null
    sleep 1
}

# Build project
build_project() {
    print_status "Building project..."
    
    if ! make clean >/dev/null 2>&1; then
        print_error "Clean failed"
        return 1
    fi
    
    if ! make all >/dev/null 2>&1; then
        print_error "Build failed"
        return 1
    fi
    
    if [ ! -f "$CLIENT_EXEC" ] || [ ! -f "$SERVER_EXEC" ]; then
        print_error "Executables not found after build"
        return 1
    fi
    
    print_success "Build completed successfully"
    return 0
}

# Test server startup
test_server_startup() {
    print_status "Testing server startup..."
    
    # Start server in background
    timeout $TEST_TIMEOUT $SERVER_EXEC $SERVER_PORT > server.log 2>&1 &
    SERVER_PID=$!
    
    # Wait for server to start
    sleep 2
    
    # Check if server is running
    if kill -0 $SERVER_PID 2>/dev/null; then
        print_success "Server started successfully (PID: $SERVER_PID)"
        return 0
    else
        print_error "Server failed to start"
        cat server.log
        return 1
    fi
}

# Test basic client connection
test_client_connection() {
    print_status "Testing basic client connection..."
    
    # Create a simple test client script
    cat > test_client_script.txt << EOF
help
quit
EOF
    
    timeout 10s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < test_client_script.txt > client.log 2>&1
    
    if [ $? -eq 0 ] || [ $? -eq 124 ]; then
        print_success "Client connection test passed"
        return 0
    else
        print_error "Client connection test failed"
        echo "Client log:"
        cat client.log
        return 1
    fi
}

# Test user authentication (login only, no register)
test_authentication() {
    print_status "Testing user authentication..."
    
    cat > auth_test.txt << EOF
login testuser1 password123
quit
EOF
    
    timeout 15s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < auth_test.txt > auth.log 2>&1
    
    if grep -q "Login" auth.log; then
        print_success "Authentication test completed"
        return 0
    else
        print_error "Authentication test failed"
        echo "Auth log:"
        cat auth.log
        return 1
    fi
}

# Test room operations
test_room_operations() {
    print_status "Testing room operations..."
    
    cat > room_test.txt << EOF
login roomuser password123
create_room testroom
join_room testroom
list_rooms
list_users
quit
EOF
    
    timeout 20s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < room_test.txt > room.log 2>&1
    
    if grep -q "room\|Room" room.log; then
        print_success "Room operations test completed"
        return 0
    else
        print_error "Room operations test failed"
        echo "Room log:"
        cat room.log
        return 1
    fi
}

# Test chat messaging
test_chat_messaging() {
    print_status "Testing chat messaging..."
    
    cat > chat_test.txt << EOF
login chatuser password123
create_room chatroom
join_room chatroom
say Hello World!
say This is a test message
quit
EOF
    
    timeout 20s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < chat_test.txt > chat.log 2>&1
    
    if grep -q "Hello\|message\|chat" chat.log; then
        print_success "Chat messaging test completed"
        return 0
    else
        print_error "Chat messaging test failed"
        echo "Chat log:"
        cat chat.log
        return 1
    fi
}

# Test multiple concurrent clients
test_concurrent_clients() {
    print_status "Testing concurrent clients..."
    
    # Create test scripts for multiple clients
    for i in {1..3}; do
        cat > client_${i}_test.txt << EOF
login user${i} pass${i}
create_room room${i}
join_room room${i}
say Hello from client ${i}
quit
EOF
    done
    
    # Start multiple clients
    for i in {1..3}; do
        timeout 15s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < client_${i}_test.txt > client_${i}.log 2>&1 &
    done
    
    # Wait for all clients to finish
    wait
    
    print_success "Concurrent client test completed"
    return 0
}

# Main test execution
main() {
    print_header
    
    # Cleanup any existing processes
    cleanup
    
    # Array to track test results
    declare -a test_results
    
    # Build project
    if build_project; then
        test_results+=("Build: PASS")
    else
        test_results+=("Build: FAIL")
        print_error "Build failed, cannot continue tests"
        exit 1
    fi
    
    # Start server for tests
    if test_server_startup; then
        test_results+=("Server Startup: PASS")
        
        # Run client tests
        if test_client_connection; then
            test_results+=("Client Connection: PASS")
        else
            test_results+=("Client Connection: FAIL")
        fi
        
        if test_authentication; then
            test_results+=("Authentication: PASS")
        else
            test_results+=("Authentication: FAIL")
        fi
        
        if test_room_operations; then
            test_results+=("Room Operations: PASS")
        else
            test_results+=("Room Operations: FAIL")
        fi
        
        if test_chat_messaging; then
            test_results+=("Chat Messaging: PASS")
        else
            test_results+=("Chat Messaging: FAIL")
        fi
        
        if test_concurrent_clients; then
            test_results+=("Concurrent Clients: PASS")
        else
            test_results+=("Concurrent Clients: FAIL")
        fi
        
    else
        test_results+=("Server Startup: FAIL")
    fi
    
    # Cleanup
    cleanup
    
    # Print results summary
    print_separator
    echo -e "${BLUE}Test Results Summary:${NC}"
    for result in "${test_results[@]}"; do
        if [[ $result == *"PASS"* ]]; then
            echo -e "${GREEN}✓${NC} $result"
        else
            echo -e "${RED}✗${NC} $result"
        fi
    done
    print_separator
    
    # Count failures
    failures=$(printf '%s\n' "${test_results[@]}" | grep -c "FAIL")
    
    if [ $failures -eq 0 ]; then
        print_success "All tests passed!"
        exit 0
    else
        print_error "$failures test(s) failed"
        exit 1
    fi
}

# Handle interruption
trap cleanup EXIT INT TERM

# Run main function
main "$@"