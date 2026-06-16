CXX := clang++
CC := clang
BUILD_DIR := build
NATIVE_DIR := ../../native-client
LZ4_DIR := ../../third_party/lz4

CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -pedantic
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS += -I$(NATIVE_DIR)/include -I$(NATIVE_DIR)/src -I$(LZ4_DIR)

OBJS := $(BUILD_DIR)/main.o $(BUILD_DIR)/protocol.o $(BUILD_DIR)/socket_compat.o $(BUILD_DIR)/lz4.o
BIN := $(BUILD_DIR)/arps-mock-source

.PHONY: all clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/main.o: main.cpp $(NATIVE_DIR)/src/socket_compat.h $(NATIVE_DIR)/include/arps/protocol.h $(NATIVE_DIR)/include/arps/socket.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/protocol.o: $(NATIVE_DIR)/src/protocol.cpp $(NATIVE_DIR)/include/arps/protocol.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/socket_compat.o: $(NATIVE_DIR)/src/socket_compat.cpp $(NATIVE_DIR)/src/socket_compat.h $(NATIVE_DIR)/include/arps/socket.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/lz4.o: $(LZ4_DIR)/lz4.c $(LZ4_DIR)/lz4.h | $(BUILD_DIR)
	$(CC) -I$(LZ4_DIR) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJS)
	$(CXX) $^ -o $@

clean:
	rm -rf $(BUILD_DIR)
