// SPDX-License-Identifier: MIT
//
// Multitasking P-b (task 015, study 026 section 7.3 "host process tests"): the real supervisor (multitask::Supervisor
// with the Linux process port) runs the spike's fake app tools/multitasking-spike fbhapp, which speaks the lifecycle
// protocol, on a file-backed framebuffer; FIFOs stand in for the input nodes (one FIFO per reader: the app's own queue
// and the supervisor's, as evdev gives each open file its own queue). Checked: the hide deadline and not responding,
// freeze (state T) and thaw, snapshot and restore, no framebuffer write while hidden (with a misbehaving app as the
// negative control), the drain of the app's queue before the thaw, SIGCONT before close / SIGTERM, exit and kill
// while hidden, the cap, the memory floor, Esc held 3 s, Alt+Tab from evdev, the dev FIFO, shutdown and the guardian.
// P-c: a FIFO stands in for the touch screen (protocol B events, as the deck's Goodix sends them): the three-finger
// hold G1 read passively while an app is in front, the switcher stand-in it runs, the false positives, the dev FIFO's
// g1 / g2 and the launcher's own gestures. P-e: the freeze, run and close scenarios again with fbhapp --helper, whose
// control socket is driven by the toolkit's app-side helper (ext_components/cp0_lvgl cp0_app_lifecycle.c).
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
    /* P-d: the frame dimmed under the legacy sheet (as cp0_display_fb_backdrop, 38 %) */
    std::atomic<int> backdrops{0};
    int backdrop(const std::vector<uint8_t> &frame) override
    {
        if (frame.size() != kLen) return -1;
        for (size_t i = 0; i < kLen; ++i) map[i] = static_cast<uint8_t>(frame[i] * 97 / 256);
        ++backdrops;
        return 0;
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
    /* P-d: "ui switcher open n=2 B:hidden-running* A:hidden-frozen sel=0 session=0 suspended=1",
     * "ui switcher close app_next=1 suspended=1", "ui sheet NAME", "ui sheet hide suspended=0", "ui apps KEY KEY" */
    void switcher(const SwitcherInfo &info) override
    {
        std::string s = info.open ? "ui switcher open n=" + std::to_string(info.cards.size()) : "ui switcher close";
        if (info.open) {
            for (const SwitcherCard &c : info.cards)
                s += " " + c.name + ":" + state_name(c.state) + (c.origin ? "*" : "");
            s += " sel=" + std::to_string(info.selected) + " session=" + (info.session ? "1" : "0");
        } else {
            s += std::string(" app_next=") + (info.app_next ? "1" : "0");
        }
        rec.add(s + " suspended=" + (fb.suspended ? "1" : "0"));
    }
    void sheet(AppId id, const std::string &name) override
    {
        rec.add(id == kNoApp ? std::string("ui sheet hide suspended=") + (fb.suspended ? "1" : "0") : "ui sheet " + name);
    }
    void apps(const std::vector<SwitcherCard> &residents) override
    {
        std::string s = "ui apps";
        for (const SwitcherCard &c : residents) s += " " + c.key;
        rec.add(s);
    }
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
    std::string path3; /* task c-7: a media-key device, the tablet's buttons ("" = none) */
    Recorder &rec;
    cp0_esc_key_watch::Sources sources;
    FifoKeys(std::string p, Recorder &r, std::string p2 = std::string(), std::string p3 = std::string())
        : path(std::move(p)), path2(std::move(p2)), path3(std::move(p3)), rec(r)
    {
    }
    void open() override
    {
        sources.open(path.c_str());
        if (!path2.empty()) sources.open(path2.c_str());
        if (!path3.empty()) sources.open(path3.c_str(), true);
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
    std::unique_ptr<EvdevTouchPort> touch; /* P-c: a FIFO as the touch screen (with_touch) */
    std::unique_ptr<Supervisor> sup;
    int kw = -1;  /* the "keyboard": every reader's FIFO stays open (no EOF) */
    int kw2 = -1; /* a second keyboard */
    int kw3 = -1; /* task c-7: a media-key device (the tablet's volume and Windows buttons) */
    int tw = -1;  /* the "touch screen" (P-c) */

    /* fast: a 5 ms tick for the pumped scenarios; false keeps the launcher's defaults (the cost scenario) */
    explicit Rig(SupervisorConfig cfg = SupervisorConfig(), const std::string &tag = "rig", bool fast = true,
                 bool with_touch = false)
        : fb(g_dir + "/fb-" + tag + ".raw"), ui(fb, rec), input(rec, g_dir + "/app-in-"),
          keys_fifo(make_fifo("keys-" + tag)), keys(keys_fifo, rec, make_fifo("keys2-" + tag), make_fifo("keys3-" + tag))
    {
        cfg.guardian = false;
        (void)fast; /* the pumped scenarios call run_once() themselves; the cost scenario runs the thread */
        if (with_touch) { /* the deck: 640x480 at 11.3 px/mm, G1 with the defaults (hold 1000 ms) */
            cfg.touch.g1 = true;
            cfg.touch.w = 640;
            cfg.touch.h = 480;
            cfg.touch.ppmm_x100 = 1130;
        }
        sup = std::make_unique<Supervisor>(cfg, proc, fb, input, keys, ui);
        sup->set_log_sink([this](const std::string &l) { rec.add("log " + l); });
        kw = writer(keys_fifo);
        kw2 = writer(keys.path2);
        kw3 = writer(keys.path3);
        if (with_touch) {
            const std::string touch_fifo = make_fifo("touch-" + tag);
            tw = writer(touch_fifo);
            touch = std::make_unique<EvdevTouchPort>(touch_fifo, nullptr, true); /* a FIFO: protocol B */
            sup->set_touch_port(touch.get());
        }
    }
    ~Rig()
    {
        sup.reset();
        if (kw >= 0) ::close(kw);
        if (kw2 >= 0) ::close(kw2);
        if (kw3 >= 0) ::close(kw3);
        if (tw >= 0) ::close(tw);
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

/* one contact in one slot: lands or moves (id >= 0) or lifts (id -1), then SYN_REPORT */
void touch_slot(int fd, int slot, int id, int x = 0, int y = 0)
{
    send_event(fd, EV_ABS, ABS_MT_SLOT, slot);
    send_event(fd, EV_ABS, ABS_MT_TRACKING_ID, id);
    if (id >= 0) {
        send_event(fd, EV_ABS, ABS_MT_POSITION_X, x);
        send_event(fd, EV_ABS, ABS_MT_POSITION_Y, y);
    }
    send_event(fd, EV_SYN, SYN_REPORT, 0);
}

/* three fingers put down 25 ms apart, 120 px (10.6 mm) apart, in the middle of the screen */
void three_fingers(Rig &r, int first_id)
{
    touch_slot(r.tw, 0, first_id, 200, 240);
    r.pump_for(25);
    touch_slot(r.tw, 1, first_id + 1, 320, 250);
    r.pump_for(25);
    touch_slot(r.tw, 2, first_id + 2, 440, 245);
}

void lift_all(Rig &r, int slots)
{
    for (int s = 0; s < slots; ++s) touch_slot(r.tw, s, -1);
}

/* "G1 fired (3 contacts, held N ms)": N, or -1 */
int fired_held_ms(Rig &r)
{
    for (const std::string &l : r.rec.get()) {
        const size_t at = l.find("G1 fired (3 contacts, held ");
        if (at != std::string::npos) return std::atoi(l.c_str() + at + 27);
    }
    return -1;
}

/* ---------------------------------------------------------------------------------------------- scenarios */

/* helper: "--helper" = fbhapp drives its control socket with the toolkit's app-side helper (cp0_app_lifecycle.c, P-e)
 * instead of its own code; the same checks */
void scenario_freeze_cycle(const std::string &helper = std::string())
{
    std::printf("== freeze%s: hide, ack, SIGSTOP, snapshot, no write while hidden, stale input drained, restore\n",
                helper.empty() ? "" : " (app-side helper)");
    Rig r(SupervisorConfig(), "freeze");
    App a = make_app("frz");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze, helper));
    check("app started and draws", r.pump(3000, [&] { return app_logged(a, "rec=start") && !r.fb.is_launcher_page() &&
                                                            r.fb.copy() != std::vector<uint8_t>(kLen, 0); }));
    check("control socket given (contract=1)", app_logged(a, "contract=1"));
    if (!helper.empty()) check("the helper drives the socket", app_logged(a, "helper=1"));
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

void scenario_run_mode(const std::string &helper = std::string())
{
    std::printf("== run%s: hidden running, not stopped, oom 700, keeps working, no draw while hidden\n",
                helper.empty() ? "" : " (app-side helper)");
    Rig r(SupervisorConfig(), "run");
    App a = make_app("run");
    r.sup->launch(spec_for(a, r.fb.path, Background::Run, helper));
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

void scenario_close_frozen(const std::string &helper = std::string())
{
    std::printf("== close a frozen app%s: SIGCONT first, close message, graceful exit\n",
                helper.empty() ? "" : " (app-side helper)");
    Rig r(SupervisorConfig(), "close");
    App a = make_app("cls");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze, helper));
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

void scenario_g1_from_app()
{
    std::printf("== P-c G1: three-finger hold in an app, read passively; the stand-in hides it; swallowed after\n");
    Rig r(SupervisorConfig(), "g1", true, true);
    App a = make_app("touchy");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    check("the touch screen read while the app is in front",
          r.pump(1000, [&] { return r.rec.has("touch: reading the touch screen passively (no grab) for G1 while touchy"); }));
    r.pump_for(50);
    check("status evdev_touch=1", r.status().find("evdev_touch=1") != std::string::npos, r.status());
    check("no touch grab while the app is in front", !r.rec.has("input grab_touch 1"));
    r.rec.clear();
    three_fingers(r, 10);
    const std::uint64_t t3 = Supervisor::now_ms();
    check("not before the hold", !r.pump(900, [&] { return r.rec.has("G1 fired"); }));
    check("G1 fires after 1 s", r.pump(1000, [&] { return r.rec.has("G1 fired (3 contacts, held "); }));
    const int held = fired_held_ms(r);
    check("held about 1000 ms", held >= 1000 && held < 1150, std::to_string(held) + " ms, " +
                                                                std::to_string(Supervisor::now_ms() - t3) + " ms seen");
    check("the stand-in: Home for the app", r.rec.has("switcher (G1): P-c stand-in = Home (as Super) for touchy"));
    check("the app is hidden (frozen)", r.pump(2000, [&] { return r.state("touchy") == "hidden-frozen"; }), r.status());
    check("the reader closes when the launcher is in front", r.pump(500, [&] { return r.rec.has("touch: reader closed"); }));
    check("touch grabbed only once the launcher is in front (P-b), not before",
          r.rec.index("input grab_touch 1") > r.rec.index("ui launcher_front"));
    check("status evdev_touch=0", r.pump(500, [&] { return r.status().find("evdev_touch=0") != std::string::npos; }));
    lift_all(r, 3);
    r.pump_for(100);
    int fires = 0;
    for (const std::string &l : r.rec.get())
        if (l.find("G1 fired") != std::string::npos) ++fires;
    check("exactly one fire (none while the hand stays down)", fires == 1, std::to_string(fires));
    // back in the app (the reader opens again, blocked until the screen is free): a fresh hold fires again
    r.rec.clear();
    r.sup->command("switch touchy");
    check("resumed", r.pump(2000, [&] { return r.state("touchy") == "foreground"; }), r.status());
    check("reader open again", r.pump(500, [&] { return r.rec.has("touch: reading"); }));
    three_fingers(r, 20);
    check("a second G1 fires", r.pump(2500, [&] { return r.rec.has("G1 fired"); }));
    lift_all(r, 3);
    r.pump(2000, [&] { return r.state("touchy") == "hidden-frozen"; });
    r.sup->stop();
}

void scenario_g1_false_positives()
{
    std::printf("== P-c G1: a 2-finger pinch, a 4th contact, an early lift, sloppy fingers do not fire\n");
    Rig r(SupervisorConfig(), "g1neg", true, true);
    App a = make_app("pinchy");
    r.sup->launch(spec_for(a, r.fb.path, Background::Run));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    check("reader open", r.pump(1000, [&] { return r.rec.has("touch: reading"); }));
    r.pump_for(50);
    r.rec.clear();
    // a 2-finger pinch over 1.5 s (viz1090's zoom)
    touch_slot(r.tw, 0, 30, 220, 240);
    touch_slot(r.tw, 1, 31, 420, 240);
    for (int i = 1; i <= 15; ++i) {
        r.pump_for(100);
        touch_slot(r.tw, 0, 30, 220 - 5 * i, 240);
        touch_slot(r.tw, 1, 31, 420 + 5 * i, 240);
    }
    lift_all(r, 2);
    r.pump_for(300);
    check("pinch: no fire, nothing logged", !r.rec.has("G1 fired") && !r.rec.has("G1 cancelled"));
    // a 4th contact during the hold (a palm, a grip)
    three_fingers(r, 40);
    r.pump_for(300);
    touch_slot(r.tw, 3, 43, 560, 300);
    r.pump_for(1200);
    lift_all(r, 4);
    check("4th contact: cancelled, reported when the hand lifts",
          r.pump(500, [&] { return r.rec.has("G1 cancelled: 4 contacts in one touch"); }));
    // one finger lifts before the second
    three_fingers(r, 50);
    r.pump_for(400);
    touch_slot(r.tw, 2, -1);
    r.pump_for(900);
    lift_all(r, 3);
    check("early lift: cancelled", r.pump(500, [&] { return r.rec.has("G1 cancelled: a finger lifted after"); }));
    // three fingers too close together (two 50 px = 4.4 mm apart)
    touch_slot(r.tw, 0, 60, 200, 240);
    touch_slot(r.tw, 1, 61, 250, 240);
    touch_slot(r.tw, 2, 62, 440, 240);
    r.pump_for(1300);
    lift_all(r, 3);
    check("too close: cancelled", r.pump(500, [&] { return r.rec.has("G1 cancelled: two fingers 4.4 mm apart"); }));
    check("none of them fired", !r.rec.has("G1 fired"));
    check("the app stayed in front", r.state("pinchy") == "foreground", r.status());
    r.sup->stop();
}

void scenario_g1_legacy_and_fifo()
{
    std::printf("== P-c G1 on a legacy app (stays); dev FIFO g1 / g2; the launcher's own gestures\n");
    Rig r(SupervisorConfig(), "g1leg", true, true);
    App a = make_app("oldie");
    r.sup->launch(spec_for(a, r.fb.path, Background::None));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    check("reader open for a legacy app too", r.pump(1000, [&] { return r.rec.has("touch: reading"); }));
    r.pump_for(50);
    three_fingers(r, 70);
    check("G1 fires", r.pump(2000, [&] { return r.rec.has("G1 fired"); }));
    check("a legacy app cannot go to the background: notice",
          r.pump(500, [&] { return r.rec.has("ui notice oldie cannot stay in the background"); }));
    lift_all(r, 3);
    r.pump_for(200);
    check("it stays in front", r.state("oldie") == "foreground", r.status());
    r.sup->command("close oldie");
    r.pump(4000, [&] { return r.status().find("front=home") != std::string::npos; });
    App b = make_app("fifo");
    r.sup->launch(spec_for(b, r.fb.path, Background::Freeze));
    check("B started", r.pump(3000, [&] { return app_logged(b, "rec=start"); }));
    r.pump_for(100);
    r.rec.clear();
    r.sup->command("g1");
    check("FIFO g1: logged", r.pump(500, [&] { return r.rec.has("G1 fired (dev FIFO)"); }));
    check("FIFO g1: the app is hidden", r.pump(2000, [&] { return r.state("fifo") == "hidden-frozen"; }), r.status());
    r.rec.clear();
    r.sup->command("g2");
    check("FIFO g2 with the launcher in front: nothing to do",
          r.pump(500, [&] { return r.rec.has("switcher (G2): the launcher is in front, nothing to do"); }));
    r.sup->launcher_gesture("G1", "G1 fired (3 contacts, held 1004 ms)", true);
    check("the launcher's own G1: logged, stand-in nothing to do",
          r.pump(500, [&] { return r.rec.has("log G1 fired (3 contacts, held 1004 ms)") &&
                                   r.rec.has("switcher (G1): the launcher is in front"); }));
    r.sup->launcher_gesture("G1", "G1 cancelled: a finger moved 5.3 mm (limit 4.0 mm), 3 contacts", false);
    check("the launcher's cancelled G1: logged only",
          r.pump(500, [&] { return r.rec.has("log G1 cancelled: a finger moved 5.3 mm"); }));
    check("still nothing in front", r.status().find("front=home") != std::string::npos, r.status());
    r.sup->stop();
}

/* ------------------------------------------------------------------------------------- P-d: the switcher screen */

SupervisorConfig pd_config()
{
    SupervisorConfig c;
    c.switcher_ui = true;
    return c;
}

AppId app_id(Rig &r, const std::string &name)
{
    return std::atoi(status_field(r.status(), name, "id").c_str());
}

/* a three-finger hold in the app in front until G1 fires, then the hand lifts */
bool g1_now(Rig &r, int first_id)
{
    three_fingers(r, first_id);
    const bool fired = r.pump(2500, [&] { return r.rec.has("G1 fired"); });
    lift_all(r, 3);
    r.pump_for(50);
    return fired;
}

/* the last recorded line that starts with prefix, "" when none */
std::string last_with(Rig &r, const std::string &prefix)
{
    std::string found;
    for (const std::string &l : r.rec.get())
        if (l.rfind(prefix, 0) == 0) found = l;
    return found;
}

bool before(Rig &r, const std::string &first, const std::string &second)
{
    const int a = r.rec.index(first), b = r.rec.index(second);
    return a >= 0 && b >= 0 && a < b;
}

void scenario_pd_switcher_from_app()
{
    std::printf("== P-d: G1 in an app opens the switcher with that app hidden; a card, the X, Home, G2 on the launcher, "
                "Close all\n");
    Rig r(pd_config(), "pdsw", true, true);
    App a = make_app("pda");
    App b = make_app("pdb");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    check("A started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    r.pump_for(100);
    r.sup->go_launcher();
    check("A frozen", r.pump(2000, [&] { return r.state("pda") == "hidden-frozen"; }), r.status());
    r.sup->launch(spec_for(b, r.fb.path, Background::Run));
    check("B started", r.pump(3000, [&] { return app_logged(b, "rec=start"); }));
    check("B: touch read passively", r.pump(1000, [&] { return r.rec.has("touch: reading the touch screen passively"); }));
    r.pump_for(100);
    r.rec.clear();
    check("G1 fires in B", g1_now(r, 10));
    check("the switcher opens", r.pump(2000, [&] { return r.rec.has("ui switcher open"); }), r.status());
    check("B is hidden first (the P-b path)", r.rec.has("log switcher (G1): hiding pdb first") &&
                                                 r.state("pdb") == "hidden-running");
    check("log: opened by G1 with 2 apps (from pdb)", r.rec.has("log switcher: opened by G1 with 2 apps (from pdb)"));
    check("two cards: B (In front, selected) first, A paused",
          r.rec.has("ui switcher open n=2 pdb:hidden-running* pda:hidden-frozen sel=0 session=0"),
          last_with(r, "ui switcher"));
    check("the switcher reaches the launcher before it takes the panel back",
          before(r, "ui switcher open", "ui launcher_front"));
    check("status switcher=open", status_field(r.status(), "", "switcher") == "open", r.status());
    check("the grid's dots: both apps resident", r.rec.has("ui apps fbhapp-pdb fbhapp-pda"));
    // stale input for A typed while the switcher is up: drained when A is resumed (P-b)
    key(a.in_writer, KEY_ENTER, 1);
    key(a.in_writer, KEY_ENTER, 0);
    r.rec.clear();
    r.sup->switcher_pick(app_id(r, "pda"));
    check("a card tapped: A is resumed", r.pump(2000, [&] { return r.state("pda") == "foreground"; }), r.status());
    check("log: card tapped pda", r.rec.has("log switcher: card tapped pda"));
    check("the switcher goes with the display already suspended, before the app is shown",
          r.rec.has("ui switcher close app_next=1 suspended=1") && before(r, "ui switcher close", "ui app_front pda"));
    check("restore and drain before the thaw", before(r, "pda: restore", "pda: SIGCONT") &&
                                                   before(r, "pda: drain", "pda: SIGCONT"));
    check("the stale Enter was drained", r.rec.has("keys=1"));
    r.pump_for(300);
    check("A acted on no stale input", count(read_file(a.log), "acted=1") == 0);
    // the X: G1 in A, then B's card is closed
    r.rec.clear();
    check("G1 fires in A", g1_now(r, 20));
    check("the switcher: A (In front) first now",
          r.pump(2000, [&] { return r.rec.has("ui switcher open n=2 pda:hidden-frozen* pdb:hidden-running"); }),
          last_with(r, "ui switcher"));
    r.sup->switcher_close(app_id(r, "pdb"));
    check("X: B closes", r.pump(4000, [&] { return r.status().find("\"pdb\"") == std::string::npos; }), r.status());
    check("log: closed pdb", r.rec.has("log switcher: closed pdb"));
    check("its card goes (one card left)", r.pump(500, [&] { return r.rec.has("ui switcher open n=1 pda:hidden-frozen*"); }),
          last_with(r, "ui switcher"));
    check("the switcher stays open", status_field(r.status(), "", "switcher") == "open");
    r.sup->switcher_home();
    check("Home: the switcher closes, A stays hidden",
          r.pump(1000, [&] { return r.rec.has("ui switcher close app_next=0"); }) && r.state("pda") == "hidden-frozen");
    check("log: home", r.rec.has("log switcher: home"));
    // G2 on the launcher with an app resident: the switcher opens; Close all
    r.rec.clear();
    r.sup->launcher_gesture("G2", "G2 fired (status strip held 503 ms)", true);
    check("G2 on the launcher opens it",
          r.pump(1000, [&] { return r.rec.has("log switcher: opened by G2 with 1 app (on the launcher)"); }));
    check("opened on the launcher: no In front card", r.rec.has("ui switcher open n=1 pda:hidden-frozen sel=0"),
          last_with(r, "ui switcher"));
    r.sup->switcher_close_all();
    check("Close all: every app ends", r.pump(4000, [&] { return r.status().find("residents=0") != std::string::npos; }),
          r.status());
    check("log: close-all (1 app)", r.rec.has("log switcher: close-all (1 app)"));
    check("the switcher closed, the launcher stays", r.rec.has("ui switcher close app_next=0"));
    check("the grid's dots: none left", r.pump(500, [&] { return last_with(r, "ui apps") == "ui apps"; }),
          last_with(r, "ui apps"));
    r.rec.clear();
    r.sup->launcher_gesture("G1", "G1 fired (3 contacts, held 1003 ms)", true);
    check("nothing open: G1 on the launcher does nothing",
          r.pump(500, [&] { return r.rec.has("log switcher (G1): nothing open, nothing to do"); }) &&
              !r.rec.has("ui switcher open"));
    r.sup->stop();
}

void scenario_pd_cancel()
{
    std::printf("== P-d: leaving the switcher without a change (Esc / the header's Esc, G1 again) resumes the app\n");
    Rig r(pd_config(), "pdcx", true, true);
    App a = make_app("pcx");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    check("touch read passively", r.pump(1000, [&] { return r.rec.has("touch: reading"); }));
    r.pump_for(100);
    r.rec.clear();
    check("G1 fires", g1_now(r, 30));
    check("the switcher opens", r.pump(2000, [&] { return r.rec.has("ui switcher open n=1 pcx:hidden-frozen*"); }));
    r.sup->switcher_cancel();
    check("Esc: back to the app", r.pump(2000, [&] { return r.state("pcx") == "foreground"; }), r.status());
    check("log: cancelled (back to pcx)", r.rec.has("log switcher: cancelled (back to pcx)"));
    check("the switcher goes as the app takes the panel", r.rec.has("ui switcher close app_next=1 suspended=1"));
    r.pump_for(300);
    r.rec.clear();
    check("G1 fires again", g1_now(r, 40));
    check("the switcher opens again", r.pump(2000, [&] { return r.rec.has("ui switcher open"); }));
    r.sup->launcher_gesture("G1", "G1 fired (3 contacts, held 1001 ms)", true); /* the launcher's own reader */
    check("G1 while it is up: back to the app", r.pump(2000, [&] { return r.state("pcx") == "foreground"; }), r.status());
    check("log: cancelled by G1 (back to pcx)", r.rec.has("log switcher: cancelled by G1 (back to pcx)"));
    r.sup->stop();
}

void scenario_pd_legacy_sheet()
{
    std::printf("== P-d: G1 in a legacy app: the sheet (frozen, its frame dimmed), Keep (no stale input), G1 again = "
                "Keep, Close\n");
    Rig r(pd_config(), "pdsh", true, true);
    App l = make_app("leg");
    r.sup->launch(spec_for(l, r.fb.path, Background::None));
    check("started", r.pump(3000, [&] { return app_logged(l, "rec=start"); }));
    check("touch read passively", r.pump(1000, [&] { return r.rec.has("touch: reading"); }));
    r.pump_for(100);
    const int pgid = r.pgid("leg");
    r.rec.clear();
    check("G1 fires", g1_now(r, 50));
    check("the sheet: the app is held", r.pump(2000, [&] { return r.state("leg") == "held"; }), r.status());
    check("log: sheet shown for leg (G1)",
          r.rec.has("log switcher: sheet shown for leg (G1): it cannot stay in the background"));
    check("frozen (SIGSTOP on its group, state T)", group_stopped(pgid));
    check("its frame kept and written back dimmed", r.rec.has("leg: snapshot") && r.fb.backdrops.load() == 1 &&
                                                        r.rec.has("leg: backdrop"));
    check("the sheet reaches the launcher before it takes the panel", before(r, "ui sheet leg", "ui launcher_front"));
    check("touch and keyboards grabbed while it is up", r.rec.has("input grab_touch 1") && r.rec.has("input grab_keys 1"));
    check("status sheet=leg", status_field(r.status(), "", "sheet") == "leg", r.status());
    // typed while the sheet is up (the FIFOs here are not grabbed): drained before the app is continued
    key(l.in_writer, KEY_ENTER, 1);
    key(l.in_writer, KEY_ENTER, 0);
    r.rec.clear();
    r.sup->sheet_answer(true);
    check("Keep: the app is back in front", r.pump(2000, [&] { return r.state("leg") == "foreground"; }), r.status());
    check("log: sheet keep leg", r.rec.has("log switcher: sheet keep leg"));
    check("the sheet goes with the display suspended", r.rec.has("ui sheet hide suspended=1"));
    check("restore and drain before the thaw", before(r, "leg: restore", "leg: SIGCONT") &&
                                                   before(r, "leg: drain", "leg: SIGCONT"));
    check("the stale Enter was drained", r.rec.has("keys=1"));
    check("running again", !group_stopped(pgid));
    r.pump_for(300);
    check("the app acted on no stale input", count(read_file(l.log), "acted=1") == 0);
    r.rec.clear();
    check("G1 fires again", g1_now(r, 60));
    check("held again", r.pump(2000, [&] { return r.state("leg") == "held"; }));
    r.sup->launcher_gesture("G1", "G1 fired (3 contacts, held 1002 ms)", true);
    check("G1 while the sheet is up = Keep", r.pump(2000, [&] { return r.state("leg") == "foreground"; }), r.status());
    check("log: the sheet is up: Keep", r.rec.has("log switcher (G1): the sheet is up: Keep"));
    r.pump_for(300);
    r.rec.clear();
    check("G1 fires a third time", g1_now(r, 70));
    check("held a third time", r.pump(2000, [&] { return r.state("leg") == "held"; }));
    r.sup->sheet_answer(false);
    check("Close: the app ends", r.pump(5000, [&] { return r.status().find("\"leg\"") == std::string::npos; }),
          r.status());
    check("log: sheet close leg", r.rec.has("log switcher: sheet close leg"));
    check("SIGCONT before SIGTERM (as Esc held 3 s, after the freeze)", before(r, "leg: SIGCONT", "leg: SIGTERM"));
    check("the sheet goes, the launcher stays in front", r.rec.has("ui sheet hide suspended=0") &&
                                                             r.status().find("front=home") != std::string::npos);
    check("the launcher repaints after the exit (it may have drawn once continued)", r.rec.has("ui repaint"));
    check("grabs released", r.rec.has("input grab_touch 0"));
    r.sup->stop();
}

void scenario_pd_alt_tab()
{
    std::printf("== P-d: Alt+Tab shows the switcher screen: the selection follows Tab / Shift+Tab, Alt release commits, "
                "Backspace cancels\n");
    Rig r(pd_config(), "pdat");
    App a = make_app("ata");
    App b = make_app("atb");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    check("A started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    r.pump_for(100);
    r.sup->go_launcher();
    check("A frozen", r.pump(2000, [&] { return r.state("ata") == "hidden-frozen"; }));
    r.sup->launch(spec_for(b, r.fb.path, Background::Run));
    check("B started", r.pump(3000, [&] { return app_logged(b, "rec=start"); }));
    r.pump_for(100);
    r.rec.clear();
    key(r.kw, KEY_LEFTALT, 1);
    key(r.kw, KEY_TAB, 1);
    key(r.kw, KEY_TAB, 0);
    check("the switcher shows the session on the previous app",
          r.pump(2000, [&] { return r.rec.has("ui switcher open n=2 atb:hidden-running* ata:hidden-frozen sel=1 session=1"); }),
          last_with(r, "ui switcher"));
    check("log: opened by Alt+Tab", r.rec.has("log switcher: opened by Alt+Tab with 2 apps (from atb)"));
    check("no P-b text overlay", !r.rec.has("ui overlay"));
    key(r.kw, KEY_TAB, 1);
    key(r.kw, KEY_TAB, 0);
    check("Tab: the selection on Home", r.pump(1000, [&] { return last_with(r, "ui switcher").find(" sel=4 session=1") != std::string::npos; }),
          last_with(r, "ui switcher"));
    key(r.kw, KEY_LEFTSHIFT, 1);
    key(r.kw, KEY_TAB, 1);
    key(r.kw, KEY_TAB, 0);
    key(r.kw, KEY_LEFTSHIFT, 0);
    check("Shift+Tab: back on the previous app", r.pump(1000, [&] { return last_with(r, "ui switcher").find(" sel=1 session=1") != std::string::npos; }),
          last_with(r, "ui switcher"));
    key(r.kw, KEY_LEFTALT, 0);
    check("Alt released: A in front", r.pump(2000, [&] { return r.state("ata") == "foreground"; }), r.status());
    check("the switcher goes as A takes the panel", r.rec.has("ui switcher close app_next=1 suspended=1"));
    check("the P-b session lines stay", r.rec.has("log session ends: ata") && r.rec.has("log session: select Home"));
    r.pump_for(300);
    r.rec.clear();
    key(r.kw, KEY_LEFTALT, 1);
    key(r.kw, KEY_TAB, 1);
    key(r.kw, KEY_TAB, 0);
    check("second session: the switcher", r.pump(2000, [&] { return r.rec.has("ui switcher open n=2 ata:hidden-frozen*"); }),
          last_with(r, "ui switcher"));
    key(r.kw, KEY_BACKSPACE, 1);
    key(r.kw, KEY_BACKSPACE, 0);
    key(r.kw, KEY_LEFTALT, 0);
    check("Backspace with Alt held: cancelled", r.pump(1000, [&] { return r.rec.has("log session: cancelled"); }));
    check("back to A", r.pump(2000, [&] { return r.state("ata") == "foreground"; }), r.status());
    check("the switcher goes", r.rec.has("ui switcher close app_next=1"));
    r.pump_for(300);
    r.rec.clear();
    key(r.kw, KEY_LEFTALT, 1);
    key(r.kw, KEY_TAB, 1);
    key(r.kw, KEY_TAB, 0);
    key(r.kw, KEY_TAB, 1);
    key(r.kw, KEY_TAB, 0);
    r.pump(2000, [&] { return last_with(r, "ui switcher").find(" sel=4") != std::string::npos; });
    key(r.kw, KEY_LEFTALT, 0);
    check("released on Home: the switcher closes to the launcher",
          r.pump(2000, [&] { return r.rec.has("ui switcher close app_next=0"); }) &&
              r.status().find("front=home") != std::string::npos,
          r.status());
    r.sup->stop();
}

void scenario_pd_hint()
{
    std::printf("== P-d: the first-run hint shows once, the first time an app is left open, and is remembered\n");
    SupervisorConfig c = pd_config();
    c.hint_path = g_dir + "/hint-home/.config/cardputerzero/multitask-hint-shown";
    c.hint_text = "Hold three fingers to switch apps";
    ::unlink(c.hint_path.c_str());
    const std::string notice = "ui notice Hold three fingers to switch apps";
    {
        Rig r(c, "pdh1");
        App a = make_app("hinta");
        r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
        check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
        r.pump_for(100);
        check("no hint while the app is in front", !r.rec.has(notice));
        r.sup->go_launcher();
        check("left open: the hint", r.pump(2000, [&] { return r.rec.has(notice); }), r.status());
        check("log: hint", r.rec.has("log hint: \"Hold three fingers to switch apps\" (the first app left open"));
        struct stat st;
        check("remembered in its file", ::stat(c.hint_path.c_str(), &st) == 0);
        r.sup->command("switch hinta");
        r.pump(2000, [&] { return r.state("hinta") == "foreground"; });
        r.pump_for(100);
        r.sup->go_launcher();
        r.pump(2000, [&] { return r.state("hinta") == "hidden-frozen"; });
        r.pump_for(200);
        int n = 0;
        for (const std::string &l : r.rec.get())
            if (l == notice) ++n;
        check("shown once", n == 1, std::to_string(n));
        r.sup->stop();
    }
    {
        Rig r(c, "pdh2"); /* the next run of the launcher */
        App a = make_app("hintb");
        r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
        r.pump(3000, [&] { return app_logged(a, "rec=start"); });
        r.pump_for(100);
        r.sup->go_launcher();
        r.pump(2000, [&] { return r.state("hintb") == "hidden-frozen"; });
        r.pump_for(200);
        check("the next run: no hint (its file exists)", !r.rec.has(notice));
        r.sup->stop();
    }
}

/* Task c-7 (owner decision 2026-10-24): the Windows key held 0.6 s opens the switcher, read from evdev while an app is in
 * front; a tap stays Home; Win+F14 (the FZ-M1's rotation-lock button) and Super+key do nothing; a media-key device (the
 * tablet's buttons) gives its volume keys only: its Alt, Tab, Esc and (c7.4) Super are no keys. The i8042 keyboard's
 * Super as the tablet sends it (auto-repeat, a copy on Intel Virtual Buttons) and the journal lines of the hold. */
void scenario_super_hold()
{
    std::printf("== c-7: Super held 0.6 s in an app opens the switcher; a tap is Home; Win+F14, Super+A nothing; a "
                "media-key device's keys\n");
    Rig r(pd_config(), "sup");
    App a = make_app("supa");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    r.pump_for(150);
    r.rec.clear();
    key(r.kw, KEY_LEFTMETA, 1); /* the rotation-lock button: Super, F14 down / up, then Super held */
    key(r.kw, KEY_F14, 1);
    key(r.kw, KEY_F14, 0);
    r.pump_for(900);
    key(r.kw, KEY_LEFTMETA, 0);
    r.pump_for(100);
    check("Win+F14 held 0.9 s: no switcher", !r.rec.has("log Super held") && !r.rec.has("ui switcher open"));
    check("... not Home either, the app stays", !r.rec.has("log Super / Home key") && r.state("supa") == "foreground",
          r.status());
    key(r.kw, KEY_LEFTMETA, 1);
    key(r.kw, KEY_A, 1);
    key(r.kw, KEY_A, 0);
    r.pump_for(900);
    key(r.kw, KEY_LEFTMETA, 0);
    r.pump_for(100);
    check("Super+A held: nothing", !r.rec.has("log Super held") && r.state("supa") == "foreground", r.status());
    /* the media-key device: Alt+Tab and Esc held from it are not keys (no session, no Esc hold) */
    key(r.kw3, KEY_LEFTALT, 1);
    key(r.kw3, KEY_TAB, 1);
    key(r.kw3, KEY_TAB, 0);
    key(r.kw3, KEY_LEFTALT, 0);
    key(r.kw3, KEY_ESC, 1);
    r.pump_for(700);
    key(r.kw3, KEY_ESC, 0);
    r.pump_for(50);
    check("a media-key device: no Alt+Tab session, no Esc held",
          !r.rec.has("log session (evdev)") && !r.rec.has("keys esc 1") && !r.rec.has("ui esc_hint 1") &&
              r.state("supa") == "foreground",
          r.status());
    /* Super held alone */
    const std::uint64_t t0 = Supervisor::now_ms();
    key(r.kw, KEY_LEFTMETA, 1);
    check("Super held: the switcher opens", r.pump(2500, [&] { return r.rec.has("ui switcher open"); }), r.status());
    const std::uint64_t dt = Supervisor::now_ms() - t0;
    check("at 0.6 s, not before", dt >= kSuperHoldMs - 10 && dt < 1500, std::to_string(dt) + " ms");
    check("log: Super held 600 ms", r.rec.has("log Super held 600 ms"));
    check("the app is hidden first (as G1)",
          r.rec.has("log switcher (Super): hiding supa first") && r.state("supa") == "hidden-frozen", r.status());
    check("the app is listed (in front, selected)", r.rec.has("ui switcher open n=1 supa:hidden-frozen* sel=0 session=0"),
          last_with(r, "ui switcher"));
    key(r.kw, KEY_LEFTMETA, 0);
    r.pump_for(200);
    check("its release is no Home: the switcher stays",
          !r.rec.has("log Super / Home key") && status_field(r.status(), "", "switcher") == "open", r.status());
    /* not an Alt+Tab session: nothing commits or closes it when Super goes up; it stays until the user acts */
    r.pump_for(1200);
    check("still open 1.4 s after the release, no session, nothing closed it",
          status_field(r.status(), "", "switcher") == "open" && !r.rec.has("ui switcher close") &&
              !r.rec.has("session=1") && r.state("supa") == "hidden-frozen",
          r.status());
    /* a second hold (the launcher's key filter now: the launcher is in front) leaves it as Esc does */
    r.sup->launcher_switch_event(SwitchEvent::Switcher);
    check("a second Super hold closes it: back to the app",
          r.pump(2000, [&] { return r.rec.has("log switcher: cancelled by Super (back to supa)") &&
                                    r.state("supa") == "foreground"; }),
          r.status());
    r.pump_for(300);
    /* hold again; Esc on the switcher closes it */
    r.rec.clear();
    key(r.kw, KEY_LEFTMETA, 1);
    check("held again: open", r.pump(2500, [&] { return r.rec.has("ui switcher open"); }), r.status());
    key(r.kw, KEY_LEFTMETA, 0);
    r.pump_for(100);
    r.sup->switcher_cancel();
    check("Esc on the switcher: back to the app", r.pump(2000, [&] { return r.state("supa") == "foreground"; }),
          r.status());
    r.pump_for(300);
    /* hold again; a card picked (Enter on the selected card, or a tap) resumes that app */
    r.rec.clear();
    key(r.kw, KEY_LEFTMETA, 1);
    check("held a third time: open", r.pump(2500, [&] { return r.rec.has("ui switcher open"); }), r.status());
    key(r.kw, KEY_LEFTMETA, 0);
    r.pump_for(100);
    r.sup->switcher_pick(app_id(r, "supa"));
    check("a card picked: that app is resumed, the switcher goes",
          r.pump(2000, [&] { return r.state("supa") == "foreground" && r.rec.has("ui switcher close app_next=1"); }),
          r.status());
    r.pump_for(300);
    /* a tap: Home, as before */
    r.rec.clear();
    key(r.kw, KEY_LEFTMETA, 1);
    r.pump_for(120);
    key(r.kw, KEY_LEFTMETA, 0);
    check("Super tapped: Home", r.pump(2000, [&] { return r.state("supa") == "hidden-frozen"; }) &&
                                    r.rec.has("log Super / Home key"),
          r.status());
    check("... and no switcher", !r.rec.has("ui switcher open") && !r.rec.has("log Super held"));
    /* c7.4: Super from a media-key device (Intel Virtual Buttons declares it) is not taken: held there, nothing */
    r.sup->command("switch supa");
    check("resumed", r.pump(2000, [&] { return r.state("supa") == "foreground"; }), r.status());
    r.pump_for(300);
    r.rec.clear();
    key(r.kw3, KEY_LEFTMETA, 1);
    r.pump_for(900);
    key(r.kw3, KEY_LEFTMETA, 0);
    r.pump_for(150);
    check("a media-key device's Super: neither hold nor Home",
          !r.rec.has("ui switcher open") && !r.rec.has("log Super") && r.state("supa") == "foreground", r.status());
    /* the tablet as it is (journal of 0.8.7-c7.3, 2026-10-24): the i8042 keyboard sends Super down, the kernel's
     * auto-repeat (value 2) from 250 ms every 30 ms, Super up 3 s later; Intel Virtual Buttons may send its own copy, held
     * or as a quick tap. The hold opens the switcher at 0.6 s, whatever the repeats and the copies. */
    const auto hold_like_the_tablet = [&](int ms, int copy) { /* copy: 0 none, 1 held on kw3, 2 a tap on kw3 */
        const std::uint64_t t0 = Supervisor::now_ms();
        key(r.kw, KEY_LEFTMETA, 1);
        if (copy == 1) key(r.kw3, KEY_LEFTMETA, 1);
        if (copy == 2) {
            key(r.kw3, KEY_LEFTMETA, 1);
            key(r.kw3, KEY_LEFTMETA, 0);
        }
        std::uint64_t next_repeat = t0 + 250;
        while (Supervisor::now_ms() < t0 + static_cast<std::uint64_t>(ms)) {
            if (Supervisor::now_ms() >= next_repeat) {
                send_event(r.kw, EV_KEY, KEY_LEFTMETA, 2);
                send_event(r.kw, EV_SYN, SYN_REPORT, 0);
                next_repeat += 30;
            }
            r.sup->run_once();
            ::usleep(5000);
        }
        key(r.kw, KEY_LEFTMETA, 0);
        if (copy == 1) key(r.kw3, KEY_LEFTMETA, 0);
        r.pump_for(150);
    };
    static const char *copies[] = {"alone", "with a held copy on Intel Virtual Buttons", "with a tap on Intel Virtual Buttons"};
    for (int copy = 0; copy < 3; ++copy) {
        const std::string tag = std::string("i8042 Super held 3 s with auto-repeat, ") + copies[copy];
        r.rec.clear();
        hold_like_the_tablet(3000, copy);
        check(tag + ": the switcher opened", r.rec.has("ui switcher open n=1 supa:hidden-frozen*"), last_with(r, "ui switcher"));
        check(tag + ": the journal tells the story",
              r.rec.has("log Super down: the switcher in 600 ms if held alone") && r.rec.has("log Super hold fired"),
              last_with(r, "log Super"));
        int opened = 0;
        for (const std::string &l : r.rec.get())
            if (l.find("log switcher: opened by Super") == 0) ++opened;
        check(tag + ": opened once, no Home, still open after the release",
              opened == 1 && !r.rec.has("Super / Home key") && status_field(r.status(), "", "switcher") == "open",
              std::to_string(opened) + " " + r.status());
        r.sup->switcher_cancel();
        r.pump(2000, [&] { return r.state("supa") == "foreground"; });
        r.pump_for(300);
    }
    /* a tap with auto-repeat never reached (120 ms): Home, as before, and the journal says so */
    r.rec.clear();
    hold_like_the_tablet(120, 0);
    check("a 120 ms tap: Home, logged as a tap", r.pump(2000, [&] { return r.state("supa") == "hidden-frozen"; }) &&
                                                     r.rec.has("log Super up after") && r.rec.has("ms: Home (a tap)"),
          last_with(r, "log Super"));
    r.sup->command("switch supa");
    r.pump(2000, [&] { return r.state("supa") == "foreground"; });
    r.pump_for(300);
    /* Super with another key while held: cancelled, and the journal names the key */
    r.rec.clear();
    key(r.kw, KEY_LEFTMETA, 1);
    r.pump_for(100);
    key(r.kw, KEY_F14, 1);
    key(r.kw, KEY_F14, 0);
    r.pump_for(800);
    key(r.kw, KEY_LEFTMETA, 0);
    r.pump_for(100);
    check("Win+F14: the hold is cancelled, named in the journal",
          r.rec.has("log Super hold cancelled: code 184 pressed while Super was down") &&
              r.rec.has("nothing (Super with another key)") && r.state("supa") == "foreground",
          last_with(r, "log Super"));
    /* back to a switcher to go on: the hold, as above */
    hold_like_the_tablet(800, 0);
    r.pump(1000, [&] { return r.rec.has("ui switcher open"); });
    /* the launcher in front: the key filter's Super hold reaches the supervisor as a Switch request */
    r.sup->switcher_home();
    r.pump(1000, [&] { return r.rec.has("ui switcher close"); });
    r.rec.clear();
    r.sup->launcher_switch_event(SwitchEvent::Switcher);
    check("from the launcher's key filter: the switcher with the app resident",
          r.pump(1000, [&] { return r.rec.has("log Super held 600 ms (launcher)") && r.rec.has("ui switcher open"); }),
          r.status());
    r.sup->stop();
}

/* Task c-7.5 (owner decision 2026-10-24): the FZ-M1's Windows button is a pulse (down and up 1 ms apart on the i8042
 * keyboard), so it cannot be held: two taps within 450 ms (press to press) open the switcher. The first tap goes home at
 * once from the app (as before); the second usually comes after the hand-over, through the launcher's key filter
 * (launcher_super_tap here), or through the supervisor's reader while the app is still hiding. */
void scenario_super_double_tap()
{
    std::printf("== c-7.5: Super double tap (the tablet's pulse): switcher; slow, triple, chord, duplicate, while hiding\n");
    Rig r(pd_config(), "dtap");
    App a = make_app("dta");
    r.sup->launch(spec_for(a, r.fb.path, Background::Freeze));
    check("started", r.pump(3000, [&] { return app_logged(a, "rec=start"); }));
    r.pump_for(150);
    const auto pulse = [&](int fd) { /* down and up 1 ms apart, as the tablet sends them */
        key(fd, KEY_LEFTMETA, 1);
        ::usleep(1000);
        key(fd, KEY_LEFTMETA, 0);
    };
    const auto back_to_app = [&]() {
        r.sup->command("switch dta");
        r.pump(2000, [&] { return r.state("dta") == "foreground"; });
        r.pump_for(600); /* the tap window passes */
    };
    /* first tap in the app: Home at once */
    r.rec.clear();
    const std::uint64_t t0 = Supervisor::now_ms(); /* the pulse is read within a pump step (5 ms) of this */
    pulse(r.kw);
    check("first tap: Home at once", r.pump(2000, [&] { return r.state("dta") == "hidden-frozen"; }) &&
                                         r.rec.has("log Super / Home key"),
          r.status());
    /* the second, 250 ms after the first's press, read by the launcher (the supervisor's reader is closed by then) */
    while (Supervisor::now_ms() < t0 + 150) r.pump_for(10);
    r.sup->launcher_super_tap(t0 + 250);
    check("second tap 250 ms later: double tap -> switcher",
          r.pump(1000, [&] { return r.rec.has("ms after the previous: double tap -> switcher"); }),
          last_with(r, "log Super tap"));
    check("the switcher: the app just left, first and In front",
          r.pump(1000, [&] { return r.rec.has("ui switcher open n=1 dta:hidden-frozen* sel=0 session=0"); }),
          last_with(r, "ui switcher"));
    check("log: opened by Super (from dta)", r.rec.has("log switcher: opened by Super with 1 app (from dta)"));
    /* a third tap right after: nothing */
    r.sup->launcher_super_tap(t0 + 400);
    r.pump_for(100);
    check("third tap within the window: ignored, the switcher stays",
          r.rec.has("ms after the double tap: ignored") && status_field(r.status(), "", "switcher") == "open", r.status());
    /* a double tap over the open switcher leaves it as Esc does: back to the app */
    const std::uint64_t t1 = Supervisor::now_ms() + 600;
    r.pump_for(700);
    r.sup->launcher_super_tap(t1);
    r.pump_for(50);
    r.sup->launcher_super_tap(t1 + 200);
    check("a double tap over the switcher: back to the app",
          r.pump(2000, [&] { return r.state("dta") == "foreground" && r.rec.has("log switcher: cancelled by Super (back to dta)"); }),
          r.status());
    r.pump_for(600);
    /* a slow pair (600 ms): two single taps, Home then nothing; no switcher */
    r.rec.clear();
    pulse(r.kw);
    r.pump(2000, [&] { return r.state("dta") == "hidden-frozen"; });
    r.pump_for(500);
    r.sup->launcher_super_tap(Supervisor::now_ms());
    r.pump_for(150);
    check("a slow pair: too late, single tap, no switcher",
          r.rec.has("too late, single tap") && !r.rec.has("ui switcher open") && r.state("dta") == "hidden-frozen",
          last_with(r, "log Super tap"));
    back_to_app();
    /* a tap then Super+A: the chord is no tap, no switcher */
    r.rec.clear();
    pulse(r.kw);
    r.pump(2000, [&] { return r.state("dta") == "hidden-frozen"; });
    r.pump_for(600);
    back_to_app();
    r.rec.clear();
    key(r.kw, KEY_LEFTMETA, 1);
    key(r.kw, KEY_A, 1);
    key(r.kw, KEY_A, 0);
    key(r.kw, KEY_LEFTMETA, 0);
    r.pump_for(100);
    pulse(r.kw);
    r.pump(2000, [&] { return r.state("dta") == "hidden-frozen"; });
    r.pump_for(150);
    check("Super+A then a tap: the chord counts nothing, the tap is a first one (Home), no switcher",
          !r.rec.has("double tap") && !r.rec.has("ui switcher open") && r.state("dta") == "hidden-frozen",
          last_with(r, "log Super"));
    /* the same tap reported by the launcher as well (the hand-over): counted once */
    r.pump_for(600);
    r.sup->launcher_super_tap(Supervisor::now_ms());
    r.pump_for(10);
    r.sup->launcher_super_tap(Supervisor::now_ms() - 5);
    r.pump_for(100);
    check("the same tap twice (hand-over): ignored once", r.rec.has("the same tap, ignored") && !r.rec.has("ui switcher open"),
          last_with(r, "log Super tap"));
    r.sup->stop();

    /* an app that takes 250 ms to answer hide: the second tap comes while it is still hiding, on the supervisor's
     * reader; the switcher shows once it is hidden */
    Rig s(pd_config(), "dtsl");
    App b = make_app("dtb");
    s.sup->launch(spec_for(b, s.fb.path, Background::Freeze, "--ack-delay-ms 250"));
    check("slow app started", s.pump(3000, [&] { return app_logged(b, "rec=start"); }));
    s.pump_for(150);
    s.rec.clear();
    pulse(s.kw);
    s.pump_for(120);
    check("first tap: hiding (slow)", s.state("dtb") == "hiding", s.status());
    pulse(s.kw);
    check("second tap while it hides: the switcher once hidden",
          s.pump(2500, [&] { return s.rec.has("ui switcher open n=1 dtb:hidden-frozen*"); }), s.status());
    check("log: double tap, once dtb is hidden",
          s.rec.has("ms after the previous: double tap -> switcher") && s.rec.has("log switcher (Super): once dtb is hidden"),
          last_with(s, "log switcher"));
    s.sup->stop();
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
        if (std::string(only) == "super") { /* MT_ONLY=super: task c-7 alone */
            scenario_super_hold();
            scenario_super_double_tap();
        }
        if (std::string(only) == "pd") { /* MT_ONLY=pd: the P-d scenarios alone */
            scenario_pd_switcher_from_app();
            scenario_pd_cancel();
            scenario_pd_legacy_sheet();
            scenario_pd_alt_tab();
            scenario_pd_hint();
        }
        std::printf("test_supervisor_host: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }
    scenario_freeze_cycle();
    scenario_run_mode();
    scenario_draw_hidden_negative();
    scenario_slow_ack_not_responding();
    scenario_close_frozen();
    /* P-e: the same three with the toolkit's app-side helper speaking the protocol */
    scenario_freeze_cycle("--helper");
    scenario_run_mode("--helper");
    scenario_close_frozen("--helper");
    scenario_close_ignores_close_message();
    scenario_exit_and_kill_while_hidden();
    scenario_esc_hold();
    scenario_alt_tab_evdev();
    scenario_alt_tab_cancel_deck();
    scenario_home_message_and_fifo();
    scenario_cap_and_memory();
    scenario_shutdown_no_stopped_left();
    scenario_g1_from_app();
    scenario_g1_false_positives();
    scenario_g1_legacy_and_fifo();
    scenario_pd_switcher_from_app();
    scenario_pd_cancel();
    scenario_pd_legacy_sheet();
    scenario_pd_alt_tab();
    scenario_pd_hint();
    scenario_super_hold();
    scenario_super_double_tap();
    scenario_guardian();
    scenario_loop_cost();
    std::printf("test_supervisor_host: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
