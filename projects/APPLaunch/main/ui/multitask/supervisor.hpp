/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-b supervisor (task 015 section 3): the loop that owns the resident fullscreen apps. It replaces the
 * blocking runner loop for the apps started under APPLAUNCH_MULTITASK=1 and runs on its own thread, so the launcher's
 * UI thread is never blocked by an app:
 *   - requests from the UI thread (launch, resume, home, close, close all, Alt+Tab events) are queued;
 *   - every tick (10 ms): control sockets (ack, home, switcher), reaping, the keyboards from evdev while an app is in
 *     front (Esc held 3 s, Alt+Tab, Super), deadlines, MemAvailable every 2 s while an app is hidden, the dev
 *     control FIFO, the status file;
 *   - the pure multitask::Model decides; the actions it returns are carried out through ports, so the same code runs
 *     in the launcher (framebuffer and input of cp0_lvgl, its keyboard thread) and in the host tests (a file-backed
 *     framebuffer, FIFOs as keyboards, a fake app).
 * Linux only (signals, sockets, /proc); elsewhere it compiles to a stub that refuses to start.
 */
#pragma once

#include "multitask_model.hpp"
#include "switch_keys.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace multitask {

/* ---------------------------------------------------------------------------------------------------- ports */

struct SpawnResult {
    bool ok = false;
    int pid = 0;
    int pgid = 0;
    int control_fd = -1; /* the launcher's end of the socketpair, or -1 */
    int oom_base = 0;    /* the app's oom_score_adj at start (inherited from the launcher) */
};

class ProcessPort {
public:
    virtual ~ProcessPort() = default;
    /* Start `spec.exec` (an sh -c command line) in its own process group; with_control: a SOCK_SEQPACKET socketpair,
     * the app's end inherited and named by APPLAUNCH_LIFECYCLE=1 APPLAUNCH_CONTROL_FD=<n>. */
    virtual SpawnResult spawn(const AppSpec &spec, bool with_control) = 0;
    virtual void signal_group(int pgid, int sig) = 0;
    /* Waits until every process of the group is stopped (or gone); false after timeout_ms. */
    virtual bool wait_stopped(int pgid, int timeout_ms) = 0;
    /* Reaps what can be reaped; true when the group is empty. */
    virtual bool reap(int pgid) = 0;
    virtual void set_oom(int pgid, int value) = 0;
    /* MemAvailable in kB, -1 when unknown. */
    virtual long mem_available_kb() = 0;
};

class FramebufferPort {
public:
    virtual ~FramebufferPort() = default;
    /* From its return on, the launcher writes no pixel (until its UI thread takes the panel back). */
    virtual void suspend() = 0;
    virtual bool snapshot(std::vector<std::uint8_t> &out, int &page_y) = 0;
    /* 0 written and verified, 1 written but read back different, -1 failed. */
    virtual int restore(const std::vector<std::uint8_t> &in, int page_y) = 0;
};

struct DrainStats {
    int fds = 0;        /* evdev fds of the group seen */
    long events = 0;
    long keys = 0;      /* key presses */
    long touches = 0;   /* touch-downs */
    long dropped = 0;   /* SYN_DROPPED */
    std::string error;  /* "" or why some fd could not be read (pidfd_getfd needs ptrace access) */
};

class InputPort {
public:
    virtual ~InputPort() = default;
    virtual void grab_keys(bool on) = 0;     /* EVIOCGRAB on the launcher's own keyboard fds */
    virtual void grab_touch(bool on) = 0;    /* EVIOCGRAB on the launcher's own touch fd */
    virtual void launcher_keys(bool on) = 0; /* resume / pause the launcher's keyboard thread */
    virtual DrainStats drain(int pgid) = 0;  /* empty the group's own evdev queues */
};

class KeyPort {
public:
    using Listener = void (*)(void *context, int code, int value);
    virtual ~KeyPort() = default;
    virtual void open() = 0;  /* start reading the keyboards (an app is in front, or an Alt+Tab session runs) */
    virtual void close() = 0;
    /* Reads what is pending: every key to `listener`; returns the new "Esc held" state (0 / 1) or -1 unchanged. */
    virtual int poll(Listener listener, void *context) = 0;
    virtual void esc_state(int held) = 0; /* publish Esc held / released (cp0_esc_state) */
};

