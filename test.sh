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
CLIENT_EXEC="./build/client"
SERVER_EXEC="./build/server"
TEST_TIMEOUT=300  # Increased to 5 minutes

# Global server PID
SERVER_PID=""

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
    if [ ! -z "$SERVER_PID" ]; then
        kill -9 $SERVER_PID 2>/dev/null
    fi
    pkill -f "$SERVER_EXEC" 2>/dev/null
    pkill -f "$CLIENT_EXEC" 2>/dev/null
    rm -f *.log *.txt test_*.txt client_*.txt 2>/dev/null
    sleep 2
}

# Start server function
start_server() {
    print_status "Starting server..."
    
    # Kill any existing server
    if [ ! -z "$SERVER_PID" ]; then
        kill -9 $SERVER_PID 2>/dev/null
    fi
    pkill -f "$SERVER_EXEC" 2>/dev/null
    sleep 2
    
    # Start new server
    timeout $TEST_TIMEOUT $SERVER_EXEC $SERVER_PORT > server.log 2>&1 &
    SERVER_PID=$!
    
    # Wait for server to start
    sleep 3
    
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

# Check and restart server if needed
restart_server_if_needed() {
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        print_status "Server died, restarting..."
        start_server
        return $?
    fi
    return 0
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
    if start_server; then
        return 0
    else
        return 1
    fi
}

# Test basic client connection
test_client_connection() {
    print_status "Testing basic client connection..."
    restart_server_if_needed
    
    cat > test_client_script.txt << EOF
help
quit
EOF
    
    timeout 10s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < test_client_script.txt > client.log 2>&1
    
    if grep -q "help\|Available commands\|Chat Client Help" client.log; then
        print_success "Client connection test passed"
        return 0
    else
        print_error "Client connection test failed"
        echo "Client log:"
        cat client.log
        return 1
    fi
}

# Test user authentication
test_authentication() {
    print_status "Testing user authentication..."
    restart_server_if_needed
    
    cat > auth_test.txt << EOF
login testuser1 password123
room_list
quit
EOF
    
    timeout 15s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < auth_test.txt > auth.log 2>&1
    
    if grep -q "logged in\|Login successful\|testuser1\|room_list\|Available rooms" auth.log; then
        print_success "Authentication test passed"
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
    restart_server_if_needed
    
    cat > room_test.txt << EOF
login roomuser password123
create_room testroom testpass
room_list
join_room testroom testpass
user_list
leave_room
quit
EOF
    
    timeout 20s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < room_test.txt > room.log 2>&1
    
    if grep -q "room\|Room\|created\|joined\|testroom\|roomuser" room.log; then
        print_success "Room operations test passed"
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
    restart_server_if_needed
    
    cat > chat_test.txt << EOF
login chatuser password123
create_room chatroom chatpass
join_room chatroom chatpass
chat Hello World!
chat This is a test message
leave_room
quit
EOF
    
    timeout 20s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < chat_test.txt > chat.log 2>&1
    
    if grep -q "Hello\|message\|chat\|Message sent\|chatroom\|chatuser" chat.log; then
        print_success "Chat messaging test passed"
        return 0
    else
        print_error "Chat messaging test failed"
        echo "Chat log:"
        cat chat.log
        return 1
    fi
}

# Test private messaging
test_private_messaging() {
    print_status "Testing private messaging..."
    restart_server_if_needed
    
    cat > private_test.txt << EOF
login privateuser password123
create_room privateroom privatepass
join_room privateroom privatepass
private testuser1 Hello this is a private message
user_list
quit
EOF
    
    timeout 15s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < private_test.txt > private.log 2>&1
    
    if grep -q "private\|Private\|message\|privateuser\|privateroom" private.log; then
        print_success "Private messaging test passed"
        return 0
    else
        print_error "Private messaging test failed"
        echo "Private log:"
        cat private.log
        return 1
    fi
}

# Test invalid commands
test_invalid_commands() {
    print_status "Testing invalid command handling..."
    restart_server_if_needed
    
    cat > invalid_test.txt << EOF
login invaliduser password123
invalid_command
unknown_command test
badcommand
help
quit
EOF
    
    timeout 15s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < invalid_test.txt > invalid.log 2>&1
    
    if grep -q "Unknown command\|Invalid\|help\|Available commands" invalid.log; then
        print_success "Invalid command handling test passed"
        return 0
    else
        print_error "Invalid command handling test failed"
        echo "Invalid log:"
        cat invalid.log
        return 1
    fi
}

# Test concurrent clients
test_concurrent_clients() {
    print_status "Testing concurrent clients..."
    restart_server_if_needed
    
    # Create test scripts for multiple clients
    for i in {1..3}; do
        cat > client_${i}_test.txt << EOF
login user${i} pass${i}
create_room room${i} roompass${i}
join_room room${i} roompass${i}
chat Hello from client ${i}
room_list
user_list
leave_room
quit
EOF
    done
    
    # Start multiple clients in background
    for i in {1..3}; do
        timeout 15s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < client_${i}_test.txt > client_${i}.log 2>&1 &
    done
    
    # Wait for all clients to finish
    wait
    
    # Check if at least one client succeeded
    success=0
    for i in {1..3}; do
        if grep -q "user${i}\|room${i}\|Hello\|client ${i}" client_${i}.log; then
            success=1
            break
        fi
    done
    
    if [ $success -eq 1 ]; then
        print_success "Concurrent client test passed"
        return 0
    else
        print_error "Concurrent client test failed"
        echo "Client logs:"
        for i in {1..3}; do
            echo "=== Client $i ==="
            cat client_${i}.log
        done
        return 1
    fi
}

# Test room password validation
test_room_password_validation() {
    print_status "Testing room password validation..."
    restart_server_if_needed
    
    cat > password_test.txt << EOF
login passuser password123
create_room secretroom secretpass
join_room secretroom wrongpass
join_room secretroom secretpass
room_list
quit
EOF
    
    timeout 15s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < password_test.txt > password.log 2>&1
    
    if grep -q "wrong\|incorrect\|failed\|success\|joined\|secretroom\|passuser" password.log; then
        print_success "Room password validation test passed"
        return 0
    else
        print_error "Room password validation test failed"
        echo "Password log:"
        cat password.log
        return 1
    fi
}

# Test stress with many operations
test_stress_operations() {
    print_status "Testing stress operations..."
    restart_server_if_needed
    
    cat > stress_test.txt << EOF
login stressuser password123
create_room stress1 pass1
create_room stress2 pass2
create_room stress3 pass3
room_list
join_room stress1 pass1
chat Message 1
chat Message 2
user_list
leave_room
join_room stress2 pass2
chat Another message
leave_room
room_list
quit
EOF
    
    timeout 25s $CLIENT_EXEC $SERVER_IP $SERVER_PORT < stress_test.txt > stress.log 2>&1
    
    if grep -q "stress\|Message\|stressuser" stress.log; then
        print_success "Stress operations test passed"
        return 0
    else
        print_error "Stress operations test failed"
        echo "Stress log:"
        cat stress.log
        return 1
    fi
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
        
        # Run all tests with server restart checks
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
        
        if test_private_messaging; then
            test_results+=("Private Messaging: PASS")
        else
            test_results+=("Private Messaging: FAIL")
        fi
        
        if test_invalid_commands; then
            test_results+=("Invalid Commands: PASS")
        else
            test_results+=("Invalid Commands: FAIL")
        fi
        
        if test_concurrent_clients; then
            test_results+=("Concurrent Clients: PASS")
        else
            test_results+=("Concurrent Clients: FAIL")
        fi
        
        if test_room_password_validation; then
            test_results+=("Room Password Validation: PASS")
        else
            test_results+=("Room Password Validation: FAIL")
        fi
        
        if test_stress_operations; then
            test_results+=("Stress Operations: PASS")
        else
            test_results+=("Stress Operations: FAIL")
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
        print_success "All tests passed! 🎉"
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