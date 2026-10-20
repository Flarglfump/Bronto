# Bronto Engine - Makefile (GNU make; macOS / Linux / MSYS2 / MinGW).
# On Windows with MSVC use build.bat instead.
#
#   make            build the static library (build/libbronto.a)
#   make test       build and run the test suite
#   make snake      build and play the Snake sample
#   make towerdefense  build and play the Pop Defense sample
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
#   Linux: libX11 is loaded at run time (dlopen) - only -ldl is needed.
#   macOS: system frameworks.  Windows (MinGW): user32 + gdi32.
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
#   Audio: ALSA is dlopen'ed too; Core Audio / waveOut are system libraries.
ifeq ($(UNAME_S),Darwin)
  PLATFORM := macos
  LDLIBS   += -framework Cocoa -framework QuartzCore -framework AudioToolbox -framework GameController
  TEST_LDLIBS := -pthread
else ifeq ($(UNAME_S),Linux)
  PLATFORM := linux
  LDLIBS   += -ldl
  TEST_LDLIBS := -pthread
else ifneq (,$(findstring BSD,$(UNAME_S)))
  PLATFORM := bsd
  TEST_LDLIBS := -pthread
else
  PLATFORM := windows
  LDLIBS   += -luser32 -lgdi32
endif

CORE_SRC := $(wildcard src/core/*.c)
PLAT_SRC := src/platform/platform_headless.c src/platform/platform_native.c
LIB_SRC  := $(CORE_SRC) $(PLAT_SRC)
LIB_OBJ  := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(LIB_SRC))
LIB      := $(BUILD)/libbronto.a

# The rules of the sample games are unit-tested too.
SNAKE_RULES := $(wildcard examples/snake/snake_*.c)
TD_RULES := $(wildcard examples/towerdefense/td_*.c)
TEST_SRC := $(wildcard tests/*.c) $(SNAKE_RULES) $(TD_RULES)
TEST_BIN := $(BUILD)/bronto_tests

# Each examples/<name>/ directory becomes $(BUILD)/examples/<name>, built
# from every .c file in it.
EXAMPLES := $(patsubst examples/%/main.c,$(BUILD)/examples/%,$(wildcard examples/*/main.c))

.PHONY: all lib test sanitize examples run-examples clean info x11-test snake towerdefense td audio-test evdev-test

all: lib

info:
	@echo "platform=$(PLATFORM) cc=$(CC)"

lib: $(LIB)

$(BUILD)/obj/%.o: src/%.c $(wildcard include/bronto/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# platform_native.c #includes the OS backend files.
$(BUILD)/obj/platform/platform_native.o: $(wildcard src/platform/platform_*.c) $(wildcard src/platform/audio_*.c) $(wildcard src/platform/gamepad_*.c)

$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(TEST_BIN): $(TEST_SRC) $(LIB) tests/test.h $(wildcard examples/snake/snake*.h) $(wildcard examples/towerdefense/td*.h)
	$(CC) $(CFLAGS) -Iexamples/snake -Iexamples/towerdefense -Wno-missing-prototypes $(TEST_SRC) $(LIB) $(LDFLAGS) $(LDLIBS) $(TEST_LDLIBS) -o $@

test: $(TEST_BIN)
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

# Runs from $(BUILD)/run so the screenshots and WAVs the demos write stay
# out of the source tree.
run-examples: examples
	@mkdir -p $(BUILD)/run
	@cd $(BUILD)/run && for e in $(abspath $(EXAMPLES)); do echo "== $$e --headless"; $$e --headless || exit 1; done

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
