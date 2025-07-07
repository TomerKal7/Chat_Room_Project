#!/bin/bash

# Multi-client test without sshpass
SERVER_IP="192.7.5.1"
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

# Create expect script for SSH
create_expect_script() {
    local client_ip=$1
    local client_id=$2
    local script_file=$3
    
    cat > ssh_${client_id}.exp << EOF
#!/usr/bin/expect -f
set timeout 20
spawn scp -o StrictHostKeyChecking=no $script_file root@$client_ip:/root/chatroom/
expect {
    "password:" { 
        send "snoopy\r"
        exp_continue
    }
    "100%" { 
        # File copied successfully
    }
    timeout { 
        exit 1 
    }
}

spawn ssh -o StrictHostKeyChecking=no root@$client_ip "cd /root/chatroom && timeout 60s ./client $SERVER_IP $SERVER_PORT < client_${client_id}_script.txt > client_${client_id}.log 2>&1"
expect {
    "password:" { 
        send "snoopy\r"
        exp_continue
    }
    timeout { 
        exit 1 
    }
}
EOF
    chmod +x ssh_${client_id}.exp
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
quit
EOF
            ;;
    esac
}

# Run test with expect
run_client_test() {
    local client_ip=$1
    local client_id=$2
    local scenario=$3
    
    print_status "Starting test on client $client_ip (user$client_id, scenario: $scenario)"
    
    create_test_scenario $client_id $scenario
    
    if command -v expect >/dev/null 2>&1; then
        create_expect_script $client_ip $client_id "client_${client_id}_script.txt"
        ./ssh_${client_id}.exp &
        local pid=$!
        echo $pid > client_${client_id}.pid
        print_success "Client $client_id test started with expect (PID: $pid)"
    else
        print_error "expect not found. Manual SSH required for client $client_id"
        return 1
    fi
}

# Rest of functions (monitor, collect, analyze) stay the same...
monitor_tests() {
    local total_clients=$1
    local max_wait=70
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

collect_results() {
    local total_clients=$1
    print_status "Collecting results from clients..."
    
    for i in $(seq 1 $total_clients); do
        local client_ip=${CLIENT_IPS[$((i-1))]}
        
        if command -v expect >/dev/null 2>&1; then
            # Use expect to get log
            expect << EOF
set timeout 10
spawn scp -o StrictHostKeyChecking=no root@$client_ip:/root/chatroom/client_${i}.log ./client_${i}.log
expect "password:" { send "snoopy\r" }
expect eof
EOF
        else
            print_error "Cannot retrieve log from client $i - expect not available"
        fi
        
        if [ -f client_${i}.log ]; then
            local lines=$(wc -l < client_${i}.log)
            print_success "Client $i (${client_ip}): $lines log lines"
        else
            print_error "Client $i (${client_ip}): No log file"
        fi
    done
}

# Cleanup
cleanup() {
    print_status "Cleaning up..."
    
    for i in $(seq 1 4); do
        if [ -f client_${i}.pid ]; then
            local pid=$(cat client_${i}.pid)
            kill $pid 2>/dev/null
            rm -f client_${i}.pid
        fi
        rm -f client_${i}_script.txt ssh_${i}.exp
    done
}

# Main function
main() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}  Multi-Client Test (No Internet)${NC}"
    echo -e "${BLUE}================================${NC}"
    
    print_status "Server: $SERVER_IP:$SERVER_PORT"
    print_status "Room: $ROOM_NAME (password: $ROOM_PASS)"
    print_status "Using: expect (if available)"
    
    if ! command -v expect >/dev/null 2>&1; then
        print_error "expect not found. Please install expect or use manual testing"
        exit 1
    fi
    
    local scenarios=("creator" "joiner" "joiner" "late_joiner")
    
    for i in $(seq 1 ${#CLIENT_IPS[@]}); do
        local client_ip=${CLIENT_IPS[$((i-1))]}
        local scenario=${scenarios[$((i-1))]}
        
        run_client_test $client_ip $i $scenario
        sleep 1
    done
    
    monitor_tests ${#CLIENT_IPS[@]}
    collect_results ${#CLIENT_IPS[@]}
    
    cleanup
    echo -e "\n${BLUE}Test completed!${NC}"
}

trap cleanup EXIT
main "$@"