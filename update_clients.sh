#!/bin/bash

# Client update script for lab environment
# Sends only necessary files to client machines

# Configuration
CLIENT_IPS=("192.7.1.1" "192.7.2.1" "192.7.3.1" "192.7.4.1")
SOURCE_DIR="./send_to_clients"
TARGET_DIR="/root/chatroom"
USERNAME="root"

# Colors for output
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

# Create send_to_clients directory with latest files
prepare_client_files() {
    print_status "Preparing client files..."
    
    # Remove old send_to_clients directory
    rm -rf "$SOURCE_DIR"
    
    # Create fresh directory
    mkdir -p "$SOURCE_DIR"
    
    # Copy necessary files
    cp build/client "$SOURCE_DIR/" 2>/dev/null || {
        print_error "Client executable not found. Run 'make' first."
        return 1
    }
    
    cp common/protocol.h "$SOURCE_DIR/" 2>/dev/null || {
        print_error "protocol.h not found"
        return 1
    }
    
    cp client/client.h "$SOURCE_DIR/" 2>/dev/null || {
        print_error "client.h not found"
        return 1
    }
    
    # Optional: copy client.o if it exists
    cp build/client.o "$SOURCE_DIR/" 2>/dev/null
    
    print_success "Client files prepared in $SOURCE_DIR"
    ls -la "$SOURCE_DIR"
    return 0
}

# Update a single client
update_client() {
    local client_ip=$1
    print_status "Updating client $client_ip..."
    
    # Delete old directory on client
    ssh -o ConnectTimeout=5 "$USERNAME@$client_ip" "rm -rf $TARGET_DIR" 2>/dev/null
    if [ $? -eq 0 ]; then
        print_status "Removed old directory on $client_ip"
    else
        print_error "Could not connect to $client_ip or remove old directory"
        return 1
    fi
    
    # Create new directory
    ssh "$USERNAME@$client_ip" "mkdir -p $TARGET_DIR" 2>/dev/null
    if [ $? -ne 0 ]; then
        print_error "Could not create directory on $client_ip"
        return 1
    fi
    
    # Copy new files
    scp -r "$SOURCE_DIR"/* "$USERNAME@$client_ip:$TARGET_DIR/" 2>/dev/null
    if [ $? -eq 0 ]; then
        print_success "Files copied to $client_ip"
        
        # Make client executable
        ssh "$USERNAME@$client_ip" "chmod +x $TARGET_DIR/client" 2>/dev/null
        
        # Verify files
        ssh "$USERNAME@$client_ip" "ls -la $TARGET_DIR/" 2>/dev/null
        
        return 0
    else
        print_error "Failed to copy files to $client_ip"
        return 1
    fi
}

# Main function
main() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}  Client Update Script          ${NC}"
    echo -e "${BLUE}================================${NC}"
    
    # Prepare files
    if ! prepare_client_files; then
        print_error "Failed to prepare client files"
        exit 1
    fi
    
    # Update each client
    success_count=0
    for client_ip in "${CLIENT_IPS[@]}"; do
        echo ""
        if update_client "$client_ip"; then
            ((success_count++))
        fi
    done
    
    echo ""
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}  Update Summary                ${NC}"
    echo -e "${BLUE}================================${NC}"
    print_status "Successfully updated: $success_count/${#CLIENT_IPS[@]} clients"
    
    if [ $success_count -eq ${#CLIENT_IPS[@]} ]; then
        print_success "All clients updated successfully!"
        exit 0
    else
        print_error "Some clients failed to update"
        exit 1
    fi
}

# Run main function
main "$@"
