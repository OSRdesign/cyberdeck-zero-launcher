/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-b supervisor (see supervisor.hpp). Linux only; other builds get a stub that never starts.
 */
#include "supervisor.hpp"

#include "lifecycle_protocol.hpp"
#include "switcher_model.hpp"

#include "cp0_esc_exit_policy.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#if defined(__linux__)
#include <cerrno>
#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* a finger on the panel while an app is in front (the launcher's idle dim for apps counts it): defined by the launcher,
 * a no-op here (the host tests) */
extern "C" void __attribute__((weak)) cp0_external_app_touch(void) {}

namespace multitask {

std::uint64_t Supervisor::now_ms()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

#if defined(__linux__)

namespace {

std::string fmt_ms(double ms)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", ms);
    return buf;
}

double elapsed_ms(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

const char *short_state(State s)
{
    switch (s) {
    case State::Foreground:
    case State::Hiding: return "in front";
    case State::HiddenRunning: return "running";
    case State::HiddenFrozen: return "paused";
    case State::NotResponding: return "not responding";
    case State::Closing: return "closing";
    case State::Held: return "held";
    case State::Gone: break;
    }
    return "gone";
}

/* "1 app", "3 apps" */
std::string apps_text(int n)
{
    return std::to_string(n) + (n == 1 ? " app" : " apps");
}

/* mkdir -p of path's directory (the hint file's: ~/.config/cardputerzero) */
void make_parent_dirs(const std::string &path)
{
    for (std::size_t at = path.find('/', 1); at != std::string::npos; at = path.find('/', at + 1))
        (void)::mkdir(path.substr(0, at).c_str(), 0755);
}

bool iequals(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

} // namespace

Supervisor::Supervisor(SupervisorConfig config, ProcessPort &proc, FramebufferPort &fb, InputPort &input,
                       KeyPort &keys, UiPort &ui)
    : config_(std::move(config)), proc_(proc), fb_(fb), input_(input), keys_(keys), ui_(ui), model_(config_.model)
{
    if (::pipe2(wake_fd_, O_CLOEXEC | O_NONBLOCK) != 0) wake_fd_[0] = wake_fd_[1] = -1;
    /* P-d: the first-run hint was shown in an earlier run when its file exists */
    struct stat st;
    hint_done_ = config_.hint_text.empty() || (!config_.hint_path.empty() && ::stat(config_.hint_path.c_str(), &st) == 0);
    if (config_.touch.params.hold_ms == 0) cp0_g1_params_default(&config_.touch.params);
    cp0_g1_init(&g1_, &config_.touch.params, config_.touch.w, config_.touch.h, config_.touch.ppmm_x100);
}

std::int64_t Supervisor::now_us()
{
    struct timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts); /* the clock of the touch frames (cp0_touch_reader) */
    return static_cast<std::int64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

Supervisor::~Supervisor()
{
    stop();
    for (int &fd : wake_fd_)
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
}

void Supervisor::set_log_sink(LogSink sink)
{
    std::lock_guard<std::mutex> lock(mutex_);
    log_sink_ = std::move(sink);
}

void Supervisor::log(const std::string &line)
{
    LogSink sink;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sink = log_sink_;
    }
    if (sink) sink(line);
    else {
        std::fprintf(stderr, "[mt] %s\n", line.c_str());
        std::fflush(stderr);
    }
}

bool Supervisor::start()
{
    if (thread_.joinable()) return true;
    /* as the runner: orphaned helpers of an app (its group) come back to us to be reaped */
    (void)::prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);
    if (config_.guardian && !guardian_.start()) log("guardian not started: apps are not ended if the launcher dies");
    if (!config_.control_fifo.empty()) {
        struct stat st;
        if (::stat(config_.control_fifo.c_str(), &st) != 0) (void)::mkfifo(config_.control_fifo.c_str(), 0600);
        fifo_fd_ = ::open(config_.control_fifo.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC); /* RDWR: never EOF */
        if (fifo_fd_ < 0) log("control FIFO " + config_.control_fifo + ": " + std::strerror(errno));
    }
    stop_ = false;
    thread_ = std::thread(&Supervisor::loop, this);
    log(std::string("supervisor started: cap ") + std::to_string(config_.model.cap) + ", input " +
        (config_.use_grab ? "grab" : "") + (config_.use_grab && config_.use_drain ? "+" : "") +
        (config_.use_drain ? "drain" : "") + (!config_.use_grab && !config_.use_drain ? "none" : "") +
        (guardian_.started() ? ", guardian on" : ""));
    return true;
}

void Supervisor::stop()
{
    if (stopped_) return;
    stopped_ = true;
    if (thread_.joinable()) {
        stop_ = true;
        if (wake_fd_[1] >= 0) {
            const char c = 1;
            (void)!::write(wake_fd_[1], &c, 1);
        }
        thread_.join();
    } else {
        shutdown_now();
    }
}

void Supervisor::loop()
{
    stat_since_ms_ = now_ms();
    while (!stop_) {
        run_once();
        if (stop_) break;
        wait_for_work();
        ++stat_wakeups_;
        struct timespec ts;
        if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
            stat_cpu_us_ = static_cast<std::uint64_t>(ts.tv_sec) * 1000000u + static_cast<std::uint64_t>(ts.tv_nsec / 1000);
    }
    shutdown_now();
}

Supervisor::Stats Supervisor::stats() const
{
    Stats s;
    s.wakeups = stat_wakeups_.load();
    s.cpu_us = stat_cpu_us_.load();
    s.since_ms = stat_since_ms_;
    return s;
}

void Supervisor::shutdown_now()
{
    execute(model_.shutdown(now_ms()));
    const std::uint64_t until = now_ms() + static_cast<std::uint64_t>(config_.stop_wait_ms) + 500;
    while (!model_.live().empty() && now_ms() < until) {
        poll_reaping(now_ms());
        execute(model_.tick(now_ms()));
        if (model_.live().empty()) break;
        ::usleep(20000);
    }
    for (const AppId id : model_.live()) {
        auto it = runtime_.find(id);
        if (it != runtime_.end() && it->second.pgid > 0) {
            proc_.signal_group(it->second.pgid, SIGCONT);
            proc_.signal_group(it->second.pgid, SIGKILL);
        }
    }
    for (auto &kv : runtime_) {
        if (kv.second.control_fd >= 0) {
            ::close(kv.second.control_fd);
            kv.second.control_fd = -1;
        }
        if (kv.second.exit_fd >= 0) {
            ::close(kv.second.exit_fd);
            kv.second.exit_fd = -1;
        }
    }
    if (raw_open_) {
        keys_.close();
        raw_open_ = false;
    }
    if (touch_open_) {
        touch_->close();
        touch_open_ = false;
    }
    if (fifo_fd_ >= 0) {
        ::close(fifo_fd_);
        fifo_fd_ = -1;
    }
    guardian_.finish();
    publish_residents();
}

