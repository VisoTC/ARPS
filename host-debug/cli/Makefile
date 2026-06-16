CXX := clang++
CC := clang
BUILD_DIR := build
NATIVE_DIR := ../../native-client
LZ4_DIR := ../../third_party/lz4
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -pedantic
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS += -I$(NATIVE_DIR)/include -I$(LZ4_DIR) $(SDL_CFLAGS)

OBJS := \
	$(BUILD_DIR)/main.o \
	$(BUILD_DIR)/receiver.o \
	$(BUILD_DIR)/protocol.o \
	$(BUILD_DIR)/socket_compat.o \
	$(BUILD_DIR)/lz4.o

BIN := $(BUILD_DIR)/arps-host-debug
DEVICE_APK := ../../device/build/outputs/apk/debug/arps-device.apk

.PHONY: all clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/main.o: main.cpp $(NATIVE_DIR)/include/arps/receiver.h $(NATIVE_DIR)/include/arps/frame.h $(NATIVE_DIR)/include/arps/protocol.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/receiver.o: $(NATIVE_DIR)/src/receiver.cpp $(NATIVE_DIR)/src/socket_compat.h $(NATIVE_DIR)/include/arps/receiver.h $(NATIVE_DIR)/include/arps/frame.h $(NATIVE_DIR)/include/arps/protocol.h $(NATIVE_DIR)/include/arps/socket.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/protocol.o: $(NATIVE_DIR)/src/protocol.cpp $(NATIVE_DIR)/include/arps/protocol.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/socket_compat.o: $(NATIVE_DIR)/src/socket_compat.cpp $(NATIVE_DIR)/src/socket_compat.h $(NATIVE_DIR)/include/arps/socket.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/lz4.o: $(LZ4_DIR)/lz4.c $(LZ4_DIR)/lz4.h | $(BUILD_DIR)
	$(CC) -I$(LZ4_DIR) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJS)
	$(CXX) $^ $(SDL_LIBS) -o $@
	@if [ -f "$(DEVICE_APK)" ]; then cp "$(DEVICE_APK)" "$(BUILD_DIR)/arps-device.apk"; fi

clean:
	rm -rf $(BUILD_DIR)
