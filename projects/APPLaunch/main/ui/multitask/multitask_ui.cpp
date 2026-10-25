/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-b in the launcher (see multitask_ui.hpp).
 */
#include "multitask_ui.hpp"

#include "app_media.hpp"
#include "display_handoff.hpp"
#include "notice_queue.hpp"
#include "supervisor.hpp"
#include "switcher_model.hpp"
#include "switcher_view.hpp"

#include "cp0_display.h"
#include "cp0_esc_state.h"
#include "cp0_gesture_g1.h"
#include "cp0_gesture_g2.h"
#include "cp0_keyboard_presence.h"
#include "cp0_lvgl_app_runner.hpp"
#include "cp0_ui_metrics.h"
#include "cp0_ui_widgets.hpp"
#include "keyboard_input.h"
#include "launcher_toast.h"
#include "launcher_zone.hpp"
#include "native_ui.hpp"
#include "ui_screensaver.h"

/* cp0_lvgl sources that have no public header (as the external app runner uses them) */
#include "../../../../../ext_components/cp0_lvgl/src/cp0/cp0_esc_key_watch.hpp"
#include "../../../../../ext_components/cp0_lvgl/src/cp0/cp0_process_commands.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#if defined(__linux__)
#include <unistd.h>
#endif

/* the keyboard thread's pause / resume (cp0_lvgl_keyboard.c), weak as in the external app runner */
extern "C" void __attribute__((weak)) keyboard_pause(void) {}
extern "C" void __attribute__((weak)) keyboard_resume(void) {}
/* the launcher's "hold Esc" hint flag (ui_global_hint.cpp) */
extern "C" void __attribute__((weak)) ui_external_esc_hint(int visible) { (void)visible; }

namespace multitask_ui {
namespace {

using multitask::AppId;

/* ------------------------------------------------------------------------------------------------ ports */

class LauncherFb : public multitask::FramebufferPort {
public:
    void suspend() override { cp0_display_suspend(); }
    bool snapshot(std::vector<std::uint8_t> &out, int &page_y) override
    {
        const size_t n = cp0_display_fb_page_bytes();
        if (n == 0) return false;
        out.resize(n);
        return cp0_display_fb_snapshot(out.data(), out.size(), &page_y) == 0;
    }
    int restore(const std::vector<std::uint8_t> &in, int page_y) override
    {
        return cp0_display_fb_restore(in.data(), in.size(), page_y);
    }
    int backdrop(const std::vector<std::uint8_t> &frame) override
    {
        return cp0_display_fb_backdrop(frame.data(), frame.size(), kSheetBackdropPct);
    }
};

class LauncherInput : public multitask::InputPort {
public:
    void grab_keys(bool on) override { (void)cp0_keyboard_set_grab(on ? 1 : 0); }
    void grab_touch(bool on) override { (void)cp0_display_set_touch_grab(on ? 1 : 0); }
    void launcher_keys(bool on) override
    {
        if (on) keyboard_resume();
        else keyboard_pause();
    }
    multitask::DrainStats drain(int pgid) override { return multitask::drain_group_input(pgid, "/dev/input/event"); }
};

/* The keyboards the external app runner's Esc watcher reads: the explicit device and every keyboard the presence
 * watcher lists, re-attached every second or when the list changes (a Bluetooth keyboard waking up). */
class LauncherKeys : public multitask::KeyPort {
public:
    void open() override
    {
        if (open_) return;
        presence_ = cp0_keyboard_get_read_all_keyboards() ? cp0_kbd_presence_shared_acquire() : nullptr;
        media_ = presence_ && cp0_keyboard_get_read_media_keys(); /* task c-7: the media-key devices too */
        attach();
        open_ = true;
        app_media::raw_session(true);
    }
    void close() override
    {
        if (!open_) return;
        app_media::raw_session(false);
        sources_.close_all();
        if (presence_) cp0_kbd_presence_shared_release();
        presence_ = nullptr;
        open_ = false;
    }
    int poll(Listener listener, void *context) override
    {
        if (!open_) return -1;
        if (cp0_kbd_presence_watcher_generation(presence_) != seen_ ||
            (media_ && cp0_kbd_presence_watcher_media_generation(presence_) != seen_media_) ||
            multitask::Supervisor::now_ms() >= next_attach_)
            attach();
        /* report 035: the volume, brightness and mute keys are the launcher's even while an app is in front */
        Tee tee = {listener, context};
        return sources_.poll(&LauncherKeys::tee_key, &tee);
    }
    void esc_state(int held) override { cp0_esc_state_write(held); }
    void fds(std::vector<int> &out) override
    {
        int f[cp0_esc_key_watch::Sources::kMax];
        out.assign(f, f + sources_.fds(f, cp0_esc_key_watch::Sources::kMax));
    }

private:
    struct Tee {
        Listener listener;
        void *context;
    };
    static void tee_key(void *context, int code, int value)
    {
        app_media::raw_key(code, value);
        const Tee *tee = static_cast<const Tee *>(context);
        if (tee->listener) tee->listener(tee->context, code, value);
    }

