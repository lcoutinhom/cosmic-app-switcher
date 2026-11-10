CC ?= cc
CFLAGS ?= -O2
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
WARNINGS = -Wall -Wextra -Wpedantic -Werror
BUILD_DIR = build
WAYLAND_CFLAGS = $(shell pkg-config --cflags wayland-client)
WAYLAND_LIBS = $(shell pkg-config --libs wayland-client wayland-cursor)
PROTOCOL_NAMES = \
	ext-foreign-toplevel-list-v1 \
	ext-workspace-v1 \
	cosmic-toplevel-info-unstable-v1 \
	cosmic-toplevel-management-unstable-v1 \
	cosmic-workspace-unstable-v1 \
	wlr-layer-shell-unstable-v1 \
	xdg-shell \
	keyboard-shortcuts-inhibit-unstable-v1
PROTOCOL_CODE = $(addprefix $(BUILD_DIR)/,$(addsuffix -protocol.c,$(PROTOCOL_NAMES)))

.PHONY: all unit-test integration-test test switcher-test clean

all: $(BUILD_DIR)/cosmic-cycle-app

unit-test: $(BUILD_DIR)/test-selection
	$(BUILD_DIR)/test-selection

integration-test: all
	bash tests/test_install.sh

test: unit-test integration-test switcher-test

$(BUILD_DIR)/wlr-layer-shell-unstable-v1-server-protocol.h: vendor/wlr-layer-shell-unstable-v1.xml | $(BUILD_DIR)
	wayland-scanner server-header $< $@

$(BUILD_DIR)/test-switcher: tests/test_switcher.c src/switcher.c src/switcher.h \
		$(BUILD_DIR)/wlr-layer-shell-unstable-v1-server-protocol.h \
		$(addprefix $(BUILD_DIR)/,$(addsuffix -client-protocol.h,$(PROTOCOL_NAMES))) $(PROTOCOL_CODE)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -Isrc -I$(BUILD_DIR) \
		tests/test_switcher.c src/switcher.c $(PROTOCOL_CODE) \
		$(WAYLAND_LIBS) -lwayland-server -o $@

$(BUILD_DIR)/test-switcher-state: tests/test_switcher_state.c src/switcher.c src/switcher.h \
		$(addprefix $(BUILD_DIR)/,$(addsuffix -client-protocol.h,$(PROTOCOL_NAMES))) $(PROTOCOL_CODE)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -Isrc -I$(BUILD_DIR) \
		tests/test_switcher_state.c $(PROTOCOL_CODE) $(WAYLAND_LIBS) -o $@

switcher-test: $(BUILD_DIR)/test-switcher $(BUILD_DIR)/test-switcher-state
	COSMIC_CYCLE_RENDERER="$(CURDIR)/scripts/render_switcher.py" $(BUILD_DIR)/test-switcher-state
	COSMIC_CYCLE_RENDERER="$(CURDIR)/scripts/render_switcher.py" $(BUILD_DIR)/test-switcher || test $$? -eq 77

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/test-selection: tests/test_selection.c src/selection.c \
		src/selection.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -Isrc \
		tests/test_selection.c src/selection.c -o $@

$(BUILD_DIR)/ext-foreign-toplevel-list-v1.xml: \
		vendor/wayland-protocols/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml \
		| $(BUILD_DIR)
	cp $< $@

$(BUILD_DIR)/ext-workspace-v1.xml: \
		vendor/wayland-protocols/staging/ext-workspace/ext-workspace-v1.xml \
		| $(BUILD_DIR)
	cp $< $@

$(BUILD_DIR)/cosmic-%.xml: vendor/cosmic-protocols/unstable/cosmic-%.xml \
		| $(BUILD_DIR)
	cp $< $@

$(BUILD_DIR)/wlr-layer-shell-unstable-v1.xml: vendor/wlr-layer-shell-unstable-v1.xml | $(BUILD_DIR)
	cp $< $@

$(BUILD_DIR)/xdg-shell.xml: vendor/xdg-shell.xml | $(BUILD_DIR)
	cp $< $@

$(BUILD_DIR)/keyboard-shortcuts-inhibit-unstable-v1.xml: vendor/keyboard-shortcuts-inhibit-unstable-v1.xml | $(BUILD_DIR)
	cp $< $@

$(BUILD_DIR)/%-client-protocol.h: $(BUILD_DIR)/%.xml
	wayland-scanner client-header $< $@

$(BUILD_DIR)/%-protocol.c: $(BUILD_DIR)/%.xml
	wayland-scanner private-code $< $@

$(BUILD_DIR)/cosmic-cycle-app: src/main.c src/selection.c src/selection.h src/switcher.c src/switcher.h \
		$(addprefix $(BUILD_DIR)/,$(addsuffix -client-protocol.h,$(PROTOCOL_NAMES))) \
		$(PROTOCOL_CODE)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(WAYLAND_CFLAGS) \
		-Isrc -I$(BUILD_DIR) src/main.c src/selection.c src/switcher.c \
		$(PROTOCOL_CODE) $(WAYLAND_LIBS) -o $@

clean:
	$(RM) -r -- $(BUILD_DIR)
