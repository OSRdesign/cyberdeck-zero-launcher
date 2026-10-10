/*
 * Keyboard presence:which physical keyboards are plugged in or connected right now.
 *
 * A "real keyboard" is an input device that
 *   - has an event node (H: Handlers=... eventN),
 *   - reports KEY_A, KEY_Z, KEY_ENTER and KEY_SPACE in its key bitmap,
 *   - is not on the virtual bus (Bus=0006, BUS_VIRTUAL),
 *   - has a name that does not start with "applaunch-" (the launcher's own uinput hub).
 * This rejects consumer/system-control siblings, mice, power buttons, IR receivers (RTL2832U), HDMI CEC,
 * Bluetooth AVRCP remotes and touch panels. Same rule as Mesh Hop's keyboard_present().
 *
 * The list is read from /proc/bus/input/devices. Its bitmaps are hexadecimal words, most significant first,
 * leading zero words and leading zero digits left out; a word is as wide as a C long of the READING process
 * (the kernel prints 32-bit words to a 32-bit process on a 64-bit kernel), so the default word size is
 * 8 * sizeof(long). The parser allocates nothing.
 *
 * Device builds only (src/cp0); no LVGL dependency.
 */
#ifndef CP0_KEYBOARD_PRESENCE_H
#define CP0_KEYBOARD_PRESENCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_KBD_PRESENCE_MAX_DEVICES 16
#define CP0_KBD_PRESENCE_PATH_SIZE   32   /* "/dev/input/eventNNN" */
#define CP0_KBD_PRESENCE_NAME_SIZE   128
#define CP0_KBD_PRESENCE_PROC_PATH   "/proc/bus/input/devices"
#define CP0_KBD_PRESENCE_DEV_DIR     "/dev/input"
#define CP0_KBD_PRESENCE_DEBOUNCE_MS 400u  /* quiet time after the last /dev/input change before a rescan */
#define CP0_KBD_PRESENCE_MAX_WAIT_MS 2000u /* a stream of changes still rescans this often */
#define CP0_KBD_PRESENCE_POLL_MS     2000u /* safety rescan even without any inotify event */

typedef struct {
    char path[CP0_KBD_PRESENCE_PATH_SIZE]; /* "/dev/input/event4" */
    char name[CP0_KBD_PRESENCE_NAME_SIZE]; /* kernel name, e.g. "M4 Keyboard" */
} cp0_kbd_presence_device_t;

typedef struct {
    unsigned count; /* entries used in devices[] (at most CP0_KBD_PRESENCE_MAX_DEVICES) */
    cp0_kbd_presence_device_t devices[CP0_KBD_PRESENCE_MAX_DEVICES];
} cp0_kbd_presence_list_t;

/* ---- parsing (pure, no I/O) ---------------------------------------------------------------- */

/* Word size of the bitmaps this process reads from /proc/bus/input/devices: 8 * sizeof(long). */
unsigned cp0_kbd_presence_native_word_bits(void);

/* Bit `bit` of a bitmap value as printed by the kernel ("1000000000007 ff9f207ac14057ff ..."), `len` bytes,
 * words of `word_bits` (32 or 64) bits. Returns 1 or 0 (0 for a malformed or empty bitmap). */
int cp0_kbd_presence_bitmap_bit(const char *bitmap, size_t len, unsigned word_bits, unsigned bit);

/* Parse the text of /proc/bus/input/devices (`len` bytes, need not be NUL terminated). Fills `out` with the
 * real keyboards in file order (the first CP0_KBD_PRESENCE_MAX_DEVICES of them) and returns how many real
 * keyboards the text lists (may exceed out->count). word_bits 0 = native. */
int cp0_kbd_presence_parse(const char *text, size_t len, unsigned word_bits, cp0_kbd_presence_list_t *out);

/* Read and parse a devices file (NULL = /proc/bus/input/devices). Returns the number of real keyboards, or -1
 * when the file cannot be read (out->count is then 0). Uses a fixed stack buffer, no allocation. */
int cp0_kbd_presence_enumerate(const char *proc_path, unsigned word_bits, cp0_kbd_presence_list_t *out);

/* Cheap presence check: number of real keyboards, -1 when the file cannot be read (NULL = /proc). */
int cp0_kbd_presence_count(const char *proc_path);

/* 1 when both lists hold the same devices in the same order. */
int cp0_kbd_presence_list_equal(const cp0_kbd_presence_list_t *a, const cp0_kbd_presence_list_t *b);

/* ---- rescan timing (pure; the watcher thread drives it with CLOCK_MONOTONIC milliseconds) -------- */

