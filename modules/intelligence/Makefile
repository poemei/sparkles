CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -lcurl -pthread

ROOT ?= ../..
ABI_INCLUDE_DIR ?= $(ROOT)/include
INCLUDE_DIR := include
BUILD_DIR ?= build

MODULE_NAME := intelligence
TARGET := $(BUILD_DIR)/$(MODULE_NAME).so
TEST_TARGET := $(BUILD_DIR)/test_intelligence

PREFIX ?= /opt/rictus
INSTALL_USER ?= rictus
INSTALL_GROUP ?= rictus
MODULEDIR ?= $(PREFIX)/modules/$(MODULE_NAME)
STATEDIR ?= $(PREFIX)/state
INTELLOGDIR ?= /var/log/rictus

CPPFLAGS += -I$(INCLUDE_DIR) -I$(ABI_INCLUDE_DIR)
MODULE_CFLAGS := $(CFLAGS) -fPIC
MODULE_SRCS := src/intelligence_module.c src/intelligence_parser.c
MODULE_OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(MODULE_SRCS))
TEST_SRCS := tests/test_intelligence.c src/intelligence_parser.c

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
	install -d -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 0750 $(STATEDIR) $(INTELLOGDIR)
	install -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 0644 module.conf $(MODULEDIR)/module.conf
	install -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 0755 $(TARGET) $(MODULEDIR)/$(MODULE_NAME).so

uninstall:
	rm -f $(MODULEDIR)/$(MODULE_NAME).so $(MODULEDIR)/module.conf

clean:
	rm -rf $(BUILD_DIR)

-include $(MODULE_OBJS:.o=.d)