/* How long the loop may sleep: until the next thing that is due, not a fixed tick (deck finding 2026-10-20: a 10 ms
 * tick cost about 5 % CPU on the Pi Zero 2 W with an app in front). Everything else wakes it through poll(): requests
 * (the wake pipe), the dev FIFO, control sockets, the keyboards, the leaders' pidfds. */
int Supervisor::next_timeout_ms()
{
    const std::uint64_t now = now_ms();
    if (model_.live().empty() && !raw_open_ && !session_.active) return 500;
    std::uint64_t next = now + 2000; /* safety: never sleep longer while an app is resident */
    const auto at = [&next](std::uint64_t t) {
        if (t != 0 && t < next) next = t;
    };
    at(model_.next_deadline());
    if (raw_open_) {
        at(now + 1000); /* keyboards that appear (a Bluetooth keyboard waking up) are attached within 1 s */
        if (esc_down_ && esc_press_ms_ != 0 && model_.front() != kNoApp && !esc_fired_)
            at(esc_press_ms_ + (esc_hint_ ? cp0_esc_exit_policy::TERMINATE_DELAY_MS : cp0_esc_exit_policy::HINT_DELAY_MS));
    }
    if (next_mem_ms_ != 0) at(next_mem_ms_);
    if (touch_open_) { /* a three-finger hold completes when its time comes, not at the next touch event */
        const std::int64_t due = cp0_g1_due_us(&g1_);
        if (due >= 0) {
            const std::int64_t left = due - now_us();
            at(now + (left > 0 ? static_cast<std::uint64_t>(left / 1000) + 1 : 0));
        }
    }
    for (const auto &kv : runtime_) {
        const Runtime &rt = kv.second;
        if (rt.show_waiting) at(rt.show_sent_ms + config_.show_ack_log_ms);
        if (rt.leader_gone) at(now + 100);          /* helpers left in the group: check them */
        else if (rt.exit_fd < 0) at(now + 250);     /* no pidfd: look for the exit now and then */
    }
    return next > now ? static_cast<int>(next - now) : 0;
}

void Supervisor::wait_for_work()
{
    std::vector<struct pollfd> fds;
    if (wake_fd_[0] >= 0) fds.push_back({wake_fd_[0], POLLIN, 0});
    if (fifo_fd_ >= 0) fds.push_back({fifo_fd_, POLLIN, 0});
    for (const auto &kv : runtime_) {
        if (kv.second.control_fd >= 0) fds.push_back({kv.second.control_fd, POLLIN, 0});
        if (kv.second.exit_fd >= 0) fds.push_back({kv.second.exit_fd, POLLIN, 0});
    }
    if (raw_open_) {
        std::vector<int> key_fds;
        keys_.fds(key_fds);
        for (const int fd : key_fds) fds.push_back({fd, POLLIN, 0});
    }
    if (touch_open_ && touch_->fd() >= 0) fds.push_back({touch_->fd(), POLLIN, 0});
    (void)::poll(fds.data(), static_cast<nfds_t>(fds.size()), next_timeout_ms());
    if (wake_fd_[0] >= 0) {
        char buf[64];
        while (::read(wake_fd_[0], buf, sizeof(buf)) > 0) {
        }
    }
}

void Supervisor::post(Request request)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(std::move(request));
    }
    if (wake_fd_[1] >= 0) {
        const char c = 1;
        (void)!::write(wake_fd_[1], &c, 1);
    }
}

void Supervisor::launch(const AppSpec &spec)
{
    Request r;
    r.kind = Request::Kind::Launch;
    r.spec = spec;
    post(std::move(r));
}

void Supervisor::resume(AppId id)
{
    Request r;
    r.kind = Request::Kind::Resume;
    r.id = id;
    post(std::move(r));
}

void Supervisor::go_launcher()
{
    Request r;
    r.kind = Request::Kind::Home;
    post(std::move(r));
}

void Supervisor::close(AppId id)
{
    Request r;
    r.kind = Request::Kind::Close;
    r.id = id;
    post(std::move(r));
}

void Supervisor::close_all()
{
    Request r;
    r.kind = Request::Kind::CloseAll;
    post(std::move(r));
}

void Supervisor::launcher_switch_event(SwitchEvent event)
{
    Request r;
    r.kind = Request::Kind::Switch;
    r.event = event;
    post(std::move(r));
}

void Supervisor::fake_memory(long kb)
{
    Request r;
    r.kind = Request::Kind::Memory;
    r.value = kb;
    post(std::move(r));
}

void Supervisor::command(const std::string &line)
{
    Request r;
    r.kind = Request::Kind::Command;
    r.text = line;
    post(std::move(r));
}

void Supervisor::launcher_gesture(const std::string &source, const std::string &line, bool fired)
{
    Request r;
    r.kind = Request::Kind::Gesture;
    r.source = source;
    r.text = line;
    r.value = fired ? 1 : 0;
    post(std::move(r));
}

void Supervisor::post_screen(const std::string &what, AppId id)
{
    Request r;
    r.kind = Request::Kind::Screen;
    r.text = what;
    r.id = id;
    post(std::move(r));
}

void Supervisor::switcher_pick(AppId id) { post_screen("pick", id); }
void Supervisor::switcher_close(AppId id) { post_screen("close", id); }
void Supervisor::switcher_close_all() { post_screen("close-all"); }
void Supervisor::switcher_home() { post_screen("home"); }
void Supervisor::switcher_cancel() { post_screen("cancel"); }
void Supervisor::sheet_answer(bool keep) { post_screen(keep ? "keep" : "sheet-close"); }

bool Supervisor::is_resident_key(const std::string &key) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::find(keys_resident_.begin(), keys_resident_.end(), key) != keys_resident_.end();
}

std::string Supervisor::status_text() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_text_;
}

/* ------------------------------------------------------------------------------------------------ the loop */

