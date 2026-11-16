/*
 * SPDX-License-Identifier: MIT
 *
 * The `screen off` / `screen on` state of launcher.sock (docs/APP-CONTRACT.md section 14): which app switched the
 * backlight fully off, and the rules that bring it back. Pure logic over a small hardware interface: no LVGL, no I/O,
 * so the state machine is tested on its own (tests/test_screen_gate.cpp).
 *
 * State: off, requested by client pid X, with the raw level to restore. The backlight is never left off:
 *   - X sends `screen on`                           -> restored;
 *   - the panel changes hands (app hidden, another app in front, launcher in front) -> release();
 *   - X is gone (crash, quit, kill; poll() looks at it)                             -> restored;
 *   - the launcher stops                                                            -> release().
 * Nothing is saved to the settings: the hardware adapter writes the raw backlight only.
 */
#pragma once

#include <atomic>
#include <mutex>

namespace app_request {

/* What the gate needs from the board. All called with the gate's lock held: they must not call back into the gate. */
class ScreenHw {
public:
    virtual ~ScreenHw() = default;
    /* Switches the backlight fully off. The raw level to hand back to restore(), > 0, or -1 when it cannot be done. */
    virtual int suspend() = 0;
    virtual void restore(int raw) = 0;
    /* True while the process is running (not a zombie). */
    virtual bool alive(int pid) = 0;
};

class ScreenGate {
public:
    explicit ScreenGate(ScreenHw &hw) : hw_(hw) {}

    enum class Result {
        Ok,     /* done (also: already in the asked state) */
        Busy,   /* another client holds the screen off */
        Failed, /* the hardware could not switch it */
    };

    /* `screen off` from pid. Twice from the same pid is Ok and changes nothing. */
    Result off(int pid);
    /* `screen on` from pid. Without a prior off: Ok, nothing happens. While another pid holds it off: Busy. */
    Result on(int pid);
    /* The panel changes hands, or the launcher stops: restores whoever holds it off. True when it was off. */
    bool release();
    /* The holder is gone: restores. True when it did. Cheap; call about once a second. */
    bool poll();

    /* Lock-free: true from before the hardware is touched until the restore is written. */
    bool is_off() const { return off_.load(); }
    /* The pid that holds it off, 0 when on. */
    int owner() const;

private:
    void restore_locked();

    ScreenHw &hw_;
    mutable std::mutex mutex_;
    std::atomic<bool> off_{false};
    int owner_ = 0;
    int raw_ = -1;
};

} // namespace app_request
