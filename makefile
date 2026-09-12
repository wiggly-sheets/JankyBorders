CC = clang
FILES = src/main.c src/parse.c src/mach.c src/hashtable.c src/events.c src/windows.c src/border.c src/animation.c src/layer.m
DEPS = $(wildcard src/*.h src/misc/*.h)
LIBS = -framework AppKit -framework CoreVideo -framework QuartzCore -F/System/Library/PrivateFrameworks/ -framework SkyLight

CFLAGS ?= -std=c99 -O3 -g
DEBUG_CFLAGS ?= -std=c99 -O0 -g -DDEBUG
TEST_CFLAGS ?= -std=c99 -O0 -g
TEST_LDFLAGS ?=
SANITIZER_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer

TEST_BIN_DIR = bin/tests
TEST_BINS = \
	$(TEST_BIN_DIR)/border-radius \
	$(TEST_BIN_DIR)/color-style \
	$(TEST_BIN_DIR)/animation-focus \
	$(TEST_BIN_DIR)/animation-tick \
	$(TEST_BIN_DIR)/animation-parse \
	$(TEST_BIN_DIR)/windows-regression \
	$(TEST_BIN_DIR)/shimmer-state \
	$(TEST_BIN_DIR)/border-geometry \
	$(TEST_BIN_DIR)/window-space \
	$(TEST_BIN_DIR)/settings-ownership \
	$(TEST_BIN_DIR)/parser-bounds \
	$(TEST_BIN_DIR)/message-payload \
	$(TEST_BIN_DIR)/config-execution \
	$(TEST_BIN_DIR)/mach-message

.PHONY: all debug test asan check clean $(TEST_BINS)

all: bin/borders

bin/borders: $(FILES) $(DEPS) | bin
	$(CC) $(CFLAGS) $(FILES) -o $@ $(LIBS)

debug: bin/debug

bin/debug: $(FILES) | bin
	$(CC) $(DEBUG_CFLAGS) $(FILES) -o $@ $(LIBS)

test: $(TEST_BINS)
	@for test_binary in $(TEST_BINS); do $$test_binary || exit 1; done

asan: TEST_CFLAGS += $(SANITIZER_FLAGS)
asan: TEST_LDFLAGS += $(SANITIZER_FLAGS)
asan: test

check:
	$(CC) -std=c99 -O0 -Wall -Wextra -fsyntax-only $(FILES)

$(TEST_BIN_DIR)/border-radius: tests/border_radius_test.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< $(TEST_LDFLAGS) $(LIBS) -o $@

$(TEST_BIN_DIR)/color-style: tests/color_style_test.c src/hashtable.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -ffunction-sections $^ $(TEST_LDFLAGS) -Wl,-dead_strip $(LIBS) -o $@

$(TEST_BIN_DIR)/animation-focus: tests/animation_focus_test.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -ffunction-sections $< $(TEST_LDFLAGS) -Wl,-dead_strip $(LIBS) -o $@

$(TEST_BIN_DIR)/animation-tick: tests/animation_tick_test.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -ffunction-sections $< $(TEST_LDFLAGS) -Wl,-dead_strip $(LIBS) -o $@

$(TEST_BIN_DIR)/animation-parse: tests/animation_parse_test.c src/hashtable.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -ffunction-sections $^ $(TEST_LDFLAGS) -Wl,-dead_strip $(LIBS) -o $@

$(TEST_BIN_DIR)/windows-regression: tests/windows_regression_test.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -ffunction-sections $< $(TEST_LDFLAGS) -Wl,-dead_strip $(LIBS) -o $@

$(TEST_BIN_DIR)/shimmer-state: tests/shimmer_state_test.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -ffunction-sections $< $(TEST_LDFLAGS) -Wl,-dead_strip $(LIBS) -o $@

$(TEST_BIN_DIR)/border-geometry: tests/border_geometry_test.c src/hashtable.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -ffunction-sections $^ $(TEST_LDFLAGS) -Wl,-dead_strip $(LIBS) -o $@

$(TEST_BIN_DIR)/window-space: tests/window_space_test.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< $(TEST_LDFLAGS) $(LIBS) -o $@

$(TEST_BIN_DIR)/settings-ownership: tests/settings_ownership_test.c src/hashtable.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $^ $(TEST_LDFLAGS) $(LIBS) -o $@

$(TEST_BIN_DIR)/parser-bounds: tests/parser_bounds_test.c src/hashtable.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -ffunction-sections $^ $(TEST_LDFLAGS) -Wl,-dead_strip $(LIBS) -o $@

$(TEST_BIN_DIR)/message-payload: tests/message_payload_test.c src/parse.c src/hashtable.c src/mach.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) -ffunction-sections $^ $(TEST_LDFLAGS) -Wl,-dead_strip $(LIBS) -o $@

$(TEST_BIN_DIR)/config-execution: tests/config_execution_test.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< $(TEST_LDFLAGS) $(LIBS) -o $@

$(TEST_BIN_DIR)/mach-message: tests/mach_message_test.c src/mach.c | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $^ $(TEST_LDFLAGS) $(LIBS) -o $@

bin:
	mkdir -p $@

$(TEST_BIN_DIR): | bin
	mkdir -p $@

clean:
	rm -rf bin