class UiPort {
public:
    virtual ~UiPort() = default;
    virtual void app_front(AppId id, const std::string &name) = 0; /* the display is already suspended */
    virtual void launcher_front(bool after_exit) = 0;
    virtual void repaint() = 0;
    virtual void notice(const std::string &text) = 0;
    virtual void overlay(const std::string &text) = 0; /* the Alt+Tab text; "" hides it */
    virtual void esc_hint(bool visible) = 0;
    virtual void residents(int count) = 0;
};

/* Guardian (task 015 section 3): a small process forked from the launcher that, when the launcher dies without
 * cleaning up (SIGKILL, a crash, the staged test launcher stopped with pkill), sends SIGCONT and SIGTERM to every
 * process group listed, then SIGKILL 3 s later. Under the systemd service the cgroup does the same; the guardian
 * covers runs outside it. Its process name is "mt-guard" (pkill -x cyberdeckzero does not match it). */
class Guardian {
public:
    static constexpr int kSlots = 16;
    Guardian() = default;
    Guardian(const Guardian &) = delete;
    Guardian &operator=(const Guardian &) = delete;
    ~Guardian() { finish(); }
    bool start();             /* forks; false on failure (then there is no guardian) */
    bool started() const { return pid_ > 0; }
    /* slot -1 = the first free one; returns the slot used, or -1. pgid 0 frees the slot. */
    int set(int slot, int pgid);
    void finish();            /* normal end: the table is cleared, the guardian exits and is reaped */

private:
    struct Shared;
    Shared *shared_ = nullptr;
    int write_fd_ = -1;
    int pid_ = -1;
};

/* --------------------------------------------------------------------------------------------- supervisor */

struct SupervisorConfig {
    Config model;
    bool use_grab = true;          /* APPLAUNCH_MT_INPUT: grab+drain (default), grab, drain, none */
    bool use_drain = true;
    std::string status_path;       /* debug status file, "" = none */
    std::string control_fifo;      /* dev control FIFO, "" = none */
    int tick_ms = 10;
    std::uint32_t mem_poll_ms = 2000;
    std::uint32_t show_ack_log_ms = 1000;
    int stop_wait_ms = 3000;       /* stop(): SIGTERM -> SIGKILL */
    bool guardian = true;          /* start() forks the guardian */
};

class Supervisor {
public:
    using LogSink = std::function<void(const std::string &line)>;

    Supervisor(SupervisorConfig config, ProcessPort &proc, FramebufferPort &fb, InputPort &input, KeyPort &keys,
               UiPort &ui);
    ~Supervisor();
    Supervisor(const Supervisor &) = delete;
    Supervisor &operator=(const Supervisor &) = delete;

    /* Runs the loop on its own thread. */
    bool start();
    /* Every app gets SIGCONT then SIGTERM, SIGKILL after stop_wait_ms; the thread ends. Idempotent. */
    void stop();
    bool running() const { return thread_.joinable(); }

    /* ---- requests, any thread (queued, carried out by the loop) ---- */
    void launch(const AppSpec &spec);
    void resume(AppId id);
    void go_launcher();
    void close(AppId id);
    void close_all();
    /* An Alt+Tab / Super event read by the launcher's key filter (launcher in front). */
    void launcher_switch_event(SwitchEvent event);
    /* A fake MemAvailable reading (dev FIFO "memory <kB>"; tests). */
    void fake_memory(long kb);
    /* One line of the dev control FIFO (also used by tests): home, switch X, close X, close-all, status, memory N. */
    void command(const std::string &line);

    /* ---- any thread ---- */
    int resident_count() const { return residents_.load(); }
    bool is_resident_key(const std::string &key) const;
    std::string status_text() const;
    void set_log_sink(LogSink sink);

    /* ---- tests: drive the loop without the thread ---- */
    void run_once();

