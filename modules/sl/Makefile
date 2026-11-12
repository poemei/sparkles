CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -lcrypto -pthread -lcurl $(shell pkg-config --libs libxml-2.0)
ROOT ?= ../..
BUILD_DIR ?= build
PREFIX ?= /opt/sparkles
MODULEDIR ?= $(PREFIX)/modules/sl
MODULE_USER ?= stnchain
MODULE_GROUP ?= stnchain
CPPFLAGS += -I$(ROOT)/include -Iinclude $(shell pkg-config --cflags libxml-2.0)
TARGET := $(BUILD_DIR)/sl.so
WIRE_TEST := $(BUILD_DIR)/test_sl_wire
SOURCES := src/sl_module.c src/sl_wire.c src/sl_config.c src/sl_login.c

.PHONY: all test clean install uninstall
all: $(TARGET)
$(TARGET): $(SOURCES) include/sl_wire.h include/sl_config.h include/sl_login.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)
$(WIRE_TEST): tests/test_sl_wire.c src/sl_wire.c include/sl_wire.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_sl_wire.c src/sl_wire.c
test: $(WIRE_TEST)
	./$(WIRE_TEST)
install: $(TARGET)
	@id -u "$(MODULE_USER)" >/dev/null && getent group "$(MODULE_GROUP)" >/dev/null
	install -d -o "$(MODULE_USER)" -g "$(MODULE_GROUP)" -m 0755 "$(MODULEDIR)"
	install -o "$(MODULE_USER)" -g "$(MODULE_GROUP)" -m 0644 module.conf "$(MODULEDIR)/module.conf"
	@set -eu; staging=$$(mktemp "$(MODULEDIR)/.sl.so.XXXXXX"); \
	 trap 'rm -f "$$staging"' EXIT HUP INT TERM; \
	 install -o "$(MODULE_USER)" -g "$(MODULE_GROUP)" -m 0755 "$(TARGET)" "$$staging"; \
	 cmp -s "$(TARGET)" "$$staging"; mv -f "$$staging" "$(MODULEDIR)/sl.so"; \
	 trap - EXIT HUP INT TERM; cmp -s "$(TARGET)" "$(MODULEDIR)/sl.so"
uninstall:
	rm -f "$(MODULEDIR)/sl.so" "$(MODULEDIR)/module.conf"
clean:
	rm -rf "$(BUILD_DIR)"
