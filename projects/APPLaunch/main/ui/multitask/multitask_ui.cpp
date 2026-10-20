/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-b in the launcher (see multitask_ui.hpp).
 */
#include "multitask_ui.hpp"

#include "display_handoff.hpp"
#include "supervisor.hpp"

#include "cp0_display.h"
#include "cp0_esc_state.h"
#include "cp0_keyboard_presence.h"
#include "cp0_lvgl_app_runner.hpp"
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
 * watcher lists, re-attached every 500 ms or when the list changes (a Bluetooth keyboard waking up). */
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
        next_attach_ = multitask::Supervisor::now_ms() + 500;
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

void post(UiEvent event)
{
    {
        std::lock_guard<std::mutex> lock(s_events_mutex);
        s_events.push_back(std::move(event));
    }
    cp0_lvgl_wake();
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
cp0_keyboard_key_filter_t s_prev_filter = nullptr;
multitask::SwitchKeys s_filter_keys;

std::unique_ptr<LauncherFb> s_fb;
std::unique_ptr<LauncherInput> s_input;
std::unique_ptr<LauncherKeys> s_keys;
std::unique_ptr<LauncherUi> s_ui;
std::unique_ptr<multitask::LinuxProcessPort> s_proc;
std::unique_ptr<multitask::Supervisor> s_sup;

/* The P-b stand-in for the switcher: one line of text in the existing toast while an Alt+Tab session runs, touch
 * ignored meanwhile. A session started inside an app asks for the text before the app is hidden: it appears when the
 * launcher is in front. */
void apply_overlay()
{
    if (s_app_front) return;
    if (s_overlay_text.empty()) {
        if (s_overlay) {
            launcher_toast().hide();
            cp0_display_set_touch_swallow(2); /* until the finger in contact, if any, is lifted */
        }
        s_overlay = false;
        return;
    }
    launcher_toast().show_persistent(s_overlay_text.c_str());
    s_overlay = true;
    cp0_display_set_touch_swallow(1);
}

/* LVGL thread, every 20 ms (the one timer that runs while an app is in front) */
void bridge_cb(lv_timer_t *)
{
    std::deque<UiEvent> batch;
    {
        std::lock_guard<std::mutex> lock(s_events_mutex);
        batch.swap(s_events);
    }
    for (const UiEvent &e : batch) {
        switch (e.kind) {
        case UiEvent::Kind::AppFront:
            if (s_overlay) {
                launcher_toast().hide();
                s_overlay = false;
            }
            s_app_front = true;
            if (s_hooks.app_front) s_hooks.app_front();
            break;
        case UiEvent::Kind::LauncherFront:
            /* also after a refused launch: undoes what the launch prepared (loading overlay, input group) */
            s_app_front = false;
            if (s_hooks.launcher_front) s_hooks.launcher_front(e.flag);
            apply_overlay();
            break;
        case UiEvent::Kind::Repaint:
            if (!s_app_front) {
                lv_obj_invalidate(lv_screen_active());
                lv_obj_invalidate(lv_layer_top());
            }
            break;
        case UiEvent::Kind::Notice:
            if (!s_app_front && !s_overlay) launcher_toast().show(e.text.c_str());
            break;
        case UiEvent::Kind::Overlay:
            s_overlay_text = e.text;
            apply_overlay();
            break;
        }
    }
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
        if (r.consume) return 1;
    }
    return s_prev_filter ? s_prev_filter(item) : 0;
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
    s_fb.reset(new LauncherFb());
    s_input.reset(new LauncherInput());
    s_keys.reset(new LauncherKeys());
    s_ui.reset(new LauncherUi());
    s_proc.reset(new multitask::LinuxProcessPort(&exec_child));
    s_sup.reset(new multitask::Supervisor(cfg, *s_proc, *s_fb, *s_input, *s_keys, *s_ui));

    s_bridge = lv_timer_create(bridge_cb, 20, nullptr);
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
    return true;
}

void stop()
{
    if (s_sup) s_sup->stop();
    if (cp0_keyboard_get_key_filter() == key_filter) cp0_keyboard_set_key_filter(s_prev_filter);
    s_prev_filter = nullptr;
    if (s_bridge) {
        lv_timer_delete(s_bridge);
        s_bridge = nullptr;
    }
    s_sup.reset();
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
