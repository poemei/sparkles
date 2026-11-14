CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -lcrypto -pthread -lcurl $(shell pkg-config --libs libxml-2.0 2>/dev/null)
ROOT ?= ../..
BUILD_DIR ?= build
PREFIX ?= /opt/sparkles
MODULEDIR ?= $(PREFIX)/modules/sl
MODULE_USER ?= stnchain
MODULE_GROUP ?= stnchain
CPPFLAGS += -I$(ROOT)/include -Iinclude $(shell pkg-config --cflags libxml-2.0 2>/dev/null)

.PHONY: check-deps
check-deps:
	@command -v pkg-config >/dev/null 2>&1 || { echo 'ERROR: pkg-config is required (install pkg-config)' >&2; exit 1; }
	@pkg-config --exists libxml-2.0 || { echo 'ERROR: missing libxml2 development files (Ubuntu/Debian: sudo apt install libxml2-dev)' >&2; exit 1; }
	@pkg-config --exists libcurl || { echo 'ERROR: missing libcurl development files (Ubuntu/Debian: sudo apt install libcurl4-openssl-dev)' >&2; exit 1; }
TARGET := $(BUILD_DIR)/sl.so
WIRE_TEST := $(BUILD_DIR)/test_sl_wire
CONFIG_TEST := $(BUILD_DIR)/test_sl_config
IM_TEST := $(BUILD_DIR)/test_sl_im
CAPS_TEST := $(BUILD_DIR)/test_sl_caps
SOURCES := src/sl_module.c src/sl_wire.c src/sl_config.c src/sl_login.c src/sl_grid.c src/sl_im.c src/sl_caps.c

.PHONY: all test clean install uninstall
all: $(TARGET)
$(TARGET): $(SOURCES) include/sl_wire.h include/sl_config.h include/sl_login.h include/sl_grid.h include/sl_im.h include/sl_caps.h $(ROOT)/include/sparkles_llm.h $(ROOT)/include/sparkles_sl_group.h $(ROOT)/include/sparkles_sl_group_db.h $(ROOT)/include/sparkles_show.h | check-deps
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)
$(WIRE_TEST): tests/test_sl_wire.c src/sl_wire.c include/sl_wire.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_sl_wire.c src/sl_wire.c
$(CONFIG_TEST): tests/test_sl_config.c src/sl_config.c src/sl_wire.c include/sl_config.h include/sl_login.h include/sl_wire.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_sl_config.c src/sl_config.c src/sl_wire.c -lcrypto
$(IM_TEST): tests/test_sl_im.c src/sl_im.c src/sl_wire.c include/sl_im.h include/sl_wire.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_sl_im.c src/sl_im.c src/sl_wire.c
$(CAPS_TEST): tests/test_sl_caps.c src/sl_caps.c src/sl_wire.c include/sl_caps.h include/sl_grid.h include/sl_im.h | check-deps
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_sl_caps.c src/sl_wire.c -lcurl -pthread $(shell pkg-config --libs libxml-2.0)
test: $(WIRE_TEST) $(CONFIG_TEST) $(IM_TEST) $(CAPS_TEST)
	./$(WIRE_TEST)
	./$(CONFIG_TEST)
	./$(IM_TEST)
	./$(CAPS_TEST)
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
