# Pt - a small Wayland-native GUI framework.
# Copyright 2026 The Pt Authors.
# Licensed under the Apache License, Version 2.0.

CC ?= gcc
CXX ?= g++
PYTHON ?= python3
JAVA ?= java
JAVAC ?= javac

VERSION := 1.0.0
SONAME := libpt.so.1
SOURCE_DATE_EPOCH ?= $(shell git log -1 --format=%ct 2>/dev/null || echo 1767225600)
.DEFAULT_GOAL := all
BUILD := build
OBJ := $(BUILD)/obj
TEST_BIN := $(BUILD)/tests
EXAMPLES := $(BUILD)/examples

PKGS := wayland-client wayland-cursor xkbcommon fontconfig freetype2 harfbuzz zlib
PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS))
PKG_LIBS := $(shell pkg-config --libs $(PKGS)) -lm

DBUS_CFLAGS := $(shell pkg-config --cflags dbus-1 2>/dev/null)
DBUS_LIBS := $(shell pkg-config --libs dbus-1 2>/dev/null)
DBUS_PC := $(if $(strip $(DBUS_LIBS)),dbus-1)
PKG_CFLAGS += $(DBUS_CFLAGS)
PKG_LIBS += $(DBUS_LIBS)
ifneq ($(strip $(DBUS_LIBS)),)
BASE_DEFINES += -DPT_HAVE_DBUS=1
endif

WARN := -Wall -Wextra -Wpedantic -Wshadow -Wformat=2
BASE_CFLAGS := -std=c23 -D_GNU_SOURCE -fPIC -fvisibility=hidden $(BASE_DEFINES) \
	-Iinclude -Isrc -I$(BUILD)/protocols $(PKG_CFLAGS) $(WARN)
OPT ?= -O2 -g
CFLAGS ?=
ALL_CFLAGS := $(BASE_CFLAGS) $(OPT) $(CFLAGS)

ifdef werror
ALL_CFLAGS += -Werror
endif

ifdef sanitize
ALL_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
SAN_LIBS := -fsanitize=address,undefined
endif

CPP_FLAGS := -std=c++23 -Iinclude -Ibindings/cpp $(WARN) $(OPT) $(CFLAGS)
ifdef werror
CPP_FLAGS += -Werror
endif
ifdef sanitize
CPP_FLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
endif

PROTO_NAMES := xdg-shell xdg-decoration-unstable-v1 viewporter fractional-scale-v1 \
	text-input-unstable-v3 pointer-gestures-unstable-v1 tablet-unstable-v2
PROTO_HEADERS := $(addprefix $(BUILD)/protocols/,$(addsuffix -client-protocol.h,$(PROTO_NAMES)))
PROTO_SOURCES := $(addprefix $(BUILD)/protocols/,$(addsuffix -protocol.c,$(PROTO_NAMES)))
PROTO_OBJECTS := $(patsubst $(BUILD)/protocols/%.c,$(OBJ)/protocols/%.o,$(PROTO_SOURCES))

