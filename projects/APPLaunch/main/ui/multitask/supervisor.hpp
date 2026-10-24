/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-b supervisor (task 015 section 3): the loop that owns the resident fullscreen apps. It replaces the
 * blocking runner loop for the apps started with multitasking on and runs on its own thread, so the launcher's
 * UI thread is never blocked by an app:
 *   - requests from the UI thread (launch, resume, home, close, close all, Alt+Tab events) are queued;
 *   - it sleeps in poll() on what can happen (requests, control sockets with ack / home / switcher, the leaders'
 *     pidfds, the keyboards from evdev while an app is in front: Esc held 3 s, Alt+Tab, Super, the dev control
 *     FIFO; P-c: the touch screen, read passively while an app is in front, for the three-finger hold G1) and wakes
 *     on its own only for what is due (deadlines, the Esc hold, a G1 hold completing, MemAvailable every 2 s while an
 *     app is hidden, keyboards re-attached once a second): no fixed tick (deck finding 2026-10-20);
 *   - the pure multitask::Model decides; the actions it returns are carried out through ports, so the same code runs
 *     in the launcher (framebuffer and input of cp0_lvgl, its keyboard thread) and in the host tests (a file-backed
 *     framebuffer, FIFOs as keyboards, a fake app).
 * Linux only (signals, sockets, /proc); elsewhere it compiles to a stub that refuses to start.
 */
#pragma once

#include "multitask_model.hpp"
#include "switch_keys.hpp"

#include "cp0_gesture_g1.h"
#include "cp0_touch_reader.h"
#include "cp0_touch_slots.h"

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
    int exit_fd = -1;    /* a pidfd of the group's leader: readable once it exited (-1: not supported) */
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
    /* P-d: the frame written back dimmed to the launcher's page (the background of the legacy sheet); 0 done, -1 not
     * supported or failed (the sheet then stands over the frame as it is). */
    virtual int backdrop(const std::vector<std::uint8_t> &frame)
    {
        (void)frame;
        return -1;
    }
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
    /* The open keyboard fds: the loop sleeps in poll() on them instead of reading on a timer. */
    virtual void fds(std::vector<int> &out) = 0;
};

/* Multitasking P-c: the touch screen while an app is in front, for the three-finger hold G1. A second file descriptor
 * on the launcher's touch node, opened when an app comes to the front and closed when the launcher takes the panel
 * back; read passively, NEVER grabbed (the app in front reads the same node and owns the touches). */
class TouchPort {
public:
    using Listener = void (*)(void *context, const cp0_touch_frame_t &frame);
    virtual ~TouchPort() = default;
    virtual bool open() = 0; /* false: no touch device (G1 then never fires from an app) */
    virtual void close() = 0;
    virtual int fd() const = 0; /* for poll(); -1 while closed */
    virtual int down() const = 0; /* contacts down as the reader knows now (after open: read back from the device) */
    /* Reads what is pending: every frame, in screen pixels, stamped on CLOCK_MONOTONIC (microseconds), to listener.
     * false when the device went away. */
    virtual bool poll(Listener listener, void *context) = 0;
};

/* Multitasking P-d: one card of the switcher screen (a resident app). */
struct SwitcherCard {
    AppId id = kNoApp;
    std::string name;
    std::string key;       /* the .desktop Exec line: the launcher finds the app's icon and tile with it */
    State state = State::Gone;
    bool origin = false;   /* the app that was in front when the switcher opened ("In front") */
};

