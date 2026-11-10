CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
ROOT ?= ../..
CPPFLAGS += -Iinclude -I$(ROOT)/include
BUILD ?= build
TARGET := $(BUILD)/identity.so
TEST := $(BUILD)/test_identity
PREFIX ?= /opt/rictus
MODULEDIR := $(PREFIX)/modules/identity
MODULE_USER ?= rictus
MODULE_GROUP ?= rictus
OBJS := $(BUILD)/identity_model.o $(BUILD)/identity_module.o
.PHONY: all test clean install
all: $(TARGET)
$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -MMD -MP -c $< -o $@
$(TARGET): $(OBJS)
	$(CC) -shared $(LDFLAGS) -o $@ $^
$(TEST): tests/test_identity.c src/identity_model.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^
test: $(TEST)
	./$(TEST)
install: $(TARGET)
	@id -u "$(MODULE_USER)" >/dev/null 2>&1 || { echo "ERROR: missing user $(MODULE_USER)"; exit 1; }
	@getent group "$(MODULE_GROUP)" >/dev/null 2>&1 || { echo "ERROR: missing group $(MODULE_GROUP)"; exit 1; }
	install -d -o "$(MODULE_USER)" -g "$(MODULE_GROUP)" -m 0755 "$(MODULEDIR)"
	install -o "$(MODULE_USER)" -g "$(MODULE_GROUP)" -m 0644 data/identity.conf "$(MODULEDIR)/identity.conf"
	@set -eu; \
	  stage=$$(mktemp "$(MODULEDIR)/.identity.so.XXXXXX"); \
	  trap 'rm -f "$$stage"' EXIT HUP INT TERM; \
	  install -o "$(MODULE_USER)" -g "$(MODULE_GROUP)" -m 0755 "$(TARGET)" "$$stage"; \
	  cmp -s "$(TARGET)" "$$stage"; \
	  mv -f "$$stage" "$(MODULEDIR)/identity.so"; \
	  trap - EXIT HUP INT TERM; \
	  cmp -s "$(TARGET)" "$(MODULEDIR)/identity.so"; \
	  test "$$(stat -c %U:%G "$(MODULEDIR)/identity.so")" = "$(MODULE_USER):$(MODULE_GROUP)"
clean:
	rm -rf $(BUILD)
-include $(OBJS:.o=.d)
