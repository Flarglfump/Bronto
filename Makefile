# Bronto Engine - Makefile (GNU make; macOS / Linux / MSYS2 / MinGW).
# On Windows with MSVC use build.bat instead.
#
#   make            build the static library (build/libbronto.a)
#   make test       build and run the test suite
#   make snake      build and play the Snake sample
#   make towerdefense  build and play the Pop Defense sample
#   make editor     build and run the scene editor (tools/editor)
#   make tools      build the editor, bscheck (checks .bs scripts) and bronto_player (runs games)
#   make game GAME=examples/pantryquest [TARGET=host|linux|macos|windows|web]
#                   pack a project into a game people can run (the editor's Build menu,
#                   as editor --build): build/games/<project>/<target>/ (host: this computer)
#   make pantryquest   the same for Pantry Quest, for this computer
#   make marble        build Marble (the 3D example) for this computer and play it
#   make explorer      the same for Explorer (3D: walk the hills, find the crystals)
#   make vscode     package the VS Code extension (tools/vscode) into build/*.vsix (needs python3)
#   make vscode-icon  redraw the extension's icon from the editor's app icon (after changing it)
#   make examples   build the examples (build/examples/*)
#   make run-examples  run every example headless in build/run (CI smoke test)
#   make sanitize   run the tests under AddressSanitizer + UBSan
#   make x11-test / audio-test / evdev-test   Linux backend tests
#   make web        build for the web: the web player (build/web) and a page for each example
#   make web-test   run the test suite as WebAssembly (in Node) and check the web C library
#   make playthrough / replay / replay-check / replay-web
#                   Pantry Quest played through by a bot, and watched back frame for frame
#   make pq2-playthrough / pq2-replay / pq2-replay-check / pq2-replay-web   the same for Pantry Quest II
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

# The C compiler on Linux: cc if there is one, otherwise gcc, otherwise clang
# (some distributions install gcc or clang without a cc link). Only when CC
# wasn't chosen on the command line or in the environment: make CC=... wins.
ifeq ($(PLATFORM),linux)
  ifneq (,$(filter default undefined file,$(origin CC)))
    ifeq (,$(shell command -v $(CC) 2>/dev/null))
      FOUND_CC := $(firstword $(foreach c,gcc clang,$(if $(shell command -v $(c) 2>/dev/null),$(c))))
      ifeq (,$(FOUND_CC))
        $(error No C compiler found: install gcc or clang (or say which: make CC=/path/to/compiler))
      endif
      CC := $(FOUND_CC)
    endif
  endif
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