void Supervisor::run_once()
{
    std::deque<Request> batch;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        batch.swap(requests_);
    }
    for (const Request &r : batch) handle(r);
    const std::uint64_t now = now_ms();
    poll_controls(now);
    poll_reaping(now);
    poll_keys(now);
    poll_touch();
    execute(model_.tick(now_ms()));
    for (auto &kv : runtime_) {
        Runtime &rt = kv.second;
        if (rt.show_waiting && now_ms() - rt.show_sent_ms >= config_.show_ack_log_ms) {
            rt.show_waiting = false;
            log(rt.name + ": no ack to show " + std::to_string(rt.show_n) + " within " +
                std::to_string(config_.show_ack_log_ms) + " ms");
        }
    }
    if (!model_.needs_memory_poll()) {
        next_mem_ms_ = 0;
    } else if (next_mem_ms_ == 0) {
        next_mem_ms_ = now + config_.mem_poll_ms;
    } else if (now >= next_mem_ms_) {
        next_mem_ms_ = now + config_.mem_poll_ms;
        const long before = model_.last_memory_kb();
        execute(model_.memory(proc_.mem_available_kb(), now));
        if (model_.last_memory_kb() != before) dirty_ = true;
    }
    poll_fifo();
    sync_touch();
    sync_screens();
    if (dirty_) { /* only after something happened: not on every wake-up */
        dirty_ = false;
        write_status();
        publish_residents();
    }
}

void Supervisor::handle(const Request &r)
{
    const std::uint64_t now = now_ms();
    dirty_ = true;
    switch (r.kind) {
    case Request::Kind::Launch: {
        log("launch " + r.spec.name + " (" + background_name(r.spec.background) + ")");
        execute(model_.launch(r.spec, now));
        /* refused (cap) or nothing to do: the UI prepared the hand-over, give it the launcher back */
        if (model_.front() == kNoApp) ui_.launcher_front(false);
        break;
    }
    case Request::Kind::Resume:
        if (sheet_ != kNoApp && r.id != sheet_) { /* P-d: the sheet waits for its answer first */
            log("resume ignored: the sheet for the app in front is up");
            break;
        }
        execute(model_.resume(r.id, now));
        break;
    case Request::Kind::Home: execute(model_.go_launcher(now)); break;
    case Request::Kind::Close: execute(model_.close(r.id, now)); break;
    case Request::Kind::CloseAll: execute(model_.close_all(now)); break;
    case Request::Kind::Switch: switch_event(r.event, false); break;
    case Request::Kind::Memory:
        log("fake MemAvailable " + std::to_string(r.value) + " kB");
        execute(model_.memory(r.value, now));
        break;
    case Request::Kind::Command: handle_command(r.text); break;
    case Request::Kind::Gesture:
        log(r.text);
        if (r.value) open_switcher(r.source);
        break;
    case Request::Kind::Screen: screen_request(r); break;
    }
}

void Supervisor::execute(Actions actions)
{
    if (!actions.empty()) dirty_ = true;
    std::deque<Action> work(actions.begin(), actions.end());
    while (!work.empty()) {
        const Action a = work.front();
        work.pop_front();
        execute_one(a, work);
    }
}

