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
 *   Super (left / right Meta) pressed and released with no other key in between: Home, marked as a Super tap (with its
 *   press time, for SuperTaps below); KEY_HOMEPAGE: Home
 *   Super held kSuperHoldMs with no other key pressed meanwhile (task c-7, owner decision 2026-10-24)    Switcher, from
 *   tick(); its release is then no Home. Any other key pressed while Super is down cancels it (Super+L, and the FZ-M1's
 *   rotation-lock button, which sends Super, F14, F14 up, Super up); the other key is not consumed. Nothing starts while
 *   Alt is held or a session runs.
 * `consume` tells the launcher's key filter to keep the key from its pages: every key of a session (Tab, Esc,
 * Backspace, the closing Alt release and the release of a consumed key). Auto-repeat starts nothing. Pure, no system
 * call, no clock of its own (the caller passes milliseconds of one steady clock).
 *
 * SuperTaps (task c-7.5, owner decision 2026-10-24: the FZ-M1's Windows button is a pulse, down and up at the same
 * instant, so it cannot be held): two Super taps whose presses are at most kSuperDoubleTapMs apart open the switcher.
 * The first tap acts at once as before (Home from an app, nothing on the launcher): no added latency. The taps may come
 * from the two readers (the first from the supervisor's, while an app is in front, the second from the launcher's once
 * it took the panel back), so the supervisor keeps one SuperTaps for both.
 * Unit-tested by tests/test_switch_keys.cpp.
 */
#pragma once

#include <cstdint>
#include <string>

namespace multitask {

/* Super held this long alone opens the switcher (task c-7). */
inline constexpr std::uint32_t kSuperHoldMs = 600;
/* Two Super taps whose presses are at most this far apart open the switcher (task c-7.5). */
inline constexpr std::uint32_t kSuperDoubleTapMs = 450;
/* Two readers' reports of one tap (the hand-over between them) are closer than this: the same tap. */
inline constexpr std::uint32_t kSuperSameTapMs = 40;

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

enum class SwitchEvent : std::uint8_t { None, Start, StartReverse, Next, Prev, Commit, Cancel, Home, Switcher };

const char *switch_event_name(SwitchEvent event);
/* "Esc", "BackSpace", "Tab", "Alt", "RightAlt", "Shift", "RightShift", "Super", "RightSuper", "HomePage", else
 * "code <n>": the names of the session's key trace in the log (non-text keys only have a name). */
std::string switch_key_name(unsigned code);

struct SwitchResult {
    SwitchEvent event = SwitchEvent::None;
    bool consume = false;
    bool super_tap = false;   /* Home from a Super tap (not KEY_HOMEPAGE) */
    std::uint64_t tap_ms = 0; /* ... its press time */
};

class SwitchKeys {
public:
    /* value: 1 press, 0 release, 2 auto-repeat (evdev); now_ms: when it was read (for the Super hold and taps). */
    SwitchResult key(unsigned code, int value, std::uint64_t now_ms = 0);
    /* The Super hold: Switcher once when Super has been held alone kSuperHoldMs at now_ms, else None. */
    SwitchEvent tick(std::uint64_t now_ms);
    /* When tick() has something to do: the hold's due time, 0 = no hold pending. */
    std::uint64_t hold_due() const;
    bool active() const { return active_; }
    bool alt_held() const { return alt_left_ || alt_right_; }
    /* Forget every key (a reader was closed or replaced). An active session ends without an event. */
    void reset();
    /* The Super key that is down starts no hold; its release is still a tap (the launcher's reader when the key may be
     * the one still held from the app as it took the panel back). */
    void block_hold();

private:
    bool shift_held() const { return shift_left_ || shift_right_; }
    bool hold_pending() const
    {
        return meta_down_ && meta_clean_ && !meta_fired_ && !meta_no_hold_ && !active_ && !alt_held();
    }

    bool alt_left_ = false, alt_right_ = false;
    bool shift_left_ = false, shift_right_ = false;
    bool active_ = false;
    bool meta_down_ = false;    /* a Super key is down */
    bool meta_clean_ = false;   /* ... and nothing else was pressed since */
    bool meta_fired_ = false;   /* ... and its hold opened the switcher (its release is no Home) */
    bool meta_no_hold_ = false; /* ... and it may not start a hold (block_hold) */
    std::uint64_t meta_at_ = 0; /* when it went down */
    bool swallow_tab_ = false; /* the Tab release of a consumed press */
    bool swallow_esc_ = false;       /* the Esc repeats and release of a Cancel */
    bool swallow_backspace_ = false; /* the Backspace repeats and release of a Cancel */
};

/* Task c-7.5: the double tap of Super, from the press times of its taps. */
enum class TapVerdict : std::uint8_t {
    First,     /* a single tap (nothing before it, or the previous one too long ago: see too_late()) */
    Double,    /* the second of two taps at most kSuperDoubleTapMs apart: open the switcher */
    Ignored,   /* within kSuperDoubleTapMs after a double tap: a third tap starts nothing */
    Duplicate, /* the same tap reported again (by the other reader, less than kSuperSameTapMs apart) */
};

const char *tap_verdict_name(TapVerdict verdict);

class SuperTaps {
public:
    TapVerdict tap(std::uint64_t press_ms);
    /* Milliseconds between this tap and the one it was compared with (the previous tap, or the double tap for Ignored). */
    std::uint64_t gap() const { return gap_; }
    /* First only: there was a previous tap, too long ago. */
    bool too_late() const { return too_late_; }
    /* Forget the taps (a hold opened the switcher). */
    void reset() { *this = SuperTaps(); }

private:
    std::uint64_t any_ = 0;      /* press time of the last tap counted, whatever its verdict (for Duplicate) */
    bool have_any_ = false;
    std::uint64_t last_ = 0;     /* press time of the pending first tap */
    bool have_last_ = false;
    std::uint64_t double_at_ = 0; /* press time of the last double tap */
    bool have_double_ = false;
    std::uint64_t gap_ = 0;
    bool too_late_ = false;
};

} // namespace multitask
