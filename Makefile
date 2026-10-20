# Bronto Engine - Makefile (GNU make; macOS / Linux / MSYS2 / MinGW).
# On Windows with MSVC use build.bat instead.
#
#   make            build the static library (build/libbronto.a)
#   make test       build and run the test suite
#   make snake      build and play the Snake sample
#   make towerdefense  build and play the Pop Defense sample
#   make editor     build and run the scene editor (tools/editor)
#   make tools      build the editor, bscheck (checks .bs scripts) and bronto_player (runs games)
#   make vscode     package the VS Code extension (tools/vscode) into build/*.vsix (needs python3)
#   make examples   build the examples (build/examples/*)
#   make run-examples  run every example headless in build/run (CI smoke test)
#   make sanitize   run the tests under AddressSanitizer + UBSan
#   make x11-test / audio-test / evdev-test   Linux backend tests
#   make clean

CC      ?= cc
AR      ?= ar
BUILD   ?= build
CSTD    := -std=c99
WARN    := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
           -Wconversion -Wno-sign-conversion
OPT     ?= -O2 -g
# -ffp-contract=off: no fused multiply-add, so float results (and therefore
# replays / lockstep simulations) match across x86-64 and ARM.
CFLAGS  += $(CSTD) $(WARN) $(OPT) -ffp-contract=off -Iinclude
LDLIBS  += -lm

# Platform detection: system libraries for the native window backend.
#   Linux: libX11 is loaded at run time (dlopen) - only -ldl is needed (and
#   -pthread: the output capture's reader thread).
#   macOS: system frameworks.  Windows (MinGW): user32 + gdi32.
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
#   Audio: ALSA is dlopen'ed too; Core Audio / waveOut are system libraries.
ifeq ($(UNAME_S),Darwin)
  PLATFORM := macos
  LDLIBS   += -framework Cocoa -framework QuartzCore -framework AudioToolbox -framework GameController
  TEST_LDLIBS := -pthread
else ifeq ($(UNAME_S),Linux)
  PLATFORM := linux
  LDLIBS   += -ldl -pthread
  TEST_LDLIBS := -pthread
else ifneq (,$(findstring BSD,$(UNAME_S)))
  PLATFORM := bsd
  LDLIBS   += -pthread
  TEST_LDLIBS := -pthread
else
  PLATFORM := windows
  LDLIBS   += -luser32 -lgdi32
endif

CORE_SRC := $(wildcard src/core/*.c)
PLAT_SRC := src/platform/platform_headless.c src/platform/platform_native.c src/platform/fs.c src/platform/capture.c
LIB_SRC  := $(CORE_SRC) $(PLAT_SRC)
LIB_OBJ  := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(LIB_SRC))
LIB      := $(BUILD)/libbronto.a

# The rules of the sample games are unit-tested too.
SNAKE_RULES := $(wildcard examples/snake/snake_*.c)
TD_RULES := $(wildcard examples/towerdefense/td_*.c)
TEST_SRC := $(wildcard tests/*.c) $(SNAKE_RULES) $(TD_RULES) tools/editor/doc.c tools/editor/console.c
TEST_BIN := $(BUILD)/bronto_tests

# Each examples/<name>/ directory becomes $(BUILD)/examples/<name>, built
# from every .c file in it.
EXAMPLES := $(patsubst examples/%/main.c,$(BUILD)/examples/%,$(wildcard examples/*/main.c))
EDITOR   := $(BUILD)/editor
BSCHECK  := $(BUILD)/bscheck
PLAYER   := $(BUILD)/bronto_player

.PHONY: all lib test sanitize examples run-examples clean info x11-test snake towerdefense td editor tools player audio-test evdev-test vscode vscode-test

all: lib

info:
	@echo "platform=$(PLATFORM) cc=$(CC)"

lib: $(LIB)

