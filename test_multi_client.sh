#!/bin/bash

# Multi-client automated test script
SERVER_IP="192.7.5.1"  # Change to your server IP
SERVER_PORT="8080"
CLIENT_IPS=("192.7.1.1" "192.7.2.1" "192.7.3.1" "192.7.4.1")
ROOM_NAME="testroom"
ROOM_PASS="testpass"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Test scenarios
create_test_scenario() {
    local client_id=$1
    local scenario=$2
    
    case $scenario in
        "creator")
            cat > client_${client_id}_script.txt << EOF
login user${client_id} pass${client_id}
sleep 2
create_room ${ROOM_NAME} ${ROOM_PASS}
sleep 2
join_room ${ROOM_NAME} ${ROOM_PASS}
sleep 3
chat Hello from room creator!
sleep 2
user_list
sleep 2
chat Testing multicast from user${client_id}
sleep 5
private user2 This is a private message from creator
sleep 3
room_list
sleep 2
quit
EOF
            ;;
        "joiner")
            cat > client_${client_id}_script.txt << EOF
login user${client_id} pass${client_id}
sleep 3
room_list
sleep 2
join_room ${ROOM_NAME} ${ROOM_PASS}
sleep 2
chat Hi everyone! User${client_id} joined
sleep 3
user_list
sleep 2
chat Testing from user${client_id}
sleep 4
private user1 Private reply from user${client_id}
sleep 3
leave_room
sleep 2
quit
EOF
            ;;
        "late_joiner")
            cat > client_${client_id}_script.txt << EOF
login user${client_id} pass${client_id}
sleep 8
room_list
sleep 2
join_room ${ROOM_NAME} ${ROOM_PASS}
sleep 2
chat Late joiner user${client_id} here!
sleep 3
user_list
sleep 4
chat Final message from user${client_id}
sleep 2
quit
EOF
            ;;
    esac
}

# Run test on single client
run_client_test() {
    local client_ip=$1
    local client_id=$2
    local scenario=$3
    
    print_status "Starting test on client $client_ip (user$client_id, scenario: $scenario)"
    
    # Create test script
    create_test_scenario $client_id $scenario
    
    # Copy script to client
    scp client_${client_id}_script.txt root@$client_ip:/root/chatroom/ 2>/dev/null
    if [ $? -ne 0 ]; then
        print_error "Failed to copy script to $client_ip"
        return 1
    fi
    
    # Run test on client
    ssh root@$client_ip "cd /root/chatroom && timeout 60s ./client $SERVER_IP $SERVER_PORT < client_${client_id}_script.txt > client_${client_id}.log 2>&1" &
    
    local pid=$!
    echo $pid > client_${client_id}.pid
    
    print_success "Client $client_id test started (PID: $pid)"
    return 0
}

# Monitor test progress
monitor_tests() {
    local total_clients=$1
    local max_wait=70  # seconds
    local wait_count=0
    
    print_status "Monitoring tests (max wait: ${max_wait}s)..."
    
    while [ $wait_count -lt $max_wait ]; do
        local running_count=0
        
        for i in $(seq 1 $total_clients); do
            if [ -f client_${i}.pid ]; then
                local pid=$(cat client_${i}.pid)
                if kill -0 $pid 2>/dev/null; then
                    ((running_count++))
                fi
            fi
        done
        
        if [ $running_count -eq 0 ]; then
            print_success "All tests completed!"
            break
        fi
        
        echo -ne "\r${YELLOW}[WAIT]${NC} $running_count clients still running... (${wait_count}s)"
        sleep 2
        ((wait_count+=2))
    done
    echo ""
}

# Collect results
collect_results() {
    local total_clients=$1
    
    print_status "Collecting results from clients..."
    
    for i in $(seq 1 $total_clients); do
        local client_ip=${CLIENT_IPS[$((i-1))]}
        
        # Get log from client
        scp root@$client_ip:/root/chatroom/client_${i}.log ./client_${i}.log 2>/dev/null
        
        if [ -f client_${i}.log ]; then
            local lines=$(wc -l < client_${i}.log)
            if [ $lines -gt 5 ]; then
                print_success "Client $i (${client_ip}): $lines log lines"
            else
                print_error "Client $i (${client_ip}): Only $lines log lines (possible failure)"
            fi
        else
            print_error "Client $i (${client_ip}): No log file retrieved"
        fi
    done
}

