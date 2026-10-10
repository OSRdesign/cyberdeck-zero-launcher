/*
 * Keyboard presence in the launcher (see input_presence.hpp).
 */

#include "input_presence.hpp"

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "cp0_input_state.h"
#include "cp0_keyboard_presence.h"
#include "keyboard_input.h"
#include "sample_log.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

struct Publisher {
    char path[512] = {};
    cp0_input_state_t state = {0, 0, 0}; // what the file says now
    unsigned watcher_generation = 0;     // list generation last published
    bool published = false;
    bool write_failed = false;
};

// Written by start() before the listener exists, then only by listener calls (the watcher serialises them).
Publisher s_publisher;
cp0_kbd_presence_watcher_t *s_watcher = nullptr;

void on_keyboards(const cp0_kbd_presence_list_t *list, unsigned generation, void *)
{
    Publisher &p = s_publisher;
    if (p.published && generation == p.watcher_generation) return; // the same list twice (start-up)

    cp0_input_state_t next;
    next.keyboards = list->count;
    next.last_seen = cp0_input_state_next_last_seen(p.state.keyboards, p.state.last_seen, list->count,
                                                    static_cast<int64_t>(std::time(nullptr)));
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

    if (cp0_input_state_write(p.path, &next) != 0) {
        if (!p.write_failed) SLOGW("[KBD] cannot write %s: %s", p.path, std::strerror(errno));
        p.write_failed = true;
    } else {
        p.write_failed = false;
    }
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
    Publisher &p = s_publisher;
    p = Publisher();
    if (cp0_input_state_path(std::getenv("XDG_RUNTIME_DIR"), p.path, sizeof(p.path)) != 0) {
        SLOGW("[KBD] XDG_RUNTIME_DIR is too long: input state not published");
        return;
    }
    // A restarted launcher keeps last_seen and continues gen from the file of its previous run.
    cp0_input_state_t previous;
    if (cp0_input_state_read(p.path, &previous) == 0) p.state = previous;

    s_watcher = cp0_kbd_presence_shared_acquire();
    if (!s_watcher) {
        SLOGW("[KBD] keyboard presence watcher unavailable: %s not published", p.path);
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

} // namespace input_presence

#else

namespace input_presence {

void configure() {}
void start() {}
void stop() {}

} // namespace input_presence

#endif
