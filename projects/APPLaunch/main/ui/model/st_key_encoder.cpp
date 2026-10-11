/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "st_key_encoder.hpp"

#include "input_keys.h"
#include "keyboard_input.h"

#include <cstring>

/* input_keys.h defines only part of the codes outside Linux (SDL builds on macOS / Windows): the US punctuation keys
 * with their Linux values (as cp0_ui_osk.c). */
#ifndef KEY_LEFTBRACE
#define KEY_LEFTBRACE 26
#endif
#ifndef KEY_RIGHTBRACE
#define KEY_RIGHTBRACE 27
#endif
#ifndef KEY_BACKSLASH
#define KEY_BACKSLASH 43
#endif
#ifndef KEY_SLASH
#define KEY_SLASH 53
#endif

std::string STKeyEncoder::encode(uint32_t evdev_key, const char *utf8,
                                 bool application_cursor_mode, uint32_t modifiers)
{
    switch (evdev_key) {
    case KEY_ENTER:
    case KEY_KPENTER: return "\r";
    case KEY_BACKSPACE: return std::string(1, static_cast<char>(0x7f));
    case KEY_TAB: return "\t";
    case KEY_ESC: return "\x1b";
    case KEY_UP: return application_cursor_mode ? "\x1bOA" : "\x1b[A";
    case KEY_DOWN: return application_cursor_mode ? "\x1bOB" : "\x1b[B";
    case KEY_RIGHT: return application_cursor_mode ? "\x1bOC" : "\x1b[C";
    case KEY_LEFT: return application_cursor_mode ? "\x1bOD" : "\x1b[D";
    case KEY_INSERT: return {};
    default: break;
    }

    const size_t length = utf8 ? std::strlen(utf8) : 0;
    if (length == 0) {
        // no text: Ctrl + a key (the on-screen keyboard sends Ctrl combinations as the key code alone)
        const int control = ctrl_byte(evdev_key, modifiers);
        return control >= 0 ? std::string(1, static_cast<char>(control)) : std::string();
    }
    if (length > MAX_TEXT_BYTES) return {};
    return std::string(utf8, length);
}

int STKeyEncoder::ctrl_byte(uint32_t evdev_key, uint32_t modifiers)
{
    if (!(modifiers & KBD_MOD_CTRL)) return -1;
    static const struct {
        uint32_t key;
        char letter;
    } LETTERS[] = {
        {KEY_A, 'a'}, {KEY_B, 'b'}, {KEY_C, 'c'}, {KEY_D, 'd'}, {KEY_E, 'e'}, {KEY_F, 'f'}, {KEY_G, 'g'},
        {KEY_H, 'h'}, {KEY_I, 'i'}, {KEY_J, 'j'}, {KEY_K, 'k'}, {KEY_L, 'l'}, {KEY_M, 'm'}, {KEY_N, 'n'},
        {KEY_O, 'o'}, {KEY_P, 'p'}, {KEY_Q, 'q'}, {KEY_R, 'r'}, {KEY_S, 's'}, {KEY_T, 't'}, {KEY_U, 'u'},
        {KEY_V, 'v'}, {KEY_W, 'w'}, {KEY_X, 'x'}, {KEY_Y, 'y'}, {KEY_Z, 'z'},
    };
    for (const auto &entry : LETTERS)
        if (entry.key == evdev_key) return entry.letter - 'a' + 1;
    switch (evdev_key) {
    case KEY_SPACE:
    case KEY_2: return 0x00;
    case KEY_LEFTBRACE:
    case KEY_3: return 0x1b;
    case KEY_BACKSLASH:
    case KEY_4: return 0x1c;
    case KEY_RIGHTBRACE:
    case KEY_5: return 0x1d;
    case KEY_6: return 0x1e;
    case KEY_MINUS:
    case KEY_7: return 0x1f;
    case KEY_SLASH: return (modifiers & KBD_MOD_SHIFT) ? 0x7f : 0x1f;
    case KEY_8: return 0x7f;
    default: return -1;
    }
}

bool STKeyEncoder::should_forward_event(uint32_t evdev_key, int key_state)
{
    if (key_state != KBD_KEY_PRESSED && key_state != KBD_KEY_REPEATED) return false;

    // UISTPage does not implement the Insert escape sequence.
    if (evdev_key == KEY_INSERT) return false;

    // Repeating Enter queues an additional shell submission for one physical press.
    if (evdev_key == KEY_ENTER || evdev_key == KEY_KPENTER)
        return key_state == KBD_KEY_PRESSED;
    return true;
}

int STKeyEncoder::scrollback_direction(uint32_t evdev_key, uint32_t modifiers,
                                       bool shift_down)
{
    const bool shift = shift_down || (modifiers & KBD_MOD_SHIFT) != 0;
    constexpr uint32_t LEGACY_MODIFIERS = KBD_MOD_CTRL | KBD_MOD_ALT;
    const bool ctrl_alt = (modifiers & LEGACY_MODIFIERS) == LEGACY_MODIFIERS;
    if (!shift && !ctrl_alt) return 0;

    if (evdev_key == KEY_PAGEUP) return 1;
    if (evdev_key == KEY_PAGEDOWN) return -1;
    return 0;
}
