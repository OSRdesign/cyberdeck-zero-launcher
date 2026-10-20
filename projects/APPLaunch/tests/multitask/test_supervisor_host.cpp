// SPDX-License-Identifier: MIT
//
// Multitasking P-b (task 015, study 026 section 7.3 "host process tests"): the real supervisor (multitask::Supervisor
// with the Linux process port) runs the spike's fake app tools/multitasking-spike fbhapp, which speaks the lifecycle
// protocol, on a file-backed framebuffer; FIFOs stand in for the input nodes (one FIFO per reader: the app's own queue
// and the supervisor's, as evdev gives each open file its own queue). Checked: the hide deadline and not responding,
// freeze (state T) and thaw, snapshot and restore, no framebuffer write while hidden (with a misbehaving app as the
// negative control), the drain of the app's queue before the thaw, SIGCONT before close / SIGTERM, exit and kill
// while hidden, the cap, the memory floor, Esc held 3 s, Alt+Tab from evdev, the dev FIFO, shutdown and the guardian.
//
//   test_supervisor_host <fbhapp binary> <work dir>
#include "multitask/supervisor.hpp"

#include "cp0_esc_key_watch.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <linux/input.h>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace multitask;

namespace {

std::string g_fbhapp;
std::string g_dir;
int g_pass = 0, g_fail = 0;
constexpr int kW = 64, kH = 48;
constexpr size_t kLen = static_cast<size_t>(kW) * kH * 4;

void check(const std::string &what, bool ok, const std::string &detail = std::string())
{
    if (ok) {
        ++g_pass;
        std::printf("ok   %s\n", what.c_str());
    } else {
        ++g_fail;
        std::printf("FAIL %s%s%s\n", what.c_str(), detail.empty() ? "" : ": ", detail.c_str());
    }
    std::fflush(stdout);
}

/* ---------------------------------------------------------------------------------------------- test ports */

struct FileFb : FramebufferPort {
    std::string path;
    int fd = -1;
    uint8_t *map = nullptr;
    std::atomic<bool> suspended{false};
    std::atomic<int> launcher_draws{0};

