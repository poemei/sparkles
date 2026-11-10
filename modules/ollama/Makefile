CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -lcurl

ROOT ?= ../..
ABI_INCLUDE_DIR ?= $(ROOT)/include
INCLUDE_DIR := include
BUILD_DIR ?= build

MODULE_NAME := ollama
TARGET := $(BUILD_DIR)/$(MODULE_NAME).so
TEST_TARGET := $(BUILD_DIR)/test_ollama
PREFIX ?= /opt/sparkles
INSTALL_USER ?= $(shell stat -c %U $(ROOT))
INSTALL_GROUP ?= $(shell stat -c %G $(ROOT))
MODULEDIR ?= $(PREFIX)/modules/$(MODULE_NAME)

CPPFLAGS += -I$(INCLUDE_DIR) -I$(ABI_INCLUDE_DIR)
MODULE_CFLAGS := $(CFLAGS) -fPIC
MODULE_SRCS := src/ollama_module.c src/ollama_protocol.c
MODULE_OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(MODULE_SRCS))
TEST_SRCS := tests/test_ollama.c src/ollama_protocol.c

.PHONY: all test clean install uninstall
all: $(TARGET)

$(TARGET): $(MODULE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) -shared $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(MODULE_CFLAGS) -MMD -MP -c $< -o $@

$(TEST_TARGET): $(TEST_SRCS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

test: $(TEST_TARGET)
	./$(TEST_TARGET)

install: $(TARGET)
	install -d -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 0755 $(MODULEDIR)
	install -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 0644 module.conf $(MODULEDIR)/module.conf
	@set -eu; \
	  stage=$(mktemp "$(MODULEDIR)/.ollama.so.XXXXXX"); \
	  trap 'rm -f "$stage"' EXIT HUP INT TERM; \
	  install -o "$(INSTALL_USER)" -g "$(INSTALL_GROUP)" -m 0755 "$(TARGET)" "$stage"; \
	  cmp -s "$(TARGET)" "$stage"; \
	  mv -f "$stage" "$(MODULEDIR)/ollama.so"; \
	  trap - EXIT HUP INT TERM; \
	  cmp -s "$(TARGET)" "$(MODULEDIR)/ollama.so"

uninstall:
	rm -f $(MODULEDIR)/$(MODULE_NAME).so $(MODULEDIR)/module.conf

clean:
	rm -rf $(BUILD_DIR)

-include $(MODULE_OBJS:.o=.d)
