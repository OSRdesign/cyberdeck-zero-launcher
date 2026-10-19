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
    }
    return "none";
}

void SwitchKeys::reset()
{
    *this = SwitchKeys();
}

SwitchResult SwitchKeys::key(unsigned code, int value)
{
    SwitchResult result;
    const bool press = value == 1, release = value == 0, repeat = value == 2;
    if (!press && !release && !repeat) return result;

    /* a Super tap: any other key pressed while it is down makes it a chord (Super+x), not Home */
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
        } else if (meta_down_) {
            if (meta_clean_) result.event = SwitchEvent::Home;
            meta_down_ = false;
            meta_clean_ = false;
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
        if (press && active_) {
            active_ = false;
            swallow_esc_ = true;
            result.event = SwitchEvent::Cancel;
            result.consume = true;
            return result;
        }
        if (release && swallow_esc_) {
            swallow_esc_ = false;
            result.consume = true;
        }
        if (repeat && swallow_esc_) result.consume = true;
        return result;
    default:
        /* any other key while a session runs belongs to the session (the pages do not see it) */
        if (active_) result.consume = true;
        return result;
    }
}

} // namespace multitask
