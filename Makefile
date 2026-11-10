# STN-LABZ Rictus executable (Linux)
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
TARGET ?= bin/rictus

PREFIX ?= /opt/rictus
INSTALL_USER ?= rictus
INSTALL_GROUP ?= rictus
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
	install -d -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 0755 $(BINDIR) $(MODULEDIR) $(CONFIGDIR)
	install -d -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 0750 $(LOGDIR) $(STATEDIR)
	install -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 0755 $(TARGET) $(BINDIR)/rictus

uninstall:
	rm -f $(BINDIR)/rictus

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEPS)