/* What the switcher screen shows (UiPort::switcher). */
struct SwitcherInfo {
    bool open = false;               /* false: the screen goes */
    bool app_next = false;           /* closing because an app takes the panel (the launcher shows nothing between) */
    std::vector<SwitcherCard> cards; /* the resident apps, most recently in front first (at most the cap) */
    int selected = -1;               /* switcher_model item (card, kItemHome, kItemCloseAll); -1 = keep the screen's */
    bool session = false;            /* an Alt+Tab session drives the selection: the screen ignores touch meanwhile */
    std::string source;              /* "G1", "G2", "app", "Alt+Tab" */
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
    /* P-d (SupervisorConfig::switcher_ui), each posted before the launcher_front() it goes with, so the launcher never
     * paints its grid in between: the switcher screen (open, update, close); the legacy sheet for `id` (kNoApp: it
     * goes); the resident apps whenever they or their states change (the home grid's dots). */
    virtual void switcher(const SwitcherInfo &info) { (void)info; }
    virtual void sheet(AppId id, const std::string &name)
    {
        (void)id;
        (void)name;
    }
    virtual void apps(const std::vector<SwitcherCard> &residents) { (void)residents; }
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

/* G1 from evdev while an app is in front (P-c). */
struct TouchConfig {
    bool g1 = false;         /* a TouchPort is set and the panel reports at least 3 slots */
    cp0_g1_params_t params{}; /* hold_ms 0 = cp0_g1_params_default() */
    int w = 640, h = 480;    /* the screen the frames' positions are in */
    int ppmm_x100 = 1130;    /* its density (screen.state ppmm) */
};

struct SupervisorConfig {
    Config model;
    TouchConfig touch;
    bool use_grab = true;          /* APPLAUNCH_MT_INPUT: grab+drain (default), grab, drain, none */
    bool use_drain = true;
    std::string status_path;       /* debug status file, "" = none */
    std::string control_fifo;      /* dev control FIFO, "" = none */
    std::uint32_t mem_poll_ms = 2000;
    std::uint32_t show_ack_log_ms = 1000;
    int stop_wait_ms = 3000;       /* stop(): SIGTERM -> SIGKILL */
    bool guardian = true;          /* start() forks the guardian */
    /* P-d: the switcher screen, the legacy sheet and the home grid's dots (canvases of 640x480 and more); false keeps
     * the P-b / P-c stand-ins (the Alt+Tab text, a gesture = Home). */
    bool switcher_ui = false;
    /* P-d, D15: the first time an app is left open the hint_text notice shows, once: hint_path (a small file) remembers
     * it was shown. "" = no hint. */
    std::string hint_path;
    std::string hint_text;
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
    /* Task c-7.5: a Super tap read by the launcher's key filter (press time on now_ms()'s clock), for the double tap. */
    void launcher_super_tap(std::uint64_t press_ms);
    /* A fake MemAvailable reading (dev FIFO "memory <kB>"; tests). */
    void fake_memory(long kb);
    /* One line of the dev control FIFO (also used by tests): home, switch X, close X, close-all, status, memory N, and
     * (P-c) g1 / g2: a recognised gesture, as if the fingers had made it. */
    void command(const std::string &line);
    /* P-c: a gesture the launcher recognised on its own touch reader (the launcher is in front): `line` is logged; fired
     * = G1 or G2 completed: the switcher stand-in runs. `source` "G1" or "G2". */
    void launcher_gesture(const std::string &source, const std::string &line, bool fired);
    /* P-c: the touch screen for G1 while an app is in front (before start(); nullptr = none). */
    void set_touch_port(TouchPort *port) { touch_ = port; }
    /* P-d: what the user did on the switcher screen and on the legacy sheet (the launcher's UI thread). */
    void switcher_pick(AppId id);  /* a card tapped (or Enter on it): resume that app */
    void switcher_close(AppId id); /* its X: close that app (the card goes) */
    void switcher_close_all();     /* Close all: every resident app closes, the home grid shows */
    void switcher_home();          /* Home: the launcher (the grid, or the page that was open) */
    void switcher_cancel();        /* the header's Esc, Esc / Backspace: back to where the switcher was opened from */
    void sheet_answer(bool keep);  /* Keep: the app is resumed as it was; Close: it is closed */

