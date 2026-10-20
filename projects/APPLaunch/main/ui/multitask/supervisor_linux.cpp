/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-b: the Linux side of the supervisor (see supervisor.hpp): spawning with a control socketpair, process
 * groups and /proc, the input drain through pidfd_getfd (as prototyped in tools/multitasking-spike/src/fbhandoff.c),
 * oom_score_adj, MemAvailable and the guardian process.
 */
#include "supervisor.hpp"

#include "lifecycle_protocol.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#if defined(__linux__)
#include <cerrno>
#include <csignal>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_getfd
#define SYS_pidfd_getfd 438
#endif
#endif

namespace multitask {

#if defined(__linux__)

namespace {

/* pid, state and process group of /proc/<pid>/stat ("pid (comm) S ppid pgrp ..."; comm may hold spaces) */
bool read_stat(int pid, char &state, int &pgrp)
{
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = std::fopen(path, "r");
    if (!f) return false;
    char buf[512];
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = '\0';
    const char *close_paren = std::strrchr(buf, ')');
    if (!close_paren) return false;
    int ppid = 0;
    return std::sscanf(close_paren + 1, " %c %d %d", &state, &ppid, &pgrp) == 3;
}

bool is_number(const char *s)
{
    if (!s || !*s) return false;
    for (; *s; ++s)
        if (*s < '0' || *s > '9') return false;
    return true;
}

std::string errno_name(int e)
{
    switch (e) {
    case EPERM: return "EPERM";
    case EACCES: return "EACCES";
    case ESRCH: return "ESRCH";
    case EBADF: return "EBADF";
    case ENOSYS: return "ENOSYS";
    case EMFILE: return "EMFILE";
    default: return std::to_string(e);
    }
}

void count_events(DrainStats &st, const unsigned char *buf, size_t bytes, bool &multitouch)
{
    const size_t n = bytes / sizeof(struct input_event);
    for (size_t i = 0; i < n; ++i) {
        struct input_event e;
        std::memcpy(&e, buf + i * sizeof(e), sizeof(e));
        ++st.events;
        if (e.type == EV_KEY && e.value == 1 && e.code < BTN_MISC) ++st.keys;
        if (e.type == EV_ABS && e.code == ABS_MT_TRACKING_ID) {
            multitouch = true;
            if (e.value >= 0) ++st.touches;
        }
        if (!multitouch && e.type == EV_KEY && e.code == BTN_TOUCH && e.value == 1) ++st.touches;
        if (e.type == EV_SYN && e.code == SYN_DROPPED) ++st.dropped;
    }
}

} // namespace

std::vector<int> group_members(int pgid)
{
    std::vector<int> pids;
    if (pgid <= 0) return pids;
    DIR *d = ::opendir("/proc");
    if (!d) return pids;
    while (struct dirent *e = ::readdir(d)) {
        if (!is_number(e->d_name)) continue;
        const int pid = std::atoi(e->d_name);
        char state = 0;
        int pgrp = 0;
        if (read_stat(pid, state, pgrp) && pgrp == pgid) pids.push_back(pid);
    }
    ::closedir(d);
    return pids;
}

DrainStats drain_group_input(int pgid, const std::string &prefix)
{
    DrainStats st;
    for (const int pid : group_members(pgid)) {
        char dir[64];
        std::snprintf(dir, sizeof(dir), "/proc/%d/fd", pid);
        DIR *d = ::opendir(dir);
        if (!d) continue;
        int pidfd = -1;
        while (struct dirent *e = ::readdir(d)) {
            if (!is_number(e->d_name)) continue;
            char target[256];
            const std::string link = std::string(dir) + "/" + e->d_name;
            const ssize_t r = ::readlink(link.c_str(), target, sizeof(target) - 1);
            if (r <= 0) continue;
            target[r] = '\0';
            if (std::strncmp(target, prefix.c_str(), prefix.size()) != 0) continue;
            if (pidfd < 0) {
                pidfd = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0));
                if (pidfd < 0) {
                    st.error = "pidfd_open:" + errno_name(errno);
                    break;
                }
            }
            const int dup = static_cast<int>(::syscall(SYS_pidfd_getfd, pidfd, std::atoi(e->d_name), 0));
            if (dup < 0) {
                st.error = "pidfd_getfd:" + errno_name(errno); /* EPERM: no ptrace access (Yama ptrace_scope 2/3) */
                continue;
            }
            ++st.fds;
            bool multitouch = false;
            for (;;) { /* the same open file as the app's: poll first (it may be a blocking fd), read until empty */
                struct pollfd p = {dup, POLLIN, 0};
                if (::poll(&p, 1, 0) != 1 || !(p.revents & POLLIN)) break;
                unsigned char buf[sizeof(struct input_event) * 64];
                const ssize_t got = ::read(dup, buf, sizeof(buf));
                if (got <= 0) break;
                count_events(st, buf, static_cast<size_t>(got), multitouch);
            }
            ::close(dup);
        }
        if (pidfd >= 0) ::close(pidfd);
        ::closedir(d);
    }
    return st;
}