$(BUILD)/obj/%.o: src/%.c $(wildcard include/bronto/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# platform_native.c #includes the OS backend files.
$(BUILD)/obj/platform/platform_native.o: $(wildcard src/platform/platform_*.c) $(wildcard src/platform/desktop_*.c) $(wildcard src/platform/audio_*.c) $(wildcard src/platform/gamepad_*.c)
$(BUILD)/obj/platform/fs.o: $(wildcard src/platform/fs_*.c)
$(BUILD)/obj/platform/capture.o: $(wildcard src/platform/capture_*.c)

$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(TEST_BIN): $(TEST_SRC) $(LIB) tests/test.h $(wildcard examples/snake/snake*.h) $(wildcard examples/towerdefense/td*.h) tools/editor/editor.h
	$(CC) $(CFLAGS) -Iexamples/snake -Iexamples/towerdefense -Itools/editor -Wno-missing-prototypes $(TEST_SRC) $(LIB) $(LDFLAGS) $(LDLIBS) $(TEST_LDLIBS) -o $@

# The single-file build (src/bronto.c, used by build.bat) must keep compiling:
# every static name in the engine has to be unique across all its files.
$(BUILD)/obj/unity.stamp: src/bronto.c $(LIB_SRC) $(wildcard src/platform/*.c) $(wildcard include/bronto/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fsyntax-only src/bronto.c
	@touch $@

test: $(TEST_BIN) $(BUILD)/obj/unity.stamp
	@mkdir -p $(BUILD)/test_out
	$(TEST_BIN) --out $(BUILD)/test_out

# Separate build dir so sanitizer objects never mix with normal ones.
sanitize:
	$(MAKE) BUILD=build-asan OPT="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
	        LDFLAGS="-fsanitize=address,undefined" test

examples: $(EXAMPLES)

.SECONDEXPANSION:
$(BUILD)/examples/%: $$(wildcard examples/%/*.c) $$(wildcard examples/%/*.h) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

snake: $(BUILD)/examples/snake
	$(BUILD)/examples/snake
	
towerdefense: $(BUILD)/examples/towerdefense
	$(BUILD)/examples/towerdefense
	
td: $(BUILD)/examples/towerdefense
	$(BUILD)/examples/towerdefense

# The editor: every .c file in tools/editor. bscheck: the script checker.
# bronto_player: runs games; the editor's Build menu packs projects into copies of it.
tools: $(EDITOR) $(BSCHECK) $(PLAYER)

player: $(PLAYER)

$(PLAYER): tools/player/main.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(BSCHECK): tools/bscheck/main.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(EDITOR): $(wildcard tools/editor/*.c) $(wildcard tools/editor/*.h) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

editor: $(EDITOR)
	$(EDITOR) $(SCENE)

# The VS Code extension for .bs scripts: package it, and test it with node.
vscode:
	python3 tools/vscode/package_vsix.py $(BUILD)

vscode-test:
	node tools/vscode/brontoscript/test/language.test.js

# Runs from $(BUILD)/run so the screenshots and WAVs the demos write stay
# out of the source tree.
run-examples: examples tools
	@mkdir -p $(BUILD)/run
	@cd $(BUILD)/run && for e in $(abspath $(EXAMPLES) $(EDITOR)); do echo "== $$e --headless"; $$e --headless || exit 1; done
	@echo "== $(abspath $(PLAYER)) examples/platformer --headless"; $(PLAYER) examples/platformer --headless --frames 180

clean:
	rm -rf build build-asan

# X11 backend integration test (Linux; needs libx11-dev for the test driver
# only, and an X server - falls back to a virtual one via xvfb-run).
x11-test: $(LIB)
	$(CC) $(CFLAGS) -Wno-missing-prototypes tests/platform/x11_integration.c $(LIB) $(LDFLAGS) -lX11 $(LDLIBS) -o $(BUILD)/x11_integration
	@if [ -n "$$DISPLAY" ]; then $(BUILD)/x11_integration $(BUILD); \
	 else xvfb-run -a -s "-screen 0 1024x768x24" $(BUILD)/x11_integration $(BUILD); fi

# Native audio output integration test (Linux: ALSA file plugin capture).
audio-test: $(LIB)
	$(CC) $(CFLAGS) -Wno-missing-prototypes tests/platform/audio_integration.c $(LIB) $(LDFLAGS) $(LDLIBS) -o $(BUILD)/audio_integration
	@if [ -n "$$DISPLAY" ]; then $(BUILD)/audio_integration $(BUILD); \
	 else xvfb-run -a -s "-screen 0 1024x768x24" $(BUILD)/audio_integration $(BUILD); fi

# Linux evdev gamepad mapping test (no hardware needed).
evdev-test: $(LIB)
	$(CC) $(CFLAGS) -Wno-missing-prototypes tests/platform/evdev_test.c $(LIB) $(LDFLAGS) $(LDLIBS) -o $(BUILD)/evdev_test
	$(BUILD)/evdev_test
