/*
 * Virtual keyboard hub for stock (framebuffer) apps on the Raspberry Pi Zero 2W port.
 */

#include "native_vkbd.hpp"

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "cp0_keyboard_presence.h"
#include "keyboard_input.h"
#include "sample_log.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr const char *kDevicePath = "/dev/input/applaunch-vkbd";

std::mutex s_mutex; // serialises writes (toolbar on the UI thread, forwarder thread)
int s_fd = -1;

std::thread s_thread;
std::atomic<bool> s_stop{false};

void emit(int type, int code, int value)
{
    struct input_event event;
    std::memset(&event, 0, sizeof(event));
    event.type = static_cast<unsigned short>(type);
    event.code = static_cast<unsigned short>(code);
    event.value = value;
    if (::write(s_fd, &event, sizeof(event)) < 0) {
        // the device is a best-effort pipe: nothing useful to do on failure
    }
}

// One physical keyboard mirrored into the hub. `pressed` remembers its keys that are down, so that they are
// released in the hub when the keyboard disappears (else the app would keep a stuck Shift/Ctrl or repeat a
// key for ever).
struct Source {
    int fd = -1;
    dev_t rdev = 0;
    unsigned char pressed[(KEY_CNT + 7) / 8] = {};
};

constexpr size_t kMaxSources = CP0_KBD_PRESENCE_MAX_DEVICES + 1; // every listed keyboard + the explicit device
constexpr auto kRescanInterval = std::chrono::milliseconds(300);

bool key_down(const Source &source, unsigned code)
{
    return code < KEY_CNT && (source.pressed[code / 8] & (1u << (code % 8))) != 0;
}

void set_key(Source &source, unsigned code, bool down)
{
    if (code >= KEY_CNT) return;
    if (down) source.pressed[code / 8] = static_cast<unsigned char>(source.pressed[code / 8] | (1u << (code % 8)));
    else source.pressed[code / 8] = static_cast<unsigned char>(source.pressed[code / 8] & ~(1u << (code % 8)));
}

// Close one source and release, in the hub, the keys it still holds that no other keyboard holds.
void close_source(Source *sources, size_t index)
{
    Source &source = sources[index];
    for (unsigned code = 0; code < KEY_CNT; ++code) {
        if (!key_down(source, code)) continue;
        set_key(source, code, false);
        bool elsewhere = false;
        for (size_t other = 0; other < kMaxSources && !elsewhere; ++other)
            elsewhere = other != index && sources[other].fd >= 0 && key_down(sources[other], code);
        if (!elsewhere) native_vkbd::send(static_cast<unsigned short>(code), 0);
    }
    if (source.fd >= 0) ::close(source.fd);
    source = Source();
}

// Open `path` unless a source already reads the same device node (the bt-keyboard symlink and the listed
// eventN are one keyboard: reading both would type every key twice).
void open_source(Source *sources, const char *path)
{
    struct stat st;
    if (!path || !path[0] || ::stat(path, &st) != 0 || !S_ISCHR(st.st_mode)) return;
    size_t free_slot = kMaxSources;
    for (size_t i = 0; i < kMaxSources; ++i) {
        if (sources[i].fd >= 0 && sources[i].rdev == st.st_rdev) return;
        if (sources[i].fd < 0 && free_slot == kMaxSources) free_slot = i;
    }
    if (free_slot == kMaxSources) return;
    const int fd = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return; // asleep, or udev has not set the permissions yet: retried
    struct stat opened;
    if (::fstat(fd, &opened) != 0 || opened.st_rdev != st.st_rdev) { // replaced between stat and open
        ::close(fd);
        return;
    }
    sources[free_slot].fd = fd;
    sources[free_slot].rdev = opened.st_rdev;
    SLOGI("[VKBD] mirroring %s", path);
}