void Supervisor::execute_one(const Action &a, std::deque<Action> &more)
{
    const std::uint64_t now = now_ms();
    auto it = runtime_.find(a.id);
    Runtime *rt = it == runtime_.end() ? nullptr : &it->second;
    const Entry *entry = model_.find(a.id);
    const std::string name = rt ? rt->name : entry ? entry->spec.name : std::string("-");
    const auto push = [&more](const Actions &extra) { more.insert(more.end(), extra.begin(), extra.end()); };
    switch (a.op) {
    case Op::LauncherFront:
        log(std::string("launcher in front") + (a.value ? " (the app in front exited)" : ""));
        /* P-d: the sheet or the switcher reach the launcher before it takes the panel back, so it paints them and not
         * its grid */
        if (config_.switcher_ui && sheet_ != kNoApp && !sheet_posted_) {
            const Entry *held = model_.find(sheet_);
            sheet_posted_ = true;
            ui_.sheet(sheet_, held ? held->spec.name : std::string("The app"));
        } else if (config_.switcher_ui && switcher_.wanted) {
            switcher_.wanted = false;
            if (model_.resident_count() > 0) post_switcher(true);
        }
        ui_.launcher_front(a.value != 0);
        break;
    case Op::AppFront:
        fb_.suspend();
        esc_press_ms_ = 0;
        esc_hint_ = false;
        esc_fired_ = false;
        if (config_.switcher_ui) { /* P-d: an app takes the panel: the switcher and the sheet go (silently) */
            if (switcher_.open) close_switcher(true);
            else switcher_ = SwitcherState();
            if (sheet_ != kNoApp) {
                if (sheet_posted_) ui_.sheet(kNoApp, std::string());
                sheet_ = kNoApp;
                sheet_posted_ = false;
            }
        }
        log(name + " in front");
        ui_.app_front(a.id, name);
        break;
    case Op::LauncherKeys: input_.launcher_keys(a.value != 0); break;
    case Op::GrabKeys:
        if (config_.use_grab) input_.grab_keys(a.value != 0);
        log(std::string("keyboard grab ") + (a.value ? "on" : "off") + (config_.use_grab ? "" : " (disabled)"));
        break;
    case Op::GrabTouch:
        if (config_.use_grab) input_.grab_touch(a.value != 0);
        log(std::string("touch grab ") + (a.value ? "on" : "off") + (config_.use_grab ? "" : " (disabled)"));
        break;
    case Op::RawKeys:
        if (a.value && !raw_open_) {
            keys_.open();
            raw_open_ = true;
            raw_keys_.reset();
            esc_down_ = false;
        } else if (!a.value && raw_open_) {
            keys_.close();
            raw_open_ = false;
            raw_keys_.reset();
            if (esc_down_) keys_.esc_state(0);
            esc_down_ = false;
        }
        break;
    case Op::Repaint: ui_.repaint(); break;
    case Op::Spawn: {
        if (!entry) break;
        const SpawnResult s = proc_.spawn(entry->spec, entry->lifecycle());
        if (!s.ok) {
            log("cannot start " + entry->spec.name + ": " + std::strerror(errno));
            push(model_.spawn_failed(a.id, now));
            break;
        }
        Runtime r;
        r.name = entry->spec.name;
        r.pgid = s.pgid;
        r.control_fd = s.control_fd;
        r.exit_fd = s.exit_fd;
        r.guardian_slot = guardian_.set(-1, s.pgid);
        runtime_[a.id] = std::move(r);
        log("spawn " + entry->spec.name + " pid=" + std::to_string(s.pid) + " pgid=" + std::to_string(s.pgid) +
            " mode=" + background_name(entry->spec.background) + " control=" +
            (s.control_fd >= 0 ? "fd" : "none") + " oom_score_adj=" + std::to_string(s.oom_base));
        push(model_.spawned(a.id, s.pgid, s.control_fd >= 0, s.oom_base, now));
        break;
    }
    case Op::SendHide:
    case Op::SendShow:
    case Op::SendClose: {
        if (!rt || rt->control_fd < 0) break;
        const protocol::Kind kind = a.op == Op::SendHide   ? protocol::Kind::Hide
                                    : a.op == Op::SendShow ? protocol::Kind::Show
                                                           : protocol::Kind::Close;
        const std::string msg = protocol::format(kind, a.n);
        if (::send(rt->control_fd, msg.data(), msg.size(), MSG_NOSIGNAL | MSG_DONTWAIT) < 0) {
            log(name + ": send \"" + msg + "\": " + std::strerror(errno) + " (control socket closed)");
            ::close(rt->control_fd);
            rt->control_fd = -1;
            push(model_.control_closed(a.id, now));
            break;
        }
        if (a.op == Op::SendHide) rt->hide_sent_ms = now;
        if (a.op == Op::SendShow) {
            rt->show_n = a.n;
            rt->show_sent_ms = now;
            rt->show_waiting = true;
        }
        log(name + ": sent \"" + msg + "\"");
        break;
    }
    case Op::Stop: {
        if (!rt) break;
        const auto t0 = std::chrono::steady_clock::now();
        proc_.signal_group(rt->pgid, SIGSTOP);
        const bool stopped = proc_.wait_stopped(rt->pgid, 100);
        log(name + ": SIGSTOP pgid=" + std::to_string(rt->pgid) + " stop_ms=" + fmt_ms(elapsed_ms(t0)) +
            (stopped ? "" : " (not all stopped yet)"));
        break;
    }
    case Op::Cont:
    case Op::Term:
    case Op::Kill: {
        if (!rt) break;
        const int sig = a.op == Op::Cont ? SIGCONT : a.op == Op::Term ? SIGTERM : SIGKILL;
        proc_.signal_group(rt->pgid, sig);
        log(name + ": " + (sig == SIGCONT ? "SIGCONT" : sig == SIGTERM ? "SIGTERM" : "SIGKILL") +
            " pgid=" + std::to_string(rt->pgid));
        break;
    }
    case Op::Snapshot: {
        if (!rt) break;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = fb_.snapshot(rt->snapshot, rt->page_y);
        log(name + ": snapshot " + (ok ? std::to_string(rt->snapshot.size()) + " bytes" : std::string("failed")) +
            " snapshot_ms=" + fmt_ms(elapsed_ms(t0)) +
            (rt->hide_sent_ms ? " since_hide_ms=" + std::to_string(now - rt->hide_sent_ms) : std::string()));
        if (!ok) rt->snapshot.clear();
        break;
    }
    case Op::Restore: {
        if (!rt || rt->snapshot.empty()) break;
        const auto t0 = std::chrono::steady_clock::now();
        const int rc = fb_.restore(rt->snapshot, rt->page_y);
        log(name + ": restore restore_ms=" + fmt_ms(elapsed_ms(t0)) + " verified=" +
            (rc == 0 ? "1" : rc == 1 ? "0" : "error"));
        break;
    }
    case Op::Backdrop: {
        if (!rt || rt->snapshot.empty()) break;
        const auto t0 = std::chrono::steady_clock::now();
        const int rc = fb_.backdrop(rt->snapshot);
        log(name + ": backdrop (its last frame dimmed under the sheet) " + (rc == 0 ? "backdrop_ms=" + fmt_ms(elapsed_ms(t0))
                                                                                     : std::string("not drawn")));
        break;
    }
    case Op::DropSnapshot:
        if (rt) {
            rt->snapshot.clear();
            rt->snapshot.shrink_to_fit();
        }
        break;
    case Op::Drain: {
        if (!rt) break;
        if (!config_.use_drain) {
            log(name + ": drain disabled");
            break;
        }
        const auto t0 = std::chrono::steady_clock::now();
        const DrainStats d = input_.drain(rt->pgid);
        log(name + ": drain fds=" + std::to_string(d.fds) + " events=" + std::to_string(d.events) + " keys=" +
            std::to_string(d.keys) + " touches=" + std::to_string(d.touches) + " syn_dropped=" +
            std::to_string(d.dropped) + " drain_ms=" + fmt_ms(elapsed_ms(t0)) +
            (d.error.empty() ? std::string() : " error=" + d.error));
        break;
    }
    case Op::SetOom:
        if (!rt) break;
        proc_.set_oom(rt->pgid, a.value);
        log(name + ": oom_score_adj " + std::to_string(a.value));
        break;
    case Op::Notice:
        log("notice: " + a.text);
        ui_.notice(a.text);
        break;
    }
}

void Supervisor::forget(AppId id)
{
    auto it = runtime_.find(id);
    if (it == runtime_.end()) return;
    if (it->second.control_fd >= 0) ::close(it->second.control_fd);
    if (it->second.exit_fd >= 0) ::close(it->second.exit_fd);
    if (it->second.guardian_slot >= 0) guardian_.set(it->second.guardian_slot, 0);
    runtime_.erase(it);
}

