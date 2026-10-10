// Esc-hold watcher sources: one keyboard behaves as the single-device watcher did, several keyboards are OR-ed,
// a keyboard that leaves releases its Esc, one node is read once, every fd is closed. FIFOs stand in for evdev.
#include "cp0_esc_key_watch.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string g_dir;

std::string make_fifo(const char *name)
{
    const std::string path = g_dir + "/" + name;
    assert(::mkfifo(path.c_str(), 0600) == 0);
    return path;
}

// O_RDWR keeps the FIFO open without a reader, so reads return EAGAIN (not EOF) until the "keyboard" leaves.
int plug(const std::string &path)
{
    const int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    assert(fd >= 0);
    return fd;
}

void send(int fd, unsigned short type, unsigned short code, int value)
{
    struct input_event event;
    std::memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = value;
    assert(::write(fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event)));
}

void key(int fd, unsigned short code, int value)
{
    send(fd, EV_MSC, MSC_SCAN, 0x29);
    send(fd, EV_KEY, code, value);
    send(fd, EV_SYN, SYN_REPORT, 0);
}

int open_fds()
{
    int n = 0;
    DIR *dir = ::opendir("/proc/self/fd");
    assert(dir);
    while (::readdir(dir)) ++n;
    ::closedir(dir);
    return n;
}

void test_single_keyboard_matches_old_watcher()
{
    const std::string m4 = make_fifo("m4");
    const int writer = plug(m4);
    cp0_esc_key_watch::Sources sources;
    assert(sources.open(m4.c_str()));
    assert(sources.count() == 1);
    assert(sources.poll() == -1);                 // nothing yet
    key(writer, KEY_A, 1);
    key(writer, KEY_A, 0);
    assert(sources.poll() == -1);                 // other keys never touch the Esc state
    key(writer, KEY_ESC, 1);
    assert(sources.poll() == 1);                  // press -> 1
    key(writer, KEY_ESC, 2);
    key(writer, KEY_ESC, 2);
    assert(sources.poll() == -1);                 // auto-repeat ignored: the 3 s hold keeps running
    assert(sources.esc_held());
    key(writer, KEY_ESC, 0);
    assert(sources.poll() == 0);                  // release -> 0
    key(writer, KEY_ESC, 0);
    assert(sources.poll() == 0);                  // a release always writes 0, as before
    ::close(writer);
    assert(sources.poll() == 0);                  // keyboard gone -> 0, as before
    assert(sources.count() == 0);
}

void test_two_keyboards_or_and_unplug_while_held()
{
    const std::string m4 = make_fifo("m4b");
    const std::string usb = make_fifo("usb");
    const int m4_writer = plug(m4);
    const int usb_writer = plug(usb);
    cp0_esc_key_watch::Sources sources;
    assert(sources.open(m4.c_str()));
    assert(sources.open(usb.c_str()));
    assert(sources.count() == 2);

    key(usb_writer, KEY_ESC, 1);                  // USB-only Esc works
    assert(sources.poll() == 1);
    key(m4_writer, KEY_ESC, 1);
    assert(sources.poll() == 1);
    key(usb_writer, KEY_ESC, 0);
    assert(sources.poll() == 1);                  // still held on the M4
    key(m4_writer, KEY_ESC, 0);
    assert(sources.poll() == 0);

    key(usb_writer, KEY_ESC, 1);
    assert(sources.poll() == 1);
    ::close(usb_writer);                          // unplugged while holding Esc
    assert(sources.poll() == 0);                  // released: the policy cancels the hold
    assert(sources.count() == 1);
    assert(!sources.esc_held());

    const int usb_again = plug(usb);              // hotplug back
    assert(sources.open(usb.c_str()));
    assert(sources.count() == 2);
    key(usb_again, KEY_ESC, 1);
    assert(sources.poll() == 1);
    ::close(usb_again);
    ::close(m4_writer);
}

void test_same_node_read_once_and_missing_path()
{
    const std::string node = make_fifo("event7");
    const std::string link = g_dir + "/by-path-kbd";
    assert(::symlink(node.c_str(), link.c_str()) == 0);
    const int writer = plug(node);
    cp0_esc_key_watch::Sources sources;
    assert(sources.open(link.c_str()));
    assert(!sources.open(node.c_str()));          // the symlink and the eventN are one keyboard
    assert(sources.count() == 1);
    assert(!sources.open((g_dir + "/asleep").c_str())); // a sleeping Bluetooth keyboard: retried later
    assert(!sources.open(""));
    assert(!sources.open(nullptr));
    assert(sources.count() == 1);
    ::close(writer);
}

void test_no_fd_leak()
{
    const int before = open_fds();
    {
        const std::string a = make_fifo("leak_a");
        const std::string b = make_fifo("leak_b");
        const int wa = plug(a);
        const int wb = plug(b);
        cp0_esc_key_watch::Sources sources;
        assert(sources.open(a.c_str()));
        assert(sources.open(b.c_str()));
        const int during = open_fds();
        assert(during == before + 4);
        sources.close_all();
        assert(sources.count() == 0);
        assert(open_fds() == before + 2);
        assert(sources.open(a.c_str()));          // reopened, then released by the destructor
        ::close(wa);
        ::close(wb);
    }
    assert(open_fds() == before);
}

void test_capacity()
{
    cp0_esc_key_watch::Sources sources;
    int writers[cp0_esc_key_watch::Sources::kMax + 1];
    for (std::size_t i = 0; i <= cp0_esc_key_watch::Sources::kMax; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "cap%zu", i);
        const std::string path = make_fifo(name);
        writers[i] = plug(path);
        assert(sources.open(path.c_str()) == (i < cp0_esc_key_watch::Sources::kMax));
    }
    assert(sources.count() == cp0_esc_key_watch::Sources::kMax);
    for (int fd : writers) ::close(fd);
}

} // namespace

int main()
{
    char tmpl[] = "/tmp/cp0_esc_watch_XXXXXX";
    assert(::mkdtemp(tmpl));
    g_dir = tmpl;
    test_single_keyboard_matches_old_watcher();
    test_two_keyboards_or_and_unplug_while_held();
    test_same_node_read_once_and_missing_path();
    test_no_fd_leak();
    test_capacity();
    const std::string cleanup = "rm -rf '" + g_dir + "'";
    if (std::system(cleanup.c_str()) != 0) return 1;
    std::printf("test_esc_key_watch: OK\n");
    return 0;
}
