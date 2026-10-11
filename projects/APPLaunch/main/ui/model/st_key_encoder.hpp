/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class STKeyEncoder
{
public:
    static constexpr size_t MAX_TEXT_BYTES = 15;

    // The bytes a key writes to the terminal's PTY. A key's text (utf8) wins; a key that comes with Ctrl and no text
    // (the on-screen keyboard's Ctrl+C: KEY_C + KBD_MOD_CTRL, no text, task 014 P4b) writes its control byte
    // (ctrl_byte), as a physical keyboard's Ctrl+C writes the "\x03" its layout gives.
    static std::string encode(uint32_t evdev_key, const char *utf8,
                              bool application_cursor_mode, uint32_t modifiers = 0);
    // The C0 control byte of Ctrl + the US-layout key (xterm): A-Z 0x01-0x1a; Space, 2 / @ 0x00; [ / { and 3 0x1b;
    // \ / | and 4 0x1c; ] / } and 5 0x1d; 6 / ^ 0x1e; - / _, 7 and / 0x1f; 8 and ? (Shift + /) 0x7f. -1: none.
    static int ctrl_byte(uint32_t evdev_key, uint32_t modifiers);
    static bool should_forward_event(uint32_t evdev_key, int key_state);
    static int scrollback_direction(uint32_t evdev_key, uint32_t modifiers,
                                    bool shift_down);
};
