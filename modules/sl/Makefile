CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -lcrypto -pthread
ROOT ?= ../..
BUILD_DIR ?= build
PREFIX ?= /opt/sparkles
MODULEDIR ?= $(PREFIX)/modules/sl
MODULE_USER ?= stnchain
MODULE_GROUP ?= stnchain
CPPFLAGS += -I$(ROOT)/include
TARGET := $(BUILD_DIR)/sl.so

.PHONY: all clean install uninstall
all: $(TARGET)
$(TARGET): src/sl_module.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $(LDFLAGS) -o $@ $< $(LDLIBS)
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
