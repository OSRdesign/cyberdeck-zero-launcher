# SPDX-License-Identifier: MIT
#
# Strokes recogniser (PDA profile, phase S1): plain Makefile for Linux / WSL, no launcher build system.
#
#   make                 library + tools + tests in build/host (-O2)
#   make test            unit tests (build/host/test_strokes)
#   make asan            the same under AddressSanitizer + UndefinedBehaviorSanitizer (build/asan) and run the tests
#   make eval            synthetic accuracy table (report 038 settings) with the regression gate
#   make bench           ns per recognition on this machine
#   make check           everything above with -Werror, the e2e capture/replay test (plain and sanitised tools)
#                        and the name check
#   make aarch64         static aarch64 binaries for the boards (build/aarch64; needs aarch64-linux-gnu-gcc)
#   make clean
#
# Variables: CC, O (output directory), OPT, SAN, WERROR.

CC ?= cc
O ?= build/host
OPT ?= -O2
SAN ?=
WERROR ?=
LDSTATIC ?=

# The library itself lives in the launcher's cp0_lvgl component (report 041, phase S3): the launcher links it from
# there, and this harness builds the very same files (no copy). The shipped tables are the launcher's resource files
# (installed as /usr/share/APPLaunch/strokes); the tools and tests read them from there (STROKES_SHARE_DIR, relative to
# this directory).
CP0 := ../../ext_components/cp0_lvgl
LIB_DIR := $(CP0)/src/strokes
SHARE := ../../projects/APPLaunch/APPLaunch/strokes

WARN := -Wall -Wextra -Wshadow -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wvla
BASE := -std=c99 $(OPT) -g $(SAN) $(WARN) $(WERROR) -I$(CP0)/include -I$(LIB_DIR) -Isrc \
        -DSTROKES_SHARE_DIR='"$(SHARE)"'
# the library is plain C99; the tools also use POSIX and Linux headers
LIB_FLAGS := $(BASE) -Wpedantic
TOOL_FLAGS := $(BASE) -D_DEFAULT_SOURCE
LDLIBS := -lm

LIB_SRC := $(addprefix $(LIB_DIR)/,strokes_geom.c strokes_table.c strokes_match.c strokes_capture.c strokes_engine.c)
TOOL_SRC := src/sample.c src/synth.c src/report.c src/tools_common.c src/evparse.c
TEST_SRC := $(wildcard tests/test_*.c)

LIB_OBJ := $(patsubst $(LIB_DIR)/%.c,$(O)/lib/%.o,$(LIB_SRC))
TOOL_OBJ := $(patsubst src/%.c,$(O)/%.o,$(TOOL_SRC))
TEST_OBJ := $(patsubst tests/%.c,$(O)/tests/%.o,$(TEST_SRC))
HDR := $(CP0)/include/strokes.h $(LIB_DIR)/strokes_internal.h src/sample.h src/tools_common.h src/evparse.h tests/test.h

PROGS := $(O)/strokes-eval $(O)/strokes-replay $(O)/strokes-capture $(O)/strokes-bench $(O)/test_strokes

.PHONY: all test asan eval bench check aarch64 clean names e2e

all: $(PROGS) $(O)/libstrokes.a

$(O) $(O)/tests $(O)/lib:
	mkdir -p $@

$(LIB_OBJ): $(O)/lib/%.o: $(LIB_DIR)/%.c $(HDR) | $(O)/lib
	$(CC) $(LIB_FLAGS) -c $< -o $@

$(TOOL_OBJ) $(O)/eval_main.o $(O)/replay_main.o $(O)/capture_main.o $(O)/bench_main.o: $(O)/%.o: src/%.c $(HDR) | $(O)
	$(CC) $(TOOL_FLAGS) -c $< -o $@

$(TEST_OBJ): $(O)/tests/%.o: tests/%.c $(HDR) | $(O)/tests
	$(CC) $(TOOL_FLAGS) -c $< -o $@

$(O)/libstrokes.a: $(LIB_OBJ)
	rm -f $@
	ar rcs $@ $^

$(O)/strokes-eval: $(O)/eval_main.o $(TOOL_OBJ) $(O)/libstrokes.a
	$(CC) $(SAN) $(LDSTATIC) -o $@ $^ $(LDLIBS)

$(O)/strokes-replay: $(O)/replay_main.o $(TOOL_OBJ) $(O)/libstrokes.a
	$(CC) $(SAN) $(LDSTATIC) -o $@ $^ $(LDLIBS)

$(O)/strokes-capture: $(O)/capture_main.o $(TOOL_OBJ) $(O)/libstrokes.a
	$(CC) $(SAN) $(LDSTATIC) -o $@ $^ $(LDLIBS)

$(O)/strokes-bench: $(O)/bench_main.o $(TOOL_OBJ) $(O)/libstrokes.a
	$(CC) $(SAN) $(LDSTATIC) -o $@ $^ $(LDLIBS)

$(O)/test_strokes: $(TEST_OBJ) $(TOOL_OBJ) $(O)/libstrokes.a
	$(CC) $(SAN) -o $@ $^ $(LDLIBS)

test: $(O)/test_strokes
	$(O)/test_strokes

asan:
	$(MAKE) O=build/asan OPT=-O1 SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all" WERROR="$(WERROR)" all
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 build/asan/test_strokes

eval: $(O)/strokes-eval
	$(O)/strokes-eval --n 200 --seed 1 --garbage 2000 --gate 98,2

bench: $(O)/strokes-bench
	$(O)/strokes-bench

names:
	sh tests/check_names.sh

e2e: $(O)/strokes-eval $(O)/strokes-capture $(O)/strokes-replay
	sh tests/e2e.sh $(O)

check:
	$(MAKE) WERROR=-Werror all
	$(O)/test_strokes
	$(MAKE) WERROR=-Werror asan
	$(MAKE) WERROR=-Werror eval
	$(MAKE) e2e
	sh tests/e2e.sh build/asan
	$(MAKE) names
	@echo "check: all passed"

aarch64:
	$(MAKE) O=build/aarch64 CC=aarch64-linux-gnu-gcc LDSTATIC=-static WERROR="$(WERROR)" \
		build/aarch64/strokes-eval build/aarch64/strokes-replay build/aarch64/strokes-capture build/aarch64/strokes-bench

clean:
	rm -rf build