    explicit FileFb(const std::string &p) : path(p)
    {
        fd = ::open(p.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0 && ::ftruncate(fd, static_cast<off_t>(kLen)) == 0)
            map = static_cast<uint8_t *>(::mmap(nullptr, kLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        if (map == MAP_FAILED) map = nullptr;
    }
    ~FileFb() override
    {
        if (map) ::munmap(map, kLen);
        if (fd >= 0) ::close(fd);
    }
    void suspend() override { suspended = true; }
    bool snapshot(std::vector<uint8_t> &out, int &page_y) override
    {
        out.assign(map, map + kLen);
        page_y = 0;
        return true;
    }
    int restore(const std::vector<uint8_t> &in, int) override
    {
        if (in.size() != kLen) return -1;
        std::memcpy(map, in.data(), kLen);
        return std::memcmp(map, in.data(), kLen) == 0 ? 0 : 1;
    }
    /* the launcher repaints its home grid: a flat 0x5A page */
    void launcher_draw()
    {
        std::memset(map, 0x5A, kLen);
        ++launcher_draws;
    }
    std::vector<uint8_t> copy() const { return std::vector<uint8_t>(map, map + kLen); }
    bool is_launcher_page() const
    {
        for (size_t i = 0; i < kLen; ++i)
            if (map[i] != 0x5A) return false;
        return true;
    }
};

struct Recorder {
    std::mutex m;
    std::vector<std::string> lines;
    void add(const std::string &s)
    {
        std::lock_guard<std::mutex> lock(m);
        lines.push_back(s);
    }
    std::vector<std::string> get()
    {
        std::lock_guard<std::mutex> lock(m);
        return lines;
    }
    bool has(const std::string &needle)
    {
        for (const std::string &l : get())
            if (l.find(needle) != std::string::npos) return true;
        return false;
    }
    /* index of the first line containing needle, -1 */
    int index(const std::string &needle)
    {
        const auto all = get();
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].find(needle) != std::string::npos) return static_cast<int>(i);
        return -1;
    }
    void clear()
    {
        std::lock_guard<std::mutex> lock(m);
        lines.clear();
    }
};

struct TestUi : UiPort {
    FileFb &fb;
    Recorder &rec;
    TestUi(FileFb &f, Recorder &r) : fb(f), rec(r) {}
    void app_front(AppId, const std::string &name) override { rec.add("ui app_front " + name); }
    void launcher_front(bool after_exit) override
    {
        fb.suspended = false;
        fb.launcher_draw();
        rec.add(std::string("ui launcher_front") + (after_exit ? " exit" : ""));
    }
    void repaint() override
    {
        fb.launcher_draw();
        rec.add("ui repaint");
    }
    void notice(const std::string &text) override { rec.add("ui notice " + text); }
    void overlay(const std::string &text) override { rec.add("ui overlay " + text); }
    void esc_hint(bool visible) override { rec.add(std::string("ui esc_hint ") + (visible ? "1" : "0")); }
    void residents(int count) override { rec.add("ui residents " + std::to_string(count)); }
};

struct TestInput : InputPort {
    Recorder &rec;
    std::string prefix;
    TestInput(Recorder &r, std::string p) : rec(r), prefix(std::move(p)) {}
    void grab_keys(bool on) override { rec.add(std::string("input grab_keys ") + (on ? "1" : "0")); }
    void grab_touch(bool on) override { rec.add(std::string("input grab_touch ") + (on ? "1" : "0")); }
    void launcher_keys(bool on) override { rec.add(std::string("input launcher_keys ") + (on ? "1" : "0")); }
    DrainStats drain(int pgid) override { return drain_group_input(pgid, prefix); }
};

struct FifoKeys : KeyPort {
    std::string path;
    std::string path2; /* a second keyboard ("" = none) */
    Recorder &rec;
    cp0_esc_key_watch::Sources sources;
    FifoKeys(std::string p, Recorder &r, std::string p2 = std::string())
        : path(std::move(p)), path2(std::move(p2)), rec(r)
    {
    }
    void open() override
    {
        sources.open(path.c_str());
        if (!path2.empty()) sources.open(path2.c_str());
        rec.add("keys open");
    }
    void close() override
    {
        sources.close_all();
        rec.add("keys close");
    }
    int poll(Listener listener, void *context) override { return sources.poll(listener, context); }
    void esc_state(int held) override { rec.add("keys esc " + std::to_string(held)); }
    void fds(std::vector<int> &out) override
    {
        int f[cp0_esc_key_watch::Sources::kMax];
        out.assign(f, f + sources.fds(f, cp0_esc_key_watch::Sources::kMax));
    }
};

/* ---------------------------------------------------------------------------------------------- helpers */

std::string make_fifo(const std::string &name)
{
    const std::string p = g_dir + "/" + name;
    ::unlink(p.c_str());
    if (::mkfifo(p.c_str(), 0600) != 0) std::perror("mkfifo");
    return p;
}

int writer(const std::string &fifo)
{
    return ::open(fifo.c_str(), O_RDWR | O_NONBLOCK); /* RDWR: the FIFO never sees EOF while we hold it */
}

void send_event(int fd, unsigned short type, unsigned short code, int value)
{
    struct input_event e;
    std::memset(&e, 0, sizeof(e));
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    e.input_event_sec = ts.tv_sec;
    e.input_event_usec = ts.tv_nsec / 1000;
    e.type = type;
    e.code = code;
    e.value = value;
    if (::write(fd, &e, sizeof(e)) != static_cast<ssize_t>(sizeof(e))) std::perror("write event");
}

void key(int fd, unsigned short code, int value)
{
    send_event(fd, EV_KEY, code, value);
    send_event(fd, EV_SYN, SYN_REPORT, 0);
}

void tap_touch(int fd)
{
    send_event(fd, EV_ABS, ABS_MT_TRACKING_ID, 7);
    send_event(fd, EV_ABS, ABS_MT_POSITION_X, 10);
    send_event(fd, EV_ABS, ABS_MT_POSITION_Y, 10);
    send_event(fd, EV_SYN, SYN_REPORT, 0);
    send_event(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
    send_event(fd, EV_SYN, SYN_REPORT, 0);
}

std::string read_file(const std::string &path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int count(const std::string &text, const std::string &needle)
{
    int n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++n;
    return n;
}

/* the group still has a process (zombies included). Not group_members(): that walks this process's descendants,
 * and the guardian scenario's apps are orphans of another process by then */
bool group_exists(int pgid)
{
    return ::kill(-pgid, 0) == 0 || errno == EPERM;
}

/* every process of the group in state T */
bool group_stopped(int pgid)
{
    const std::vector<int> pids = group_members(pgid);
    if (pids.empty()) return false;
    for (const int pid : pids) {
        const std::string stat = read_file("/proc/" + std::to_string(pid) + "/stat");
        const size_t paren = stat.rfind(')');
        if (paren == std::string::npos || paren + 2 >= stat.size()) return false;
        const char s = stat[paren + 2];
        if (s != 'T' && s != 't') return false;
    }
    return true;
}

int read_oom(int pid)
{
    const std::string s = read_file("/proc/" + std::to_string(pid) + "/oom_score_adj");
    return s.empty() ? -9999 : std::atoi(s.c_str());
}

/* "key=value" from the status text, for the app line with name=...; or a top-level key */
std::string status_field(const std::string &status, const std::string &app, const std::string &field)
{
    std::istringstream in(status);
    std::string line;
    while (std::getline(in, line)) {
        if (!app.empty() && line.find("name=\"" + app + "\"") == std::string::npos) continue;
        if (app.empty() && line.rfind(field + "=", 0) != 0) continue;
        const size_t at = line.find(field + "=");
        if (at == std::string::npos) continue;
        const size_t end = line.find(' ', at);
        return line.substr(at + field.size() + 1, end == std::string::npos ? std::string::npos : end - at - field.size() - 1);
    }
    return std::string();
}

struct Rig {
    Recorder rec;   /* ports and the supervisor's log */
    FileFb fb;
    TestUi ui;
    TestInput input;
    std::string keys_fifo;
    FifoKeys keys;
    LinuxProcessPort proc;
    std::unique_ptr<Supervisor> sup;
    int kw = -1;  /* the "keyboard": every reader's FIFO stays open (no EOF) */
    int kw2 = -1; /* a second keyboard */

    /* fast: a 5 ms tick for the pumped scenarios; false keeps the launcher's defaults (the cost scenario) */
    explicit Rig(SupervisorConfig cfg = SupervisorConfig(), const std::string &tag = "rig", bool fast = true)
        : fb(g_dir + "/fb-" + tag + ".raw"), ui(fb, rec), input(rec, g_dir + "/app-in-"),
          keys_fifo(make_fifo("keys-" + tag)), keys(keys_fifo, rec, make_fifo("keys2-" + tag))
    {
        cfg.guardian = false;
        (void)fast; /* the pumped scenarios call run_once() themselves; the cost scenario runs the thread */
        sup = std::make_unique<Supervisor>(cfg, proc, fb, input, keys, ui);
        sup->set_log_sink([this](const std::string &l) { rec.add("log " + l); });
        kw = writer(keys_fifo);
        kw2 = writer(keys.path2);
    }
    ~Rig()
    {
        sup.reset();
        if (kw >= 0) ::close(kw);
        if (kw2 >= 0) ::close(kw2);
    }
    /* run the loop until cond() or timeout */
    bool pump(int timeout_ms, const std::function<bool()> &cond)
    {
        const std::uint64_t until = Supervisor::now_ms() + static_cast<std::uint64_t>(timeout_ms);
        while (Supervisor::now_ms() < until) {
            sup->run_once();
            if (cond && cond()) return true;
            ::usleep(5000);
        }
        sup->run_once();
        return cond ? cond() : true;
    }
    void pump_for(int ms) { pump(ms, nullptr); }
    std::string status() { return sup->status_text(); }
    std::string state(const std::string &app) { return status_field(status(), app, "state"); }
    int pgid(const std::string &app) { return std::atoi(status_field(status(), app, "pgid").c_str()); }
};

struct App {
    std::string name;
    std::string in_fifo;   /* the app's own input queue */
    std::string log;
    int in_writer = -1;
};

App make_app(const std::string &name)
{
    App a;
    a.name = name;
    a.in_fifo = make_fifo("app-in-" + name);
    a.log = g_dir + "/" + name + ".log";
    ::unlink(a.log.c_str());
    a.in_writer = writer(a.in_fifo);
    return a;
}

AppSpec spec_for(const App &a, const std::string &fb_path, Background bg, const std::string &extra = std::string())
{
    AppSpec s;
    s.key = "fbhapp-" + a.name;
    s.name = a.name;
    s.background = bg;
    s.exec = "exec " + g_fbhapp + " --fb " + fb_path + " --fb-geom " + std::to_string(kW) + "x" + std::to_string(kH) +
             "x32 --input " + a.in_fifo + " --log " + a.log + " --tag " + a.name + " --no-esc " + extra;
    return s;
}

bool app_logged(const App &a, const std::string &needle)
{
    return read_file(a.log).find(needle) != std::string::npos;
}

/* ---------------------------------------------------------------------------------------------- scenarios */

void scenario_freeze_cycle()
{
    std::printf("== freeze: hide, ack, SIGSTOP, snapshot, no write while hidden, stale input drained, restore\n");
    Rig r(SupervisorConfig(), "freeze");
    App a = make_app("frz");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    check("app started and draws", r.pump(3000, [&] { return app_logged(a, "rec=start") && !r.fb.is_launcher_page() &&
                                                            r.fb.copy() != std::vector<uint8_t>(kLen, 0); }));
    check("control socket given (contract=1)", app_logged(a, "contract=1"));
    check("display suspended before the app ran", r.fb.suspended.load());
    const int pgid = r.pgid("frz");
    check("pgid known", pgid > 0, r.status());
    r.pump_for(200);
    r.sup->go_launcher();
    check("hidden and frozen", r.pump(2000, [&] { return r.state("frz") == "hidden-frozen"; }), r.status());
    check("ack within the deadline", r.rec.has("ack 1 (hide) ack_ms="));
    check("SIGSTOP after the ack", r.rec.index("SIGSTOP") > r.rec.index("ack 1 (hide)"));
    check("group stopped (state T)", group_stopped(pgid));
    check("snapshot taken", r.rec.has("frz: snapshot " + std::to_string(kLen) + " bytes"));
    check("launcher in front", r.rec.has("ui launcher_front") && r.fb.is_launcher_page());
    check("oom_score_adj 900 while frozen", read_oom(pgid) == 900, std::to_string(read_oom(pgid)));
    check("grab on while the launcher is in front", r.rec.has("input grab_keys 1") && r.rec.has("input grab_touch 1"));
    // the user types and touches while the app is frozen: its own queue fills
    key(a.in_writer, KEY_ENTER, 1);
    key(a.in_writer, KEY_ENTER, 0);
    key(a.in_writer, KEY_ENTER, 1);
    key(a.in_writer, KEY_ENTER, 0);
    tap_touch(a.in_writer);
    r.pump_for(400);
    check("no framebuffer write while hidden", r.fb.is_launcher_page());
    r.rec.clear();
    r.sup->command("switch frz");
    check("resumed", r.pump(2000, [&] { return r.state("frz") == "foreground"; }), r.status());
    const auto log = r.rec.get();
    check("grab released before the thaw", r.rec.index("input grab_keys 0") >= 0 &&
                                               r.rec.index("input grab_keys 0") < r.rec.index("SIGCONT"));
    check("restore before the thaw", r.rec.index("restore") >= 0 && r.rec.index("restore") < r.rec.index("SIGCONT"));
    check("restore verified", r.rec.has("verified=1"));
    check("drain before the thaw", r.rec.index("drain fds=") >= 0 && r.rec.index("drain fds=") < r.rec.index("SIGCONT"));
    check("drain took the stale input (2 Enter, 1 touch)", r.rec.has("keys=2 touches=1"));
    check("oom_score_adj back to the start value", read_oom(pgid) != 900, std::to_string(read_oom(pgid)));
    check("show acked", r.pump(1500, [&] { return r.rec.has("(show) ack_ms="); }));
    r.pump_for(300);
    const std::string applog = read_file(a.log);
    check("the app found nothing stale to drain itself", applog.find("rec=show n=2 drained_events=0") != std::string::npos);
    check("the app acted on no stale input", count(applog, "acted=1") == 0, applog);
    check("the app repaints after show", !r.fb.is_launcher_page());
    r.sup->stop();
    check("app ended at shutdown", app_logged(a, "rec=end"));
}

void scenario_run_mode()
{
    std::printf("== run: hidden running, not stopped, oom 700, keeps working, no draw while hidden\n");
    Rig r(SupervisorConfig(), "run");
    App a = make_app("run");
    r.sup->launch(spec_for(a, r.fb.path, Background::Run));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    r.pump_for(200);
    const int pgid = r.pgid("run");
    const int base = read_oom(pgid);
    r.sup->go_launcher();
    check("hidden running", r.pump(2000, [&] { return r.state("run") == "hidden-running"; }), r.status());
    check("not stopped", !group_stopped(pgid) && !r.rec.has("SIGSTOP"));
    check("oom_score_adj 700", read_oom(pgid) == 700);
    r.pump_for(600);
    check("no framebuffer write while hidden (it honours hide)", r.fb.is_launcher_page());
    r.sup->command("switch run");
    check("resumed", r.pump(2000, [&] { return r.state("run") == "foreground"; }));
    check("no SIGCONT for a running app", !r.rec.has("SIGCONT"));
    check("oom back to the start value", read_oom(pgid) == base, std::to_string(read_oom(pgid)));
    r.pump_for(300);
    const std::string applog = read_file(a.log);
    const size_t hide_at = applog.find("rec=hide n=1 work=");
    const size_t show_at = applog.find("rec=show n=2");
    long work_hide = -1, work_show = -1;
    if (hide_at != std::string::npos) work_hide = std::atol(applog.c_str() + hide_at + 18);
    if (show_at != std::string::npos) {
        const size_t w = applog.find("work=", show_at);
        if (w != std::string::npos) work_show = std::atol(applog.c_str() + w + 5);
    }
    check("kept working while hidden", work_show >= work_hide + 4,
          "work at hide " + std::to_string(work_hide) + ", at show " + std::to_string(work_show));
    r.sup->stop();
}

void scenario_draw_hidden_negative()
{
    std::printf("== negative control: an app that draws while hidden is seen\n");
    Rig r(SupervisorConfig(), "neg");
    App a = make_app("bad");
    r.sup->launch(spec_for(a, r.fb.path, Background::Run, "--draw-hidden"));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    r.pump_for(200);
    r.sup->go_launcher();
    r.pump(2000, [&] { return r.state("bad") == "hidden-running"; });
    r.pump_for(400);
    check("its writes over the launcher are detected", !r.fb.is_launcher_page());
    r.sup->stop();
}

void scenario_slow_ack_not_responding()
{
    std::printf("== no ack within 300 ms: SIGSTOP, not responding, resumable, late ack ignored\n");
    Rig r(SupervisorConfig(), "slow");
    App a = make_app("slow");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze, "--ack-delay-ms 700"));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    r.pump_for(200);
    const std::uint64_t t0 = Supervisor::now_ms();
    r.sup->go_launcher();
    check("not responding", r.pump(2000, [&] { return r.state("slow") == "not-responding"; }), r.status());
    const std::uint64_t dt = Supervisor::now_ms() - t0;
    check("after about 300 ms", dt >= 280 && dt < 700, std::to_string(dt) + " ms");
    check("frozen", group_stopped(r.pgid("slow")));
    check("notice", r.rec.has("ui notice slow is not responding"));
    r.sup->command("switch slow");
    check("resumed", r.pump(2000, [&] { return r.state("slow") == "foreground"; }));
    check("show acked", r.pump(2500, [&] { return r.rec.has("(show) ack_ms="); }));
    r.pump_for(500);
    check("still in front (a late ack 1, if any, is ignored)", r.state("slow") == "foreground", r.status());
    r.sup->stop();
}

void scenario_close_frozen()
{
    std::printf("== close a frozen app: SIGCONT first, close message, graceful exit\n");
    Rig r(SupervisorConfig(), "close");
    App a = make_app("cls");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    r.pump_for(150);
    r.sup->go_launcher();
    check("frozen", r.pump(2000, [&] { return r.state("cls") == "hidden-frozen"; }));
    r.rec.clear();
    r.sup->command("close cls");
    check("gone", r.pump(3000, [&] { return r.rec.has("process group") && r.status().find("cls") == std::string::npos; }),
          r.status());
    check("SIGCONT before the close message", r.rec.index("SIGCONT") >= 0 && r.rec.index("SIGCONT") < r.rec.index("sent \"close"));
    check("no SIGTERM needed", !r.rec.has("SIGTERM"));
    check("the app closed itself", app_logged(a, "rec=exit reason=close"));
    check("grab released (nothing hidden)", r.rec.has("input grab_keys 0"));
    r.sup->stop();
}

void scenario_close_ignores_close_message()
{
    std::printf("== close a not responding app: SIGCONT, close, the launcher repaints\n");
    Rig r(SupervisorConfig(), "term");
    App a = make_app("deaf");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze, "--ignore-hide"));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    r.pump_for(150);
    r.sup->go_launcher();
    check("not responding", r.pump(2000, [&] { return r.state("deaf") == "not-responding"; }));
    r.rec.clear();
    r.sup->command("close deaf");
    // fbhapp with --ignore-hide still exits on "close": it is continued first, then reads it
    check("gone", r.pump(5000, [&] { return r.status().find("deaf") == std::string::npos; }), r.status());
    check("SIGCONT first", r.rec.index("SIGCONT") >= 0 && r.rec.index("SIGCONT") < r.rec.index("sent \"close"));
    check("the launcher repaints after a not responding app closed", r.rec.has("ui repaint"));
    r.sup->stop();
}

