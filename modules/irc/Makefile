CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -lssl -lcrypto -pthread

ROOT ?= ../..
ABI_INCLUDE_DIR ?= $(ROOT)/include
INCLUDE_DIR := include
BUILD_DIR ?= build

MODULE_NAME := irc
TARGET := $(BUILD_DIR)/$(MODULE_NAME).so
TEST_TARGET := $(BUILD_DIR)/test_irc

PREFIX ?= /opt/sparkles
MODULEDIR ?= $(PREFIX)/modules/$(MODULE_NAME)
MODULE_USER ?= stnlabz
MODULE_GROUP ?= stnlabz

CPPFLAGS += -I$(INCLUDE_DIR) -I$(ABI_INCLUDE_DIR)
MODULE_CFLAGS := $(CFLAGS) -fPIC
MODULE_SRCS := src/irc_module.c src/irc_config.c src/irc_protocol.c
MODULE_OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(MODULE_SRCS))
TEST_SRCS := tests/test_irc.c src/irc_protocol.c

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
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lcrypto

test: $(TEST_TARGET)
	./$(TEST_TARGET)

install: $(TARGET)
	@id -u "$(MODULE_USER)" >/dev/null 2>&1 || { echo "ERROR: missing install user $(MODULE_USER)" >&2; exit 1; }
	@getent group "$(MODULE_GROUP)" >/dev/null 2>&1 || { echo "ERROR: missing install group $(MODULE_GROUP)" >&2; exit 1; }
	install -d -o "$(MODULE_USER)" -g "$(MODULE_GROUP)" -m 0755 "$(MODULEDIR)"
	install -o "$(MODULE_USER)" -g "$(MODULE_GROUP)" -m 0644 module.conf "$(MODULEDIR)/module.conf"
	@set -eu; \
	  staging=$$(mktemp "$(MODULEDIR)/.$(MODULE_NAME).so.XXXXXX"); \
	  trap 'rm -f "$$staging"' EXIT HUP INT TERM; \
	  install -o "$(MODULE_USER)" -g "$(MODULE_GROUP)" -m 0755 "$(TARGET)" "$$staging"; \
	  cmp -s "$(TARGET)" "$$staging" || { echo "ERROR: staged module differs from build" >&2; exit 1; }; \
	  mv -f "$$staging" "$(MODULEDIR)/$(MODULE_NAME).so"; \
	  trap - EXIT HUP INT TERM; \
	  cmp -s "$(TARGET)" "$(MODULEDIR)/$(MODULE_NAME).so" || { echo "ERROR: installed module differs from build" >&2; exit 1; }; \
	  test "$$(stat -c %U:%G "$(MODULEDIR)/$(MODULE_NAME).so")" = "$(MODULE_USER):$(MODULE_GROUP)" || { echo "ERROR: module ownership incorrect" >&2; exit 1; }; \
	  echo "Installed verified $(MODULE_NAME).so as $(MODULE_USER):$(MODULE_GROUP)"

uninstall:
	rm -f $(MODULEDIR)/$(MODULE_NAME).so $(MODULEDIR)/module.conf

clean:
	rm -rf $(BUILD_DIR)

-include $(MODULE_OBJS:.o=.d)
