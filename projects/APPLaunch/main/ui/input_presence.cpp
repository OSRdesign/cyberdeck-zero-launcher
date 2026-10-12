/*
 * Keyboard presence in the launcher (see input_presence.hpp).
 */

#include "input_presence.hpp"

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "cp0_input_state.h"
#include "cp0_keyboard_presence.h"
#include "keyboard_input.h"
#include "sample_log.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

namespace {

struct Publisher {
    char path[512] = {};
    cp0_input_state_t state = {};        // what the file says now
    unsigned watcher_generation = 0;     // list generation last published
    bool published = false;
    bool write_failed = false;
};

// Written by start() before the listener exists, then by listener calls (the watcher thread) and set_vkb() (the UI
// thread), under s_mutex.
Publisher s_publisher;
std::mutex s_mutex;
cp0_kbd_presence_watcher_t *s_watcher = nullptr;

// vkb until launcher_osk says which rule is in force: off (the launcher's keyboard is not installed yet, or never: a
// desktop session), unless APPLAUNCH_OSK says force / off.
cp0_app_vkb_t initial_vkb()
{
    const char *env = std::getenv("APPLAUNCH_OSK");
    if (env && std::strcmp(env, "force") == 0) return CP0_APP_VKB_FORCE;
    return CP0_APP_VKB_OFF;
}

// Caller holds s_mutex.
void write_locked(Publisher &p)
{
    if (cp0_input_state_write(p.path, &p.state) != 0) {
        if (!p.write_failed) SLOGW("[KBD] cannot write %s: %s", p.path, std::strerror(errno));
        p.write_failed = true;
    } else {
        p.write_failed = false;
    }
}

// The on-screen keyboard policy's view (input_presence::keyboards()), written by the listener, read by the UI thread.
std::atomic<bool> s_known{false};
std::atomic<unsigned> s_count{0};
std::atomic<unsigned> s_bt_count{0};
std::atomic<std::int64_t> s_any_gone{0};
std::atomic<std::int64_t> s_bt_gone{0};

std::int64_t monotonic_seconds()
{
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 1;
    return now.tv_sec > 0 ? static_cast<std::int64_t>(now.tv_sec) : 1; /* 0 means "never" */
}

void track_for_policy(const cp0_kbd_presence_list_t *list)
{
    unsigned bluetooth = 0;
    for (unsigned i = 0; i < list->count; ++i)
        if (list->devices[i].bus == CP0_KBD_PRESENCE_BUS_BLUETOOTH) ++bluetooth;
    const std::int64_t now = monotonic_seconds();
    if (s_count.load() > 0 && list->count == 0) s_any_gone.store(now);
    if (s_bt_count.load() > 0 && bluetooth == 0) s_bt_gone.store(now); /* it sleeps: present for N minutes more */
    s_bt_count.store(bluetooth);
    s_count.store(list->count);
    s_known.store(true);
}

void on_keyboards(const cp0_kbd_presence_list_t *list, unsigned generation, void *)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    Publisher &p = s_publisher;
    if (p.published && generation == p.watcher_generation) return; // the same list twice (start-up)
    track_for_policy(list);

    cp0_input_state_t next = p.state;
    next.keyboards = list->count;
    next.last_seen = cp0_input_state_next_last_seen(p.state.keyboards, p.state.last_seen, list->count,
                                                    static_cast<int64_t>(std::time(nullptr)));
    next.bt_keyboards = s_bt_count.load();
    next.kb_gone = s_any_gone.load();
    next.bt_gone = s_bt_gone.load();
    next.gen = p.state.gen + 1;
    p.state = next;
    p.watcher_generation = generation;
    p.published = true;

    char names[512];
    size_t used = 0;
    names[0] = '\0';
    for (unsigned i = 0; i < list->count && used < sizeof(names); ++i) {
        const int n = std::snprintf(names + used, sizeof(names) - used, "%s%s (%s)", i ? ", " : "",
                                    list->devices[i].name, list->devices[i].path);
        if (n < 0) break;
        used += static_cast<size_t>(n);
    }
    SLOGI("[KBD] keyboards=%u gen=%u: %s", next.keyboards, next.gen, list->count ? names : "none");
    write_locked(p);
}

} // namespace

namespace input_presence {

void configure()
{
    const char *scan = std::getenv("APPLAUNCH_KEYBOARD_SCAN");
    const bool off = scan && scan[0] == '0';
    cp0_keyboard_set_read_all_keyboards(off ? 0 : 1);
    if (off) SLOGI("[KBD] APPLAUNCH_KEYBOARD_SCAN=0: reading LV_LINUX_KEYBOARD_DEVICE only");
}

void start()
{
    if (s_watcher) return;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        Publisher &p = s_publisher;
        p = Publisher();
        p.state.vkb = initial_vkb();
        if (cp0_input_state_path(std::getenv("XDG_RUNTIME_DIR"), p.path, sizeof(p.path)) != 0) {
            SLOGW("[KBD] XDG_RUNTIME_DIR is too long: input state not published");
            return;
        }
        // A restarted launcher keeps last_seen and continues gen from the file of its previous run; the policy
        // inputs (Bluetooth count, gone times) start again, like the launcher's own policy.
        cp0_input_state_t previous;
        if (cp0_input_state_read(p.path, &previous) == 0) {
            p.state.last_seen = previous.last_seen;
            p.state.gen = previous.gen;
        }
    }

    s_watcher = cp0_kbd_presence_shared_acquire();
    if (!s_watcher) {
        SLOGW("[KBD] keyboard presence watcher unavailable: %s not published", s_publisher.path);
        return;
    }
    if (cp0_kbd_presence_watcher_add_listener(s_watcher, on_keyboards, nullptr) != 0) {
        SLOGW("[KBD] no room for the input state listener");
        cp0_kbd_presence_shared_release();
        s_watcher = nullptr;
    }
}

void stop()
{
    if (!s_watcher) return;
    cp0_kbd_presence_watcher_remove_listener(s_watcher, on_keyboards, nullptr);
    cp0_kbd_presence_shared_release();
    s_watcher = nullptr;
}

KeyboardSnapshot keyboards()
{
    KeyboardSnapshot snapshot;
    snapshot.known = s_known.load();
    snapshot.keyboards = s_count.load();
    snapshot.now_s = monotonic_seconds();
    snapshot.any_gone_s = s_any_gone.load();
    snapshot.bt_gone_s = s_bt_gone.load();
    return snapshot;
}

void set_vkb(cp0_app_vkb_t vkb)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    Publisher &p = s_publisher;
    if (p.state.vkb == vkb) return;
    p.state.vkb = vkb;
    if (!p.published || !p.path[0]) return; // the first presence report writes it
    p.state.gen += 1;
    SLOGI("[KBD] vkb=%s gen=%u", cp0_app_vkb_name(vkb), p.state.gen);
    write_locked(p);
}

} // namespace input_presence

#else

namespace input_presence {

void configure() {}
void start() {}
void stop() {}
KeyboardSnapshot keyboards() { return KeyboardSnapshot(); }
void set_vkb(cp0_app_vkb_t) {}

} // namespace input_presence

#endif
