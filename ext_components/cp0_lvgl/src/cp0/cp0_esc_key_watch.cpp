/*
 * Esc-hold watcher sources (see cp0_esc_key_watch.hpp).
 */

#include "cp0_esc_key_watch.hpp"

#if !defined(_WIN32)

#include <cerrno>
#include <cstdio>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cp0_esc_key_watch {

bool Sources::open(const char *path)
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
    std::printf("[cp0] Esc watcher reads %s\n", path);
    std::fflush(stdout);
    return true;
}

void Sources::drop(std::size_t index)
{
    if (sources_[index].fd >= 0) ::close(sources_[index].fd);
    sources_[index] = Source();
}

int Sources::poll()
{
    int changed = -1;
    for (std::size_t i = 0; i < kMax; ++i) {
        Source &source = sources_[i];
        if (source.fd < 0) continue;
        struct input_event event;
        ssize_t got;
        while ((got = ::read(source.fd, &event, sizeof(event))) == static_cast<ssize_t>(sizeof(event))) {
            if (event.type != EV_KEY || event.code != KEY_ESC) continue;
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
}

} // namespace cp0_esc_key_watch

#endif
