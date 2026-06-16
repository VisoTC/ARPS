CXX := clang++
CC := clang
BUILD_DIR := build
LZ4_DIR := ../third_party/lz4

CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -pedantic
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS += -Iinclude -I$(LZ4_DIR)

LIB_OBJS := $(BUILD_DIR)/receiver.o $(BUILD_DIR)/protocol.o $(BUILD_DIR)/socket_compat.o $(BUILD_DIR)/lz4.o
TEST_BIN := $(BUILD_DIR)/receiver_tests

.PHONY: all test clean

all: $(TEST_BIN)

test: $(TEST_BIN)
	$(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/receiver.o: src/receiver.cpp src/socket_compat.h include/arps/receiver.h include/arps/frame.h include/arps/protocol.h include/arps/socket.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/protocol.o: src/protocol.cpp include/arps/protocol.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/socket_compat.o: src/socket_compat.cpp src/socket_compat.h include/arps/socket.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/lz4.o: $(LZ4_DIR)/lz4.c $(LZ4_DIR)/lz4.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/receiver_tests.o: tests/receiver_tests.cpp src/socket_compat.h include/arps/receiver.h include/arps/frame.h include/arps/protocol.h include/arps/socket.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(TEST_BIN): $(BUILD_DIR)/receiver_tests.o $(LIB_OBJS)
	$(CXX) $^ -o $@

clean:
	rm -rf $(BUILD_DIR)
