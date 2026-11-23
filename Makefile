CC ?= gcc
PREFIX ?= $(HOME)/.local

TARGET := embroidery-preview
BUILD_DIR := build
ASAN_TARGET := $(BUILD_DIR)/embroidery-preview-asan
TEST_BIN := $(BUILD_DIR)/tests

COMMON_CFLAGS := -std=c23 -Wall -Wextra -Wpedantic -Werror
RELEASE_CFLAGS := $(COMMON_CFLAGS) -O3
SANITIZER_CFLAGS := $(COMMON_CFLAGS) -O1 -g -fsanitize=address,undefined

.PHONY: all test install uninstall clean

all: $(TARGET)

$(TARGET): src/embroidery-preview.c
	$(CC) $(RELEASE_CFLAGS) src/embroidery-preview.c -o $(TARGET) -lz

$(ASAN_TARGET): src/embroidery-preview.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(SANITIZER_CFLAGS) src/embroidery-preview.c -o $(ASAN_TARGET) -lz

$(TEST_BIN): tests/tests.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(SANITIZER_CFLAGS) tests/tests.c -o $(TEST_BIN)

test: $(ASAN_TARGET) $(TEST_BIN)
	./$(TEST_BIN) ./$(ASAN_TARGET)

install: all
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)

clean:
	rm -rf $(TARGET) $(BUILD_DIR)
