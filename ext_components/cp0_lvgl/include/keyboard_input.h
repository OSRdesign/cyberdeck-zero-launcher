/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CP0_LVGL_KEYBOARD_INPUT_H
#define CP0_LVGL_KEYBOARD_INPUT_H

#include <pthread.h>
#include <stdint.h>
#include <sys/queue.h>
#ifdef __cplusplus
extern "C" {
#endif /* CP0_LVGL_KEYBOARD_INPUT_H */

// modifier bitmask
#define KBD_MOD_SHIFT  (1u << 0)
#define KBD_MOD_CTRL   (1u << 1)
#define KBD_MOD_ALT    (1u << 2)
#define KBD_MOD_LOGO   (1u << 3)
#define KBD_MOD_CAPS   (1u << 4)
#define KBD_MOD_NUM    (1u << 5)
#define KBD_MOD_FN     (1u << 6)

typedef enum {
    KBD_INPUT_CONTEXT_NAVIGATION = 0,
    KBD_INPUT_CONTEXT_TEXT = 1,
    KBD_INPUT_CONTEXT_GAME = 2,
} cp0_keyboard_input_context_t;

// key state
#define KBD_KEY_RELEASED  0
#define KBD_KEY_PRESSED   1
#define KBD_KEY_REPEATED  2

struct key_item {
    uint32_t key_code;      // Linux evdev key code
    uint32_t keysym;        // primary XKB keysym (xkb_keysym_t)
    uint32_t codepoint;     // corresponding Unicode code point, or 0 if none
    uint32_t mods;          // modifier bitmask (KBD_MOD_*)
    int      key_state;     // 0=released, 1=pressed, 2=repeat
    char     sym_name[65];  // XKB keysym name
    char     utf8[16];      // UTF-8 character (supports multi-byte compose output)
    char     flage;         // whether free is required
    uint32_t semantic_key;  // context-normalized KEY_* value
    cp0_keyboard_input_context_t input_context;
    STAILQ_ENTRY(key_item) entries;
};

typedef void (*cp0_keyboard_key_handler_t)(const struct key_item *item);
typedef int (*cp0_keyboard_key_filter_t)(const struct key_item *item);

STAILQ_HEAD(keyboard_queue_t, key_item);
extern struct keyboard_queue_t keyboard_queue;
extern pthread_mutex_t keyboard_mutex;
extern volatile int LVGL_RUN_FLAGE;
extern volatile uint32_t LV_EVENT_KEYBOARD;

void *keyboard_read_thread(void *argv);
int cp0_keyboard_inject(uint32_t key_code, int key_state, uint32_t mods);
int cp0_keyboard_inject_text(const char *utf8);
/* One key as the reader thread queues it (F1 phase 1 item 4, the on-screen keyboard): key_code (Linux KEY_*, 0 for a
 * character without a key), state (pressed / released / repeated), mods and the UTF-8 it types (NULL or "" for none;
 * the code point is derived from it). Unlike cp0_keyboard_inject_text(), a caller sends the press AND the release,
 * both carrying the code and the text, so consumers that read either state see the key. Any thread; returns 0 or -1
 * (invalid state or UTF-8). */
int cp0_keyboard_inject_key(uint32_t key_code, int key_state, uint32_t mods, const char *utf8);

/* What the focused text field expects (F1 phase 1 item 5), for the on-screen keyboard and later the compat apps'
 * hint: set by a field when it starts editing, restored when it stops. Default TEXT. */
typedef enum {
    KBD_TEXT_HINT_TEXT = 0,
    KBD_TEXT_HINT_NUMBER = 1,
    KBD_TEXT_HINT_PASSWORD = 2,
} cp0_keyboard_text_hint_t;
void cp0_keyboard_set_text_hint(cp0_keyboard_text_hint_t hint);
cp0_keyboard_text_hint_t cp0_keyboard_get_text_hint(void);
void cp0_keyboard_set_global_key_handler(cp0_keyboard_key_handler_t handler);
/* LVGL thread only. After the screensaver filter, a nonzero result consumes the
 * key before custom events, global shortcuts, and native keypad delivery.
 * The item is borrowed for this call only. NULL removes the filter. */
void cp0_keyboard_set_key_filter(cp0_keyboard_key_filter_t filter);
cp0_keyboard_key_filter_t cp0_keyboard_get_key_filter(void);
/* Keep LV_EVENT_KEYBOARD delivery while suppressing the LVGL keypad group path. */
void cp0_keyboard_set_lvgl_keypad_intercept(int intercept);
int cp0_keyboard_get_lvgl_keypad_intercept(void);
/* Device builds: make the keyboard thread read every real keyboard (cp0_keyboard_presence.h: USB and
 * Bluetooth) in addition to LV_LINUX_KEYBOARD_DEVICE. Off by default, so an app built on cp0_lvgl keeps reading
 * only that device (under the launcher it is the uinput hub, which already mirrors every keyboard). The
 * launcher turns it on before init_input(); the thread reads it when it starts. */
void cp0_keyboard_set_read_all_keyboards(int enable);
int cp0_keyboard_get_read_all_keyboards(void);
void cp0_keyboard_set_input_context(cp0_keyboard_input_context_t context);
cp0_keyboard_input_context_t cp0_keyboard_get_input_context(void);
const char *kbd_state_name(int state);
void kbd_dump_keymap_table(void);
#ifdef __cplusplus
}
#endif
#endif