.PHONY: all lib test sanitize examples run-examples clean info x11-test snake towerdefense td editor tools player audio-test evdev-test vscode vscode-test vscode-icon web web-test web-check game pantryquest marble explorer

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
# Every file under a folder, in make itself (not the shell's find, which on Windows is
# a different program).
rwildcard = $(foreach d,$(wildcard $(1)/*),$(call rwildcard,$(d)) $(d))
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

$(EDITOR): $(wildcard tools/editor/*.c) $(wildcard tools/editor/*.h) tools/web/webpage.c tools/web/webpage.h $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

editor: $(EDITOR)
	$(EDITOR) $(SCENE)

# A project packed into a game (the editor's Build menu, from the command line):
# the player for this computer is built here; for macos / windows, the player built
# there goes in $(BUILD)/players/ (bronto_player-macos, bronto_player-windows.exe);
# for the web, "make web" builds it first.
GAME   ?= examples/pantryquest
TARGET ?= host
GAMES  := $(BUILD)/games

GAME_TARGET := $(if $(filter host,$(TARGET)),$(if $(filter bsd,$(PLATFORM)),linux,$(PLATFORM)),$(TARGET))

# The game's folder name, from the end of its path: make splits words at spaces, so
# (for paths like C:/Users/Jo Smith/...) only the last word of the full path is used.
GAME_NAME := $(notdir $(lastword $(abspath $(GAME))))

game: $(EDITOR) $(PLAYER) $(if $(filter web,$(GAME_TARGET)),web)
	$(EDITOR) --build $(GAME_TARGET) "$(GAME)" --out "$(GAMES)/$(GAME_NAME)/$(GAME_TARGET)"

pantryquest:
	@$(MAKE) --no-print-directory game GAME=examples/pantryquest TARGET=$(TARGET)

# Pantry Quest II: pack it like any game; remake its art, sound, music, rooms and prefabs
# from the Python tools in examples/pantryquest2/tools (standard library only); and play the
# world through on paper to be sure every room, item and guardian can be reached.
PQ2 := examples/pantryquest2
pantryquest2:
	@$(MAKE) --no-print-directory game GAME=$(PQ2) TARGET=$(TARGET)
pantryquest2-assets:
	python3 $(PQ2)/tools/make_art.py && python3 $(PQ2)/tools/make_sound.py && python3 $(PQ2)/tools/make_music.py
	python3 $(PQ2)/tools/make_locks.py && python3 $(PQ2)/tools/make_rooms.py && python3 $(PQ2)/tools/make_prefabs.py
pantryquest2-check: $(BSCHECK)
	$(BSCHECK) --strict --project $(PQ2) $(wildcard $(PQ2)/scripts/*.bs) $(wildcard $(PQ2)/scripts/bosses/*.bs)
	python3 $(PQ2)/tools/check_scenes.py
	python3 $(PQ2)/tools/check_world.py
	python3 $(PQ2)/tools/check_softlocks.py
.PHONY: pantryquest2 pantryquest2-assets pantryquest2-check

marble: $(PLAYER)
	$(PLAYER) examples/marble

explorer: $(PLAYER)
	$(PLAYER) examples/explorer

# The VS Code extension for .bs scripts: package it, and test it with node.
vscode:
	python3 tools/vscode/package_vsix.py $(BUILD)

vscode-icon: $(EDITOR)
	$(EDITOR) --write-icon tools/vscode/brontoscript/images/icon.png 256

vscode-test:
	node tools/vscode/brontoscript/test/language.test.js

# Runs from $(BUILD)/run so the screenshots and WAVs the demos write stay
# out of the source tree.
run-examples: examples tools
	@mkdir -p $(BUILD)/run
	@cd $(BUILD)/run && for e in $(abspath $(EXAMPLES) $(EDITOR)); do echo "== $$e --headless"; $$e --headless || exit 1; done
	@echo "== $(abspath $(PLAYER)) examples/platformer --headless"; $(PLAYER) examples/platformer --headless --frames 180
	@echo "== $(abspath $(PLAYER)) examples/pantryquest --headless"; $(PLAYER) examples/pantryquest --headless --frames 120 && \
	 $(PLAYER) examples/pantryquest/main.scene --headless --frames 300
	@echo "== $(abspath $(PLAYER)) examples/marble --headless"; $(PLAYER) examples/marble --headless --frames 240
	@echo "== $(abspath $(PLAYER)) examples/explorer --headless"; $(PLAYER) examples/explorer --headless --frames 240

clean:
	rm -rf build build-asan

# ---- the web (WebAssembly) -------------------------------------------------
# Needs clang with its WebAssembly target and wasm-ld (LLVM's linker) - not
# Emscripten: src/platform/web is the C library, tools/web/bronto.js the
# browser side. Linux: the clang and lld packages. macOS: Homebrew's llvm and
# lld (Apple's clang has no WebAssembly target). Windows: the LLVM installer.
#
#   build/web/bronto_player.wasm, bronto.js, shell.html   the web player and its page
#                                    (the editor's Build > Web uses these)
#   build/bronto_web                 makes a page: bronto_web <project> | --wasm game.wasm
#   build/web/<example>/index.html   each example as a web page
WASM_CC ?= $(firstword $(wildcard /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang) clang)
WEB     := $(BUILD)/web
WASM_OBJ := $(BUILD)/wasm
WASM_CFLAGS := --target=wasm32 $(CSTD) $(WARN) -O2 -ffp-contract=off -Iinclude \
               -nostdlibinc -isystem src/platform/web/include \
               -mbulk-memory -mnontrapping-fptoint -msign-ext -mexception-handling -mllvm -wasm-enable-sjlj
WASM_LDFLAGS := --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--stack-first -Wl,-z,stack-size=4194304 -Wl,--strip-debug
WEB_LIB_SRC := $(CORE_SRC) $(PLAT_SRC) $(wildcard src/platform/web/*.c)
WEB_LIB_OBJ := $(patsubst src/%.c,$(WASM_OBJ)/%.o,$(WEB_LIB_SRC)) $(WASM_OBJ)/platform/web/tag.o
WEB_PLAYER  := $(WEB)/bronto_player.wasm
WEB_TOOL    := $(BUILD)/bronto_web
WEB_RUNTIME := $(WEB)/bronto.js $(WEB)/shell.html
WEB_C_PAGES := $(patsubst examples/%/main.c,$(WEB)/%/index.html,$(wildcard examples/*/main.c))
WEB_PROJECT_PAGES := $(patsubst examples/%/project.bronto,$(WEB)/%/index.html,$(wildcard examples/*/project.bronto))
WEB_PAGES   := $(WEB_C_PAGES) $(WEB_PROJECT_PAGES)
WEB_TITLE_snake := Bronto Snake
WEB_TITLE_towerdefense := Pop Defense
WEB_TITLE_bounce := Bounce

web: $(WEB_PLAYER) $(WEB_RUNTIME) $(WEB_TOOL) $(WEB_PAGES) $(WASM_OBJ)/unity.stamp
	@echo "Web pages: $(WEB_PAGES)"

# A clear message, rather than a pile of errors, when the compiler can't make WebAssembly:
# it has to have the wasm32 target, link with wasm-ld, and turn setjmp/longjmp into
# WebAssembly exceptions (-mllvm -wasm-enable-sjlj: clang 14 or later; tested with clang 18).
# (Checked again whenever this Makefile changes; make web-check runs it on its own.)
$(WASM_OBJ)/toolchain.ok: Makefile
	@mkdir -p $(dir $@)
	@rm -f $@
	@printf 'int x;\n' > $(WASM_OBJ)/check.c
	@printf '#include <setjmp.h>\nstatic jmp_buf j;\nint y(int n);\nint y(int n) { if (setjmp(j)) return 1; if (n) longjmp(j, 1); return 0; }\n' > $(WASM_OBJ)/check_sjlj.c
	@if ! $(WASM_CC) --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=x $(WASM_OBJ)/check.c -o $(WASM_OBJ)/check.wasm 2>$(WASM_OBJ)/check.log; then \
	      echo "make web: $(WASM_CC) can't build WebAssembly. It needs clang's wasm32 target and wasm-ld:"; \
	      echo "  Linux: install clang and lld.  macOS: brew install llvm lld.  Windows: the LLVM installer (llvm.org)."; \
	      echo "  Or point WASM_CC at such a clang:  make web WASM_CC=/path/to/clang"; \
	      echo "  ($(WASM_CC) is: `$(WASM_CC) --version 2>&1 | head -n 1`)"; \
	      echo "  What it said:"; sed 's/^/    /' $(WASM_OBJ)/check.log | head -n 8; exit 1; fi
	@if ! $(WASM_CC) $(WASM_CFLAGS) -c $(WASM_OBJ)/check_sjlj.c -o $(WASM_OBJ)/check_sjlj.o 2>$(WASM_OBJ)/check.log; then \
	      echo "make web: $(WASM_CC) makes WebAssembly, but can't compile setjmp/longjmp into it"; \
	      echo "  (-mllvm -wasm-enable-sjlj), which the scripting language needs. That takes clang 14 or later"; \
	      echo "  (Bronto is tested with clang 18) built with its WebAssembly target."; \
	      echo "  ($(WASM_CC) is: `$(WASM_CC) --version 2>&1 | head -n 1`)"; \
	      echo "  On Windows, the clang that comes with Visual Studio or an older LLVM may be first on the PATH:"; \
	      echo "  install the current LLVM from llvm.org, or name its clang:  WASM_CC=C:/PROGRA~1/LLVM/bin/clang.exe"; \
	      echo "  What it said:"; sed 's/^/    /' $(WASM_OBJ)/check.log | head -n 8; exit 1; fi
	@echo "$(WASM_CC)" > $@

web-check: $(WASM_OBJ)/toolchain.ok
	@echo "$(WASM_CC) builds WebAssembly."

$(WASM_OBJ)/%.o: src/%.c $(wildcard include/bronto/*.h) $(wildcard src/platform/web/*.h) $(wildcard src/platform/web/include/*.h) | $(WASM_OBJ)/toolchain.ok
	@mkdir -p $(dir $@)
	$(WASM_CC) $(WASM_CFLAGS) -c $< -o $@
# The C library itself: -fno-builtin, or the compiler turns the insides of
# calloc into a call to calloc (and the like).
$(WASM_OBJ)/platform/web/%.o: WASM_CFLAGS += -fno-builtin
# The longjmp exception tag: see tag.s.
$(WASM_OBJ)/platform/web/tag.o: src/platform/web/tag.s | $(WASM_OBJ)/toolchain.ok
	@mkdir -p $(dir $@)
	$(WASM_CC) --target=wasm32 -mexception-handling -c $< -o $@
$(WASM_OBJ)/platform/platform_native.o: src/platform/platform_web.c
$(WASM_OBJ)/platform/fs.o: src/platform/fs_web.c

# build.bat builds the web player from the single-file build: keep it compiling.
$(WASM_OBJ)/unity.stamp: src/bronto.c $(LIB_SRC) $(wildcard src/platform/*.c) $(wildcard include/bronto/*.h) | $(WASM_OBJ)/toolchain.ok
	$(WASM_CC) $(WASM_CFLAGS) -fsyntax-only src/bronto.c
	@touch $@

$(WEB_PLAYER): tools/player/main.c $(WEB_LIB_OBJ)
	@mkdir -p $(dir $@)
	$(WASM_CC) $(WASM_CFLAGS) -c $< -o $(WASM_OBJ)/player.o
	$(WASM_CC) $(WASM_LDFLAGS) $(WEB_LIB_OBJ) $(WASM_OBJ)/player.o -o $@

$(WEB)/bronto.js: tools/web/bronto.js
	@mkdir -p $(dir $@)
	cp $< $@
$(WEB)/shell.html: tools/web/shell.html
	@mkdir -p $(dir $@)
	cp $< $@

$(WEB_TOOL): tools/web/main.c tools/web/webpage.c tools/web/webpage.h $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

# The C examples, compiled to WebAssembly, each in a page.
.PRECIOUS: $(WEB)/%.wasm
$(WEB)/%.wasm: $$(wildcard examples/%/*.c) $$(wildcard examples/%/*.h) $(WEB_LIB_OBJ)
	@mkdir -p $(dir $@) $(WASM_OBJ)/examples/$*
	$(WASM_CC) $(WASM_CFLAGS) $(filter %.c,$^) $(WEB_LIB_OBJ) $(WASM_LDFLAGS) -o $@

$(WEB)/%/index.html: $(WEB)/%.wasm $(WEB_RUNTIME) $(WEB_TOOL)
	$(WEB_TOOL) --wasm $< --title "$(or $(WEB_TITLE_$*),$*)" --runtime $(WEB) -o $@

# Projects made with the editor: the web player with the project packed in.
$(WEB_PROJECT_PAGES): $(WEB)/%/index.html: $(WEB_PLAYER) $(WEB_RUNTIME) $(WEB_TOOL) $$(call rwildcard,examples/$$*)
	$(WEB_TOOL) examples/$* --runtime $(WEB) -o $@

# The test suite as WebAssembly, run by Node; then the web C library is
# checked against this computer's (the same program built both ways must
# print the same thing); then the player must draw the same frames.
WEB_TESTS := $(WEB)/bronto_tests.wasm
$(WEB_TESTS): $(TEST_SRC) $(WEB_LIB_OBJ) tests/test.h
	@mkdir -p $(dir $@)
	$(WASM_CC) $(WASM_CFLAGS) -Iexamples/snake -Iexamples/towerdefense -Itools/editor -Wno-missing-prototypes $(TEST_SRC) $(WEB_LIB_OBJ) $(WASM_LDFLAGS) -o $@

$(WEB)/libc_check.wasm: tests/web/libc_check.c $(WEB_LIB_OBJ)
	$(WASM_CC) $(WASM_CFLAGS) -Wno-format-nonliteral $< $(filter $(WASM_OBJ)/platform/web/%,$(WEB_LIB_OBJ)) $(WASM_LDFLAGS) -o $@

$(WEB)/persist.wasm: tests/web/persist.c $(WEB_LIB_OBJ)
	$(WASM_CC) $(WASM_CFLAGS) $< $(WEB_LIB_OBJ) $(WASM_LDFLAGS) -o $@

$(BUILD)/libc_check: tests/web/libc_check.c
	$(CC) $(CSTD) -O2 -w $< -lm -o $@

web-test: $(WEB_TESTS) $(WEB)/libc_check.wasm $(BUILD)/libc_check $(WEB)/persist.wasm $(PLAYER) $(EXAMPLES) web
	node tools/web/run_node.js $(WEB_TESTS) --mkdir build/test_out --dir examples/platformer=examples/platformer \
	     --dir examples/pantryquest=examples/pantryquest \
	     --dir examples/marble=examples/marble \
	     --dir examples/explorer=examples/explorer \
	     --file tools/vscode/brontoscript/lib/builtins.json=tools/vscode/brontoscript/lib/builtins.json \
	     --file tools/vscode/brontoscript/syntaxes/brontoscript.tmLanguage.json=tools/vscode/brontoscript/syntaxes/brontoscript.tmLanguage.json \
	     --file docs/scripting.md=docs/scripting.md -- --out build/test_out
	@rm -rf $(BUILD)/libc_check.out && mkdir -p $(BUILD)/libc_check.out
	@$(BUILD)/libc_check $(BUILD)/libc_check.out > $(BUILD)/libc_check.native.txt
	@node tools/web/run_node.js $(WEB)/libc_check.wasm --mkdir scratch -- scratch > $(BUILD)/libc_check.web.txt
	@diff $(BUILD)/libc_check.native.txt $(BUILD)/libc_check.web.txt && echo "web C library: same output as this computer's ($$(wc -l < $(BUILD)/libc_check.web.txt) lines)"
	node tools/web/test/web.test.js $(BUILD)

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

# Pantry Quest, played through by a bot and replayed frame for frame
# (tools/pantrybot, include/bronto/replay.h, docs/replay.md):
#   make playthrough      the bot plays the whole game and records it: $(PLAYTHROUGH)
#   make replay           watch the recording in a window (SEGMENT=N starts at segment N)
#   make replay-check     play it back headless and check every recorded frame
#   make replay-web       a one-file web page that plays it: $(WEB)/pantryquest-replay/index.html
PANTRYBOT   := $(BUILD)/pantrybot
PLAYTHROUGH ?= examples/pantryquest/tools/playthrough.brec
$(PANTRYBOT): $(wildcard tools/pantrybot/*.c) tools/pantrybot/bot.h $(LIB)
	$(CC) $(CFLAGS) -Wno-missing-prototypes $(wildcard tools/pantrybot/*.c) $(LIB) $(LDFLAGS) $(LDLIBS) -pthread -o $@
pantrybot: $(PANTRYBOT)
playthrough: $(PANTRYBOT)
	$(PANTRYBOT) --game examples/pantryquest --out $(PLAYTHROUGH)
replay: $(PLAYER)
	$(PLAYER) examples/pantryquest --replay $(PLAYTHROUGH) $(if $(SEGMENT),--segment $(SEGMENT))
replay-check: $(PLAYER)
	$(PLAYER) examples/pantryquest --replay $(PLAYTHROUGH) --headless
replay-web: $(WEB_PLAYER) $(WEB_RUNTIME) $(WEB_TOOL)
	$(WEB_TOOL) examples/pantryquest --runtime $(WEB) --replay $(PLAYTHROUGH) -o $(WEB)/pantryquest-replay/index.html
.PHONY: pantrybot playthrough replay replay-check replay-web

# botrunner: runs a game headless for a bot in another program (stdin/stdout) and records
# the run; Pantry Quest II's bot (examples/pantryquest2/tools/bot) drives it.
BOTRUNNER := $(BUILD)/botrunner
$(BOTRUNNER): tools/botrunner/main.c $(LIB)
	$(CC) $(CFLAGS) tools/botrunner/main.c $(LIB) $(LDFLAGS) $(LDLIBS) -o $@
botrunner: $(BOTRUNNER)
.PHONY: botrunner

# Pantry Quest II, played through by its bot (examples/pantryquest2/tools/bot, Python 3) the same way:
#   make pq2-playthrough      the bot plays the whole game and records it: $(PQ2_PLAYTHROUGH)
#   make pq2-replay           watch the recording in a window (SEGMENT=N starts at segment N)
#   make pq2-replay-check     play it back headless and check every recorded frame
#   make pq2-replay-web       a one-file web page that plays it: $(WEB)/pantryquest2-replay/index.html
PQ2_PLAYTHROUGH ?= examples/pantryquest2/tools/playthrough.brec
pq2-playthrough: $(BOTRUNNER)
	python3 -u examples/pantryquest2/tools/bot/route.py --out $(PQ2_PLAYTHROUGH)
pq2-replay: $(PLAYER)
	$(PLAYER) examples/pantryquest2 --replay $(PQ2_PLAYTHROUGH) $(if $(SEGMENT),--segment $(SEGMENT))
pq2-replay-check: $(PLAYER)
	$(PLAYER) examples/pantryquest2 --replay $(PQ2_PLAYTHROUGH) --headless
pq2-replay-web: $(WEB_PLAYER) $(WEB_RUNTIME) $(WEB_TOOL)
	$(WEB_TOOL) examples/pantryquest2 --runtime $(WEB) --replay $(PQ2_PLAYTHROUGH) -o $(WEB)/pantryquest2-replay/index.html
.PHONY: pq2-playthrough pq2-replay pq2-replay-check pq2-replay-web
