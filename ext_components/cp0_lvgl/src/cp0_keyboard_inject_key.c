/*
 * SPDX-License-Identifier: MIT
 *
 * Press + release key injection with text (F1 phase 1 item 4) and the text field hint (item 5); see keyboard_input.h.
 * Every backend (device, SDL, the render harness) shares the queue (cp0_keyboard_queue.c) and the input context, so
 * this file is the same for all of them: a key queued here reaches LV_EVENT_KEYBOARD and the LVGL keypad exactly as a
 * key of the reader thread does, and an injected Esc moves the Esc state (hold-to-exit) like the toolbar's.
 */

#include "cp0_keyboard_navigation_contract.h"
#include "cp0_keyboard_queue.h"
#include "cp0_keyboard_text.h"
#include "keyboard_input.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

int cp0_keyboard_inject_key(uint32_t key_code, int key_state, uint32_t mods, const char *utf8)
{
    if (key_state != KBD_KEY_RELEASED && key_state != KBD_KEY_PRESSED && key_state != KBD_KEY_REPEATED) return -1;
    struct key_item item;
    memset(&item, 0, sizeof(item));
    if (utf8 && utf8[0]) {
        size_t length = strlen(utf8);
        if (length >= sizeof(item.utf8) || cp0_keyboard_utf8_validate(utf8) != 0) return -1;
        memcpy(item.utf8, utf8, length);
        size_t first = 0;
        uint32_t codepoint = 0;
        if (cp0_keyboard_utf8_decode_one(utf8, &codepoint, &first) == 0 && first == length) item.codepoint = codepoint;
    }
    item.key_code = key_code;
    item.key_state = key_state;
    item.mods = mods;
    item.input_context = cp0_keyboard_get_input_context();
    item.semantic_key = cp0_keyboard_semantic_key(key_code, item.input_context);
    snprintf(item.sym_name, sizeof(item.sym_name), "OSK_%u", key_code);
    return cp0_keyboard_queue_push(&item) == 0 ? 0 : -1;
}

static atomic_int text_hint = KBD_TEXT_HINT_TEXT;

void cp0_keyboard_set_text_hint(cp0_keyboard_text_hint_t hint)
{
    if (hint < KBD_TEXT_HINT_TEXT || hint > KBD_TEXT_HINT_PASSWORD) hint = KBD_TEXT_HINT_TEXT;
    atomic_store_explicit(&text_hint, (int)hint, memory_order_release);
}

cp0_keyboard_text_hint_t cp0_keyboard_get_text_hint(void)
{
    return (cp0_keyboard_text_hint_t)atomic_load_explicit(&text_hint, memory_order_acquire);
}
