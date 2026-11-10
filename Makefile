# STN-LABZ Sparkles executable (Linux)
# The shared STN-LABZ ABI is maintained outside this repository.
#
# Default layout:
#   ../ABI/includes/*.h
#   ../ABI/src/*.c
# Follow Digit's Core ABI source selection; exclude standalone ABI inventory/catalog.
# Override ABI_DIR / ABI_INCLUDE_DIR / ABI_SRC_DIR for a different layout.

CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -ldl -lcrypto

ABI_DIR ?= ../ABI
ABI_INCLUDE_DIR ?= $(ABI_DIR)/includes
ABI_SRC_DIR ?= $(ABI_DIR)/src

CPPFLAGS += -Iinclude -I$(ABI_INCLUDE_DIR) -Iinclude/stnlabz_abi

BUILD_DIR ?= build
TARGET ?= bin/sparkles

PREFIX ?= /opt/sparkles
INSTALL_USER ?= $(shell stat -c %U .)
INSTALL_GROUP ?= $(shell stat -c %G .)
RUNTIME_USER ?= $(INSTALL_USER)
RUNTIME_GROUP ?= $(INSTALL_GROUP)
BINDIR ?= $(PREFIX)/bin
LOGDIR ?= $(PREFIX)/logs
MODULEDIR ?= $(PREFIX)/modules
CONFIGDIR ?= $(PREFIX)/config
STATEDIR ?= $(PREFIX)/state

CORE_SRCS := $(sort $(wildcard src/*.c))
ABI_SRCS := $(addprefix $(ABI_SRC_DIR)/,abi.c module.c module_registry.c loader_linux.c module_discovery_linux.c)
CORE_OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(CORE_SRCS))
ABI_OBJS := $(patsubst $(ABI_SRC_DIR)/%.c,$(BUILD_DIR)/abi/%.o,$(ABI_SRCS))
OBJS := $(CORE_OBJS) $(ABI_OBJS)
DEPS := $(OBJS:.o=.d)

.PHONY: all clean install uninstall
all: $(TARGET)

.PHONY: check-abi
check-abi:
	@test -f "$(ABI_SRC_DIR)/abi.c" || { \
		echo "ERROR: Shared ABI C sources not found in $(ABI_SRC_DIR)" >&2; \
		echo "Set ABI_DIR or ABI_SRC_DIR to the actual external ABI location." >&2; \
		exit 1; \
	}

$(TARGET): $(OBJS) | check-abi
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/abi/%.o: $(ABI_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

install: $(TARGET)
	@id -u "$(RUNTIME_USER)" >/dev/null 2>&1 || { echo "ERROR: unknown runtime user $(RUNTIME_USER)" >&2; exit 1; }
	@getent group "$(RUNTIME_GROUP)" >/dev/null 2>&1 || { echo "ERROR: unknown runtime group $(RUNTIME_GROUP)" >&2; exit 1; }
	install -d -m 0755 "$(PREFIX)" "$(BINDIR)" "$(MODULEDIR)" "$(CONFIGDIR)"
	install -d -o "$(RUNTIME_USER)" -g "$(RUNTIME_GROUP)" -m 0750 "$(LOGDIR)" "$(STATEDIR)"
	@touch "$(LOGDIR)/sparkles.log" "$(LOGDIR)/modules.log"
	chown "$(RUNTIME_USER):$(RUNTIME_GROUP)" "$(LOGDIR)/sparkles.log" "$(LOGDIR)/modules.log"
	chmod 0640 "$(LOGDIR)/sparkles.log" "$(LOGDIR)/modules.log"
	install -m 0755 "$(TARGET)" "$(BINDIR)/sparkles"

uninstall:
	rm -f $(BINDIR)/sparkles

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEPS)
