# Chat Room Project Makefile
# Cross-platform compilation support

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -std=c99 -g

# Platform-specific settings
ifeq ($(OS),Windows_NT)
    # Windows settings
    LIBS = -lws2_32 -lpthread
    EXEC_EXT = .exe
    RM = del /Q
    MKDIR = mkdir
else
    # Unix/Linux settings
    LIBS = -lpthread
    EXEC_EXT =
    RM = rm -f
    MKDIR = mkdir -p
endif

# Directories
SERVER_DIR = server
CLIENT_DIR = client
COMMON_DIR = common
BUILD_DIR = build

# Source files
SERVER_SRC = $(SERVER_DIR)/server.c
CLIENT_SRC = $(CLIENT_DIR)/client.c

# Object files
SERVER_OBJ = $(BUILD_DIR)/server.o
CLIENT_OBJ = $(BUILD_DIR)/client.o

# Executables
SERVER_EXEC = $(BUILD_DIR)/server$(EXEC_EXT)
CLIENT_EXEC = $(BUILD_DIR)/client$(EXEC_EXT)

# Include directories
INCLUDES = -I$(COMMON_DIR)

# Default target
all: directories $(SERVER_EXEC) $(CLIENT_EXEC)

# Create build directory
directories:
	$(MKDIR) $(BUILD_DIR)

# Server executable
$(SERVER_EXEC): $(SERVER_OBJ)
	$(CC) $(SERVER_OBJ) -o $@ $(LIBS)

# Client executable
$(CLIENT_EXEC): $(CLIENT_OBJ)
	$(CC) $(CLIENT_OBJ) -o $@ $(LIBS)

# Server object file
$(SERVER_OBJ): $(SERVER_SRC) $(SERVER_DIR)/server.h $(COMMON_DIR)/protocol.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SERVER_SRC) -o $@

# Client object file
$(CLIENT_OBJ): $(CLIENT_SRC) $(COMMON_DIR)/protocol.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $(CLIENT_SRC) -o $@

# Server only
server: directories $(SERVER_EXEC)

# Client only
client: directories $(CLIENT_EXEC)

# Clean build files
clean:
ifeq ($(OS),Windows_NT)
	if exist $(BUILD_DIR) $(RM) $(BUILD_DIR)\*.o $(BUILD_DIR)\*.exe
else
	$(RM) $(BUILD_DIR)/*.o $(BUILD_DIR)/*$(EXEC_EXT)
endif

# Clean everything
distclean: clean
ifeq ($(OS),Windows_NT)
	if exist $(BUILD_DIR) rmdir /Q $(BUILD_DIR)
else
	rm -rf $(BUILD_DIR)
endif

# Test the server (run server in background)
test-server: $(SERVER_EXEC)
	$(SERVER_EXEC) 8080

# Test the client (connect to localhost)
test-client: $(CLIENT_EXEC)
	$(CLIENT_EXEC) 127.0.0.1 8080

# Help
help:
	@echo "Available targets:"
	@echo "  all         - Build both server and client"
	@echo "  server      - Build server only"
	@echo "  client      - Build client only"
	@echo "  clean       - Remove object files"
	@echo "  distclean   - Remove all build files"
	@echo "  test-server - Run server on port 8080"
	@echo "  test-client - Connect client to localhost:8080"
	@echo "  help        - Show this help message"

# Phony targets
.PHONY: all clean distclean server client test-server test-client help directories