void Supervisor::poll_controls(std::uint64_t now)
{
    std::vector<AppId> ids;
    for (const auto &kv : runtime_)
        if (kv.second.control_fd >= 0) ids.push_back(kv.first);
    for (const AppId id : ids) {
        for (;;) {
            auto it = runtime_.find(id);
            if (it == runtime_.end() || it->second.control_fd < 0) break;
            Runtime &rt = it->second;
            char buf[protocol::kMaxMessage + 1];
            const ssize_t n = ::recv(rt.control_fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (n <= 0) {
                log(rt.name + ": control socket closed by the app");
                ::close(rt.control_fd);
                rt.control_fd = -1;
                execute(model_.control_closed(id, now_ms()));
                break;
            }
            const protocol::Message m = protocol::parse(buf, static_cast<std::size_t>(n));
            switch (m.kind) {
            case protocol::Kind::Ack:
                if (rt.show_waiting && m.n == rt.show_n) {
                    rt.show_waiting = false;
                    log(rt.name + ": ack " + std::to_string(m.n) + " (show) ack_ms=" +
                        std::to_string(now_ms() - rt.show_sent_ms));
                } else {
                    const Entry *e = model_.find(id);
                    const bool hiding = e && e->state == State::Hiding && e->hide_n == m.n;
                    log(rt.name + ": ack " + std::to_string(m.n) + (hiding ? " (hide) ack_ms=" : " (late) after_ms=") +
                        std::to_string(now_ms() - rt.hide_sent_ms));
                }
                execute(model_.ack(id, m.n, now_ms()));
                break;
            case protocol::Kind::Home:
                log(rt.name + ": asks for home");
                if (model_.front() == id) execute(model_.go_launcher(now_ms()));
                break;
            case protocol::Kind::Switcher: /* G2 on the app's own status strip (P-e) */
                log(rt.name + ": asks for switcher");
                if (model_.front() == id) open_switcher("app");
                break;
            default:
                log(rt.name + ": unknown message ignored (" + std::to_string(n) + " bytes)");
                break;
            }
        }
    }
    (void)now;
}

void Supervisor::poll_reaping(std::uint64_t now)
{
    for (const AppId id : model_.live()) {
        auto it = runtime_.find(id);
        if (it == runtime_.end()) continue;
        Runtime &rt = it->second;
        if (rt.exit_fd >= 0) { /* readable once the leader exited; then it stays readable: close it */
            struct pollfd p = {rt.exit_fd, POLLIN, 0};
            if (::poll(&p, 1, 0) == 1) {
                ::close(rt.exit_fd);
                rt.exit_fd = -1;
                rt.leader_gone = true;
            }
        }
        if (!proc_.reap(rt.pgid)) continue;
        log(it->second.name + ": process group " + std::to_string(it->second.pgid) + " gone");
        execute(model_.exited(id, now));
        forget(id);
    }
}

void Supervisor::on_raw_key(void *context, int code, int value)
{
    auto *self = static_cast<Supervisor *>(context);
    const SwitchResult r = self->raw_keys_.key(static_cast<unsigned>(code), value);
    if (r.event != SwitchEvent::None) self->raw_events_.push_back(r.event);
    /* the key trace of a session (deck finding, build pb.2: Esc with Alt held did not cancel there; the log must show
     * what the keyboard really sent): every press and release from the key that starts a session to 2 s after it
     * ends. Nothing is logged outside sessions (no typed text in the journal). */
    if (value != 2 && (self->session_.active || r.event != SwitchEvent::None || now_ms() < self->trace_until_ms_))
        self->log("session key: " + switch_key_name(static_cast<unsigned>(code)) + (value ? " down" : " up") +
                  (r.event != SwitchEvent::None ? std::string(" -> ") + switch_event_name(r.event) : std::string()) +
                  (self->session_.active || r.event != SwitchEvent::None ? "" : " (after the session)"));
}

void Supervisor::poll_keys(std::uint64_t now)
{
    if (!raw_open_) return;
    raw_events_.clear();
    const int esc = keys_.poll(&Supervisor::on_raw_key, this);
    if (esc >= 0) {
        keys_.esc_state(esc);
        esc_down_ = esc != 0;
        if (esc_down_ && esc_press_ms_ == 0) esc_press_ms_ = now;
        if (!esc_down_) {
            esc_press_ms_ = 0;
            esc_fired_ = false;
        }
    }
    const std::vector<SwitchEvent> events = raw_events_;
    for (const SwitchEvent ev : events) switch_event(ev, true);

    /* today's Esc policy on the app in front: hint at 500 ms, SIGTERM at 3 s (SIGKILL 3 s later, by the model) */
    const bool app_in_front = model_.front() != kNoApp;
    const bool held = app_in_front && esc_down_ && esc_press_ms_ != 0;
    const std::uint64_t held_ms = held ? now - esc_press_ms_ : 0;
    const bool want_hint = held && held_ms >= cp0_esc_exit_policy::HINT_DELAY_MS;
    if (want_hint != esc_hint_) {
        esc_hint_ = want_hint;
        ui_.esc_hint(want_hint);
    }
    if (held && !esc_fired_ && held_ms >= cp0_esc_exit_policy::TERMINATE_DELAY_MS) {
        esc_fired_ = true;
        log("Esc held " + std::to_string(held_ms) + " ms: force close the app in front");
        execute(model_.force_close_front(now));
    }
}

void Supervisor::poll_fifo()
{
    if (fifo_fd_ < 0) return;
    char buf[256];
    ssize_t n;
    while ((n = ::read(fifo_fd_, buf, sizeof(buf))) > 0) fifo_buf_.append(buf, static_cast<std::size_t>(n));
    std::size_t at;
    while ((at = fifo_buf_.find('\n')) != std::string::npos) {
        const std::string line = fifo_buf_.substr(0, at);
        fifo_buf_.erase(0, at + 1);
        if (!line.empty()) handle_command(line);
    }
    if (fifo_buf_.size() > 4096) fifo_buf_.clear();
}

/* ------------------------------------------------------------------------------------------------ Alt+Tab */

std::string Supervisor::session_text() const
{
    std::string text = "Alt+Tab:";
    for (std::size_t i = 0; i < session_.cycle.size(); ++i) {
        const AppId id = session_.cycle[i];
        std::string label = "Home";
        if (id != kNoApp) {
            const Entry *e = model_.find(id);
            label = e ? e->spec.name + " (" + short_state(e->state) + ")" : std::string("(gone)");
        }
        const bool selected = static_cast<int>(i) == session_.selected;
        text += selected ? "  [" + label + "]" : "  " + label;
    }
    return text;
}

std::string Supervisor::session_selection() const
{
    if (session_.selected < 0 || session_.selected >= static_cast<int>(session_.cycle.size())) return "-";
    const AppId id = session_.cycle[static_cast<std::size_t>(session_.selected)];
    if (id == kNoApp) return "Home";
    const Entry *e = model_.find(id);
    return e ? e->spec.name : std::string("(gone)");
}

void Supervisor::switch_event(SwitchEvent event, bool raw)
{
    const std::uint64_t now = now_ms();
    dirty_ = true;
    if (event == SwitchEvent::Home) {
        if (session_.active) return;
        if (model_.front() != kNoApp) {
            log("Super / Home key");
            execute(model_.go_launcher(now));
        }
        return;
    }
    if (event == SwitchEvent::Start || event == SwitchEvent::StartReverse) {
        if (session_.active) return;
        const AppId front = model_.front();
        if (front != kNoApp) {
            const Entry *e = model_.find(front);
            if (!e || !e->lifecycle() || e->state != State::Foreground) {
                log("Alt+Tab ignored: " + (e ? e->spec.name : std::string("the app in front")) +
                    " cannot stay in the background");
                return;
            }
        }
        if (sheet_ != kNoApp) { /* P-d: the sheet waits for its answer (Keep / Close) */
            log("Alt+Tab ignored: the sheet is up");
            return;
        }
        session_ = Session();
        session_.active = true;
        session_.raw = raw;
        session_.origin = front;
        /* P-d: over a switcher opened by a gesture, a cancel goes back to where that switcher was opened from */
        if (config_.switcher_ui && front == kNoApp && switcher_.open) session_.origin = switcher_.origin;
        session_.cycle.push_back(front);
        for (const AppId id : model_.mru())
            if (id != front) session_.cycle.push_back(id);
        if (front != kNoApp) session_.cycle.push_back(kNoApp);
        const int n = static_cast<int>(session_.cycle.size());
        session_.selected = n > 1 ? (event == SwitchEvent::Start ? 1 : n - 1) : 0;
        if (config_.switcher_ui && !switcher_.open) { /* P-d: the switcher screen shows the session */
            switcher_.origin = front;
            switcher_.source = "Alt+Tab";
            switcher_.wanted = front != kNoApp; /* after the hide, at LauncherFront */
        }
        if (raw) execute(model_.key_session(true, now));
        if (front != kNoApp) execute(model_.go_launcher(now));
        const std::string text = session_text();
        log("session " + std::string(raw ? "(evdev) " : "(launcher) ") + text);
        log("session: select " + session_selection()); /* one line per Tab / Shift+Tab, the first included */
        if (!config_.switcher_ui) ui_.overlay(text);
        else if (front == kNoApp) post_switcher(!switcher_.open);
        return;
    }
    if (!session_.active) return;
    const int n = static_cast<int>(session_.cycle.size());
    switch (event) {
    case SwitchEvent::Next:
    case SwitchEvent::Prev:
        if (n > 0) session_.selected = (session_.selected + (event == SwitchEvent::Next ? 1 : n - 1)) % n;
        log("session: select " + session_selection());
        if (!config_.switcher_ui) ui_.overlay(session_text());
        else if (switcher_.open) post_switcher();
        break;
    case SwitchEvent::Commit: end_session(n > 0 ? session_.cycle[static_cast<std::size_t>(session_.selected)] : kNoApp); break;
    case SwitchEvent::Cancel:
        log("session: cancelled");
        end_session(session_.origin);
        break;
    default: break;
    }
}

void Supervisor::end_session(AppId target)
{
    const std::uint64_t now = now_ms();
    const bool raw = session_.raw;
    const Entry *e = target == kNoApp ? nullptr : model_.find(target);
    log("session ends: " + (target == kNoApp ? std::string("Home") : e ? e->spec.name : std::string("(gone)")));
    session_ = Session();
    trace_until_ms_ = now + 2000;
    if (!config_.switcher_ui) ui_.overlay("");
    if (raw) execute(model_.key_session(false, now));
    if (target == kNoApp) execute(model_.go_launcher(now));
    else execute(model_.resume(target, now));
    if (!config_.switcher_ui) return;
    /* P-d: an app that comes to the front takes the switcher away (AppFront); Home closes it now; an app that cannot
     * come (closing) leaves it open, out of the session (touch works again) */
    if (target == kNoApp) close_switcher(false);
    else if (switcher_.open) post_switcher();
}

/* ------------------------------------------------------------------------------------------------ dev entry */

AppId Supervisor::lookup(const std::string &what) const
{
    bool number = !what.empty();
    for (const char c : what)
        if (c < '0' || c > '9') number = false;
    for (const AppId id : model_.live()) {
        const Entry *e = model_.find(id);
        if (!e) continue;
        if (number && (std::to_string(e->pgid) == what || std::to_string(e->id) == what)) return id;
        if (iequals(e->spec.name, what)) return id;
    }
    return kNoApp;
}

void Supervisor::handle_command(const std::string &raw_line)
{
    std::string line = raw_line;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    const std::size_t space = line.find(' ');
    const std::string word = line.substr(0, space);
    const std::string arg = space == std::string::npos ? std::string() : line.substr(space + 1);
    const std::uint64_t now = now_ms();
    log("command: " + line);
    if (word == "home") {
        execute(model_.go_launcher(now));
    } else if (word == "switch" || word == "close") {
        const AppId id = lookup(arg);
        if (id == kNoApp) {
            log("command: no resident app \"" + arg + "\"");
            return;
        }
        execute(word == "switch" ? model_.resume(id, now) : model_.close(id, now));
    } else if (word == "close-all") {
        execute(model_.close_all(now));
    } else if (word == "status") {
        last_status_.clear();
        write_status();
        log("status:\n" + status_text());
    } else if (word == "memory") {
        execute(model_.memory(std::strtol(arg.c_str(), nullptr, 10), now));
    } else if (word == "g1" || word == "g2") { /* P-c: a recognised gesture without the fingers */
        const std::string gesture = word == "g1" ? "G1" : "G2";
        log(gesture + " fired (dev FIFO)");
        open_switcher(gesture);
    } else {
        log("command: unknown (home, switch X, close X, close-all, status, memory KB, g1, g2)");
    }
}

/* ------------------------------------------------------------------------------------------------ touch (P-c) */

/* G1, G2, the app's "switcher", the FIFO's g1 / g2.
 * P-d (switcher_ui): a lifecycle app in front is hidden (the P-b path: hide, ack, freeze, snapshot) and the switcher
 * shows once the launcher is in front; a legacy app in front is frozen behind the sheet "Close <app>?"; on the launcher
 * the switcher shows when an app is resident (nothing otherwise). While the switcher or the sheet is up a gesture
 * leaves it as Esc does (back to where it was opened from; Keep).
 * Without switcher_ui (canvases under 640x480) the P-c stand-in: from an app it is Home, as Super (a legacy app
 * cannot: the model's notice says so); with the launcher in front there is nothing to choose from. */
void Supervisor::open_switcher(const std::string &source)
{
    dirty_ = true;
    const AppId front = model_.front();
    if (!config_.switcher_ui) {
        if (front == kNoApp) {
            log("switcher (" + source + "): the launcher is in front, nothing to do (P-c stand-in; P-d opens the switcher)");
            return;
        }
        const Entry *e = model_.find(front);
        log("switcher (" + source + "): P-c stand-in = Home (as Super) for " + (e ? e->spec.name : std::string("the app")));
        execute(model_.go_launcher(now_ms()));
        return;
    }
    const std::uint64_t now = now_ms();
    if (sheet_ != kNoApp) {
        log("switcher (" + source + "): the sheet is up: Keep");
        Request keep;
        keep.text = "keep";
        screen_request(keep);
        return;
    }
    if (switcher_.open || switcher_.wanted) {
        Request cancel;
        cancel.text = "cancel";
        cancel.source = source;
        screen_request(cancel);
        return;
    }
    if (session_.active) {
        log("switcher (" + source + "): an Alt+Tab session runs, ignored");
        return;
    }
    if (front != kNoApp) {
        const Entry *e = model_.find(front);
        if (!e) return;
        if (e->state != State::Foreground) {
            log("switcher (" + source + "): " + e->spec.name + " is " + state_name(e->state) + ", ignored");
            return;
        }
        if (!e->lifecycle()) { /* study 6.2, D12: it cannot stay in the background; the user decides */
            sheet_ = front;
            sheet_posted_ = false;
            log("switcher: sheet shown for " + e->spec.name + " (" + source + "): it cannot stay in the background");
            execute(model_.hold_front(now));
            return;
        }
        switcher_.wanted = true;
        switcher_.origin = front;
        switcher_.source = source;
        log("switcher (" + source + "): hiding " + e->spec.name + " first");
        execute(model_.go_launcher(now));
        return;
    }
    if (model_.resident_count() == 0) {
        log("switcher (" + source + "): nothing open, nothing to do");
        return;
    }
    switcher_.origin = kNoApp;
    switcher_.source = source;
    post_switcher(true);
}

/* ------------------------------------------------------------------------------------------- switcher (P-d) */

SwitcherInfo Supervisor::switcher_info()
{
    SwitcherInfo info;
    info.open = true;
    info.source = switcher_.source;
    for (const AppId id : model_.mru()) {
        const Entry *e = model_.find(id);
        if (!e || static_cast<int>(info.cards.size()) >= kSwitcherSlots) continue;
        SwitcherCard c;
        c.id = id;
        c.name = e->spec.name;
        c.key = e->spec.key;
        c.state = e->state;
        c.origin = id == switcher_.origin;
        info.cards.push_back(std::move(c));
    }
    info.session = session_.active;
    if (session_.active) {
        std::vector<AppId> ids;
        for (const SwitcherCard &c : info.cards) ids.push_back(c.id);
        info.selected = session_item(session_.cycle, session_.selected, ids);
    }
    return info;
}

void Supervisor::post_switcher(bool opening)
{
    SwitcherInfo info = switcher_info();
    if (opening) {
        switcher_.open = true;
        const Entry *origin = model_.find(switcher_.origin);
        log("switcher: opened by " + switcher_.source + " with " + apps_text(static_cast<int>(info.cards.size())) +
            (origin ? " (from " + origin->spec.name + ")" : std::string(" (on the launcher)")));
        if (info.selected < 0) info.selected = 0; /* the first card: the app it was opened from, or the last used */
    }
    ui_.switcher(info);
}

void Supervisor::close_switcher(bool app_next)
{
    const bool was_open = switcher_.open;
    switcher_ = SwitcherState();
    if (!was_open) return;
    SwitcherInfo info;
    info.open = false;
    info.app_next = app_next;
    ui_.switcher(info);
}

/* What the user did on the screens (the launcher's UI thread posts it), and gestures / Esc over them. */
void Supervisor::screen_request(const Request &r)
{
    dirty_ = true;
    const std::uint64_t now = now_ms();
    const Entry *e = model_.find(r.id);
    const std::string name = e ? e->spec.name : std::string("(gone)");
    if (r.text == "keep" || r.text == "sheet-close") {
        if (sheet_ == kNoApp) return;
        const Entry *held = model_.find(sheet_);
        const std::string held_name = held ? held->spec.name : std::string("(gone)");
        if (r.text == "keep") {
            log("switcher: sheet keep " + held_name);
            execute(model_.resume(sheet_, now)); /* the sheet goes when the app takes the panel (AppFront) */
        } else {
            log("switcher: sheet close " + held_name);
            execute(model_.close(sheet_, now)); /* the sheet goes now (sync_screens): the launcher shows its grid */
        }
        return;
    }
    if (!switcher_.open && !(r.text == "cancel" && switcher_.wanted)) return;
    if (r.text == "pick") {
        if (!e) return;
        log("switcher: card tapped " + name);
        execute(model_.resume(r.id, now)); /* the switcher goes when the app takes the panel (AppFront) */
    } else if (r.text == "close") {
        if (!e) return;
        log("switcher: closed " + name);
        execute(model_.close(r.id, now)); /* the card goes with the next update */
    } else if (r.text == "close-all") {
        log("switcher: close-all (" + apps_text(model_.resident_count()) + ")");
        close_switcher(false);
        execute(model_.close_all(now));
    } else if (r.text == "home") {
        log("switcher: home");
        close_switcher(false);
    } else if (r.text == "cancel") {
        const AppId origin = switcher_.origin;
        const Entry *o = model_.find(origin);
        const std::string by = r.source.empty() ? std::string() : " by " + r.source;
        if (o && (is_hidden(o->state) || o->state == State::Hiding)) { /* hiding: it comes back once hidden */
            log("switcher: cancelled" + by + " (back to " + o->spec.name + ")");
            execute(model_.resume(origin, now));
        } else {
            log("switcher: cancelled" + by + " (back to the launcher)");
            close_switcher(false);
        }
    }
}

/* After every turn: the sheet goes once its app is no longer held (closed, or gone by itself); the switcher closes
 * when no app is left; the cards follow the apps (UiPort::apps for the home grid's dots, the switcher's update); the
 * first-run hint. */
void Supervisor::sync_screens()
{
    if (!config_.switcher_ui) return;
    if (sheet_ != kNoApp) {
        const Entry *held = model_.find(sheet_);
        if (!held || held->state != State::Held) {
            if (sheet_posted_) ui_.sheet(kNoApp, std::string());
            sheet_ = kNoApp;
            sheet_posted_ = false;
            dirty_ = true;
        }
    }
    if (switcher_.open && model_.resident_count() == 0) {
        log("switcher: no app open any more: the launcher");
        close_switcher(false);
        dirty_ = true;
    }
    std::vector<SwitcherCard> cards;
    std::string sig;
    for (const AppId id : model_.mru()) {
        const Entry *e = model_.find(id);
        if (!e) continue;
        SwitcherCard c;
        c.id = id;
        c.name = e->spec.name;
        c.key = e->spec.key;
        c.state = e->state;
        sig += std::to_string(id) + ":" + state_name(e->state) + ";";
        cards.push_back(std::move(c));
    }
    if (sig != cards_sig_) {
        cards_sig_ = sig;
        ui_.apps(cards);
        if (switcher_.open) post_switcher();
    }
    show_hint();
}

/* D15: the first time an app is left open (hidden), once per install: the file hint_path remembers it. */
void Supervisor::show_hint()
{
    if (hint_done_ || model_.hidden_count() == 0) return;
    hint_done_ = true;
    if (!config_.hint_path.empty()) {
        struct stat st;
        if (::stat(config_.hint_path.c_str(), &st) == 0) return; /* shown in an earlier run */
        make_parent_dirs(config_.hint_path);
        const int fd = ::open(config_.hint_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd >= 0) {
            static const char text[] = "the launcher's first-run hint was shown (multitasking); delete this file to see "
                                       "it again\n";
            (void)!::write(fd, text, sizeof(text) - 1);
            ::close(fd);
        } else {
            log("hint: cannot write " + config_.hint_path + ": " + std::strerror(errno) + " (it may show again)");
        }
    }
    log("hint: \"" + config_.hint_text + "\" (the first app left open; shown once, " +
        (config_.hint_path.empty() ? std::string("not remembered") : "remembered in " + config_.hint_path) + ")");
    ui_.notice(config_.hint_text);
}

/* The passive reader is open exactly while an app is in front (the launcher reads its own touch fd otherwise). */
void Supervisor::sync_touch()
{
    const AppId front = model_.front();
    if (front == kNoApp) touch_failed_ = kNoApp;
    const bool want = touch_ && config_.touch.g1 && front != kNoApp && touch_failed_ != front;
    if (want == touch_open_) return;
    dirty_ = true;
    if (!want) {
        touch_->close();
        touch_open_ = false;
        log("touch: reader closed (the launcher is in front)");
        return;
    }
    if (!touch_->open()) {
        touch_failed_ = front;
        log(std::string("touch: cannot open the touch screen (") + std::strerror(errno) + "): no G1 in this app");
        return;
    }
    touch_open_ = true;
    /* fingers down as the app comes to the front (the tap that opened it, the hand of a G1 made on the grid) are not
     * a G1: ignored until they lift */
    if (touch_->down() > 0) cp0_g1_block(&g1_, now_us());
    else cp0_g1_reset(&g1_);
    const Entry *e = model_.find(front);
    log("touch: reading the touch screen passively (no grab) for G1 while " +
        (e ? e->spec.name : std::string("the app")) + " is in front");
}

void Supervisor::on_touch_frame(void *context, const cp0_touch_frame_t &frame)
{
    auto *self = static_cast<Supervisor *>(context);
    cp0_external_app_touch();
    self->touch_event(cp0_g1_frame(&self->g1_, &frame));
}

void Supervisor::poll_touch()
{
    if (!touch_open_) return;
    if (!touch_->poll(&Supervisor::on_touch_frame, this)) {
        log("touch: the touch screen went away");
        touch_->close();
        touch_open_ = false;
        touch_failed_ = model_.front();
        return;
    }
    if (touch_open_) touch_event(cp0_g1_tick(&g1_, now_us()));
}

void Supervisor::touch_event(const cp0_g1_event_t &event)
{
    if (event.outcome == CP0_G1_NONE) return;
    char line[192];
    cp0_g1_describe(&g1_, &event, line, sizeof(line));
    log(line);
    if (event.outcome == CP0_G1_FIRED) open_switcher("G1");
}

void Supervisor::write_status()
{
    std::string text = "v=1\n";
    const AppId front = model_.front();
    const Entry *fe = model_.find(front);
    text += "front=" + (front == kNoApp ? std::string("home") : fe ? fe->spec.name : std::string("?")) + "\n";
    text += "residents=" + std::to_string(model_.resident_count()) + "\n";
    text += "hidden=" + std::to_string(model_.hidden_count()) + "\n";
    text += std::string("session=") + (session_.active ? "on" : "off") + "\n";
    if (config_.switcher_ui) { /* P-d */
        const Entry *held = model_.find(sheet_);
        text += std::string("switcher=") + (switcher_.open ? "open" : "off") + "\n";
        text += "sheet=" + (held && held->state == State::Held ? held->spec.name : std::string("-")) + "\n";
    }
    text += "mem_available_kb=" + std::to_string(model_.last_memory_kb()) + "\n";
    const Derived &d = model_.derived();
    text += std::string("grab_keys=") + (d.grab_keys ? "1" : "0") + " grab_touch=" + (d.grab_touch ? "1" : "0") +
            " launcher_keys=" + (d.launcher_keys ? "1" : "0") + " evdev_keys=" + (d.raw_keys ? "1" : "0") +
            " evdev_touch=" + (touch_open_ ? "1" : "0") + "\n";
    for (const AppId id : model_.live()) {
        const Entry *e = model_.find(id);
        if (!e) continue;
        const auto it = runtime_.find(id);
        text += "app id=" + std::to_string(e->id) + " name=\"" + e->spec.name + "\" state=" + state_name(e->state) +
                " bg=" + background_name(e->spec.background) + " pgid=" + std::to_string(e->pgid) +
                " mru=" + std::to_string(e->mru) + " oom=" + std::to_string(e->oom_now) + " snapshot=" +
                std::to_string(it == runtime_.end() ? 0 : it->second.snapshot.size()) + " control=" +
                (it != runtime_.end() && it->second.control_fd >= 0 ? "1" : "0") + "\n";
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_text_ = text;
    }
    if (text == last_status_ || config_.status_path.empty()) return;
    last_status_ = text;
    const std::string tmp = config_.status_path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "w");
    if (!f) return;
    std::fputs(text.c_str(), f);
    std::fclose(f);
    (void)std::rename(tmp.c_str(), config_.status_path.c_str());
}