    /* ---- any thread ---- */
    int resident_count() const { return residents_.load(); }
    /* The loop's cost since start(): wake-ups and CPU time of its thread (task 015 deck findings). */
    struct Stats {
        std::uint64_t wakeups = 0;
        std::uint64_t cpu_us = 0;
        std::uint64_t since_ms = 0;
    };
    Stats stats() const;
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
        int exit_fd = -1;          /* the leader's pidfd, closed once it fired */
        bool leader_gone = false;  /* the leader exited, helpers may remain: checked every 100 ms */
        std::vector<std::uint8_t> snapshot;
        int page_y = 0;
        std::uint64_t hide_sent_ms = 0;
        std::uint32_t show_n = 0;
        std::uint64_t show_sent_ms = 0;
        bool show_waiting = false;
    };
    struct Request {
        enum class Kind { Launch, Resume, Home, Close, CloseAll, Switch, Memory, Command, Gesture, Screen, SuperTap } kind =
            Kind::Home;
        AppSpec spec;
        AppId id = kNoApp;
        SwitchEvent event = SwitchEvent::None;
        long value = 0;
        std::string text;
        std::string source;
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
    /* Task c-7.5: a Super tap (either reader): Home at once from an app, the second within kSuperDoubleTapMs the switcher */
    void super_tap(std::uint64_t press_ms, bool raw);
    void double_tap_switcher();
    void end_session(AppId target);
    std::string session_text() const;
    std::string session_selection() const;
    void handle_command(const std::string &line);
    /* The switcher: G1, G2, the app's "switcher" message and the dev FIFO's g1 / g2 all come here. P-d: the switcher
     * screen (a lifecycle app in front is hidden first, the launcher in front with an app resident shows it at once),
     * the legacy sheet (a legacy app in front), or leaving them (a gesture while they are up); without switcher_ui the
     * P-c stand-in (from an app = Home). */
    void open_switcher(const std::string &source);
    /* P-d */
    void screen_request(const Request &request);
    SwitcherInfo switcher_info();
    void post_switcher(bool opening = false);                  /* open (logged) or update, with the current cards */
    void close_switcher(bool app_next);
    void sync_screens();                                       /* the sheet's app went; the cards changed; no app left */
    void show_hint();
    void post_screen(const std::string &what, AppId id = kNoApp);
    void sync_touch();
    void poll_touch();
    void touch_event(const cp0_g1_event_t &event);
    static void on_touch_frame(void *context, const cp0_touch_frame_t &frame);
    static std::int64_t now_us();
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
    std::uint64_t super_down_ms_ = 0; /* task c-7: when Super went down on the raw reader (0: up), for the log */
    bool super_fired_ = false;        /* ... and its hold opened the switcher */
    bool raw_open_ = false;
    bool dirty_ = true; /* the status file and the resident list need writing */
    int next_timeout_ms();
    Session session_;
    std::uint64_t trace_until_ms_ = 0; /* the session key trace goes on 2 s after a session ended */
    std::vector<SwitchResult> raw_events_;
    SuperTaps taps_;            /* task c-7.5: the double tap, over both readers */
    AppId tap_app_ = kNoApp;    /* the app the last first tap sent home */
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
    TouchPort *touch_ = nullptr;
    bool touch_open_ = false;
    AppId touch_failed_ = kNoApp; /* the app in front for which the reader could not open (not retried for it) */
    cp0_g1_t g1_{};
    /* P-d: the switcher screen (wanted: shown as soon as the launcher is in front, after the hide of the app it was
     * opened from) and the legacy sheet (the HELD app it asks about) */
    struct SwitcherState {
        bool wanted = false;
        bool open = false;
        AppId origin = kNoApp;
        std::string source;
    } switcher_;
    AppId sheet_ = kNoApp;
    bool sheet_posted_ = false;
    std::string cards_sig_;  /* the resident apps and their states as last posted (UiPort::apps) */
    bool hint_done_ = false; /* the first-run hint was shown (now or in an earlier run: hint_path exists) */

    mutable std::mutex mutex_;          /* requests_, status_text_, keys_resident_, log_sink_ */
    std::deque<Request> requests_;
    std::string status_text_;
    std::vector<std::string> keys_resident_;
    LogSink log_sink_;
    std::atomic<int> residents_{0};
    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> stat_wakeups_{0};
    std::atomic<std::uint64_t> stat_cpu_us_{0};
    std::uint64_t stat_since_ms_ = 0;
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

/* The TouchPort of the launcher and of the host tests (P-c): a cp0_touch_reader on `path`, read-only, never grabbed.
 * map: a raw device position -> screen pixels (the launcher: cp0_display_touch_map, the pointer's own transform;
 * nullptr: raw positions are screen pixels). mt_fallback: the mode when the node's capabilities cannot be read (a FIFO
 * standing in for the device in the host tests: protocol B). */
class EvdevTouchPort : public TouchPort {
public:
    using Map = void (*)(int raw_x, int raw_y, int *x, int *y);
    EvdevTouchPort(std::string path, Map map, bool mt_fallback);
    ~EvdevTouchPort() override { close(); }
    bool open() override;
    void close() override;
    int fd() const override { return open_ ? reader_.fd : -1; }
    int down() const override { return open_ ? cp0_touch_slots_down(&reader_.t) : 0; }
    bool poll(Listener listener, void *context) override;

private:
    static void on_frame(void *context, const cp0_touch_frame_t *frame);
    std::string path_;
    Map map_;
    bool mt_fallback_;
    bool open_ = false;
    cp0_touch_reader_t reader_{};
    Listener listener_ = nullptr;
    void *context_ = nullptr;
};

} // namespace multitask
