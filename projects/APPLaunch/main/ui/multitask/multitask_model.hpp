/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-b (task 015, study 026 sections 4.2 and 6.3, decisions D1, D5, D6, D10): the registry of resident
 * fullscreen apps and their lifecycle state machine, as a pure model. Every request or report returns the actions the
 * supervisor carries out, in order (signals, control messages, snapshots, input protection, who is in front). No
 * system call, no clock, no LVGL: unit-tested by tests/test_multitask_model.cpp.
 *
 *   FOREGROUND --hide n--> HIDING --ack n--> HIDDEN_RUNNING (run) | HIDDEN_FROZEN (freeze: SIGSTOP)
 *                             '--300 ms--> NOT_RESPONDING (SIGSTOP)
 *   hidden --resume--> FOREGROUND (restore the snapshot, drain input, oom back, SIGCONT, show n)
 *   any --group empty--> GONE;  close: SIGCONT first, "close n", SIGTERM +3 s, SIGKILL +3 s (CLOSING)
 * An app without X-Background (legacy) is only ever FOREGROUND, CLOSING or GONE.
 */
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace multitask {

enum class Background : std::uint8_t { None, Freeze, Run };

/* The .desktop value of X-Background: "freeze" or "run" (ASCII case and surrounding blanks ignored); anything else,
 * empty included, is None (a legacy app, unchanged behaviour). */
Background parse_background(std::string_view value);
const char *background_name(Background background);

enum class State : std::uint8_t { Foreground, Hiding, HiddenRunning, HiddenFrozen, NotResponding, Closing, Gone };
const char *state_name(State state);
/* Counts against the cap: Foreground, Hiding, HiddenRunning, HiddenFrozen, NotResponding. */
bool is_resident(State state);
/* Behind the launcher: HiddenRunning, HiddenFrozen, NotResponding. */
bool is_hidden(State state);
/* Stopped with SIGSTOP: HiddenFrozen, NotResponding. */
bool is_frozen(State state);

struct Config {
    int cap = 4;                          /* resident apps (D6) */
    std::uint32_t hide_ack_ms = 300;      /* hide -> ack deadline, then SIGSTOP + not responding */
    std::uint32_t close_term_ms = 3000;   /* "close n" -> SIGTERM */
    std::uint32_t term_kill_ms = 3000;    /* SIGTERM -> SIGKILL (today's Esc policy) */
    long floor_freeze_kb = 64L * 1024;    /* MemAvailable under it: the LRU frozen app is closed (D6) */
    long floor_run_kb = 32L * 1024;       /* under it: a hidden running app too */
    int oom_hidden_run = 700;
    int oom_frozen = 900;
};

using AppId = int;
inline constexpr AppId kNoApp = -1;

struct AppSpec {
    std::string key;  /* identity, one instance per key: the .desktop Exec line (unique per the loader) */
    std::string name; /* for logs and notices */
    std::string exec;
    bool keep_root = false;
    Background background = Background::None;
};

enum class Op : std::uint8_t {
    /* who is in front and what follows from it (emitted when it changes) */
    LauncherFront, /* the launcher draws again; value 1 = the app in front exited (republish screen.state) */
    AppFront,      /* id: the launcher stops drawing (done before any restore, thaw or spawn) */
    LauncherKeys,  /* value: 1 run the launcher's keyboard thread, 0 pause it */
    GrabKeys,      /* value: launcher's own keyboard fds EVIOCGRAB on / off */
    GrabTouch,     /* value: launcher's own touch fd EVIOCGRAB on / off */
    RawKeys,       /* value: the supervisor reads the keyboards from evdev (Esc hold, Alt+Tab, Super) */
    Repaint,       /* the launcher repaints everything (a hidden app may have drawn while it closed) */
    /* per app */
    Spawn,         /* id: start it in its own process group (control socket when it has X-Background) */
    SendHide,      /* id, n */
    SendShow,      /* id, n */
    SendClose,     /* id, n */
    Stop,          /* id: SIGSTOP the group (and wait until it is stopped) */
    Cont,          /* id: SIGCONT the group */
    Term,          /* id: SIGTERM the group */
    Kill,          /* id: SIGKILL the group */
    Snapshot,      /* id: copy the visible framebuffer page */
    Restore,       /* id: write it back */
    DropSnapshot,  /* id */
    Drain,         /* id: empty the app's own evdev queues */
    SetOom,        /* id, value: oom_score_adj of every process of the group */
    Notice,        /* id or kNoApp, text: a log line, and a toast when the launcher is in front */
};
const char *op_name(Op op);

struct Action {
    Op op = Op::Notice;
    AppId id = kNoApp;
    std::uint32_t n = 0;
    int value = 0;
    std::string text;
};
using Actions = std::vector<Action>;

enum class CloseReason : std::uint8_t { None, User, CloseAll, Cap, Memory, EscHold, Shutdown };
const char *close_reason_name(CloseReason reason);

struct Entry {
    AppId id = kNoApp;
    AppSpec spec;
    State state = State::Gone;
    State closed_from = State::Gone; /* the state a close started from */
    CloseReason close_reason = CloseReason::None;
    int pgid = 0;
    bool has_control = false;        /* a control socket was given (X-Background apps) */
    bool control_lost = false;       /* ... and the app closed it */
    int oom_base = 0;                /* oom_score_adj at spawn */
    int oom_now = 0;
    std::uint32_t seq = 0;           /* last message number sent */
    std::uint32_t hide_n = 0;
    std::uint64_t hide_deadline = 0;
    std::uint64_t term_at = 0;       /* CLOSING: when SIGTERM is due (0 = already sent) */
    std::uint64_t kill_at = 0;       /* CLOSING: when SIGKILL is due (0 = not scheduled or sent) */
    bool term_sent = false;
    bool kill_sent = false;
    std::uint64_t mru = 0;           /* larger = more recently in front */
    bool has_snapshot = false;
    std::uint64_t since = 0;         /* entered the current state */

    bool lifecycle() const { return spec.background != Background::None; }
    bool can_talk() const { return has_control && !control_lost; }
};

/* Who is in front and what follows (see Op::LauncherFront .. Op::RawKeys). */
struct Derived {
    AppId front = kNoApp;
    bool launcher_keys = true;
    bool grab_keys = false;
    bool grab_touch = false;
    bool raw_keys = false;
    bool operator==(const Derived &o) const
    {
        return front == o.front && launcher_keys == o.launcher_keys && grab_keys == o.grab_keys &&
               grab_touch == o.grab_touch && raw_keys == o.raw_keys;
    }
};

class Model {
public:
    explicit Model(Config config = Config());
    const Config &config() const { return config_; }

    /* ---- requests ---- */
    /* A tile (or the control FIFO): start the app, or resume it when it is already resident (one instance). */
    Actions launch(const AppSpec &spec, std::uint64_t now);
    /* Bring a hidden app to the front (Alt+Tab, P-d card); the app in front is hidden first. */
    Actions resume(AppId id, std::uint64_t now);
    /* Super, Alt+Tab to Home, the app's "home" / "switcher": hide the app in front (only a lifecycle app). */
    Actions go_launcher(std::uint64_t now);
    /* Close one app gracefully (SIGCONT first when frozen). */
    Actions close(AppId id, std::uint64_t now) { return close_for(id, CloseReason::User, now); }
    /* Close every hidden app (study 5.2: "Close all" closes the hidden apps). */
    Actions close_all(std::uint64_t now);
    /* Esc held 3 s: SIGTERM the app in front now, SIGKILL 3 s later (today's policy). */
    Actions force_close_front(std::uint64_t now);
    /* The launcher stops: SIGCONT + SIGTERM every app, SIGKILL when the grace is over (tick). */
    Actions shutdown(std::uint64_t now);
    /* An Alt+Tab session read from evdev runs (started inside an app): the launcher keyboard thread stays paused and
     * its keyboards are not grabbed until it ends, so the supervisor sees the Alt release. */
    Actions key_session(bool raw_active, std::uint64_t now);

    /* ---- reports from the supervisor ---- */
    Actions spawned(AppId id, int pgid, bool has_control, int oom_base, std::uint64_t now);
    Actions spawn_failed(AppId id, std::uint64_t now);
    Actions ack(AppId id, std::uint32_t n, std::uint64_t now);
    Actions control_closed(AppId id, std::uint64_t now);
    /* The app's process group is empty (it exited, was killed, the kernel OOM killer). */
    Actions exited(AppId id, std::uint64_t now);
    /* Deadlines: hide -> not responding, close -> SIGTERM -> SIGKILL. */
    Actions tick(std::uint64_t now);
    /* A MemAvailable reading (kB), every 2 s while an app is hidden. */
    Actions memory(long available_kb, std::uint64_t now);

    /* ---- queries ---- */
    const Entry *find(AppId id) const;
    /* The live (not GONE) entry with this key, or kNoApp. */
    AppId find_key(const std::string &key) const;
    AppId front() const { return front_; }
    const Derived &derived() const { return derived_; }
    /* Resident apps, most recently in front first. */
    std::vector<AppId> mru() const;
    /* Every live entry (residents and CLOSING), in id order. */
    std::vector<AppId> live() const;
    int resident_count() const;
    int hidden_count() const;
    bool needs_memory_poll() const { return hidden_count() > 0; }
    bool key_session_active() const { return raw_session_; }
    long last_memory_kb() const { return last_memory_kb_; }

private:
    struct Pending {
        enum class Kind : std::uint8_t { None, Launcher, App, Launch } kind = Kind::None;
        AppId id = kNoApp;
        AppSpec spec;
    };

    Entry *get(AppId id);
    Actions close_for(AppId id, CloseReason reason, std::uint64_t now);
    void close_into(Entry &e, CloseReason reason, std::uint64_t now, Actions &out);
    void start_hide(Entry &e, std::uint64_t now, Actions &out);
    /* the hide of `id` ended (ack, deadline, no socket); then the pending target runs */
    void hide_done(AppId id, bool acked, std::uint64_t now, Actions &out);
    /* runs a pending target; false when there was none or it could not run (the launcher then comes front) */
    bool run_pending(std::uint64_t now, Actions &out);
    void show(AppId id, std::uint64_t now, Actions &out);
    void spawn_new(const AppSpec &spec, std::uint64_t now, Actions &out);
    /* cap check then spawn; false when the launch was refused */
    bool launch_into(const AppSpec &spec, std::uint64_t now, Actions &out);
    void set_front(AppId id, bool after_exit, Actions &out);
    void sync_derived(Actions &out);
    Entry *lru_hidden(bool frozen_only);
    void notice(AppId id, std::string text, Actions &out);
    static void set_state(Entry &e, State s, std::uint64_t now);
    void set_oom(Entry &e, int value, Actions &out);
    void erase_gone();

    Config config_;
    std::deque<Entry> entries_; /* a deque: references survive a push_back during a transition */
    AppId next_id_ = 1;
    std::uint64_t mru_clock_ = 0;
    AppId front_ = kNoApp;
    Derived derived_;
    bool raw_session_ = false;
    Pending pending_;
    long last_memory_kb_ = -1;
};

} // namespace multitask