void Supervisor::publish_residents()
{
    std::vector<std::string> keys;
    for (const AppId id : model_.live()) {
        const Entry *e = model_.find(id);
        if (e && is_resident(e->state)) keys.push_back(e->spec.key);
    }
    const int count = model_.resident_count();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        keys_resident_ = std::move(keys);
    }
    if (residents_.exchange(count) != count) ui_.residents(count);
}

#else // !__linux__: no supervisor

Supervisor::Supervisor(SupervisorConfig config, ProcessPort &proc, FramebufferPort &fb, InputPort &input,
                       KeyPort &keys, UiPort &ui)
    : config_(std::move(config)), proc_(proc), fb_(fb), input_(input), keys_(keys), ui_(ui), model_(config_.model)
{
}
Supervisor::~Supervisor() = default;
bool Supervisor::start() { return false; }
void Supervisor::stop() {}
void Supervisor::launch(const AppSpec &) {}
void Supervisor::resume(AppId) {}
void Supervisor::go_launcher() {}
void Supervisor::close(AppId) {}
void Supervisor::close_all() {}
void Supervisor::launcher_switch_event(SwitchEvent) {}
void Supervisor::fake_memory(long) {}
void Supervisor::command(const std::string &) {}
void Supervisor::launcher_gesture(const std::string &, const std::string &, bool) {}
void Supervisor::switcher_pick(AppId) {}
void Supervisor::switcher_close(AppId) {}
void Supervisor::switcher_close_all() {}
void Supervisor::switcher_home() {}
void Supervisor::switcher_cancel() {}
void Supervisor::sheet_answer(bool) {}
bool Supervisor::is_resident_key(const std::string &) const { return false; }
std::string Supervisor::status_text() const { return std::string(); }
void Supervisor::set_log_sink(LogSink) {}
Supervisor::Stats Supervisor::stats() const { return Stats(); }
void Supervisor::run_once() {}

#endif

} // namespace multitask
