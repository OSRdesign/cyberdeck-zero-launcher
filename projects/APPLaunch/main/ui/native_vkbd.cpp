/*
 * Virtual keyboard hub for stock (framebuffer) apps on the Raspberry Pi Zero 2W port.
 */

#include "native_vkbd.hpp"

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "sample_log.h"

#include <atomic>
#include <cerrno>
#include <chrono>
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

void forward_loop(std::string source)
{
    // same Fn alias as the launcher (APPLAUNCH_FN_KEY=<evdev code>): the app sees the Cardputer's KEY_FN
    unsigned fn_alias = 0;
    if (const char *fn_env = std::getenv("APPLAUNCH_FN_KEY")) fn_alias = static_cast<unsigned>(std::strtoul(fn_env, nullptr, 0));
    int fd = -1;
    while (!s_stop.load()) {
        if (fd < 0) {
            fd = ::open(source.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                // the Bluetooth keyboard may be asleep: try again shortly
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                continue;
            }
        }
        struct pollfd pfd = {fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, 100);
        if (ready < 0 && errno != EINTR) break;
        if (ready <= 0) continue;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            ::close(fd);
            fd = -1;
            continue;
        }
        struct input_event event;
        ssize_t n;
        while ((n = ::read(fd, &event, sizeof(event))) == static_cast<ssize_t>(sizeof(event))) {
            // press/release only: the app generates its own key repeat
            if (event.type == EV_KEY && (event.value == 0 || event.value == 1))
                native_vkbd::send(fn_alias != 0 && event.code == fn_alias ? KEY_FN : event.code, event.value);
        }
        if (n < 0 && errno != EAGAIN && errno != EINTR) {
            ::close(fd); // ENODEV when the keyboard disconnects
            fd = -1;
        }
    }
    if (fd >= 0) ::close(fd);
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
    s_thread = std::thread(forward_loop, physical_device);
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