long read_mem_available_kb(const char *meminfo_path)
{
    FILE *f = std::fopen(meminfo_path, "r");
    if (!f) return -1;
    char line[256];
    long kb = -1;
    while (std::fgets(line, sizeof(line), f))
        if (std::sscanf(line, "MemAvailable: %ld kB", &kb) == 1) break;
    std::fclose(f);
    return kb;
}

/* ------------------------------------------------------------------------------------------------ processes */

LinuxProcessPort::LinuxProcessPort(ExecChild exec_child) : exec_child_(exec_child) {}

SpawnResult LinuxProcessPort::spawn(const AppSpec &spec, bool with_control)
{
    SpawnResult result;
    int sv[2] = {-1, -1};
    if (with_control && ::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) != 0) {
        std::fprintf(stderr, "[mt] socketpair: %s (no control socket for %s)\n", std::strerror(errno),
                     spec.name.c_str());
        sv[0] = sv[1] = -1;
    }
    const std::string command = (sv[1] >= 0 ? protocol::env_exports(sv[1]) : std::string()) + spec.exec;
    int base = 0;
    {
        FILE *f = std::fopen("/proc/self/oom_score_adj", "r");
        if (f) {
            if (std::fscanf(f, "%d", &base) != 1) base = 0;
            std::fclose(f);
        }
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        const int saved = errno;
        if (sv[0] >= 0) ::close(sv[0]);
        if (sv[1] >= 0) ::close(sv[1]);
        errno = saved;
        return result;
    }
    if (pid == 0) {
        /* the child: async-signal-safe calls until exec */
        ::setpgid(0, 0);
        sigset_t none;
        sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr); /* the supervisor thread's mask is not the app's business */
        if (sv[1] >= 0) ::fcntl(sv[1], F_SETFD, 0); /* inherited: APPLAUNCH_CONTROL_FD */
        if (exec_child_) exec_child_(command.c_str(), spec.keep_root);
        else ::execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char *>(nullptr));
        ::_exit(127);
    }
    ::setpgid(pid, pid);
    if (sv[1] >= 0) ::close(sv[1]);
    result.ok = true;
    result.pid = pid;
    result.pgid = pid;
    result.control_fd = sv[0];
    result.oom_base = base;
    return result;
}

void LinuxProcessPort::signal_group(int pgid, int sig)
{
    if (pgid > 0) (void)::killpg(pgid, sig);
}

bool LinuxProcessPort::wait_stopped(int pgid, int timeout_ms)
{
    const std::uint64_t until = Supervisor::now_ms() + static_cast<std::uint64_t>(timeout_ms);
    for (;;) {
        bool all = true;
        for (const int pid : group_members(pgid)) {
            char state = 0;
            int pgrp = 0;
            if (!read_stat(pid, state, pgrp)) continue;
            if (state != 'T' && state != 't' && state != 'Z' && state != 'X') all = false;
        }
        if (all) return true;
        if (Supervisor::now_ms() >= until) return false;
        ::usleep(1000);
    }
}

bool LinuxProcessPort::reap(int pgid)
{
    if (pgid <= 0) return true;
    for (;;) {
        int status = 0;
        const pid_t r = ::waitpid(-pgid, &status, WNOHANG);
        if (r > 0) continue;
        if (r < 0 && errno == EINTR) continue;
        break;
    }
    if (::kill(-pgid, 0) == 0) return false;
    return errno != EPERM;
}

void LinuxProcessPort::set_oom(int pgid, int value)
{
    const std::string text = std::to_string(value);
    for (const int pid : group_members(pgid)) {
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%d/oom_score_adj", pid);
        const int fd = ::open(path, O_WRONLY | O_CLOEXEC);
        if (fd < 0) continue;
        if (::write(fd, text.data(), text.size()) < 0)
            std::fprintf(stderr, "[mt] oom_score_adj %d for pid %d: %s\n", value, pid, std::strerror(errno));
        ::close(fd);
    }
}

