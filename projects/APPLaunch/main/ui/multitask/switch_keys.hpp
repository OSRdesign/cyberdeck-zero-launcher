/*
 * SPDX-License-Identifier: MIT
 *
 * The keyboard side of app switching (task 015, study 026 section 5.2 K1/K2, decision D4), as a pure model fed with
 * Linux key codes from either reader: the supervisor's evdev watcher while an app is in front, the launcher's key
 * filter while the launcher is in front.
 *   Alt (left or right) held + Tab        Start (Shift held: StartReverse); further Tabs Next / Prev (Shift)
 *   Alt released during a session          Commit (switch to the selection)
 *   Esc or Backspace during a session      Cancel (back to where the session started; Backspace: owner decision
 *                                          2026-10-20, the M4 keyboard cannot send Esc while Alt is held)
 *   Super (left / right Meta) pressed and released with no other key in between, or KEY_HOMEPAGE   Home
 * `consume` tells the launcher's key filter to keep the key from its pages: every key of a session (Tab, Esc,
 * Backspace, the closing Alt release and the release of a consumed key). Auto-repeat starts nothing. Pure, no system
 * call.
 * Unit-tested by tests/test_switch_keys.cpp.
 */
#pragma once

#include <cstdint>
#include <string>

namespace multitask {

/* Linux input-event-codes.h values (stable kernel ABI), so this file builds without Linux headers. */
namespace keys {
inline constexpr unsigned kEsc = 1;
inline constexpr unsigned kBackspace = 14;
inline constexpr unsigned kTab = 15;
inline constexpr unsigned kLeftShift = 42;
inline constexpr unsigned kRightShift = 54;
inline constexpr unsigned kLeftAlt = 56;
inline constexpr unsigned kRightAlt = 100;
inline constexpr unsigned kLeftMeta = 125;
inline constexpr unsigned kRightMeta = 126;
inline constexpr unsigned kHomePage = 172;
} // namespace keys

enum class SwitchEvent : std::uint8_t { None, Start, StartReverse, Next, Prev, Commit, Cancel, Home };

const char *switch_event_name(SwitchEvent event);
/* "Esc", "BackSpace", "Tab", "Alt", "RightAlt", "Shift", "RightShift", "Super", "RightSuper", "HomePage", else
 * "code <n>": the names of the session's key trace in the log (non-text keys only have a name). */
std::string switch_key_name(unsigned code);

struct SwitchResult {
    SwitchEvent event = SwitchEvent::None;
    bool consume = false;
};

class SwitchKeys {
public:
    /* value: 1 press, 0 release, 2 auto-repeat (evdev). */
    SwitchResult key(unsigned code, int value);
    bool active() const { return active_; }
    bool alt_held() const { return alt_left_ || alt_right_; }
    /* Forget every key (a reader was closed or replaced). An active session ends without an event. */
    void reset();

private:
    bool shift_held() const { return shift_left_ || shift_right_; }

    bool alt_left_ = false, alt_right_ = false;
    bool shift_left_ = false, shift_right_ = false;
    bool active_ = false;
    bool meta_down_ = false;   /* a Super key is down */
    bool meta_clean_ = false;  /* ... and nothing else was pressed since */
    bool swallow_tab_ = false; /* the Tab release of a consumed press */
    bool swallow_esc_ = false;       /* the Esc repeats and release of a Cancel */
    bool swallow_backspace_ = false; /* the Backspace repeats and release of a Cancel */
};

} // namespace multitask
