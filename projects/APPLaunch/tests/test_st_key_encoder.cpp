/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "../main/ui/model/st_key_encoder.hpp"
#include "../main/ui/model/st_page_contract.hpp"

#include "input_keys.h"
#include "keyboard_input.h"

#include <cassert>
#include <string>

int main()
{
    static_assert(noexcept(st_callback_failure_state()));
    constexpr auto callback_failure = st_callback_failure_state();
    static_assert(!callback_failure.terminal_active);
    static_assert(callback_failure.waiting_key_to_exit);
    static_assert(callback_failure.home_hold_status == 0);

    int child_status = 7;
    assert(st_parse_child_status("0", child_status) && child_status == 0);
    assert(st_parse_child_status("-15", child_status) && child_status == -15);
    assert(!st_parse_child_status("", child_status));
    assert(!st_parse_child_status("12 exited", child_status));
    assert(!st_parse_child_status(" 12", child_status));
    assert(!st_parse_child_status("99999999999999999999", child_status));

    int container = 0;
    int canvas = 0;
    int cursor = 0;
    int renderer_part = 0;
    assert(st_page_renderer_ready(&container, &canvas, &cursor));
    assert(!st_page_renderer_ready(nullptr, &canvas, &cursor));
    assert(!st_page_renderer_ready(&container, nullptr, &cursor));
    assert(!st_page_renderer_ready(&container, &canvas, nullptr));
    assert(st_page_renderer_ready(&container, &canvas, &cursor, &renderer_part));
    assert(!st_page_renderer_ready(&container, &canvas, &cursor, nullptr));

    assert(STKeyEncoder::encode(KEY_ENTER, nullptr, false) == "\r");
    assert(STKeyEncoder::encode(KEY_KPENTER, nullptr, false) == "\r");
    assert(STKeyEncoder::encode(KEY_BACKSPACE, nullptr, false) ==
           std::string(1, static_cast<char>(0x7f)));
    assert(STKeyEncoder::encode(KEY_ESC, nullptr, false) == "\x1b");

    assert(STKeyEncoder::encode(KEY_UP, nullptr, false) == "\x1b[A");
    assert(STKeyEncoder::encode(KEY_DOWN, nullptr, false) == "\x1b[B");
    assert(STKeyEncoder::encode(KEY_RIGHT, nullptr, false) == "\x1b[C");
    assert(STKeyEncoder::encode(KEY_LEFT, nullptr, false) == "\x1b[D");
    assert(STKeyEncoder::encode(KEY_UP, nullptr, true) == "\x1bOA");
    assert(STKeyEncoder::encode(KEY_DOWN, nullptr, true) == "\x1bOB");
    assert(STKeyEncoder::encode(KEY_RIGHT, nullptr, true) == "\x1bOC");
    assert(STKeyEncoder::encode(KEY_LEFT, nullptr, true) == "\x1bOD");
    // Insert is unsupported by UISTPage and must not write ESC[2~ to the PTY.
    assert(STKeyEncoder::encode(KEY_INSERT, "\033[2~", false).empty());

    assert(STKeyEncoder::encode(KEY_A, "a", false) == "a");
    assert(STKeyEncoder::encode(KEY_A, "\xe4\xbd\xa0", false) == "\xe4\xbd\xa0");
    assert(STKeyEncoder::encode(KEY_A, nullptr, false).empty());
    assert(STKeyEncoder::encode(KEY_A, "", false).empty());

    const std::string maximum(STKeyEncoder::MAX_TEXT_BYTES, 'x');
    const std::string too_long(STKeyEncoder::MAX_TEXT_BYTES + 1, 'x');
    assert(STKeyEncoder::encode(KEY_A, maximum.c_str(), false) == maximum);
    assert(STKeyEncoder::encode(KEY_A, too_long.c_str(), false).empty());

    // A recognized control key never falls through to the supplied text.
    assert(STKeyEncoder::encode(KEY_ENTER, "ignored", false) == "\r");

    // Tab is a tab character with or without text (the on-screen keyboard's Tab of a one-line field has no text).
    assert(STKeyEncoder::encode(KEY_TAB, nullptr, false) == "\t");
    assert(STKeyEncoder::encode(KEY_TAB, "\t", false) == "\t");
    assert(STKeyEncoder::encode(KEY_TAB, nullptr, false, KBD_MOD_CTRL) == "\t");

    // Ctrl + a key without text (the on-screen keyboard: key code + KBD_MOD_CTRL, no text, task 014 P4b): the C0
    // control byte of the US layout, as xterm.
    const auto ctrl = [](uint32_t key, uint32_t mods = KBD_MOD_CTRL) {
        return STKeyEncoder::encode(key, nullptr, false, mods);
    };
    assert(ctrl(KEY_C) == "\x03");
    assert(ctrl(KEY_D) == "\x04");
    assert(ctrl(KEY_Z) == "\x1a");
    assert(ctrl(KEY_L) == "\x0c");
    assert(ctrl(KEY_A) == "\x01");
    assert(ctrl(KEY_E) == "\x05");
    assert(ctrl(KEY_R) == "\x12");
    assert(ctrl(KEY_U) == "\x15");
    assert(ctrl(KEY_W) == "\x17");
    assert(ctrl(KEY_X) == "\x18");
    assert(ctrl(KEY_O) == "\x0f");
    assert(ctrl(KEY_C, KBD_MOD_CTRL | KBD_MOD_SHIFT) == "\x03"); /* Shift armed too: still ^C */
    assert(ctrl(KEY_SPACE) == std::string(1, '\0'));
    assert(ctrl(KEY_2, KBD_MOD_CTRL | KBD_MOD_SHIFT) == std::string(1, '\0')); /* Ctrl+@ */
    assert(ctrl(KEY_LEFTBRACE) == "\x1b");
    assert(ctrl(KEY_BACKSLASH) == "\x1c");
    assert(ctrl(KEY_RIGHTBRACE) == "\x1d");
    assert(ctrl(KEY_6, KBD_MOD_CTRL | KBD_MOD_SHIFT) == "\x1e"); /* Ctrl+^ */
    assert(ctrl(KEY_MINUS, KBD_MOD_CTRL | KBD_MOD_SHIFT) == "\x1f"); /* Ctrl+_ */
    assert(ctrl(KEY_SLASH) == "\x1f");
    assert(ctrl(KEY_SLASH, KBD_MOD_CTRL | KBD_MOD_SHIFT) == "\x7f"); /* Ctrl+? */
    assert(ctrl(KEY_1).empty());
    assert(ctrl(KEY_DOT).empty());
    // without Ctrl a key without text writes nothing (unchanged)
    assert(STKeyEncoder::encode(KEY_C, nullptr, false, 0).empty());
    assert(STKeyEncoder::encode(KEY_C, "", false, KBD_MOD_SHIFT).empty());
    // a key's text wins over its control byte (a physical keyboard's layout gives Ctrl+C as "\x03" itself)
    assert(STKeyEncoder::encode(KEY_C, "\x03", false, KBD_MOD_CTRL) == "\x03");
    assert(STKeyEncoder::encode(KEY_C, "c", false, KBD_MOD_CTRL) == "c");
    // the control keys keep their own sequences with Ctrl (the on-screen arrows and Backspace under an armed Ctrl)
    assert(ctrl(KEY_UP) == "\x1b[A");
    assert(ctrl(KEY_BACKSPACE) == std::string(1, static_cast<char>(0x7f)));
    assert(ctrl(KEY_ENTER) == "\r");
    assert(ctrl(KEY_ESC) == "\x1b");
    assert(STKeyEncoder::ctrl_byte(KEY_C, 0) == -1);
    assert(STKeyEncoder::ctrl_byte(KEY_C, KBD_MOD_CTRL) == 3);

    assert(STKeyEncoder::should_forward_event(KEY_ENTER, KBD_KEY_PRESSED));
    assert(!STKeyEncoder::should_forward_event(KEY_ENTER, KBD_KEY_REPEATED));
    assert(!STKeyEncoder::should_forward_event(KEY_ENTER, KBD_KEY_RELEASED));
    assert(STKeyEncoder::should_forward_event(KEY_KPENTER, KBD_KEY_PRESSED));
    assert(!STKeyEncoder::should_forward_event(KEY_KPENTER, KBD_KEY_REPEATED));
    assert(!STKeyEncoder::should_forward_event(KEY_INSERT, KBD_KEY_PRESSED));
    assert(!STKeyEncoder::should_forward_event(KEY_INSERT, KBD_KEY_REPEATED));
    assert(STKeyEncoder::should_forward_event(KEY_A, KBD_KEY_PRESSED));
    assert(STKeyEncoder::should_forward_event(KEY_A, KBD_KEY_REPEATED));
    assert(!STKeyEncoder::should_forward_event(KEY_A, KBD_KEY_RELEASED));
    assert(!STKeyEncoder::should_forward_event(KEY_A, 99));

    assert(STKeyEncoder::scrollback_direction(KEY_PAGEUP, 0, false) == 0);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEDOWN, KBD_MOD_CTRL, false) == 0);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEDOWN, KBD_MOD_ALT, false) == 0);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEUP, KBD_MOD_SHIFT, false) == 1);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEDOWN, KBD_MOD_SHIFT, false) == -1);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEUP, 0, true) == 1);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEDOWN, 0, true) == -1);

    constexpr uint32_t ctrl_alt = KBD_MOD_CTRL | KBD_MOD_ALT;
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEUP, ctrl_alt, false) == 1);
    assert(STKeyEncoder::scrollback_direction(KEY_PAGEDOWN, ctrl_alt, false) == -1);
    assert(STKeyEncoder::scrollback_direction(KEY_A, ctrl_alt, false) == 0);
    return 0;
}