// Mirror every keyboard into the hub while a stock app runs: the explicit device (LV_LINUX_KEYBOARD_DEVICE,
// e.g. the Bluetooth keyboard's symlink) and, when the launcher reads all keyboards, every keyboard the
// presence watcher lists. Keyboards that come and go while the app runs are picked up or dropped.
void forward_loop(std::string explicit_device, bool every_keyboard)
{
    // same Fn alias as the launcher (APPLAUNCH_FN_KEY=<evdev code>): the app sees the Cardputer's KEY_FN
    unsigned fn_alias = 0;
    if (const char *fn_env = std::getenv("APPLAUNCH_FN_KEY")) fn_alias = static_cast<unsigned>(std::strtoul(fn_env, nullptr, 0));

    cp0_kbd_presence_watcher_t *watcher = every_keyboard ? cp0_kbd_presence_shared_acquire() : nullptr;
    cp0_kbd_presence_list_t list;
    list.count = 0;
    Source sources[kMaxSources];
    struct pollfd fds[kMaxSources];
    size_t fd_source[kMaxSources];
    unsigned seen_generation = 0;
    bool first = true;
    auto next_rescan = std::chrono::steady_clock::now();

    while (!s_stop.load()) {
        // Open new keyboards when the list changed, and every kRescanInterval for the ones that are not open
        // yet (a Bluetooth keyboard waking up). A keyboard that went away is dropped by its read error.
        const unsigned generation = cp0_kbd_presence_watcher_generation(watcher);
        const auto now = std::chrono::steady_clock::now();
        if (first || generation != seen_generation || now >= next_rescan) {
            first = false;
            seen_generation = generation;
            next_rescan = now + kRescanInterval;
            open_source(sources, explicit_device.c_str());
            if (watcher) {
                cp0_kbd_presence_watcher_snapshot(watcher, &list);
                for (unsigned i = 0; i < list.count; ++i) open_source(sources, list.devices[i].path);
            }
        }

        nfds_t count = 0;
        for (size_t i = 0; i < kMaxSources; ++i) {
            if (sources[i].fd < 0) continue;
            fds[count].fd = sources[i].fd;
            fds[count].events = POLLIN;
            fds[count].revents = 0;
            fd_source[count++] = i;
        }
        if (count == 0) {
            // no keyboard yet (the Bluetooth keyboard may be asleep): try again shortly
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        const int ready = ::poll(fds, count, 100);
        if (ready < 0 && errno != EINTR) break;
        if (ready <= 0) continue;
        for (nfds_t k = 0; k < count; ++k) {
            const size_t index = fd_source[k];
            Source &source = sources[index];
            if (fds[k].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close_source(sources, index);
                continue;
            }
            if (!(fds[k].revents & POLLIN)) continue;
            struct input_event event;
            ssize_t n;
            while ((n = ::read(source.fd, &event, sizeof(event))) == static_cast<ssize_t>(sizeof(event))) {
                // press/release only: the app generates its own key repeat
                if (event.type != EV_KEY || (event.value != 0 && event.value != 1)) continue;
                const unsigned code = fn_alias != 0 && event.code == fn_alias ? KEY_FN : event.code;
                set_key(source, code, event.value == 1);
                native_vkbd::send(static_cast<unsigned short>(code), event.value);
            }
            if (n < 0 && errno != EAGAIN && errno != EINTR) close_source(sources, index); // ENODEV: it left
        }
    }
    for (size_t i = 0; i < kMaxSources; ++i)
        if (sources[i].fd >= 0) close_source(sources, i);
    if (watcher) cp0_kbd_presence_shared_release();
}

} // namespace

namespace native_vkbd {

bool ensure()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_fd < 0) {
        const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            SLOGW("[VKBD] cannot open /dev/uinput: %s", std::strerror(errno));
            return false;
        }
        bool ok = ::ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ::ioctl(fd, UI_SET_EVBIT, EV_SYN) == 0;
        for (int key = 1; ok && key < 0x2ff; ++key) ok = ::ioctl(fd, UI_SET_KEYBIT, key) == 0; // includes KEY_FN
        struct uinput_setup setup;
        std::memset(&setup, 0, sizeof(setup));
        setup.id.bustype = BUS_VIRTUAL;
        setup.id.vendor = 0x1209;
        setup.id.product = 0x0001;
        std::snprintf(setup.name, sizeof(setup.name), "applaunch-vkbd");
        ok = ok && ::ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ::ioctl(fd, UI_DEV_CREATE) == 0;
        if (!ok) {
            SLOGW("[VKBD] uinput setup failed: %s", std::strerror(errno));
            ::close(fd);
            return false;
        }
        s_fd = fd;
    }
    // udev creates the symlink a moment after the device appears
    for (int i = 0; i < 40; ++i) {
        if (::access(kDevicePath, R_OK) == 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    SLOGW("[VKBD] %s did not appear (udev rule installed?)", kDevicePath);
    return false;
}

const char *device_path()
{
    return kDevicePath;
}

void send(unsigned short code, int value)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_fd < 0) return;
    emit(EV_KEY, code, value);
    emit(EV_SYN, SYN_REPORT, 0);
}

void start_forwarding(const std::string &physical_device)
{
    stop_forwarding();
    s_stop.store(false);
    s_thread = std::thread(forward_loop, physical_device, cp0_keyboard_get_read_all_keyboards() != 0);
}

void stop_forwarding()
{
    s_stop.store(true);
    if (s_thread.joinable()) s_thread.join();
}

} // namespace native_vkbd

#else

namespace native_vkbd {

bool ensure() { return false; }
const char *device_path() { return ""; }
void send(unsigned short, int) {}
void start_forwarding(const std::string &) {}
void stop_forwarding() {}

} // namespace native_vkbd

#endif