LIB_SOURCES := $(wildcard src/*.c) $(wildcard src/wayland/*.c)
LIB_OBJECTS := $(patsubst %.c,$(OBJ)/%.o,$(LIB_SOURCES)) $(PROTO_OBJECTS)
LIB_DEPS := $(LIB_OBJECTS:.o=.d)
$(LIB_OBJECTS): $(PROTO_HEADERS)

CORE_SOURCES := src/util.c src/canvas.c src/png.c src/font.c
CORE_OBJECTS := $(patsubst %.c,$(OBJ)/%.o,$(CORE_SOURCES))
CORE_TEST_BINS := $(TEST_BIN)/test_core $(TEST_BIN)/test_canvas $(TEST_BIN)/test_font
TEST_NAMES := test_core test_abi test_canvas test_font test_layout test_widget test_window \
	test_controls test_overlay test_a11y test_a11y_bridge test_dnd test_gestures \
	test_damage test_flex_grid test_text_area test_ime test_rich_text test_data_view test_motion \
	test_table test_tree test_navigation test_property test_theme test_editing test_visuals \
	test_combobox test_selection
TEST_BINS := $(addprefix $(TEST_BIN)/,$(TEST_NAMES))
TEST_OBJECTS := $(CORE_OBJECTS)
TEST_DEPS := $(CORE_OBJECTS:.o=.d)

C_EXAMPLES := $(patsubst examples/c/%.c,$(EXAMPLES)/c/%,$(wildcard examples/c/*.c))
CPP_EXAMPLES := $(patsubst examples/cpp/%.cpp,$(EXAMPLES)/cpp/%,$(wildcard examples/cpp/*.cpp))

.PHONY: all lib clean check check-core check-wayland check-typing check-bindings check-sanitize protocols \
	install install-check uninstall examples java-example java-test bench run-bench run-hello \
	run-gallery run-notes run-counter run-interaction run-data run-a11y perf-check \
	run-python run-java run-cpp-workspace run-python-workspace run-java-workspace \
	run-combobox run-cpp-combobox run-python-combobox run-java-combobox dist \
	api-check api-update api-docs

all: lib examples

lib: $(BUILD)/libpt.so $(BUILD)/libpt.a $(BUILD)/pt.pc

$(BUILD)/libpt.so: $(BUILD)/libpt.so.$(VERSION)
	ln -sfn libpt.so.$(VERSION) $@
	ln -sfn libpt.so.$(VERSION) $(BUILD)/$(SONAME)

$(BUILD)/libpt.so.$(VERSION): $(LIB_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) -shared -Wl,-soname,$(SONAME) -o $@ $(LIB_OBJECTS) $(PKG_LIBS) $(SAN_LIBS)

$(BUILD)/libpt.a: $(LIB_OBJECTS)
	@mkdir -p $(dir $@)
	ar rcs $@ $(LIB_OBJECTS)

$(BUILD)/pt.pc: Makefile
	@mkdir -p $(BUILD)
	@printf '%s\n' \
		'prefix=$${pcfiledir}/../..' \
		'exec_prefix=$${prefix}' \
		'libdir=$${exec_prefix}/lib' \
		'includedir=$${prefix}/include' \
		'' \
		'Name: Pt' \
		'Description: Lightweight Wayland-native GUI framework' \
		'Version: $(VERSION)' \
		'Requires.private: wayland-client wayland-cursor xkbcommon fontconfig freetype2 harfbuzz zlib $(DBUS_PC)' \
		'Libs: -L$${libdir} -lpt' \
		'Libs.private: -lm $(DBUS_LIBS)' \
		'Cflags: -I$${includedir}' > $@

$(OBJ)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ)/protocols/%.o: $(BUILD)/protocols/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BASE_CFLAGS) $(OPT) -w -c $< -o $@

$(BUILD)/protocols/%-client-protocol.h: protocols/%.xml
	@mkdir -p $(BUILD)/protocols
	wayland-scanner client-header $< $@

$(BUILD)/protocols/%-protocol.c: protocols/%.xml
	@mkdir -p $(BUILD)/protocols
	wayland-scanner private-code $< $@

protocols: $(PROTO_HEADERS) $(PROTO_SOURCES)

# The live typing test injects keys through a virtual keyboard. The protocol is
# test-only, so its generated code stays out of the library.
LIVE_TYPING_PROTO := $(BUILD)/protocols/virtual-keyboard-unstable-v1

$(LIVE_TYPING_PROTO)-client-protocol.h: tests/protocols/virtual-keyboard-unstable-v1.xml
	@mkdir -p $(BUILD)/protocols
	wayland-scanner client-header $< $@

$(LIVE_TYPING_PROTO)-protocol.c: tests/protocols/virtual-keyboard-unstable-v1.xml
	@mkdir -p $(BUILD)/protocols
	wayland-scanner private-code $< $@

$(TEST_BIN)/vkbd_type: tests/vkbd_type.c $(LIVE_TYPING_PROTO)-protocol.c \
		$(LIVE_TYPING_PROTO)-client-protocol.h
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $< $(LIVE_TYPING_PROTO)-protocol.c $(PKG_LIBS) $(SAN_LIBS) -o $@

check-typing: $(TEST_BIN)/typing_probe $(TEST_BIN)/vkbd_type
	BUILD=$(abspath $(BUILD)) tools/live-typing.sh

$(EXAMPLES)/c/%: examples/c/%.c $(BUILD)/libpt.a
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $< $(BUILD)/libpt.a $(PKG_LIBS) $(SAN_LIBS) -o $@

$(EXAMPLES)/cpp/%: examples/cpp/%.cpp bindings/cpp/pt/pt.hpp $(BUILD)/libpt.a
	@mkdir -p $(dir $@)
	$(CXX) $(CPP_FLAGS) $< $(BUILD)/libpt.a $(PKG_LIBS) $(SAN_LIBS) -o $@

examples: $(C_EXAMPLES) $(CPP_EXAMPLES)

$(TEST_BIN)/%: tests/%.c $(BUILD)/libpt.a
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -I tests $< $(BUILD)/libpt.a $(PKG_LIBS) $(SAN_LIBS) -o $@

check-core: $(CORE_TEST_BINS)
	@mkdir -p $(TEST_BIN)
	@for test in $(CORE_TEST_BINS); do \
		echo "== $$test"; \
		$$test || exit $$?; \
	done

api-check: lib
	$(PYTHON) tools/api-audit.py --library $(abspath $(BUILD)/libpt.so)
	$(PYTHON) tools/gen-api-reference.py --check

api-update: lib
	$(PYTHON) tools/api-audit.py --update --library $(abspath $(BUILD)/libpt.so)
	$(PYTHON) tools/gen-api-reference.py --write

api-docs:
	$(PYTHON) tools/gen-api-reference.py --write

check: $(TEST_BINS) check-bindings api-check
	@mkdir -p $(TEST_BIN)
	@for test in $(TEST_BINS); do \
		echo "== $$test"; \
		$$test || exit $$?; \
	done
	@echo "all checks passed"

# The live suite needs a private seat: a desktop the user is working in can move
# focus or the pointer at any point in a popup test. Prefer a fullscreen nested
# niri, which also keeps the nested pointer inside the test window.
check-wayland: $(TEST_BIN)/test_wayland
	@if [ -n "$${WAYLAND_DISPLAY:-}$${WAYLAND_SOCKET:-}" ] && command -v niri >/dev/null 2>&1; then \
		tools/nested-wayland.sh --fullscreen -- $(TEST_BIN)/test_wayland; \
	else \
		$(TEST_BIN)/test_wayland; \
	fi

check-sanitize: $(TEST_BINS) $(TEST_BIN)/test_cpp
	@mkdir -p $(TEST_BIN)
	@for test in $(TEST_BINS); do \
		echo "== $$test (sanitized)"; \
		ASAN_OPTIONS=detect_leaks=1 $$test || exit $$?; \
	done
	@echo "== $(TEST_BIN)/test_cpp (sanitized)"
	@ASAN_OPTIONS=detect_leaks=1 $(TEST_BIN)/test_cpp

$(TEST_BIN)/test_cpp: bindings/cpp/test_pt.cpp bindings/cpp/pt/pt.hpp $(BUILD)/libpt.a
	@mkdir -p $(dir $@)
	$(CXX) $(CPP_FLAGS) $< $(BUILD)/libpt.a $(PKG_LIBS) $(SAN_LIBS) -o $@

check-bindings: lib $(TEST_BIN)/test_cpp
	$(TEST_BIN)/test_cpp
	PT_HEADLESS=1 PT_LIBRARY=$(abspath $(BUILD)/libpt.so) PYTHONPATH=bindings/python \
		$(PYTHON) -m unittest discover -s bindings/python -p 'test_*.py'
	$(JAVAC) -Xlint:all -d $(BUILD)/java $(wildcard bindings/java/src/pt/*.java) \
		bindings/java/test/pt/PtTest.java
	PT_HEADLESS=1 $(JAVA) --enable-native-access=ALL-UNNAMED \
		-Dpt.library=$(abspath $(BUILD)/libpt.so) -cp $(BUILD)/java pt.PtTest

java-test: lib
	$(JAVAC) -Xlint:all -d $(BUILD)/java $(wildcard bindings/java/src/pt/*.java) \
		bindings/java/test/pt/PtTest.java
	PT_HEADLESS=1 $(JAVA) --enable-native-access=ALL-UNNAMED \
		-Dpt.library=$(abspath $(BUILD)/libpt.so) -cp $(BUILD)/java pt.PtTest

$(BUILD)/bench: bench/bench.c $(BUILD)/libpt.a
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $< $(BUILD)/libpt.a $(PKG_LIBS) $(SAN_LIBS) -o $@

$(BUILD)/bench-startup: bench/startup.c $(BUILD)/libpt.a
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $< $(BUILD)/libpt.a $(PKG_LIBS) $(SAN_LIBS) -o $@

$(BUILD)/bench-views: bench/views.c $(BUILD)/libpt.a
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $< $(BUILD)/libpt.a $(PKG_LIBS) $(SAN_LIBS) -o $@

$(BUILD)/bench-guards: bench/guards.c $(BUILD)/libpt.a
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $< $(BUILD)/libpt.a $(PKG_LIBS) $(SAN_LIBS) -o $@

bench: $(BUILD)/bench $(BUILD)/bench-startup $(BUILD)/bench-views $(BUILD)/bench-guards

run-bench: $(BUILD)/bench $(BUILD)/bench-startup $(BUILD)/bench-views
	PT_HEADLESS=1 $(BUILD)/bench
	PT_HEADLESS=1 $(BUILD)/bench-startup
	PT_HEADLESS=1 $(BUILD)/bench-views

perf-check: $(BUILD)/bench $(BUILD)/bench-startup $(BUILD)/bench-views $(BUILD)/bench-guards
	PT_HEADLESS=1 $(BUILD)/bench
	PT_HEADLESS=1 $(BUILD)/bench-startup
	PT_HEADLESS=1 $(BUILD)/bench-views
	PT_HEADLESS=1 $(BUILD)/bench-guards

java-example: lib
	$(JAVAC) -Xlint:all -d $(BUILD)/java $(wildcard bindings/java/src/pt/*.java) \
		$(wildcard examples/java/*.java)

run-hello: all
	$(EXAMPLES)/c/hello

run-gallery: all
	$(EXAMPLES)/c/gallery

run-notes: all
	$(EXAMPLES)/c/notes

run-interaction: all
	$(EXAMPLES)/c/interaction

run-data: all
	$(EXAMPLES)/c/data

run-a11y: all
	$(EXAMPLES)/c/accessibility

run-counter: all
	$(EXAMPLES)/cpp/counter

run-python: lib
	PT_LIBRARY=$(abspath $(BUILD)/libpt.so) PYTHONPATH=bindings/python \
		$(PYTHON) examples/python/counter.py

run-java: java-example
	$(JAVA) --enable-native-access=ALL-UNNAMED -Dpt.library=$(abspath $(BUILD)/libpt.so) \
		-cp $(BUILD)/java HelloPt

run-cpp-workspace: all
	$(EXAMPLES)/cpp/workspace

run-python-workspace: lib
	PT_LIBRARY=$(abspath $(BUILD)/libpt.so) PYTHONPATH=bindings/python \
		$(PYTHON) examples/python/workspace.py

run-java-workspace: java-example
	$(JAVA) --enable-native-access=ALL-UNNAMED -Dpt.library=$(abspath $(BUILD)/libpt.so) \
		-cp $(BUILD)/java Workspace

run-combobox: all
	$(EXAMPLES)/c/combobox

run-cpp-combobox: all
	$(EXAMPLES)/cpp/combobox

run-python-combobox: lib
	PT_LIBRARY=$(abspath $(BUILD)/libpt.so) PYTHONPATH=bindings/python \
		$(PYTHON) examples/python/combobox.py

run-java-combobox: java-example
	$(JAVA) --enable-native-access=ALL-UNNAMED -Dpt.library=$(abspath $(BUILD)/libpt.so) \
		-cp $(BUILD)/java Combobox

install: lib
	@test -n "$(PREFIX)" || (echo "set PREFIX, e.g. make install PREFIX=/usr/local"; exit 2)
	install -Dm755 $(BUILD)/libpt.so.$(VERSION) $(DESTDIR)$(PREFIX)/lib/libpt.so.$(VERSION)
	ln -sfn libpt.so.$(VERSION) $(DESTDIR)$(PREFIX)/lib/$(SONAME)
	ln -sfn libpt.so.$(VERSION) $(DESTDIR)$(PREFIX)/lib/libpt.so
	install -Dm644 $(BUILD)/libpt.a $(DESTDIR)$(PREFIX)/lib/libpt.a
	install -d $(DESTDIR)$(PREFIX)/include/pt
	install -m644 include/pt/*.h $(DESTDIR)$(PREFIX)/include/pt/
	install -Dm644 $(BUILD)/pt.pc $(DESTDIR)$(PREFIX)/lib/pkgconfig/pt.pc
	install -Dm644 bindings/cpp/pt/pt.hpp $(DESTDIR)$(PREFIX)/include/pt/pt.hpp
	install -Dm644 bindings/python/pt.py $(DESTDIR)$(PREFIX)/share/pt/bindings/python/pt.py
	install -Dm644 bindings/java/src/pt/Pt.java \
		$(DESTDIR)$(PREFIX)/share/pt/bindings/java/src/pt/Pt.java

uninstall:
	@test -n "$(PREFIX)" || (echo "set PREFIX"; exit 2)
	rm -f $(DESTDIR)$(PREFIX)/lib/libpt.so $(DESTDIR)$(PREFIX)/lib/libpt.so.$(VERSION) \
		$(DESTDIR)$(PREFIX)/lib/$(SONAME) $(DESTDIR)$(PREFIX)/lib/libpt.a \
		$(DESTDIR)$(PREFIX)/lib/pkgconfig/pt.pc
	rm -rf $(DESTDIR)$(PREFIX)/include/pt $(DESTDIR)$(PREFIX)/share/pt

install-check:
	tools/install-check.sh $(BUILD)

clean:
	rm -rf $(BUILD)

dist:
	@rm -rf $(BUILD)/dist/pt-$(VERSION) $(BUILD)/dist/pt-$(VERSION).tar.gz \
		$(BUILD)/dist/pt-$(VERSION).tar \
		$(BUILD)/dist/pt-$(VERSION).tar.gz.sha256
	@mkdir -p $(BUILD)/dist/pt-$(VERSION)
	@tar -cf - --exclude=./build --exclude=./.git --exclude=./.cache \
		--exclude='*/__pycache__' --exclude='*.pyc' . | \
		tar -xf - -C $(BUILD)/dist/pt-$(VERSION)
	@tar --sort=name --format=gnu --mtime=@$(SOURCE_DATE_EPOCH) \
		--owner=0 --group=0 --numeric-owner -cf $(BUILD)/dist/pt-$(VERSION).tar \
		-C $(BUILD)/dist pt-$(VERSION)
	@gzip -9n -f $(BUILD)/dist/pt-$(VERSION).tar
	@cd $(BUILD)/dist && sha256sum pt-$(VERSION).tar.gz > pt-$(VERSION).tar.gz.sha256
	@echo "wrote $(BUILD)/dist/pt-$(VERSION).tar.gz (SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH))"
	@echo "wrote $(BUILD)/dist/pt-$(VERSION).tar.gz.sha256"

-include $(LIB_DEPS) $(TEST_DEPS)
