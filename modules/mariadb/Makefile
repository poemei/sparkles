CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -pthread
LDFLAGS ?=
ROOT ?= ../..
BUILD ?= build
PREFIX ?= /opt/sparkles
MODULEDIR := $(PREFIX)/modules/mariadb
INSTALL_USER ?= $(shell stat -c %U $(ROOT))
INSTALL_GROUP ?= $(shell stat -c %G $(ROOT))
TARGET := $(BUILD)/mariadb.so
TEST := $(BUILD)/test_mariadb
CPPFLAGS += -Iinclude -I$(ROOT)/include $(shell pkg-config --cflags libmariadb)
LDLIBS := $(shell pkg-config --libs libmariadb) -pthread
OBJS := $(BUILD)/mariadb_module.o $(BUILD)/mariadb_config.o $(BUILD)/mariadb_dates.o $(BUILD)/mariadb_auth.o

.PHONY: all test install clean
all: $(TARGET)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -MMD -MP -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) -shared $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST): tests/test_mariadb.c src/mariadb_config.c src/mariadb_dates.c src/mariadb_auth.c
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

test: $(TEST)
	./$(TEST)

install: $(TARGET)
	@id -u "$(INSTALL_USER)" >/dev/null 2>&1 || { echo "ERROR: missing user $(INSTALL_USER)"; exit 1; }
	@getent group "$(INSTALL_GROUP)" >/dev/null 2>&1 || { echo "ERROR: missing group $(INSTALL_GROUP)"; exit 1; }
	install -d -o "$(INSTALL_USER)" -g "$(INSTALL_GROUP)" -m 0755 "$(MODULEDIR)"
	install -o "$(INSTALL_USER)" -g "$(INSTALL_GROUP)" -m 0644 module.conf "$(MODULEDIR)/module.conf"
	@set -eu; \
	  stage=$$(mktemp "$(MODULEDIR)/.mariadb.so.XXXXXX"); \
	  trap 'rm -f "$$stage"' EXIT HUP INT TERM; \
	  install -o "$(INSTALL_USER)" -g "$(INSTALL_GROUP)" -m 0755 "$(TARGET)" "$$stage"; \
	  cmp -s "$(TARGET)" "$$stage"; \
	  mv -f "$$stage" "$(MODULEDIR)/mariadb.so"; \
	  trap - EXIT HUP INT TERM

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
