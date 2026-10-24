/*
 * Esc-hold watcher sources (see cp0_esc_key_watch.hpp).
 */

#include "cp0_esc_key_watch.hpp"

#if !defined(_WIN32)

#include <cerrno>
#include <cstdio>
#include <ctime>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cp0_esc_key_watch {

namespace {

std::uint64_t monotonic_ms()
{
    struct timespec now;
    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return static_cast<std::uint64_t>(now.tv_sec) * 1000u + static_cast<std::uint64_t>(now.tv_nsec) / 1000000u;
}

} // namespace

bool Sources::open(const char *path, bool media)
{
    struct stat st;
    if (!path || !path[0] || ::stat(path, &st) != 0) return false;
    std::size_t free_slot = kMax;
    for (std::size_t i = 0; i < kMax; ++i) {
        if (sources_[i].fd >= 0 && sources_[i].dev == st.st_dev && sources_[i].ino == st.st_ino) return false;
        if (sources_[i].fd < 0 && free_slot == kMax) free_slot = i;
    }
    if (free_slot == kMax) return false;
    const int fd = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false; // asleep, or udev has not set the permissions yet: retried by the caller
    struct stat opened;
    if (::fstat(fd, &opened) != 0 || opened.st_dev != st.st_dev || opened.st_ino != st.st_ino) {
        ::close(fd); // replaced between stat and open
        return false;
    }
    Source &source = sources_[free_slot];
    source.fd = fd;
    source.dev = opened.st_dev;
    source.ino = opened.st_ino;
    source.esc_down = false;
    source.media = media;
    std::snprintf(source.path, sizeof(source.path), "%s", path);
    cp0_media_keys_forget(&media_keys_, static_cast<int>(free_slot)); // a fresh source holds nothing
    std::printf("[cp0] Esc watcher reads %s%s\n", path, media ? " (media keys only)" : "");
    std::fflush(stdout);
    return true;
}

void Sources::drop(std::size_t index)
{
    if (sources_[index].fd >= 0) ::close(sources_[index].fd);
    sources_[index] = Source();
    cp0_media_keys_forget(&media_keys_, static_cast<int>(index));
}

int Sources::poll(KeyListener listener, void *context)
{
    int changed = -1;
    const std::uint64_t now = monotonic_ms(); // one read pass: two devices' reports of one press land together
    for (std::size_t i = 0; i < kMax; ++i) {
        Source &source = sources_[i];
        if (source.fd < 0) continue;
        struct input_event event;
        ssize_t got;
        while ((got = ::read(source.fd, &event, sizeof(event))) == static_cast<ssize_t>(sizeof(event))) {
            if (event.type != EV_KEY) continue;
            // task c-7: a media-key device gives its media keys only; one press seen twice counts once
            const bool pass = cp0_media_keys_filter(&media_keys_, static_cast<int>(i), source.media ? 1 : 0, event.code,
                                                    event.value, now) != 0;
            if (cp0_media_key_watched(event.code) && (event.value != 2 || !source.repeat_logged)) {
                // the journal says which device sent the launcher's keys and when (c7.4: the Windows key on the tablet)
                std::printf("[cp0] key %u %s from %s%s\n", event.code,
                            event.value == 1 ? "down" : event.value == 0 ? "up" : "auto-repeat (held)", source.path,
                            pass ? "" : source.media && !cp0_media_key_passes(event.code)
                                            ? ": dropped (not a media key)"
                                            : ": dropped (the same press from another device)");
                std::fflush(stdout);
                source.repeat_logged = event.value == 2; // one auto-repeat line per press
            }
            if (!pass) continue;
            if (listener) listener(context, event.code, event.value);
            if (event.code != KEY_ESC) continue;
            if (event.value == 1) source.esc_down = true;
            else if (event.value == 0) source.esc_down = false;
            else continue; // auto-repeat
            changed = esc_held() ? 1 : 0;
        }
        // 0 = end of file (a FIFO writer left), < 0 other than EAGAIN = the keyboard slept or was unplugged
        if (got == 0 || (got < 0 && errno != EAGAIN && errno != EINTR)) {
            drop(i);
            changed = esc_held() ? 1 : 0;
        }
    }
    return changed;
}

bool Sources::esc_held() const
{
    for (const Source &source : sources_)
        if (source.fd >= 0 && source.esc_down) return true;
    return false;
}

std::size_t Sources::fds(int *out, std::size_t max) const
{
    std::size_t n = 0;
    for (const Source &source : sources_)
        if (source.fd >= 0 && n < max && out) out[n++] = source.fd;
    return n;
}

std::size_t Sources::count() const
{
    std::size_t n = 0;
    for (const Source &source : sources_)
        if (source.fd >= 0) ++n;
    return n;
}

void Sources::close_all()
{
    for (std::size_t i = 0; i < kMax; ++i)
        if (sources_[i].fd >= 0) drop(i);
    cp0_media_keys_init(&media_keys_);
}

} // namespace cp0_esc_key_watch

#endif
