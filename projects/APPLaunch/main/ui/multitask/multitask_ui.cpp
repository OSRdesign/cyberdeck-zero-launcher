/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-b in the launcher (see multitask_ui.hpp).
 */
#include "multitask_ui.hpp"

#include "display_handoff.hpp"
#include "notice_queue.hpp"
#include "supervisor.hpp"

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
#include "native_ui.hpp"

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
        attach();
        open_ = true;
    }
    void close() override
    {
        if (!open_) return;
        sources_.close_all();
        if (presence_) cp0_kbd_presence_shared_release();
        presence_ = nullptr;
        open_ = false;
    }
    int poll(Listener listener, void *context) override
    {
        if (!open_) return -1;
        if (cp0_kbd_presence_watcher_generation(presence_) != seen_ || multitask::Supervisor::now_ms() >= next_attach_)
            attach();
        return sources_.poll(listener, context);
    }
    void esc_state(int held) override { cp0_esc_state_write(held); }
    void fds(std::vector<int> &out) override
    {
        int f[cp0_esc_key_watch::Sources::kMax];
        out.assign(f, f + sources_.fds(f, cp0_esc_key_watch::Sources::kMax));
    }

private:
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
        }
        next_attach_ = multitask::Supervisor::now_ms() + 1000; /* the supervisor wakes at least once a second */
    }

    cp0_esc_key_watch::Sources sources_;
    cp0_kbd_presence_watcher_t *presence_ = nullptr;
    unsigned seen_ = 0;
    std::uint64_t next_attach_ = 0;
    bool open_ = false;
};

struct UiEvent {
    enum class Kind { AppFront, LauncherFront, Repaint, Notice, Overlay } kind = Kind::Repaint;
    bool flag = false;
    std::string text;
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

/* The toast carries two things: the P-b stand-in for the switcher (one line of text while an Alt+Tab session runs,
 * touch ignored meanwhile; a session started inside an app asks for it before the app is hidden: it appears when the
 * launcher is in front) and the supervisor's notices, one at a time, each readable (NoticeQueue). */
void refresh_toast()
{
    const bool overlay_wanted = !s_app_front && !s_overlay_text.empty();
    const multitask::NoticeQueue::Step step =
        s_notices.tick(multitask::Supervisor::now_ms(), !s_app_front && !overlay_wanted);
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
    if (e1.outcome != CP0_G1_NONE) {
        char line[192];
        cp0_g1_describe(&s_g1, &e1, line, sizeof(line));
        s_sup->launcher_gesture("G1", line, e1.outcome == CP0_G1_FIRED);
        swallow = e1.outcome == CP0_G1_FIRED;
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
            if (s_hooks.launcher_front) s_hooks.launcher_front(e.flag);
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
        }
    }
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

/* LVGL thread, launcher in front: Alt+Tab is consumed (and handed to the supervisor) only while an app is resident */
int key_filter(const struct key_item *item)
{
    if (item && s_sup) {
        const int value = item->key_state == KBD_KEY_PRESSED ? 1 : item->key_state == KBD_KEY_RELEASED ? 0 : 2;
        multitask::SwitchResult r = s_filter_keys.key(item->key_code, value);
        if ((r.event == multitask::SwitchEvent::Start || r.event == multitask::SwitchEvent::StartReverse) &&
            s_residents.load() == 0) {
            s_filter_keys.reset(); /* nothing to switch to: the key belongs to the page */
            r = multitask::SwitchResult();
        }
        if (r.event != multitask::SwitchEvent::None && r.event != multitask::SwitchEvent::Home)
            s_sup->launcher_switch_event(r.event);
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

bool enabled()
{
#if defined(__linux__)
    static const bool on = [] {
        const char *v = std::getenv("APPLAUNCH_MULTITASK");
        return v && std::strcmp(v, "1") == 0;
    }();
    return on && native_ui::enabled();
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
    const cp0_ui_metrics_t *metrics = cp0_ui_metrics_current();
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

    s_fb.reset(new LauncherFb());
    s_input.reset(new LauncherInput());
    s_keys.reset(new LauncherKeys());
    s_ui.reset(new LauncherUi());
    s_proc.reset(new multitask::LinuxProcessPort(&exec_child));
    s_touch.reset(new multitask::EvdevTouchPort(cp0_display_touch_path(), &cp0_display_touch_map, false));
    s_sup.reset(new multitask::Supervisor(cfg, *s_proc, *s_fb, *s_input, *s_keys, *s_ui));
    s_sup->set_touch_port(s_touch.get());

    s_bridge = lv_timer_create(bridge_cb, 1000, nullptr); /* the fallback; events come through the loop hook */
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
    std::fprintf(stderr, "[mt] multitasking P-b on (APPLAUNCH_MULTITASK=1): dev FIFO %s, status %s\n",
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
    cp0_lvgl_set_loop_hook(nullptr);
    if (cp0_keyboard_get_key_filter() == key_filter) cp0_keyboard_set_key_filter(s_prev_filter);
    s_prev_filter = nullptr;
    if (s_bridge) {
        lv_timer_delete(s_bridge);
        s_bridge = nullptr;
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
