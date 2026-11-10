CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -lcurl -pthread

ROOT ?= ../..
BUILD_DIR ?= build
PREFIX ?= /opt/rictus
INSTALL_USER ?= rictus
INSTALL_GROUP ?= rictus
MODULEDIR ?= $(PREFIX)/modules/web

CPPFLAGS += -I$(ROOT)/include
TARGET := $(BUILD_DIR)/web.so
TEST_TARGET := $(BUILD_DIR)/test_web

.PHONY: all test install uninstall clean
all: $(TARGET)

$(TARGET): src/web_module.c $(ROOT)/include/rictus_route.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $(LDFLAGS) -o $@ src/web_module.c $(LDLIBS)

$(TEST_TARGET): tests/test_web.c src/web_module.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_web.c src/web_module.c $(LDLIBS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

install: $(TARGET)
	install -d -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 0755 $(MODULEDIR)
	install -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 0644 module.conf $(MODULEDIR)/module.conf
	install -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 0755 $(TARGET) $(MODULEDIR)/web.so

uninstall:
	rm -f $(MODULEDIR)/web.so $(MODULEDIR)/module.conf

clean:
	rm -rf $(BUILD_DIR)
