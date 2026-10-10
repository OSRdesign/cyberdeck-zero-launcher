/* SPDX-License-Identifier: MIT
 *
 * Render harness: keyboard backend.
 *
 * On the device cp0_lvgl_keyboard.c reads libinput + xkbcommon on a thread and pushes key_items
 * into the shared queue; that file cannot be built here (no libinput/xkb headers). This file
 * provides the same globals and the injection entry point with the same semantics as
 * cp0_keyboard_inject() there (context, semantic key, control UTF-8). Everything downstream is the
 * real code: the queue (cp0_keyboard_queue.c) and the LVGL bridge (cp0_keyboard_lvgl_input.c),
 * which sends LV_EVENT_KEYBOARD to the active screen and feeds the LVGL keypad indev.
 */
#include "harness.h"

#include "cp0_keyboard_navigation_contract.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"

#include "cp0_keyboard_key_contract.h"
#include "cp0_keyboard_queue.h"
#include "cp0/cp0_keyboard_lvgl_input.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

struct keyboard_queue_t keyboard_queue;
pthread_mutex_t keyboard_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int LVGL_RUN_FLAGE = 1;
volatile uint32_t LV_EVENT_KEYBOARD;

static cp0_keyboard_input_context_t input_context = KBD_INPUT_CONTEXT_NAVIGATION;

void cp0_keyboard_set_input_context(cp0_keyboard_input_context_t context)
{
    if (context < KBD_INPUT_CONTEXT_NAVIGATION || context > KBD_INPUT_CONTEXT_GAME)
        context = KBD_INPUT_CONTEXT_NAVIGATION;
    input_context = context;
}

cp0_keyboard_input_context_t cp0_keyboard_get_input_context(void)
{
    return input_context;
}

int cp0_keyboard_inject(uint32_t key_code, int key_state, uint32_t mods)
{
    if (key_state != KBD_KEY_RELEASED && key_state != KBD_KEY_PRESSED && key_state != KBD_KEY_REPEATED)
        return -1;
    struct key_item item;
    memset(&item, 0, sizeof(item));
    item.key_code = key_code;
    item.key_state = key_state;
    item.mods = mods;
    item.input_context = cp0_keyboard_get_input_context();
    item.semantic_key = cp0_keyboard_semantic_key(key_code, item.input_context);
    const char *ctrl = cp0_keyboard_control_utf8(key_code);
    if (ctrl) snprintf(item.utf8, sizeof(item.utf8), "%s", ctrl);
    snprintf(item.sym_name, sizeof(item.sym_name), "RPC_%u", key_code);
    return cp0_keyboard_queue_push(&item) == 0 ? 0 : -1;
}

/* A printable key as the reader thread queues it: key code plus the character it typed. */
int harness_keyboard_inject_text(uint32_t key_code, int key_state, const char *utf8)
{
    struct key_item item;
    memset(&item, 0, sizeof(item));
    item.key_code = key_code;
    item.key_state = key_state;
    item.input_context = cp0_keyboard_get_input_context();
    item.semantic_key = cp0_keyboard_semantic_key(key_code, item.input_context);
    if (utf8) snprintf(item.utf8, sizeof(item.utf8), "%s", utf8);
    if (utf8 && utf8[0] && !utf8[1]) item.codepoint = (unsigned char)utf8[0];
    snprintf(item.sym_name, sizeof(item.sym_name), "HARNESS_%u", key_code);
    return cp0_keyboard_queue_push(&item) == 0 ? 0 : -1;
}

void harness_keyboard_init(void)
{
    cp0_keyboard_queue_init();
    if (LV_EVENT_KEYBOARD == 0) LV_EVENT_KEYBOARD = lv_event_register_id();
    cp0_keyboard_create_lvgl_input_devices();
}
