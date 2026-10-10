/*
 * Input policy file published by the launcher for apps: $XDG_RUNTIME_DIR/applaunch/input.state
 * (/tmp/applaunch-input.state when XDG_RUNTIME_DIR is unset). Written by atomic rename, one key per line:
 *
 *   v=1
 *   keyboards=N        real keyboards present now (cp0_keyboard_presence.h rule)
 *   last_seen=T        unix seconds of the last time keyboards was > 0 (0 = never seen since boot); exact at
 *                      the moment keyboards drops to 0, and while keyboards > 0 the time of the last write
 *   gen=G              increases on every write (every change of the keyboard list)
 *
 * Readers must ignore unknown keys (later versions add lines). last_seen exists for a later rule ("a
 * sleeping Bluetooth keyboard still counts as present for N minutes"); nothing applies that rule yet.
 *
 * Device builds only (src/cp0); no LVGL dependency.
 */
#ifndef CP0_INPUT_STATE_H
#define CP0_INPUT_STATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_INPUT_STATE_VERSION 1
#define CP0_INPUT_STATE_SUBDIR "applaunch"
#define CP0_INPUT_STATE_FILE "input.state"
#define CP0_INPUT_STATE_FALLBACK_PATH "/tmp/applaunch-input.state"

typedef struct {
    unsigned keyboards;
    int64_t last_seen;
    unsigned gen;
} cp0_input_state_t;

/* Path of the state file: <runtime_dir>/applaunch/input.state, or the /tmp fallback when runtime_dir is NULL
 * or empty. Pass getenv("XDG_RUNTIME_DIR"). Returns 0, or -1 when `buf` is too small. */
int cp0_input_state_path(const char *runtime_dir, char *buf, size_t size);

/* The file text ("v=1\nkeyboards=..\nlast_seen=..\ngen=..\n"). Returns its length, or -1 if it does not fit. */
int cp0_input_state_format(const cp0_input_state_t *state, char *buf, size_t size);

/* Parse the file text. Returns 0 when it is a v=1 file (missing keys stay 0), -1 otherwise. */
int cp0_input_state_parse(const char *text, size_t len, cp0_input_state_t *out);

/* Read and parse `path`. Returns 0 or -1. */
int cp0_input_state_read(const char *path, cp0_input_state_t *out);

/* Write `path` atomically: create its directory if needed (one level, mode 0755), write <path>.tmp, rename it
 * over `path` (mode 0644). Returns 0 or -1 (errno set). */
int cp0_input_state_write(const char *path, const cp0_input_state_t *state);

/* last_seen for the next write: `now` while keyboards are present or have just gone (prev_keyboards > 0),
 * otherwise the previous value. */
int64_t cp0_input_state_next_last_seen(unsigned prev_keyboards, int64_t prev_last_seen, unsigned keyboards,
                                       int64_t now);

#ifdef __cplusplus
}
#endif

#endif /* CP0_INPUT_STATE_H */
