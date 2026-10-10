/*
 * Esc-hold watcher sources for an external (full-screen) app.
 *
 * While an external app runs, the launcher's keyboard thread is paused, so the runner reads the Esc key
 * straight from evdev to drive the "hold Esc 3 s to exit" policy. It reads the explicit keyboard device
 * (APPLAUNCH_LINUX_KEYBOARD_DEVICE, e.g. the M4 / Bluetooth keyboard) and every real keyboard the presence
 * watcher lists (USB, Bluetooth), opening and dropping them as they come and go.
 *
 *   - Two paths to the same node (the by-path symlink and the listed eventN) are read once.
 *   - Esc counts as held while ANY source holds it; a source that disappears releases its Esc.
 *   - Only the explicit device and presence-listed keyboards are opened: never a mouse, touch panel or remote.
 *   - All fds are O_CLOEXEC and closed by close_all() (or the destructor).
 *
 * No LVGL, no presence watcher dependency (the caller passes the list), so it is unit-tested on a PC with FIFOs.
 */
#pragma once

#include <cstddef>
#include <sys/types.h>

namespace cp0_esc_key_watch {

class Sources {
public:
    static constexpr std::size_t kMax = 17; // CP0_KBD_PRESENCE_MAX_DEVICES + the explicit device

    Sources() = default;
    ~Sources() { close_all(); }
    Sources(const Sources &) = delete;
    Sources &operator=(const Sources &) = delete;

    // Open `path` unless it is already read (same node) or cannot be opened yet (asleep, permissions):
    // the caller retries later. Returns true when a new source was opened.
    bool open(const char *path);

    // Read every source. Returns -1 when no Esc press/release/loss happened, else the new "Esc held by any
    // source" state (0 or 1) to write to cp0_esc_state. A source that fails (ENODEV, HUP) is closed and its
    // Esc released.
    int poll();

    bool esc_held() const;
    std::size_t count() const;
    void close_all();

private:
    struct Source {
        int fd = -1;
        dev_t dev = 0;
        ino_t ino = 0;
        bool esc_down = false;
    };
    void drop(std::size_t index);
    Source sources_[kMax];
};

} // namespace cp0_esc_key_watch