# Analyze results
analyze_results() {
    local total_clients=$1
    
    print_status "Analyzing test results..."
    
    echo -e "\n${BLUE}=== TEST ANALYSIS ===${NC}"
    
    # Check for successful logins
    local login_success=0
    for i in $(seq 1 $total_clients); do
        if [ -f client_${i}.log ] && grep -q "Login successful" client_${i}.log; then
            ((login_success++))
        fi
    done
    echo "Successful logins: $login_success/$total_clients"
    
    # Check for room operations
    local room_joins=0
    for i in $(seq 1 $total_clients); do
        if [ -f client_${i}.log ] && grep -q "Successfully joined room" client_${i}.log; then
            ((room_joins++))
        fi
    done
    echo "Successful room joins: $room_joins/$total_clients"
    
    # Check for chat messages
    local chat_messages=0
    for i in $(seq 1 $total_clients); do
        if [ -f client_${i}.log ] && grep -q "\[.*\]:" client_${i}.log; then
            ((chat_messages++))
        fi
    done
    echo "Clients that received chat messages: $chat_messages/$total_clients"
    
    # Check for private messages
    local private_messages=0
    for i in $(seq 1 $total_clients); do
        if [ -f client_${i}.log ] && grep -q "PRIVATE" client_${i}.log; then
            ((private_messages++))
        fi
    done
    echo "Clients with private message activity: $private_messages/$total_clients"
    
    # Overall success rate
    local overall_score=$((login_success + room_joins + chat_messages))
    local max_score=$((total_clients * 3))
    local success_rate=$((overall_score * 100 / max_score))
    
    echo -e "\n${BLUE}Overall Success Rate: ${success_rate}%${NC}"
    
    if [ $success_rate -ge 80 ]; then
        print_success "Test PASSED! Multi-client functionality working well."
    elif [ $success_rate -ge 60 ]; then
        echo -e "${YELLOW}[WARNING]${NC} Test partially successful. Some issues detected."
    else
        print_error "Test FAILED! Significant issues with multi-client functionality."
    fi
}

# Show detailed logs
show_detailed_logs() {
    echo -e "\n${BLUE}=== DETAILED LOGS ===${NC}"
    
    for i in $(seq 1 4); do
        echo -e "\n${YELLOW}--- Client $i Log ---${NC}"
        if [ -f client_${i}.log ]; then
            tail -20 client_${i}.log
        else
            echo "No log available"
        fi
    done
}

# Cleanup
cleanup() {
    print_status "Cleaning up..."
    
    # Kill any remaining processes
    for i in $(seq 1 4); do
        if [ -f client_${i}.pid ]; then
            local pid=$(cat client_${i}.pid)
            kill $pid 2>/dev/null
            rm -f client_${i}.pid
        fi
        rm -f client_${i}_script.txt
    done
}

# Main test execution
main() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}  Multi-Client Chat Room Test   ${NC}"
    echo -e "${BLUE}================================${NC}"
    
    print_status "Server: $SERVER_IP:$SERVER_PORT"
    print_status "Room: $ROOM_NAME (password: $ROOM_PASS)"
    print_status "Clients: ${#CLIENT_IPS[@]}"
    
    # Test scenarios: creator, joiner, joiner, late_joiner
    local scenarios=("creator" "joiner" "joiner" "late_joiner")
    
    # Start tests on all clients
    for i in $(seq 1 ${#CLIENT_IPS[@]}); do
        local client_ip=${CLIENT_IPS[$((i-1))]}
        local scenario=${scenarios[$((i-1))]}
        
        run_client_test $client_ip $i $scenario
        sleep 1  # Small delay between client starts
    done
    
    # Monitor progress
    monitor_tests ${#CLIENT_IPS[@]}
    
    # Collect and analyze results
    collect_results ${#CLIENT_IPS[@]}
    analyze_results ${#CLIENT_IPS[@]}
    
    # Show logs if requested
    if [ "$1" = "--verbose" ]; then
        show_detailed_logs
    fi
    
    # Cleanup
    cleanup
    
    echo -e "\n${BLUE}Test completed!${NC}"
}

# Handle script termination
trap cleanup EXIT

# Run main function
main "$@"