    static std::uint64_t now_ms();

private:
    struct Runtime {
        std::string name;
        int pgid = 0;
        int guardian_slot = -1;
        int control_fd = -1;
        std::vector<std::uint8_t> snapshot;
        int page_y = 0;
        std::uint64_t hide_sent_ms = 0;
        std::uint32_t show_n = 0;
        std::uint64_t show_sent_ms = 0;
        bool show_waiting = false;
    };
    struct Request {
        enum class Kind { Launch, Resume, Home, Close, CloseAll, Switch, Memory, Command } kind = Kind::Home;
        AppSpec spec;
        AppId id = kNoApp;
        SwitchEvent event = SwitchEvent::None;
        long value = 0;
        std::string text;
    };
    struct Session {
        bool active = false;
        bool raw = false;
        AppId origin = kNoApp;
        std::vector<AppId> cycle; /* kNoApp = Home */
        int selected = 0;
    };

    void loop();
    void shutdown_now();
    void wait_for_work();
    void forget(AppId id);
    void post(Request request);
    void handle(const Request &request);
    void execute(Actions actions);
    void execute_one(const Action &action, std::deque<Action> &more);
    void poll_controls(std::uint64_t now);
    void poll_reaping(std::uint64_t now);
    void poll_keys(std::uint64_t now);
    void poll_fifo();
    void switch_event(SwitchEvent event, bool raw);
    void end_session(AppId target);
    std::string session_text() const;
    void handle_command(const std::string &line);
    AppId lookup(const std::string &what) const;
    void write_status();
    void log(const std::string &line);
    void publish_residents();
    static void on_raw_key(void *context, int code, int value);

    SupervisorConfig config_;
    ProcessPort &proc_;
    FramebufferPort &fb_;
    InputPort &input_;
    KeyPort &keys_;
    UiPort &ui_;
    Model model_;
    std::map<AppId, Runtime> runtime_;
    SwitchKeys raw_keys_;
    bool raw_open_ = false;
    Session session_;
    std::vector<SwitchEvent> raw_events_;
    bool esc_down_ = false;
    std::uint64_t esc_press_ms_ = 0; /* the Esc-hold policy of today: hint at 500 ms, SIGTERM at 3 s */
    bool esc_hint_ = false;
    bool esc_fired_ = false;
    Guardian guardian_;
    std::uint64_t next_mem_ms_ = 0;
    long fake_mem_kb_ = -1;
    int fifo_fd_ = -1;
    std::string fifo_buf_;
    std::string last_status_;

    mutable std::mutex mutex_;          /* requests_, status_text_, keys_resident_, log_sink_ */
    std::deque<Request> requests_;
    std::string status_text_;
    std::vector<std::string> keys_resident_;
    LogSink log_sink_;
    std::atomic<int> residents_{0};
    std::atomic<bool> stop_{false};
    int wake_fd_[2] = {-1, -1};
    std::thread thread_;
    bool stopped_ = false;
};

/* ------------------------------------------------------------------------------ Linux helpers (supervisor_linux) */

/* Reads `pgid`'s processes from /proc (pid list). */
std::vector<int> group_members(int pgid);
/* Empties every evdev queue the group has open (fds whose /proc link starts with `prefix`), through pidfd_getfd. */
DrainStats drain_group_input(int pgid, const std::string &prefix);
/* MemAvailable from /proc/meminfo (kB), -1 when missing. */
long read_mem_available_kb(const char *meminfo_path = "/proc/meminfo");

/* The ProcessPort of the launcher and of the host tests. `exec_child` runs in the forked child to replace it with the
 * command (it must exec or _exit); nullptr = /bin/sh -c. */
class LinuxProcessPort : public ProcessPort {
public:
    using ExecChild = void (*)(const char *command, bool keep_root);
    explicit LinuxProcessPort(ExecChild exec_child = nullptr);
    SpawnResult spawn(const AppSpec &spec, bool with_control) override;
    void signal_group(int pgid, int sig) override;
    bool wait_stopped(int pgid, int timeout_ms) override;
    bool reap(int pgid) override;
    void set_oom(int pgid, int value) override;
    long mem_available_kb() override;

private:
    ExecChild exec_child_;
};

} // namespace multitask
