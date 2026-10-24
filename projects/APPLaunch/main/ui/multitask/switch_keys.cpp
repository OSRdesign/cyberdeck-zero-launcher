/*
 * SPDX-License-Identifier: MIT
 *
 * Alt+Tab / Super key session (see switch_keys.hpp).
 */
#include "switch_keys.hpp"

namespace multitask {

const char *switch_event_name(SwitchEvent event)
{
    switch (event) {
    case SwitchEvent::None: return "none";
    case SwitchEvent::Start: return "start";
    case SwitchEvent::StartReverse: return "start-reverse";
    case SwitchEvent::Next: return "next";
    case SwitchEvent::Prev: return "prev";
    case SwitchEvent::Commit: return "commit";
    case SwitchEvent::Cancel: return "cancel";
    case SwitchEvent::Home: return "home";
    case SwitchEvent::Switcher: return "switcher";
    }
    return "none";
}

std::string switch_key_name(unsigned code)
{
    switch (code) {
    case keys::kEsc: return "Esc";
    case keys::kBackspace: return "BackSpace";
    case keys::kTab: return "Tab";
    case keys::kLeftAlt: return "Alt";
    case keys::kRightAlt: return "RightAlt";
    case keys::kLeftShift: return "Shift";
    case keys::kRightShift: return "RightShift";
    case keys::kLeftMeta: return "Super";
    case keys::kRightMeta: return "RightSuper";
    case keys::kHomePage: return "HomePage";
    default: break;
    }
    return "code " + std::to_string(code);
}

void SwitchKeys::reset()
{
    *this = SwitchKeys();
}

void SwitchKeys::block_hold()
{
    meta_no_hold_ = true;
}

const char *tap_verdict_name(TapVerdict verdict)
{
    switch (verdict) {
    case TapVerdict::First: return "first";
    case TapVerdict::Double: return "double";
    case TapVerdict::Ignored: return "ignored";
    case TapVerdict::Duplicate: return "duplicate";
    }
    return "first";
}

TapVerdict SuperTaps::tap(std::uint64_t press_ms)
{
    gap_ = 0;
    too_late_ = false;
    /* the same tap seen by the other reader (the hand-over from the supervisor's to the launcher's) */
    if (have_any_) {
        const std::uint64_t d = press_ms >= any_ ? press_ms - any_ : any_ - press_ms;
        if (d < kSuperSameTapMs) {
            gap_ = d;
            return TapVerdict::Duplicate;
        }
    }
    any_ = press_ms;
    have_any_ = true;
    /* a third tap (or more) right after a double tap starts nothing: the window has to pass first */
    if (have_double_ && press_ms >= double_at_ && press_ms - double_at_ <= kSuperDoubleTapMs) {
        gap_ = press_ms - double_at_;
        return TapVerdict::Ignored;
    }
    if (have_last_ && press_ms >= last_ && press_ms - last_ <= kSuperDoubleTapMs) {
        gap_ = press_ms - last_;
        have_last_ = false;
        have_double_ = true;
        double_at_ = press_ms;
        return TapVerdict::Double;
    }
    if (have_last_) {
        too_late_ = true;
        gap_ = press_ms >= last_ ? press_ms - last_ : 0;
    }
    last_ = press_ms;
    have_last_ = true;
    return TapVerdict::First;
}

SwitchEvent SwitchKeys::tick(std::uint64_t now_ms)
{
    if (!hold_pending() || now_ms < meta_at_ + kSuperHoldMs) return SwitchEvent::None;
    meta_fired_ = true;
    return SwitchEvent::Switcher;
}

std::uint64_t SwitchKeys::hold_due() const
{
    return hold_pending() ? meta_at_ + kSuperHoldMs : 0;
}

SwitchResult SwitchKeys::key(unsigned code, int value, std::uint64_t now_ms)
{
    SwitchResult result;
    const bool press = value == 1, release = value == 0, repeat = value == 2;
    if (!press && !release && !repeat) return result;

    /* a Super tap: any other key pressed while it is down makes it a chord (Super+x), not Home; nor a hold (Super+F14,
     * the tablet's rotation lock) */
    const bool is_meta = code == keys::kLeftMeta || code == keys::kRightMeta;
    if (press && !is_meta && meta_down_) meta_clean_ = false;

    switch (code) {
    case keys::kLeftAlt:
    case keys::kRightAlt:
        if (repeat) return result;
        (code == keys::kLeftAlt ? alt_left_ : alt_right_) = press;
        if (release && active_ && !alt_held()) {
            active_ = false;
            result.event = SwitchEvent::Commit;
            result.consume = true;
        }
        return result;
    case keys::kLeftShift:
    case keys::kRightShift:
        if (!repeat) (code == keys::kLeftShift ? shift_left_ : shift_right_) = press;
        return result;
    case keys::kLeftMeta:
    case keys::kRightMeta:
        if (repeat) return result;
        if (press) {
            meta_down_ = true;
            meta_clean_ = true;
            meta_fired_ = false;
            meta_no_hold_ = false;
            meta_at_ = now_ms; /* the hold starts (again: a press never seen released restarts it) */
        } else if (meta_down_) {
            if (meta_clean_ && !meta_fired_) {
                result.event = SwitchEvent::Home;
                result.super_tap = true; /* task c-7.5: a tap, for the double tap */
                result.tap_ms = meta_at_;
            }
            meta_down_ = false;
            meta_clean_ = false;
            meta_fired_ = false;
            meta_no_hold_ = false;
        }
        return result;
    case keys::kHomePage:
        if (press) result.event = SwitchEvent::Home;
        return result;
    case keys::kTab:
        if (press && alt_held()) {
            result.consume = true;
            swallow_tab_ = true;
            if (!active_) {
                active_ = true;
                result.event = shift_held() ? SwitchEvent::StartReverse : SwitchEvent::Start;
            } else {
                result.event = shift_held() ? SwitchEvent::Prev : SwitchEvent::Next;
            }
            return result;
        }
        if (repeat && active_) result.consume = true;
        if (release && swallow_tab_) {
            swallow_tab_ = false;
            result.consume = true;
        }
        return result;
    case keys::kEsc:
    case keys::kBackspace: {
        /* Backspace cancels too (owner decision 2026-10-20: the M4 keyboard cannot send Esc while Alt is held). Its
         * repeats and release are the session's as well; outside a session both keys stay the page's / app's. */
        bool &swallow = code == keys::kEsc ? swallow_esc_ : swallow_backspace_;
        if (press && active_) {
            active_ = false;
            swallow = true;
            result.event = SwitchEvent::Cancel;
            result.consume = true;
            return result;
        }
        if (release && swallow) {
            swallow = false;
            result.consume = true;
        }
        if (repeat && swallow) result.consume = true;
        return result;
    }
    default:
        /* any other key while a session runs belongs to the session (the pages do not see it) */
        if (active_) result.consume = true;
        return result;
    }
}

} // namespace multitask