typedef struct {
    uint32_t debounce_ms;
    uint32_t max_wait_ms;
    uint32_t poll_ms;
    int pending;             /* a /dev/input change waits for its rescan */
    uint64_t first_event_ms; /* first change of the pending burst */
    uint64_t deadline_ms;    /* rescan time of the pending burst */
    uint64_t next_poll_ms;   /* next safety rescan */
} cp0_kbd_presence_timer_t;

/* Zero durations take the defaults above. The safety rescan is due poll_ms after now. */
void cp0_kbd_presence_timer_init(cp0_kbd_presence_timer_t *t, uint32_t debounce_ms, uint32_t max_wait_ms,
                                 uint32_t poll_ms, uint64_t now_ms);
/* A change was seen in /dev/input: rescan debounce_ms after the LAST change of a burst, but no later than
 * max_wait_ms after its first change. */
void cp0_kbd_presence_timer_on_event(cp0_kbd_presence_timer_t *t, uint64_t now_ms);
/* 1 when a rescan is due (debounced change or safety poll). */
int cp0_kbd_presence_timer_due(const cp0_kbd_presence_timer_t *t, uint64_t now_ms);
/* A rescan just ran: clears the pending burst and schedules the next safety rescan. */
void cp0_kbd_presence_timer_scanned(cp0_kbd_presence_timer_t *t, uint64_t now_ms);
/* Milliseconds until the next due time (0 when due now), for poll(). */
int cp0_kbd_presence_timer_timeout_ms(const cp0_kbd_presence_timer_t *t, uint64_t now_ms);

/* ---- watcher (Linux: inotify on /dev/input + safety poll, one thread) ---------------------------- */

typedef struct {
    const char *proc_path;  /* NULL: /proc/bus/input/devices */
    const char *dev_dir;    /* NULL: /dev/input */
    uint32_t debounce_ms;   /* 0: CP0_KBD_PRESENCE_DEBOUNCE_MS */
    uint32_t max_wait_ms;   /* 0: CP0_KBD_PRESENCE_MAX_WAIT_MS */
    uint32_t poll_ms;       /* 0: CP0_KBD_PRESENCE_POLL_MS */
    unsigned word_bits;     /* 0: native */
} cp0_kbd_presence_options_t;

typedef struct cp0_kbd_presence_watcher cp0_kbd_presence_watcher_t;

/* Called with the new list after every change, and once with the current list when the listener is added.
 * Calls are serialised; they run on the watcher thread (changes) or on the caller of add_listener (first
 * call). The list is only valid during the call. A listener may call snapshot()/generation(), never
 * add_listener()/remove_listener()/stop(). `generation` is the watcher generation of that list. */
typedef void (*cp0_kbd_presence_listener_t)(const cp0_kbd_presence_list_t *list, unsigned generation,
                                            void *user);

/* Scans once (so snapshot() is valid on return) and starts the watcher thread. NULL options = defaults.
 * Returns NULL only when memory or the thread cannot be had; without inotify it still polls. */
cp0_kbd_presence_watcher_t *cp0_kbd_presence_watcher_start(const cp0_kbd_presence_options_t *options);
/* Stops the thread and frees the watcher. */
void cp0_kbd_presence_watcher_stop(cp0_kbd_presence_watcher_t *watcher);
/* Increases by one each time the keyboard list changes (1 after the first scan). Lock free. */
unsigned cp0_kbd_presence_watcher_generation(const cp0_kbd_presence_watcher_t *watcher);
/* Copies the current list into `out` and returns its generation. Thread safe. */
unsigned cp0_kbd_presence_watcher_snapshot(cp0_kbd_presence_watcher_t *watcher, cp0_kbd_presence_list_t *out);
/* Up to 4 listeners. Returns 0, or -1 when the table is full. */
int cp0_kbd_presence_watcher_add_listener(cp0_kbd_presence_watcher_t *watcher,
                                          cp0_kbd_presence_listener_t listener, void *user);
/* Returns once a running call of that listener has finished. */
void cp0_kbd_presence_watcher_remove_listener(cp0_kbd_presence_watcher_t *watcher,
                                              cp0_kbd_presence_listener_t listener, void *user);

/* One watcher per process with default options, shared by the keyboard thread, the uinput hub and the state
 * publisher. Started by the first acquire, stopped by the last release. NULL if it cannot start. */
cp0_kbd_presence_watcher_t *cp0_kbd_presence_shared_acquire(void);
void cp0_kbd_presence_shared_release(void);

#ifdef __cplusplus
}
#endif

#endif /* CP0_KEYBOARD_PRESENCE_H */