    void attach()
    {
        const char *configured = std::getenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE");
        sources_.open(configured ? configured : "/dev/input/by-path/platform-3f804000.i2c-event");
        seen_ = cp0_kbd_presence_watcher_generation(presence_);
        if (presence_) {
            cp0_kbd_presence_list_t list;
            list.count = 0;
            seen_ = cp0_kbd_presence_watcher_snapshot(presence_, &list);
            for (unsigned i = 0; i < list.count; ++i) sources_.open(list.devices[i].path);
            if (media_) { /* their media keys only (not Esc nor Super); never grabbed (cp0_esc_key_watch.hpp) */
                seen_media_ = cp0_kbd_presence_watcher_media_snapshot(presence_, &list);
                for (unsigned i = 0; i < list.count && i < CP0_KBD_PRESENCE_MAX_MEDIA; ++i)
                    sources_.open(list.devices[i].path, true);
            }
        }
        next_attach_ = multitask::Supervisor::now_ms() + 1000; /* the supervisor wakes at least once a second */
    }

    cp0_esc_key_watch::Sources sources_;
    cp0_kbd_presence_watcher_t *presence_ = nullptr;
    bool media_ = false;
    unsigned seen_ = 0;
    unsigned seen_media_ = 0;
    std::uint64_t next_attach_ = 0;
    bool open_ = false;
};

struct UiEvent {
    enum class Kind { AppFront, LauncherFront, Repaint, Notice, Overlay, Switcher, Sheet, Apps } kind = Kind::Repaint;
    bool flag = false;
    std::string text;
    /* P-d */
    std::shared_ptr<const multitask::SwitcherInfo> info; /* Switcher */
    AppId id = multitask::kNoApp;                        /* Sheet: the held app, kNoApp = the sheet goes */
    std::vector<std::string> keys;                       /* Apps: the resident apps' Exec lines */
};

std::mutex s_events_mutex;
std::deque<UiEvent> s_events;
std::atomic<int> s_residents{0};

std::atomic<bool> s_pending{false}; /* the supervisor posted events: the main-loop hook takes them */

void post(UiEvent event)
{
    {
        std::lock_guard<std::mutex> lock(s_events_mutex);
        s_events.push_back(std::move(event));
    }
    s_pending.store(true);
    cp0_lvgl_wake(); /* the main loop turns and its hook takes the event at once */
}

class LauncherUi : public multitask::UiPort {
public:
    void app_front(AppId, const std::string &) override { post({UiEvent::Kind::AppFront, false, {}}); }
    void launcher_front(bool after_exit) override { post({UiEvent::Kind::LauncherFront, after_exit, {}}); }
    void repaint() override { post({UiEvent::Kind::Repaint, false, {}}); }
    void notice(const std::string &text) override { post({UiEvent::Kind::Notice, false, text}); }
    void overlay(const std::string &text) override { post({UiEvent::Kind::Overlay, false, text}); }
    void esc_hint(bool visible) override { ui_external_esc_hint(visible ? 1 : 0); }
    void residents(int count) override { s_residents.store(count); }
    void switcher(const multitask::SwitcherInfo &info) override
    {
        UiEvent e;
        e.kind = UiEvent::Kind::Switcher;
        e.info = std::make_shared<const multitask::SwitcherInfo>(info);
        post(std::move(e));
    }
    void sheet(AppId id, const std::string &name) override
    {
        UiEvent e;
        e.kind = UiEvent::Kind::Sheet;
        e.id = id;
        e.text = name;
        post(std::move(e));
    }
    void apps(const std::vector<multitask::SwitcherCard> &residents) override
    {
        UiEvent e;
        e.kind = UiEvent::Kind::Apps;
        for (const multitask::SwitcherCard &c : residents) e.keys.push_back(c.key);
        post(std::move(e));
    }
};

#if defined(__linux__)
void exec_child(const char *command, bool keep_root)
{
    if (keep_root) ::execlp("/bin/sh", "sh", "-c", command, static_cast<char *>(nullptr));
    else cp0_process_commands::exec_shell_as_configured_user(command);
    ::_exit(127);
}
#else
void exec_child(const char *, bool) {}
#endif

/* ------------------------------------------------------------------------------------------------ state */

Hooks s_hooks;
lv_timer_t *s_bridge = nullptr;
bool s_app_front = false;
std::uint64_t s_launcher_front_ms = 0; /* when the launcher last took the panel back (task c-7) */
bool s_overlay = false;        /* the Alt+Tab text is on the panel */
std::string s_overlay_text;    /* the Alt+Tab text wanted ("" = none); shown once the launcher is in front */
std::string s_overlay_shown;   /* the text last put on the toast for it */
multitask::NoticeQueue s_notices;
cp0_keyboard_key_filter_t s_prev_filter = nullptr;
multitask::SwitchKeys s_filter_keys;

std::unique_ptr<LauncherFb> s_fb;
std::unique_ptr<LauncherInput> s_input;
std::unique_ptr<LauncherKeys> s_keys;
std::unique_ptr<LauncherUi> s_ui;
std::unique_ptr<multitask::LinuxProcessPort> s_proc;
std::unique_ptr<multitask::EvdevTouchPort> s_touch;
std::unique_ptr<multitask::Supervisor> s_sup;

/* P-c: the launcher's own gestures while it is in front (LVGL thread): G1 on its touch reader, G2 on the status strip
 * of the active screen. While an app is in front the supervisor runs G1 on its passive reader. */
cp0_g1_t s_g1;
cp0_g2_t s_g2;
bool s_g1_on = false;

/* P-d: the switcher screen and the legacy sheet, each built when the supervisor opens it and deleted when it closes;
 * the launcher screen that was active before (a page or the grid) comes back after. */
bool s_switcher_ui = false;
std::unique_ptr<SwitcherScreen> s_switcher;
lv_obj_t *s_switcher_prev = nullptr;
bool s_switcher_session = false; /* an Alt+Tab session drives it: touch is ignored meanwhile (as P-b's text) */
std::unique_ptr<LegacySheet> s_sheet;
lv_obj_t *s_sheet_prev = nullptr;

/* While the switcher or the sheet is up the keypad has no input group: their keys come on the screen
 * (LV_EVENT_KEYBOARD, as the home grid's), and the group of a page left under them (Settings' list) must not get them
 * too (an Enter would also act there). The group comes back when they close. */
lv_group_t *s_keypad_group = nullptr;

void detach_keypad()
{
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    if (!indev) return;
    if (lv_group_t *group = lv_indev_get_group(indev)) s_keypad_group = group;
    lv_indev_set_group(indev, nullptr);
}

void restore_keypad()
{
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    if (indev && s_keypad_group && !lv_indev_get_group(indev)) lv_indev_set_group(indev, s_keypad_group);
    s_keypad_group = nullptr;
}

/* The screen that was active before `ours` comes back (or the home grid), so `ours` can be deleted. */
void leave_screen(lv_obj_t *ours, lv_obj_t *&prev)
{
    if (lv_screen_active() == ours) {
        if (prev && prev != ours && lv_obj_is_valid(prev)) lv_screen_load(prev);
        else if (s_hooks.show_home) s_hooks.show_home();
    }
    prev = nullptr;
}

/* The screen that will come back after `ours` closes: the one active now, unless it is ours. */
lv_obj_t *active_besides(lv_obj_t *ours)
{
    lv_obj_t *active = lv_screen_active();
    return active != ours ? active : nullptr;
}

void close_switcher_screen()
{
    if (!s_switcher) return;
    std::fprintf(stderr, "[mt] switcher screen down\n");
    leave_screen(s_switcher->screen(), s_switcher_prev);
    s_switcher.reset();
    restore_keypad();
    if (s_switcher_session) {
        s_switcher_session = false;
        if (!s_app_front) cp0_display_set_touch_swallow(2); /* until the finger in contact, if any, is lifted */
    }
}

void show_switcher(const multitask::SwitcherInfo &info)
{
    if (!info.open) {
        close_switcher_screen();
        return;
    }
    if (!s_switcher) {
        SwitcherEvents events;
        events.pick = [](std::intptr_t id) {
            if (s_sup) s_sup->switcher_pick(static_cast<AppId>(id));
        };
        events.close = [](std::intptr_t id) {
            if (s_sup) s_sup->switcher_close(static_cast<AppId>(id));
        };
        events.home = [] {
            if (s_sup) s_sup->switcher_home();
        };
        events.close_all = [] {
            if (s_sup) s_sup->switcher_close_all();
        };
        events.cancel = [] {
            if (s_sup) s_sup->switcher_cancel();
        };
        s_switcher.reset(new SwitcherScreen(std::move(events)));
        s_switcher_prev = active_besides(s_switcher->screen());
        launcher_zone::close_strip(); /* the PDA writing zone's key strip would cover the switcher's bar (report 041) */
        lv_screen_load(s_switcher->screen()); /* while an app is still in front it is drawn when the launcher is */
        detach_keypad();
        std::fprintf(stderr, "[mt] switcher screen up: %zu card(s), from %s%s\n", info.cards.size(),
                     info.source.empty() ? "-" : info.source.c_str(), s_app_front ? " (shown once the launcher is in front)" : "");
    }
    std::vector<CardView> cards;
    for (const multitask::SwitcherCard &c : info.cards) {
        CardView v;
        v.id = c.id;
        v.name = c.name;
        v.icon = native_ui::app_icon(c.key);
        v.label = multitask::card_label(c.state, c.origin);
        cards.push_back(std::move(v));
    }
    s_switcher->set_cards(cards);
    if (info.selected >= 0) s_switcher->select(info.selected);
    if (info.session != s_switcher_session) {
        s_switcher_session = info.session;
        if (!s_app_front) cp0_display_set_touch_swallow(info.session ? 1 : 2);
    }
}

/* Task c-7 (FZ-M1, 0.8.7-c7.2): the supervisor's switcher is open but another screen is in front of the panel (seen on
 * the tablet after a Super hold: the home grid, the switcher open in the supervisor's status; not reproduced off the
 * board). The switcher is put back in front and the log says what was there, so the state the user sees is always the
 * state the supervisor acts on. Never while an app is in front (the switcher waits for the launcher then). */
void keep_switcher_in_front()
{
    if (!s_switcher || s_app_front) return;
    lv_obj_t *ours = s_switcher->screen();
    lv_display_t *display = lv_obj_get_display(ours);
    lv_obj_t *active = display ? lv_display_get_screen_active(display) : nullptr;
    if (!display || active == ours) return;
    std::fprintf(stderr, "[mt] switcher screen was not in front (screen %p with %u children was): put back\n",
                 static_cast<void *>(active), active ? static_cast<unsigned>(lv_obj_get_child_count(active)) : 0u);
    lv_screen_load(ours);
    detach_keypad();
}

/* The sheet over the frozen app's frame: only its box is drawn (the native clip), the supervisor already wrote the
 * frame back dimmed around it. */
void show_sheet(AppId id, const std::string &name)
{
    if (id == multitask::kNoApp) {
        if (!s_sheet) return;
        cp0_display_set_native_clip(0, 0, 0, 0);
        leave_screen(s_sheet->screen(), s_sheet_prev);
        s_sheet.reset();
        restore_keypad();
        if (!s_app_front) { /* Close: the launcher takes the whole panel again (Keep: the app has it already) */
            lv_obj_invalidate(lv_screen_active());
            lv_obj_invalidate(lv_layer_top());
            ui_screensaver_set_foreground(1);
        }
        return;
    }
    if (s_sheet) return;
    s_sheet.reset(new LegacySheet(
        name,
        [] {
            if (s_sup) s_sup->sheet_answer(true);
        },
        [] {
            if (s_sup) s_sup->sheet_answer(false);
        }));
    s_sheet_prev = active_besides(s_sheet->screen());
    const lv_area_t box = s_sheet->box();
    cp0_display_set_native_clip(box.x1, box.y1, box.x2 - box.x1 + 1, box.y2 - box.y1 + 1);
    lv_screen_load(s_sheet->screen());
    detach_keypad();
}

/* The toast carries two things: the P-b stand-in for the switcher (one line of text while an Alt+Tab session runs,
 * touch ignored meanwhile; a session started inside an app asks for it before the app is hidden: it appears when the
 * launcher is in front) and the supervisor's notices, one at a time, each readable (NoticeQueue). */
void refresh_toast()
{
    const bool overlay_wanted = !s_app_front && !s_overlay_text.empty();
    /* P-d: notices wait while the switcher or the sheet is up (the toast would cover the bar; the sheet draws only its
     * box) */
    const multitask::NoticeQueue::Step step = s_notices.tick(
        multitask::Supervisor::now_ms(), !s_app_front && !overlay_wanted && !s_switcher && !s_sheet);
    if (overlay_wanted) {
        if (!s_overlay || s_overlay_shown != s_overlay_text) {
            launcher_toast().show_persistent(s_overlay_text.c_str());
            s_overlay_shown = s_overlay_text;
        }
        s_overlay = true;
        cp0_display_set_touch_swallow(1);
        return;
    }
    if (s_overlay) {
        launcher_toast().hide();
        if (!s_app_front) cp0_display_set_touch_swallow(2); /* until the finger in contact, if any, is lifted */
        s_overlay = false;
        s_overlay_shown.clear();
    }
    if (step.kind == multitask::NoticeQueue::Step::Kind::Show) launcher_toast().show_persistent(step.text.c_str());
    else if (step.kind == multitask::NoticeQueue::Step::Kind::Hide) launcher_toast().hide();
}

/* The bridge timer is only the fallback now: events arrive through the main-loop hook at once. Fast while notices
 * are on the toast (their timing), slow otherwise, very slow while an app is in front (deck finding 2026-10-20: a
 * 20 ms timer kept the launcher's loop turning 50 times a second behind the app). */
uint32_t s_bridge_period = 1000;

void tune_bridge()
{
    if (!s_bridge) return;
    const uint32_t period = s_app_front ? 2000 : s_notices.active() ? 100 : 1000;
    if (period == s_bridge_period) return;
    s_bridge_period = period;
    lv_timer_set_period(s_bridge, period);
}


/* LVGL thread, from the display manager (cp0_display_set_touch_frame_cb): every touch frame while the launcher draws,
 * and a tick per read. A completed G1 or G2 goes to the supervisor (the switcher stand-in) and its contacts are
 * swallowed: nothing under the fingers is clicked. */
int touch_frame_cb(const cp0_touch_frame_t *frame, int64_t now_us)
{
    if (!s_sup) return 0;
    cp0_g1_event_t e1;
    cp0_g2_event_t e2;
    if (frame) {
        lv_area_t a;
        cp0_g2_rect_t strip = {0, 0, 0, 0};
        const bool on_screen = cp0::ui::StatusStrip::active_area(cp0_display_native(), &a);
        if (on_screen) strip = {a.x1, a.y1, a.x2 - a.x1 + 1, a.y2 - a.y1 + 1};
        e2 = cp0_g2_frame(&s_g2, frame, on_screen ? &strip : nullptr);
        e1 = s_g1_on ? cp0_g1_frame(&s_g1, frame) : cp0_g1_event_t{};
    } else {
        e2 = cp0_g2_tick(&s_g2, now_us);
        e1 = s_g1_on ? cp0_g1_tick(&s_g1, now_us) : cp0_g1_event_t{};
    }
    int swallow = 0;
    /* a wrapped (stock) app in the compat window cannot be hidden nor frozen (D12): its toolbar's Esc held 3 s ends
     * it; the gesture is only logged (and its fingers swallowed) */
    const bool wrapped = s_switcher_ui && native_ui::external_app_running();
    if (e1.outcome != CP0_G1_NONE) {
        char line[192];
        cp0_g1_describe(&s_g1, &e1, line, sizeof(line));
        const bool fired = e1.outcome == CP0_G1_FIRED;
        s_sup->launcher_gesture("G1", std::string(line) + (fired && wrapped ? " in a wrapped app: nothing" : ""),
                                fired && !wrapped);
        swallow = fired;
    }
    if (e2.fired) {
        s_sup->launcher_gesture("G2", "G2 fired (status strip held " + std::to_string(e2.held_ms) + " ms)", true);
        swallow = 1;
    }
    return swallow;
}

/* The touch screen changes hands: the launcher's queue holds what happened meanwhile (read now, without frames); a hand
 * still down (the one that made a G1 in the app) is not a gesture of the launcher's: ignored until it lifts. */
void hand_over_gestures()
{
    if (cp0_display_touch_sync() > 0) {
        const int64_t now = cp0_touch_now_us();
        cp0_g1_block(&s_g1, now);
        cp0_g2_block(&s_g2, now);
    } else {
        cp0_g1_reset(&s_g1);
        cp0_g2_reset(&s_g2);
    }
}

/* LVGL thread: the supervisor's UI events, then the toast. */
void process_events()
{
    s_pending.store(false);
    std::deque<UiEvent> batch;
    {
        std::lock_guard<std::mutex> lock(s_events_mutex);
        batch.swap(s_events);
    }
    for (const UiEvent &e : batch) {
        switch (e.kind) {
        case UiEvent::Kind::AppFront:
            s_app_front = true;
            refresh_toast(); /* hides what the toast shows; a notice not read yet comes back later */
            if (s_hooks.app_front) s_hooks.app_front();
            hand_over_gestures();
            break;
        case UiEvent::Kind::LauncherFront:
            /* also after a refused launch: undoes what the launch prepared (loading overlay, input group) */
            s_app_front = false;
            s_launcher_front_ms = multitask::Supervisor::now_ms(); /* task c-7: a Super still held is not a new press */
            if (s_hooks.launcher_front) s_hooks.launcher_front(e.flag);
            if (s_switcher || s_sheet) detach_keypad(); /* the hook gave the keypad the page's group back */
            hand_over_gestures();
            break;
        case UiEvent::Kind::Repaint:
            if (!s_app_front) {
                lv_obj_invalidate(lv_screen_active());
                lv_obj_invalidate(lv_layer_top());
            }
            break;
        case UiEvent::Kind::Notice:
            s_notices.push(e.text);
            break;
        case UiEvent::Kind::Overlay:
            s_overlay_text = e.text;
            break;
        case UiEvent::Kind::Switcher:
            if (e.info) show_switcher(*e.info);
            break;
        case UiEvent::Kind::Sheet:
            show_sheet(e.id, e.text);
            break;
        case UiEvent::Kind::Apps:
            native_ui::set_resident_apps(e.keys);
            break;
        }
    }
    /* an Alt+Tab session on the switcher: touch ignored (as P-b's text), also once the launcher took the panel back */
    if (s_switcher && s_switcher_session && !s_app_front) cp0_display_set_touch_swallow(1);
    keep_switcher_in_front();
    refresh_toast();
    tune_bridge();
}

/* every turn of the LVGL main loop (cp0_lvgl_set_loop_hook): cheap unless the supervisor posted something */
void loop_hook()
{
    if (s_pending.load()) process_events();
}

void bridge_cb(lv_timer_t *)
{
    process_events();
}

/* Task c-7: the Super hold while the launcher is in front. Its keys come one by one through the key filter, so a one-shot
 * timer fires at the hold's due time (paused otherwise); the supervisor then opens the switcher as for G1 / G2. */
lv_timer_t *s_super_hold = nullptr;
/* A Super press the launcher reads within this long after it took the panel back may be the key still held from the app
 * (the hold that opened the switcher there): it starts no hold (which would close the switcher again). Its release is
 * still a tap: since c7.5 the second tap of a double tap comes this soon. */
constexpr std::uint64_t kSuperHandOverMs = 400;
std::uint64_t s_filter_super_ms = 0; /* when Super went down on the launcher's keyboard (0: up), for the log */
bool s_filter_super_fired = false;   /* ... and its hold opened the switcher */

void arm_super_hold()
{
    if (!s_super_hold) return;
    const std::uint64_t due = s_filter_keys.hold_due();
    if (due == 0) {
        lv_timer_pause(s_super_hold);
        return;
    }
    const std::uint64_t now = multitask::Supervisor::now_ms();
    lv_timer_set_period(s_super_hold, due > now ? static_cast<uint32_t>(due - now) : 1);
    lv_timer_reset(s_super_hold);
    lv_timer_resume(s_super_hold);
}

void super_hold_cb(lv_timer_t *timer)
{
    lv_timer_pause(timer);
    /* an app in front reads its own keys (the supervisor's reader has the hold then); a wrapped app in the compat window
     * cannot be hidden (D12), as for G1 */
    if (!s_sup || s_app_front || (s_switcher_ui && native_ui::external_app_running())) return;
    const std::uint64_t now = multitask::Supervisor::now_ms();
    if (s_filter_keys.tick(now) == multitask::SwitchEvent::Switcher) {
        s_filter_super_fired = true;
        std::fprintf(stderr, "[mt] session key (launcher): Super held -> switcher (hold fired %llu ms after the press)\n",
                     static_cast<unsigned long long>(s_filter_super_ms ? now - s_filter_super_ms : 0));
        s_sup->launcher_switch_event(multitask::SwitchEvent::Switcher);
    } else {
        arm_super_hold(); /* not due yet (the timer ran early) or cancelled meanwhile */
    }
}

/* LVGL thread, launcher in front: Alt+Tab is consumed (and handed to the supervisor) only while an app is resident */
int key_filter(const struct key_item *item)
{
    if (item && s_sup) {
        const int value = item->key_state == KBD_KEY_PRESSED ? 1 : item->key_state == KBD_KEY_RELEASED ? 0 : 2;
        const std::uint64_t now_ms = multitask::Supervisor::now_ms();
        const std::uint64_t due_before = s_filter_keys.hold_due();
        multitask::SwitchResult r = s_filter_keys.key(item->key_code, value, now_ms);
        const bool super = item->key_code == multitask::keys::kLeftMeta || item->key_code == multitask::keys::kRightMeta;
        /* task c-7: the Super key's story in the journal, as the supervisor's for an app in front */
        if (super && value == 1) {
            s_filter_super_ms = now_ms;
            s_filter_super_fired = false;
            if (now_ms - s_launcher_front_ms < kSuperHandOverMs) {
                /* maybe the key still held from the app: no hold from it; its release still counts as a tap (c7.5: the
                 * second tap of a double tap often comes this soon) */
                std::fprintf(stderr, "[mt] session key (launcher): Super down %llu ms after the hand-over: no hold from it\n",
                             static_cast<unsigned long long>(now_ms - s_launcher_front_ms));
                s_filter_keys.block_hold();
            } else {
                std::fprintf(stderr, "[mt] session key (launcher): Super down: %s\n",
                             s_filter_keys.hold_due() ? "the switcher in 600 ms if held alone"
                                                      : "no hold (Alt is held or an Alt+Tab session runs)");
            }
        } else if (super && value == 0 && s_filter_super_ms != 0) {
            std::fprintf(stderr, "[mt] session key (launcher): Super up after %llu ms: %s\n",
                         static_cast<unsigned long long>(now_ms - s_filter_super_ms),
                         r.event == multitask::SwitchEvent::Home ? "a tap (to the supervisor, for the double tap)"
                         : s_filter_super_fired                 ? "its hold opened the switcher"
                                                                : "nothing (Super with another key, or ignored)");
            s_filter_super_ms = 0;
        } else if (!super && value == 1 && due_before != 0 && s_filter_keys.hold_due() == 0) {
            std::fprintf(stderr, "[mt] session key (launcher): Super hold cancelled: %s pressed while Super was down\n",
                         multitask::switch_key_name(item->key_code).c_str());
        }
        arm_super_hold();
        /* nothing to switch to, or (P-d) a wrapped app runs in the compat window (it cannot be hidden): the key belongs
         * to the page / the app */
        if ((r.event == multitask::SwitchEvent::Start || r.event == multitask::SwitchEvent::StartReverse) &&
            (s_residents.load() == 0 || (s_switcher_ui && native_ui::external_app_running()))) {
            s_filter_keys.reset();
            r = multitask::SwitchResult();
        }
        if (r.event != multitask::SwitchEvent::None && r.event != multitask::SwitchEvent::Home)
            s_sup->launcher_switch_event(r.event);
        /* task c-7.5: a Super tap goes to the supervisor, which counts the double tap over both readers (a tap alone does
         * nothing on the launcher, as before) */
        if (r.event == multitask::SwitchEvent::Home && r.super_tap) s_sup->launcher_super_tap(r.tap_ms);
        /* the session key trace, as the supervisor's for sessions read from evdev (see Supervisor::on_raw_key) */
        static std::uint64_t trace_until = 0;
        const std::uint64_t now = multitask::Supervisor::now_ms();
        const bool session_key = r.event != multitask::SwitchEvent::None && r.event != multitask::SwitchEvent::Home;
        if (value != 2 && (s_filter_keys.active() || session_key || now < trace_until))
            std::fprintf(stderr, "[mt] session key (launcher): %s %s%s%s\n",
                         multitask::switch_key_name(item->key_code).c_str(), value ? "down" : "up",
                         session_key ? " -> " : "", session_key ? multitask::switch_event_name(r.event) : "");
        if (r.event == multitask::SwitchEvent::Commit || r.event == multitask::SwitchEvent::Cancel) trace_until = now + 2000;
        if (r.consume) return 1;
    }
    return s_prev_filter ? s_prev_filter(item) : 0;
}

const char *env_get(const char *name)
{
    return std::getenv(name);
}

int env_int(const char *name, int fallback, int lo, int hi)
{
    const char *v = std::getenv(name);
    if (!v || !*v) return fallback;
    const int n = std::atoi(v);
    return n < lo || n > hi ? fallback : n;
}

} // namespace

/* Why enabled() said yes, for start()'s log line. */
static const char *s_on_reason = "";

bool enabled()
{
#if defined(__linux__)
    /* On by default; the rule (APPLAUNCH_MULTITASK=1 / =0 force it, else on where the switcher fits) is
     * multitask::multitask_on(), the only place that decides. Read once, on the native UI only. */
    static const bool on = [] {
        if (!native_ui::enabled()) return false;
        const multitask::MultitaskFlag flag = multitask::parse_multitask_flag(std::getenv("APPLAUNCH_MULTITASK"));
        const cp0_ui_metrics_t *m = cp0_ui_metrics_panel(); /* the panel, not the PDA's app area (report 041) */
        const int w = m ? m->w : 0, h = m ? m->h : 0;
        /* off: no log line at all, as before (the protocols count `[mt]` lines) */
        s_on_reason = flag == multitask::MultitaskFlag::On ? "APPLAUNCH_MULTITASK on" : "default";
        return multitask::multitask_on(flag, w, h);
    }();
    return on;
#else
    return false;
#endif
}

bool start(Hooks hooks)
{
    if (s_sup) return true;
    if (!enabled()) return false;
    s_hooks = std::move(hooks);

    multitask::SupervisorConfig cfg;
    cfg.model.cap = env_int("APPLAUNCH_MT_CAP", 4, 1, 8);
    if (const char *mode = std::getenv("APPLAUNCH_MT_INPUT")) {
        const std::string m = mode;
        cfg.use_grab = m == "grab+drain" || m == "grab";
        cfg.use_drain = m == "grab+drain" || m == "drain";
        if (!cfg.use_grab && !cfg.use_drain && m != "none") cfg.use_grab = cfg.use_drain = true; /* unknown: default */
    }
    if (const char *rt = std::getenv("XDG_RUNTIME_DIR")) {
        if (rt[0]) {
            cfg.status_path = std::string(rt) + "/applaunch/multitask.status";
            cfg.control_fifo = std::string(rt) + "/applaunch/multitask.ctl";
        }
    }
    /* P-c: touch gestures. G1 needs a panel with at least 3 slots; G2 works on any touch screen. */
    const int slots = cp0_display_touch_slots();
    const cp0_ui_metrics_t *metrics = cp0_ui_metrics_panel(); /* the touch frames cover the whole panel */
    cfg.touch.w = metrics ? metrics->w : 640;
    cfg.touch.h = metrics ? metrics->h : 480;
    cfg.touch.ppmm_x100 = metrics ? metrics->ppmm_x100 : 1130;
    cp0_g1_params_default(&cfg.touch.params);
    (void)cp0_g1_params_apply_env(&cfg.touch.params, env_get);
    cfg.touch.g1 = slots >= CP0_G1_CONTACTS;
    cp0_g2_params_t g2;
    cp0_g2_params_default(&g2);
    (void)cp0_g2_params_apply_env(&g2, env_get);
    cp0_g1_init(&s_g1, &cfg.touch.params, cfg.touch.w, cfg.touch.h, cfg.touch.ppmm_x100);
    cp0_g2_init(&s_g2, &g2, cfg.touch.ppmm_x100);
    s_g1_on = cfg.touch.g1;
    /* P-d: the switcher screen, the legacy sheet, the grid's dots and the first-run hint where the short side is 480
     * and more; smaller canvases keep the P-b / P-c stand-ins */
    s_switcher_ui = multitask::switcher_fits(cfg.touch.w, cfg.touch.h);
    cfg.switcher_ui = s_switcher_ui;
    if (s_switcher_ui) {
        cfg.hint_text = multitask::first_run_hint(slots);
        const char *home = std::getenv("HOME");
        if (home && home[0]) cfg.hint_path = std::string(home) + "/.config/cardputerzero/multitask-hint-shown";
    }

    s_fb.reset(new LauncherFb());
    s_input.reset(new LauncherInput());
    s_keys.reset(new LauncherKeys());
    s_ui.reset(new LauncherUi());
    s_proc.reset(new multitask::LinuxProcessPort(&exec_child));
    s_touch.reset(new multitask::EvdevTouchPort(cp0_display_touch_path(), &cp0_display_touch_map, false));
    s_sup.reset(new multitask::Supervisor(cfg, *s_proc, *s_fb, *s_input, *s_keys, *s_ui));
    s_sup->set_touch_port(s_touch.get());

    s_bridge = lv_timer_create(bridge_cb, 1000, nullptr); /* the fallback; events come through the loop hook */
    s_super_hold = lv_timer_create(super_hold_cb, multitask::kSuperHoldMs, nullptr); /* task c-7 */
    if (s_super_hold) lv_timer_pause(s_super_hold);
    cp0_lvgl_set_loop_hook(&loop_hook);
    s_prev_filter = cp0_keyboard_get_key_filter();
    cp0_keyboard_set_key_filter(key_filter);
    bool started = false;
    try {
        started = s_sup->start();
    } catch (...) {
        started = false;
    }
    if (!started) {
        std::fprintf(stderr, "[mt] the supervisor did not start: full-screen apps keep the blocking path\n");
        stop();
        return false;
    }
    std::fprintf(stderr, "[mt] multitasking P-b on (%s): dev FIFO %s, status %s\n", s_on_reason,
                 cfg.control_fifo.empty() ? "-" : cfg.control_fifo.c_str(),
                 cfg.status_path.empty() ? "-" : cfg.status_path.c_str());
    const cp0_g1_params_t &p = cfg.touch.params;
    char size[24] = "size off";
    if (p.major_max > 0) std::snprintf(size, sizeof(size), "size max %d", p.major_max);
    if (slots >= CP0_G1_CONTACTS)
        std::fprintf(stderr,
                     "[mt] touch gestures (P-c): %s, %d slots; G1 three-finger hold %d ms (land %d ms, still %d.%d mm, "
                     "sep %d.%d mm, edge %d.%d mm, %s; %.0f / %.0f / %.0f px at %d.%02d px/mm); G2 status strip %d ms\n",
                     cp0_display_touch_path(), slots, p.hold_ms, p.land_ms, p.still_mm_x10 / 10, p.still_mm_x10 % 10,
                     p.sep_mm_x10 / 10, p.sep_mm_x10 % 10, p.edge_mm_x10 / 10, p.edge_mm_x10 % 10, size, s_g1.still_px,
                     s_g1.sep_px, s_g1.edge_px, cfg.touch.ppmm_x100 / 100, cfg.touch.ppmm_x100 % 100, g2.hold_ms);
    else
        std::fprintf(stderr, "[mt] touch gestures (P-c): %s, %d slot(s): G1 off (needs 3), G2 status strip %d ms\n",
                     slots ? cp0_display_touch_path() : "no touch screen", slots, g2.hold_ms);
    if (s_switcher_ui)
        std::fprintf(stderr, "[mt] switcher screen (P-d): on (%dx%d); first-run hint \"%s\" %s\n", cfg.touch.w,
                     cfg.touch.h, cfg.hint_text.c_str(),
                     cfg.hint_path.empty() ? "(not remembered: no HOME)" : ("(" + cfg.hint_path + ")").c_str());
    else
        std::fprintf(stderr, "[mt] switcher screen (P-d): off (%dx%d: short side under 480, the P-b / P-c stand-ins)\n",
                     cfg.touch.w, cfg.touch.h);
    /* screen.state says how many contacts the panel tracks (study 6.4; only with the flag on) */
    cp0_ui_state_set_touch_slots(slots);
    (void)cp0_ui_state_republish();
    cp0_display_set_touch_frame_cb(&touch_frame_cb);
    return true;
}

void stop()
{
    cp0_display_set_touch_frame_cb(nullptr);
    if (s_sup) s_sup->stop();
    close_switcher_screen();
    if (s_sheet) {
        cp0_display_set_native_clip(0, 0, 0, 0);
        leave_screen(s_sheet->screen(), s_sheet_prev);
        s_sheet.reset();
    }
    cp0_lvgl_set_loop_hook(nullptr);
    if (cp0_keyboard_get_key_filter() == key_filter) cp0_keyboard_set_key_filter(s_prev_filter);
    s_prev_filter = nullptr;
    if (s_bridge) {
        lv_timer_delete(s_bridge);
        s_bridge = nullptr;
    }
    if (s_super_hold) {
        lv_timer_delete(s_super_hold);
        s_super_hold = nullptr;
    }
    s_sup.reset();
    s_touch.reset();
    s_proc.reset();
    s_ui.reset();
    s_keys.reset();
    s_input.reset();
    s_fb.reset();
}

bool active()
{
    return s_sup != nullptr;
}

lv_timer_t *bridge_timer()
{
    return s_bridge;
}

bool is_resident(const std::string &key)
{
    return s_sup && s_sup->is_resident_key(key);
}

bool screen_up()
{
    return s_switcher || s_sheet;
}

bool sheet_up()
{
    return s_sheet != nullptr;
}

bool toggle_switcher(const char *source)
{
    if (!s_sup) return false;
    if (s_switcher) {
        s_sup->switcher_cancel();
        return true;
    }
    const std::string name = source ? source : "button";
    s_sup->launcher_gesture(name, name + " soft button", true);
    return true;
}

bool switcher_home()
{
    if (!s_sup || !s_switcher) return false;
    s_sup->switcher_home();
    return true;
}

void launch(const std::string &name, const std::string &key, const std::string &command, bool keep_root,
            multitask::Background background)
{
    if (!s_sup) return;
    multitask::AppSpec spec;
    spec.key = key;
    spec.name = name;
    spec.exec = command;
    spec.keep_root = keep_root;
    spec.background = background;
    s_sup->launch(spec);
}

} // namespace multitask_ui