void scenario_exit_and_kill_while_hidden()
{
    std::printf("== an app exits while hidden; an app is killed while frozen\n");
    Rig r(SupervisorConfig(), "exit");
    App a = make_app("brief");
    App b = make_app("victim");
    r.sup->launch(spec_for(a, r.fb.path, Background::Run, "--duration 1.5"));
    check("A started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    r.pump_for(100);
    r.sup->go_launcher();
    check("A hidden running", r.pump(2000, [&] { return r.state("brief") == "hidden-running"; }));
    r.sup->launch(spec_for(b, r.fb.path, Background::Freeze));
    check("B started", r.pump(3000, [&] { return app_logged(b, "rec=start"); }));
    r.pump_for(100);
    r.sup->go_launcher();
    check("B frozen", r.pump(2000, [&] { return r.state("victim") == "hidden-frozen"; }));
    check("A exits while hidden", r.pump(3000, [&] { return r.rec.has("ui notice brief exited"); }), r.status());
    const int pgid = r.pgid("victim");
    ::killpg(pgid, SIGKILL);
    check("B gone after SIGKILL while frozen", r.pump(2000, [&] { return r.rec.has("ui notice victim exited"); }));
    check("no resident left", r.status().find("residents=0") != std::string::npos, r.status());
    check("grab released", r.rec.has("input grab_touch 0"));
    r.sup->stop();
}

void scenario_esc_hold()
{
    std::printf("== Esc held 3 s on the app in front: SIGTERM (today's policy)\n");
    Rig r(SupervisorConfig(), "esc");
    App a = make_app("esc");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    check("evdev keys read while the app is in front", r.rec.has("keys open"));
    const int kw = r.kw;
    key(kw, KEY_ESC, 1);
    check("hint at 500 ms", r.pump(1500, [&] { return r.rec.has("ui esc_hint 1"); }));
    check("SIGTERM at 3 s", r.pump(3500, [&] { return r.rec.has("esc: SIGTERM"); }));
    check("launcher back after the exit", r.pump(3000, [&] { return r.rec.has("ui launcher_front exit"); }));
    key(kw, KEY_ESC, 0);
    r.sup->stop();
}

void scenario_alt_tab_evdev()
{
    std::printf("== Alt+Tab from evdev inside an app: hide, text, Tab, release Alt switches\n");
    Rig r(SupervisorConfig(), "alttab");
    App a = make_app("alpha");
    App b = make_app("beta");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    check("A started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    r.pump_for(100);
    r.sup->go_launcher();
    check("A frozen", r.pump(2000, [&] { return r.state("alpha") == "hidden-frozen"; }));
    r.sup->launch(spec_for(b, r.fb.path, Background::Run));
    check("B started", r.pump(3000, [&] { return app_logged(b, "rec=start"); }));
    r.pump_for(100);
    const int kw = r.kw;
    r.rec.clear();
    key(kw, KEY_LEFTALT, 1);
    key(kw, KEY_TAB, 1);
    key(kw, KEY_TAB, 0);
    check("session text with the previous app selected",
          r.pump(2000, [&] { return r.rec.has("ui overlay Alt+Tab:  beta (in front)  [alpha (paused)]  Home") ||
                                    r.rec.has("ui overlay Alt+Tab:  beta (running)  [alpha (paused)]  Home"); }));
    check("B hidden for the session", r.pump(2000, [&] { return r.state("beta") == "hidden-running"; }));
    check("keyboard grab waits for the Alt release", !r.rec.has("input grab_keys 1"));
    key(kw, KEY_TAB, 1);
    key(kw, KEY_TAB, 0);
    check("Tab moves on", r.pump(1000, [&] { return r.rec.has("[Home]"); }));
    key(kw, KEY_LEFTSHIFT, 1);
    key(kw, KEY_TAB, 1);
    key(kw, KEY_TAB, 0);
    key(kw, KEY_LEFTSHIFT, 0);
    r.pump_for(100);
    key(kw, KEY_LEFTALT, 0);
    check("Alt release switches to A", r.pump(2000, [&] { return r.state("alpha") == "foreground"; }), r.status());
    bool hidden_text = false;
    for (const std::string &l : r.rec.get())
        if (l == "ui overlay ") hidden_text = true;
    check("text hidden", hidden_text);
    int selects = 0;
    for (const std::string &l : r.rec.get())
        if (l.find("log session: select ") == 0) ++selects;
    check("one 'session: select' line per Tab / Shift+Tab (3)", selects == 3, std::to_string(selects));
    check("the lines name the selection", r.rec.has("log session: select Home") && r.rec.has("log session: select alpha"));
    // Esc with Alt held cancels: back to the app the session started from
    r.pump_for(300);
    r.rec.clear();
    key(kw, KEY_LEFTALT, 1);
    key(kw, KEY_TAB, 1);
    key(kw, KEY_TAB, 0);
    check("second session hides alpha", r.pump(2000, [&] { return r.state("alpha") == "hidden-frozen"; }), r.status());
    key(kw, KEY_ESC, 1);
    key(kw, KEY_ESC, 0);
    key(kw, KEY_LEFTALT, 0);
    check("Esc with Alt held: 'session: cancelled'", r.pump(1000, [&] { return r.rec.has("log session: cancelled"); }));
    check("back to alpha", r.pump(2000, [&] { return r.state("alpha") == "foreground"; }), r.status());
    r.sup->stop();
}

/* Deck finding (build pb.2): Esc with Alt held did not cancel an Alt+Tab session on the deck. The deck's state
 * exactly (a frozen app hidden, a run app in front, the session started inside it), the keyboard's real ordering with
 * gaps between the events, and the variants: Esc autorepeat, Esc from a second keyboard, Esc before the app in front
 * is hidden, and a keyboard whose firmware repeats a held Tab as new presses (the deck log's "select Home").
 * Build pb.4 (owner decision 2026-10-20): Backspace cancels like Esc (the M4 keyboard sends no Esc while Alt is held),
 * so every variant runs with each of the two keys. */
void scenario_alt_tab_cancel_deck()
{
    std::printf("== Alt+Tab cancel as on the deck: a run app in front, a frozen app hidden\n");
    static const char *names[] = {"plain", "autorepeat", "second-keyboard", "before-hide", "tab-firmware-repeat"};
    static const struct {
        int code;
        const char *name; /* switch_key_name */
    } cancel_keys[] = {{KEY_ESC, "Esc"}, {KEY_BACKSPACE, "BackSpace"}};
    for (int i = 0; i < 10; ++i) {
        const int k = i / 5, v = i % 5;
        const int cancel = cancel_keys[k].code;
        const std::string key_name = cancel_keys[k].name;
        const std::string tag = key_name + " " + names[v];
        const std::string sfx = std::to_string(k) + std::to_string(v);
        Rig r(SupervisorConfig(), "cx" + sfx);
        App frozen = make_app("cf" + sfx);
        App front = make_app("cr" + sfx);
        r.sup->launch(spec_for(frozen, r.fb.path, Background::Freeze));
        r.pump(3000, [&] { return app_logged(frozen, "rec=start"); });
        r.pump_for(100);
        r.sup->go_launcher();
        r.pump(2000, [&] { return r.state(frozen.name) == "hidden-frozen"; });
        r.sup->launch(spec_for(front, r.fb.path, Background::Run));
        r.pump(3000, [&] { return app_logged(front, "rec=start"); });
        r.pump_for(150);
        r.rec.clear();
        key(r.kw, KEY_LEFTALT, 1);
        r.pump_for(40);
        key(r.kw, KEY_TAB, 1);
        r.pump_for(80);
        if (v == 4) { /* firmware typematic: the held Tab comes again as release + press */
            key(r.kw, KEY_TAB, 0);
            key(r.kw, KEY_TAB, 1);
            r.pump_for(40);
        }
        key(r.kw, KEY_TAB, 0);
        if (v != 3) r.pump(2000, [&] { return r.state(front.name) == "hidden-running"; });
        else r.pump_for(2);
        r.pump_for(v == 3 ? 0 : 300);
        const int cancel_fd = v == 2 ? r.kw2 : r.kw;
        key(cancel_fd, cancel, 1);
        if (v == 1)
            for (int i = 0; i < 6; ++i) {
                r.pump_for(35);
                send_event(cancel_fd, EV_KEY, cancel, 2);
                send_event(cancel_fd, EV_SYN, SYN_REPORT, 0);
            }
        r.pump_for(90);
        key(cancel_fd, cancel, 0);
        r.pump_for(120);
        key(r.kw, KEY_LEFTALT, 0);
        check(tag + ": 'session: cancelled'", r.pump(1500, [&] { return r.rec.has("log session: cancelled"); }));
        check(tag + ": back to the app the session started from",
              r.pump(2500, [&] { return r.state(front.name) == "foreground"; }), r.status());
        check(tag + ": the Alt release after the cancel commits nothing", !r.rec.has("log session ends: Home"));
        check(tag + ": the cancel key is traced as the cancel",
              r.rec.has("log session key: " + key_name + " down -> cancel"));
        check(tag + ": every key of the session is in the log", r.rec.has("log session key: " + key_name + " up") &&
                                                                    r.rec.has("log session key: Alt up"));
        r.sup->stop();
    }
}

void scenario_home_message_and_fifo()
{
    std::printf("== the app's \"home\" message; dev FIFO close-all and status\n");
    Rig r(SupervisorConfig(), "home");
    App a = make_app("homer");
    App b = make_app("other");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    r.pump_for(150);
    key(a.in_writer, KEY_H, 1); // fbhapp sends "home" on h
    key(a.in_writer, KEY_H, 0);
    check("home message hides it", r.pump(2000, [&] { return r.state("homer") == "hidden-frozen"; }), r.status());
    check("logged", r.rec.has("asks for home"));
    r.sup->launch(spec_for(b, r.fb.path, Background::Run));
    check("B started", r.pump(3000, [&] { return app_logged(b, "rec=start"); }));
    r.pump_for(100);
    r.sup->command("home");
    check("B hidden", r.pump(2000, [&] { return r.state("other") == "hidden-running"; }));
    r.sup->command("close-all");
    check("close-all ends both", r.pump(5000, [&] { return r.status().find("residents=0") != std::string::npos; }),
          r.status());
    r.sup->command("status");
    r.pump_for(50);
    check("status command", r.rec.has("status:"));
    r.sup->stop();
}

void scenario_cap_and_memory()
{
    std::printf("== cap 2: LRU frozen app closed at the 3rd launch; memory floor with a fake reading\n");
    SupervisorConfig cfg;
    cfg.model.cap = 2;
    Rig r(cfg, "cap");
    App a = make_app("one");
    App b = make_app("two");
    App c = make_app("three");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    r.pump(3000, [&] { return app_logged(a, "rec=start"); });
    r.pump_for(100);
    r.sup->go_launcher();
    r.pump(2000, [&] { return r.state("one") == "hidden-frozen"; });
    r.sup->launch(spec_for(b, r.fb.path, Background::Freeze));
    r.pump(3000, [&] { return app_logged(b, "rec=start"); });
    r.pump_for(100);
    r.sup->go_launcher();
    check("two resident", r.pump(2000, [&] { return r.state("two") == "hidden-frozen"; }));
    r.sup->launch(spec_for(c, r.fb.path, Background::Freeze));
    check("third launched", r.pump(3000, [&] { return app_logged(c, "rec=start"); }));
    check("LRU closed with a notice", r.rec.has("ui notice Closed one (2 apps open)"));
    check("one gone", r.pump(3000, [&] { return r.status().find("\"one\"") == std::string::npos; }), r.status());
    r.pump_for(100);
    r.sup->go_launcher();
    r.pump(2000, [&] { return r.state("three") == "hidden-frozen"; });
    r.sup->fake_memory(50 * 1024);
    check("under 64 MB: the LRU frozen app closes", r.pump(3000, [&] { return r.rec.has("low memory (50 MB free)"); }));
    check("two closed first (LRU)", r.rec.has("Closed two: low memory"));
    r.sup->stop();
}

void scenario_shutdown_no_stopped_left()
{
    std::printf("== shutdown with a frozen and a running app: nothing left stopped\n");
    Rig r(SupervisorConfig(), "stop");
    App a = make_app("ice");
    App b = make_app("fire");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    r.pump(3000, [&] { return app_logged(a, "rec=start"); });
    r.pump_for(100);
    r.sup->go_launcher();
    r.pump(2000, [&] { return r.state("ice") == "hidden-frozen"; });
    const int frozen = r.pgid("ice");
    r.sup->launch(spec_for(b, r.fb.path, Background::Run));
    r.pump(3000, [&] { return app_logged(b, "rec=start"); });
    const int running = r.pgid("fire");
    r.rec.clear();
    const std::uint64_t t0 = Supervisor::now_ms();
    r.sup->stop();
    check("both groups gone", !group_exists(frozen) && !group_exists(running));
    check("quickly (SIGTERM honoured)", Supervisor::now_ms() - t0 < 2500, std::to_string(Supervisor::now_ms() - t0));
    check("SIGCONT before SIGTERM for the frozen app", r.rec.index("ice: SIGCONT") >= 0 &&
                                                          r.rec.index("ice: SIGCONT") < r.rec.index("ice: SIGTERM"));
}

/* Deck finding (2026-10-20): about 5 % launcher CPU with an app in front. The supervisor's own loop, threaded as in
 * the launcher, with a legacy app in front that draws nothing, then with an app hidden and the launcher in front. */
void scenario_loop_cost()
{
    std::printf("== loop cost: wake-ups and CPU of the supervisor thread\n");
    Rig r(SupervisorConfig(), "cost", false);
    App a = make_app("idle");
    App b = make_app("parked");
    r.sup->start();
    const auto wait_for = [](const std::function<bool()> &cond, int ms) {
        for (int i = 0; i < ms / 10 && !cond(); ++i) ::usleep(10000);
        return cond();
    };
    const auto measure = [&r](const char *what, double max_wakeups) {
        ::usleep(300000);
        const Supervisor::Stats s0 = r.sup->stats();
        ::usleep(3000000);
        const Supervisor::Stats s1 = r.sup->stats();
        const double wps = static_cast<double>(s1.wakeups - s0.wakeups) / 3.0;
        const double cpu = static_cast<double>(s1.cpu_us - s0.cpu_us) / 3000.0; /* ms of CPU per second */
        std::printf("     %s: %.1f wake-ups/s, %.3f ms CPU/s (%.3f %%)\n", what, wps, cpu, cpu / 10.0);
        if (std::getenv("MT_COST_DEBUG")) {
            std::printf("%s\n", r.status().c_str());
            for (const std::string &l : r.rec.get()) std::printf("  | %s\n", l.c_str());
        }
        char detail[96];
        std::snprintf(detail, sizeof(detail), "%.1f wake-ups/s", wps);
        check(std::string("at most ") + std::to_string(static_cast<int>(max_wakeups)) + " wake-ups/s, " + what,
              wps <= max_wakeups, detail);
    };
    r.sup->launch(spec_for(a, r.fb.path, Background::None, "--static"));
    wait_for([&] { return app_logged(a, "rec=start"); }, 3000);
    measure("legacy app in front", 5);
    r.sup->command("close idle");
    wait_for([&] { return r.status().find("front=home") != std::string::npos; }, 5000);
    r.sup->launch(spec_for(b, r.fb.path, Background::Freeze, "--static"));
    wait_for([&] { return app_logged(b, "rec=start"); }, 3000);
    ::usleep(150000);
    r.sup->go_launcher();
    check("an app hidden for the second measurement",
          wait_for([&] { return r.state("parked") == "hidden-frozen"; }, 3000), r.status());
    measure("one app hidden, launcher in front", 5);
    r.sup->stop();
}

void scenario_guardian()
{
    /* Two apps that would outlive the supervisor without the guardian: a frozen one that ignores SIGHUP (the kernel
     * sends SIGHUP + SIGCONT to a stopped, newly orphaned process group; --no-hup survives that, so only the guardian
     * can end it) and a hidden running one (no stopped member: the kernel sends it nothing). */
    std::printf("== guardian: the supervisor's process is SIGKILLed with a frozen and a running app hidden\n");
    App a = make_app("orphan");
    App b = make_app("runner");
    const std::string pg_file = g_dir + "/orphan.pgid";
    ::unlink(pg_file.c_str());
    std::fflush(stdout);
    const pid_t child = ::fork();
    if (child == 0) {
        SupervisorConfig cfg;
        Recorder rec;
        FileFb fb(g_dir + "/fb-guard.raw");
        TestUi ui(fb, rec);
        TestInput input(rec, g_dir + "/app-in-");
        FifoKeys keys(make_fifo("keys-guard"), rec);
        LinuxProcessPort proc;
        cfg.guardian = true;
        Supervisor sup(cfg, proc, fb, input, keys, ui);
        sup.set_log_sink([&rec](const std::string &l) { rec.add(l); });
        sup.start();
        const auto wait_state = [&sup](const std::string &app, const std::string &state) {
            for (int i = 0; i < 300; ++i) {
                if (status_field(sup.status_text(), app, "state") == state) return;
                ::usleep(10000);
            }
        };
        sup.launch(spec_for(a, fb.path, Background::Freeze, "--no-hup"));
        for (int i = 0; i < 300 && !app_logged(a, "rec=start"); ++i) ::usleep(10000);
        ::usleep(150000);
        sup.go_launcher();
        wait_state("orphan", "hidden-frozen");
        sup.launch(spec_for(b, fb.path, Background::Run));
        for (int i = 0; i < 300 && !app_logged(b, "rec=start"); ++i) ::usleep(10000);
        ::usleep(150000);
        sup.go_launcher();
        wait_state("runner", "hidden-running");
        std::ofstream(pg_file) << status_field(sup.status_text(), "orphan", "pgid") << " "
                               << status_field(sup.status_text(), "runner", "pgid");
        for (;;) ::pause(); /* killed by the parent */
    }
    int frozen = 0, running = 0;
    for (int i = 0; i < 500 && (frozen <= 0 || running <= 0); ++i) {
        std::istringstream in(read_file(pg_file));
        in >> frozen >> running;
        if (frozen <= 0 || running <= 0) ::usleep(10000);
    }
    check("a frozen and a running app in the child supervisor", frozen > 0 && running > 0 && group_stopped(frozen));
    ::kill(child, SIGKILL);
    ::waitpid(child, nullptr, 0);
    bool gone = false;
    for (int i = 0; i < 450 && !gone; ++i) {
        gone = !group_exists(frozen) && !group_exists(running);
        if (!gone) ::usleep(10000);
    }
    check("the guardian ended both within 4.5 s", gone);
    check("the frozen app ran its SIGTERM handler (it was continued first)", app_logged(a, "rec=end"));
    check("the running app ended", app_logged(b, "rec=end"));
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: test_supervisor_host FBHAPP WORKDIR\n");
        return 2;
    }
    g_fbhapp = argv[1];
    g_dir = argv[2];
    ::signal(SIGPIPE, SIG_IGN);
    if (const char *only = std::getenv("MT_ONLY")) { /* MT_ONLY=cost: the loop cost measurement alone */
        if (std::string(only) == "cost") scenario_loop_cost();
        if (std::string(only) == "cancel") scenario_alt_tab_cancel_deck();
        std::printf("test_supervisor_host: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }
    scenario_freeze_cycle();
    scenario_run_mode();
    scenario_draw_hidden_negative();
    scenario_slow_ack_not_responding();
    scenario_close_frozen();
    scenario_close_ignores_close_message();
    scenario_exit_and_kill_while_hidden();
    scenario_esc_hold();
    scenario_alt_tab_evdev();
    scenario_alt_tab_cancel_deck();
    scenario_home_message_and_fifo();
    scenario_cap_and_memory();
    scenario_shutdown_no_stopped_left();
    scenario_guardian();
    scenario_loop_cost();
    std::printf("test_supervisor_host: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
