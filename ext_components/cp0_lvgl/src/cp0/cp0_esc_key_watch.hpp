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
 *   - Task c-7: the launcher also opens the media-key devices (open(path, true): a tablet's volume buttons, the ACPI
 *     video bus, ...). Only their media keys reach the listener; they never press Esc (nor Super, c7.4). For the volume,
 *     mute, brightness keys and Super, one press seen by two sources reaches the listener once (cp0_media_keys.h).
 *     Never grabbed (nothing here grabs).
 *   - All fds are O_CLOEXEC and closed by close_all() (or the destructor).
 *
 * No LVGL, no presence watcher dependency (the caller passes the list), so it is unit-tested on a PC with FIFOs.
 */
#pragma once

#include "cp0_media_keys.h"

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace cp0_esc_key_watch {

class Sources {
public:
    // CP0_KBD_PRESENCE_MAX_DEVICES + CP0_KBD_PRESENCE_MAX_MEDIA + the explicit device
    static constexpr std::size_t kMax = 25;

    Sources() { cp0_media_keys_init(&media_keys_); }
    ~Sources() { close_all(); }
    Sources(const Sources &) = delete;
    Sources &operator=(const Sources &) = delete;

    // Open `path` unless it is already read (same node) or cannot be opened yet (asleep, permissions):
    // the caller retries later. Returns true when a new source was opened. media: a media-key device (above).
    bool open(const char *path, bool media = false);

    // Read every source. Returns -1 when no Esc press/release/loss happened, else the new "Esc held by any
    // source" state (0 or 1) to write to cp0_esc_state. A source that fails (ENODEV, HUP) is closed and its
    // Esc released.
    int poll() { return poll(nullptr, nullptr); }
    // The same, and every key event read (EV_KEY: code, value 1 press / 0 release / 2 repeat) also goes to
    // `listener` (multitasking P-b: Alt+Tab and Super while an app is in front).
    using KeyListener = void (*)(void *context, int code, int value);
    int poll(KeyListener listener, void *context);

    bool esc_held() const;
    std::size_t count() const;
    // The open fds (at most `max`), for a caller that poll()s them instead of reading on a timer. Returns how many.
    std::size_t fds(int *out, std::size_t max) const;
    void close_all();

private:
    struct Source {
        int fd = -1;
        dev_t dev = 0;
        ino_t ino = 0;
        bool esc_down = false;
        bool media = false;
        bool repeat_logged = false; /* the first auto-repeat of a watched key is logged, not the others */
        char path[48] = {};         /* for the log */
    };
    void drop(std::size_t index);
    Source sources_[kMax];
    cp0_media_keys_t media_keys_;
};

} // namespace cp0_esc_key_watch