long LinuxProcessPort::mem_available_kb()
{
    return read_mem_available_kb();
}

/* ------------------------------------------------------------------------------------------------ guardian */

struct Guardian::Shared {
    volatile int pgid[Guardian::kSlots];
};

bool Guardian::start()
{
    if (pid_ > 0) return true;
    void *mem = ::mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return false;
    shared_ = static_cast<Shared *>(mem);
    for (int i = 0; i < kSlots; ++i) shared_->pgid[i] = 0;
    int p[2];
    if (::pipe2(p, O_CLOEXEC) != 0) { /* CLOEXEC: an app must never hold the write end */
        ::munmap(mem, sizeof(Shared));
        shared_ = nullptr;
        return false;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(p[0]);
        ::close(p[1]);
        ::munmap(mem, sizeof(Shared));
        shared_ = nullptr;
        return false;
    }
    if (pid == 0) {
        /* the guardian: only async-signal-safe calls (the launcher has threads) */
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_IGN;
        const int ignored[] = {SIGTERM, SIGINT, SIGHUP, SIGQUIT, SIGPIPE, SIGUSR1, SIGUSR2, SIGTSTP};
        for (const int s : ignored) ::sigaction(s, &sa, nullptr);
        sigset_t none;
        sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        ::prctl(PR_SET_NAME, "mt-guard", 0, 0, 0);
        for (int fd = 3; fd < 4096; ++fd)
            if (fd != p[0]) ::close(fd);
        char c;
        for (;;) {
            const ssize_t r = ::read(p[0], &c, 1);
            if (r == 0) break;
            if (r < 0 && errno != EINTR) break;
        }
        volatile int *table = shared_->pgid;
        int any = 0;
        for (int i = 0; i < kSlots; ++i) {
            const int g = table[i];
            if (g <= 0) continue;
            ::kill(-g, SIGCONT); /* a stopped process acts on SIGTERM only once continued */
            ::kill(-g, SIGTERM);
            any = 1;
        }
        if (any) {
            const struct timespec step = {0, 100 * 1000 * 1000};
            for (int t = 0; t < 30; ++t) {
                int alive = 0;
                for (int i = 0; i < kSlots; ++i)
                    if (table[i] > 0 && ::kill(-table[i], 0) == 0) alive = 1;
                if (!alive) break;
                ::nanosleep(&step, nullptr);
            }
            for (int i = 0; i < kSlots; ++i)
                if (table[i] > 0) ::kill(-table[i], SIGKILL);
        }
        ::_exit(0);
    }
    ::close(p[0]);
    write_fd_ = p[1];
    pid_ = pid;
    return true;
}

int Guardian::set(int slot, int pgid)
{
    if (!shared_) return -1;
    if (slot < 0) {
        for (int i = 0; i < kSlots && slot < 0; ++i)
            if (shared_->pgid[i] == 0) slot = i;
        if (slot < 0) return -1;
    }
    if (slot >= kSlots) return -1;
    shared_->pgid[slot] = pgid;
    return slot;
}

void Guardian::finish()
{
    if (shared_)
        for (int i = 0; i < kSlots; ++i) shared_->pgid[i] = 0;
    if (write_fd_ >= 0) {
        ::close(write_fd_);
        write_fd_ = -1;
    }
    if (pid_ > 0) {
        (void)::waitpid(pid_, nullptr, 0);
        pid_ = -1;
    }
    if (shared_) {
        ::munmap(shared_, sizeof(Shared));
        shared_ = nullptr;
    }
}

#else // !__linux__

std::vector<int> group_members(int) { return {}; }
DrainStats drain_group_input(int, const std::string &) { return DrainStats(); }
long read_mem_available_kb(const char *) { return -1; }
LinuxProcessPort::LinuxProcessPort(ExecChild exec_child) : exec_child_(exec_child) {}
SpawnResult LinuxProcessPort::spawn(const AppSpec &, bool) { return SpawnResult(); }
void LinuxProcessPort::signal_group(int, int) {}
bool LinuxProcessPort::wait_stopped(int, int) { return true; }
bool LinuxProcessPort::reap(int) { return true; }
void LinuxProcessPort::set_oom(int, int) {}
long LinuxProcessPort::mem_available_kb() { return -1; }
struct Guardian::Shared {
    int unused;
};
bool Guardian::start() { return false; }
int Guardian::set(int, int) { return -1; }
void Guardian::finish() {}

#endif

} // namespace multitask
