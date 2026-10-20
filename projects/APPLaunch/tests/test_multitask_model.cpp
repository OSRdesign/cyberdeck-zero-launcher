// SPDX-License-Identifier: MIT
//
// Multitasking P-b (task 015): the lifecycle state machine as pure transitions, table-driven. Each step is one
// request or report and the exact actions it must produce, written as text. Study 026 section 7.3: ack, no ack, app
// exits while hidden, killed while frozen, close of a frozen app, resume of a running app, LRU at the cap, the
// memory floor with fake readings, launcher restart; plus legacy apps, one instance, pending targets, the Alt+Tab
// session and input protection order.
#include "multitask/multitask_model.hpp"

#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace multitask;

namespace {

int g_failures = 0;
int g_steps = 0;

struct World {
    Model m;
    std::map<std::string, AppId> ids;
    std::map<AppId, std::string> names;
    explicit World(Config c = Config()) : m(c) {}

    AppSpec spec(const std::string &name, Background bg)
    {
        AppSpec s;
        s.key = "/opt/" + name + "/bin/" + name;
        s.name = name;
        s.exec = s.key;
        s.background = bg;
        return s;
    }
    AppId id(const std::string &name) const
    {
        auto it = ids.find(name);
        return it == ids.end() ? kNoApp : it->second;
    }
    // launch, and register the name of a newly spawned id
    Actions launch(const std::string &name, Background bg, std::uint64_t now)
    {
        Actions a = m.launch(spec(name, bg), now);
        for (const Action &x : a)
            if (x.op == Op::Spawn) {
                ids[name] = x.id;
                names[x.id] = name;
            }
        return a;
    }
    std::string render(const Actions &actions) const
    {
        std::string s;
        for (const Action &a : actions) {
            if (!s.empty()) s += " ";
            const auto it = names.find(a.id);
            const std::string who = it == names.end() ? std::string("?") : it->second;
            switch (a.op) {
            case Op::LauncherFront: s += a.value ? "LauncherFront(exit)" : "LauncherFront"; break;
            case Op::LauncherKeys:
            case Op::GrabKeys:
            case Op::GrabTouch:
            case Op::RawKeys: s += std::string(op_name(a.op)) + "(" + std::to_string(a.value) + ")"; break;
            case Op::Repaint: s += "Repaint"; break;
            case Op::SendHide:
            case Op::SendShow:
            case Op::SendClose: s += std::string(op_name(a.op)) + "(" + who + "," + std::to_string(a.n) + ")"; break;
            case Op::SetOom: s += "SetOom(" + who + "," + std::to_string(a.value) + ")"; break;
            case Op::Notice: s += "Notice(" + a.text + ")"; break;
            default: s += std::string(op_name(a.op)) + "(" + who + ")"; break;
            }
        }
        return s;
    }
    State state(const std::string &name) const
    {
        const Entry *e = m.find(id(name));
        return e ? e->state : State::Gone;
    }
};

void expect(const char *test, const std::string &step, const std::string &got, const std::string &want)
{
    ++g_steps;
    if (got == want) return;
    ++g_failures;
    std::printf("FAIL %s / %s\n  want: %s\n  got:  %s\n", test, step.c_str(), want.c_str(), got.c_str());
}

void expect_state(const char *test, const World &w, const std::string &name, State want)
{
    expect(test, name + " state", state_name(w.state(name)), state_name(want));
}

struct Step {
    std::string what;
    std::function<Actions(World &)> run;
    std::string want;
};

void run_table(const char *test, World &w, const std::vector<Step> &steps)
{
    for (const Step &s : steps) expect(test, s.what, w.render(s.run(w)), s.want);
}

const char *kToApp = "RawKeys(1) LauncherKeys(0)";                         // launcher -> app, nothing hidden
const char *kToAppGrabbed = "RawKeys(1) GrabKeys(0) GrabTouch(0) LauncherKeys(0)"; // launcher -> app, apps hidden
const char *kToLauncherGrab = "LauncherKeys(1) GrabTouch(1) GrabKeys(1) RawKeys(0)";

std::string cat(std::initializer_list<std::string> parts)
{
    std::string s;
    for (const std::string &p : parts) {
        if (p.empty()) continue;
        if (!s.empty()) s += " ";
        s += p;
    }
    return s;
}

/* A launched and hidden with an ack: the common start of several tests. */
void launch_and_hide(World &w, const std::string &name, Background bg, std::uint64_t t, int pgid)
{
    w.launch(name, bg, t);
    w.m.spawned(w.id(name), pgid, true, 200, t);
    w.m.go_launcher(t + 10);
    const Entry *e = w.m.find(w.id(name));
    w.m.ack(w.id(name), e ? e->hide_n : 0, t + 11);
}

void test_background_parse()
{
    const char *t = "parse_background";
    expect(t, "freeze", background_name(parse_background("freeze")), "freeze");
    expect(t, "run", background_name(parse_background("run")), "run");
    expect(t, " Run ", background_name(parse_background(" Run ")), "run");
    expect(t, "FREEZE", background_name(parse_background("FREEZE")), "freeze");
    expect(t, "empty", background_name(parse_background("")), "none");
    expect(t, "none", background_name(parse_background("none")), "none");
    expect(t, "yes", background_name(parse_background("yes")), "none");
    expect(t, "frozen", background_name(parse_background("frozen")), "none");
}

void test_freeze_ack_and_resume()
{
    const char *t = "freeze: hide, ack, resume";
    World w;
    run_table(t, w, {
        {"launch A", [](World &w) { return w.launch("A", Background::Freeze, 0); }, cat({"AppFront(A)", kToApp, "Spawn(A)"})},
        {"spawned", [](World &w) { return w.m.spawned(w.id("A"), 100, true, 200, 5); }, ""},
        {"Super", [](World &w) { return w.m.go_launcher(1000); }, "SendHide(A,1)"},
        {"ack 1", [](World &w) { return w.m.ack(w.id("A"), 1, 1001); },
         cat({"Stop(A) Snapshot(A) SetOom(A,900) LauncherFront", kToLauncherGrab})},
        {"resume", [](World &w) { return w.m.resume(w.id("A"), 5000); },
         cat({"AppFront(A)", kToAppGrabbed, "Restore(A) Drain(A) SetOom(A,200) Cont(A) SendShow(A,2)"})},
        {"late ack of show", [](World &w) { return w.m.ack(w.id("A"), 2, 5001); }, ""},
    });
    expect_state(t, w, "A", State::Foreground);
}

void test_run_ack_and_resume()
{
    const char *t = "run: hidden running, resume without SIGCONT";
    World w;
    run_table(t, w, {
        {"launch A", [](World &w) { return w.launch("A", Background::Run, 0); }, cat({"AppFront(A)", kToApp, "Spawn(A)"})},
        {"spawned", [](World &w) { return w.m.spawned(w.id("A"), 100, true, 0, 5); }, ""},
        {"Super", [](World &w) { return w.m.go_launcher(1000); }, "SendHide(A,1)"},
        {"ack 1", [](World &w) { return w.m.ack(w.id("A"), 1, 1000); },
         cat({"Snapshot(A) SetOom(A,700) LauncherFront", kToLauncherGrab})},
    });
    expect_state(t, w, "A", State::HiddenRunning);
    run_table(t, w, {
        {"resume", [](World &w) { return w.m.resume(w.id("A"), 3000); },
         cat({"AppFront(A)", kToAppGrabbed, "Restore(A) Drain(A) SetOom(A,0) SendShow(A,2)"})},
    });
}

void test_no_ack_not_responding()
{
    const char *t = "no ack: not responding at 300 ms";
    World w;
    w.launch("A", Background::Freeze, 0);
    w.m.spawned(w.id("A"), 100, true, 200, 0);
    run_table(t, w, {
        {"Super", [](World &w) { return w.m.go_launcher(1000); }, "SendHide(A,1)"},
        {"tick 299 ms", [](World &w) { return w.m.tick(1299); }, ""},
        {"tick 300 ms", [](World &w) { return w.m.tick(1300); },
         cat({"Stop(A) Snapshot(A) SetOom(A,900) Notice(A is not responding) LauncherFront", kToLauncherGrab})},
        {"late ack ignored", [](World &w) { return w.m.ack(w.id("A"), 1, 1400); }, ""},
    });
    expect_state(t, w, "A", State::NotResponding);
    run_table(t, w, {
        {"resume a not responding app", [](World &w) { return w.m.resume(w.id("A"), 2000); },
         cat({"AppFront(A)", kToAppGrabbed, "Restore(A) Drain(A) SetOom(A,200) Cont(A) SendShow(A,2)"})},
    });
}

void test_control_closed()
{
    const char *t = "socket closed by the app";
    World w;
    w.launch("A", Background::Freeze, 0);
    w.m.spawned(w.id("A"), 100, true, 0, 0);
    run_table(t, w, {
        {"Super", [](World &w) { return w.m.go_launcher(10); }, "SendHide(A,1)"},
        {"socket closed while hiding", [](World &w) { return w.m.control_closed(w.id("A"), 20); },
         cat({"Stop(A) Snapshot(A) SetOom(A,900) Notice(A is not responding) LauncherFront", kToLauncherGrab})},
        {"resume: no show message", [](World &w) { return w.m.resume(w.id("A"), 30); },
         cat({"AppFront(A)", kToAppGrabbed, "Restore(A) Drain(A) SetOom(A,0) Cont(A)"})},
        {"Super: hidden at once (no socket)", [](World &w) { return w.m.go_launcher(40); },
         cat({"Stop(A) Snapshot(A) SetOom(A,900) Notice(A is not responding) LauncherFront", kToLauncherGrab})},
    });
}

void test_exit_while_hidden()
{
    const char *t = "app exits while hidden (running)";
    World w;
    launch_and_hide(w, "A", Background::Run, 0, 100);
    run_table(t, w, {
        {"exited", [](World &w) { return w.m.exited(w.id("A"), 5000); },
         "DropSnapshot(A) Notice(A exited) GrabKeys(0) GrabTouch(0)"},
    });
    expect(t, "residents", std::to_string(w.m.resident_count()), "0");
    expect(t, "find", w.m.find(w.id("A")) ? "found" : "gone", "gone");
}

void test_killed_while_frozen()
{
    const char *t = "killed while frozen";
    World w;
    launch_and_hide(w, "A", Background::Freeze, 0, 100);
    launch_and_hide(w, "B", Background::Freeze, 100, 200);
    run_table(t, w, {
        {"A killed (OOM)", [](World &w) { return w.m.exited(w.id("A"), 5000); }, "DropSnapshot(A) Notice(A exited)"},
        {"B killed", [](World &w) { return w.m.exited(w.id("B"), 5001); },
         "DropSnapshot(B) Notice(B exited) GrabKeys(0) GrabTouch(0)"},
    });
}

void test_close_frozen()
{
    const char *t = "close a frozen app: SIGCONT first";
    World w;
    launch_and_hide(w, "A", Background::Freeze, 0, 100);
    run_table(t, w, {
        {"close", [](World &w) { return w.m.close(w.id("A"), 2000); }, "Cont(A) SendClose(A,2) GrabKeys(0) GrabTouch(0)"},
        {"tick 2.9 s", [](World &w) { return w.m.tick(4999); }, ""},
        {"tick 3 s: SIGTERM (with SIGCONT)", [](World &w) { return w.m.tick(5000); }, "Cont(A) Term(A)"},
        {"tick 5.9 s", [](World &w) { return w.m.tick(7999); }, ""},
        {"tick 6 s: SIGKILL", [](World &w) { return w.m.tick(8000); }, "Kill(A)"},
        {"tick again: nothing", [](World &w) { return w.m.tick(9000); }, ""},
        {"exited", [](World &w) { return w.m.exited(w.id("A"), 9001); }, "DropSnapshot(A)"},
    });
}

void test_close_without_socket_and_not_responding_repaint()
{
    const char *t = "close a not responding app: SIGCONT + SIGTERM, repaint after";
    World w;
    w.launch("A", Background::Freeze, 0);
    w.m.spawned(w.id("A"), 100, true, 0, 0);
    w.m.go_launcher(10);
    w.m.control_closed(w.id("A"), 11); // hidden at once, not responding, no socket
    run_table(t, w, {
        {"close", [](World &w) { return w.m.close(w.id("A"), 100); }, "Cont(A) Term(A) GrabKeys(0) GrabTouch(0)"},
        {"tick 3 s: SIGKILL", [](World &w) { return w.m.tick(3100); }, "Kill(A)"},
        {"exited", [](World &w) { return w.m.exited(w.id("A"), 3200); }, "DropSnapshot(A) Repaint"},
    });
}

void test_switch_between_apps()
{
    const char *t = "resume a running app while another is in front";
    World w;
    launch_and_hide(w, "A", Background::Run, 0, 100);
    w.launch("B", Background::Freeze, 100);
    w.m.spawned(w.id("B"), 200, true, 200, 100);
    run_table(t, w, {
        {"resume A: B hidden first", [](World &w) { return w.m.resume(w.id("A"), 1000); }, "SendHide(B,1)"},
        {"ack B: straight to A, no launcher frame", [](World &w) { return w.m.ack(w.id("B"), 1, 1001); },
         "Stop(B) Snapshot(B) SetOom(B,900) AppFront(A) Restore(A) Drain(A) SetOom(A,200) SendShow(A,2)"},
    });
    expect_state(t, w, "A", State::Foreground);
    expect_state(t, w, "B", State::HiddenFrozen);
    const std::vector<AppId> mru = w.m.mru();
    expect(t, "mru", mru.size() == 2 && mru[0] == w.id("A") && mru[1] == w.id("B") ? "A,B" : "wrong", "A,B");
}

void test_pending_last_request_wins()
{
    const char *t = "a request during a hide replaces the pending target";
    World w;
    launch_and_hide(w, "A", Background::Freeze, 0, 100);
    w.launch("B", Background::Freeze, 100);
    w.m.spawned(w.id("B"), 200, true, 0, 100);
    run_table(t, w, {
        {"resume A", [](World &w) { return w.m.resume(w.id("A"), 1000); }, "SendHide(B,1)"},
        {"Home instead, while hiding", [](World &w) { return w.m.go_launcher(1001); }, ""},
        {"ack: launcher", [](World &w) { return w.m.ack(w.id("B"), 1, 1002); },
         cat({"Stop(B) Snapshot(B) SetOom(B,900) LauncherFront", kToLauncherGrab})},
    });
}

void test_exit_during_hide_runs_pending()
{
    const char *t = "the app exits while being hidden: the pending target runs";
    World w;
    launch_and_hide(w, "A", Background::Freeze, 0, 100);
    w.launch("B", Background::Run, 100);
    w.m.spawned(w.id("B"), 200, true, 0, 100);
    run_table(t, w, {
        {"resume A", [](World &w) { return w.m.resume(w.id("A"), 1000); }, "SendHide(B,1)"},
        {"B exits instead of acking", [](World &w) { return w.m.exited(w.id("B"), 1001); },
         "AppFront(A) Restore(A) Drain(A) SetOom(A,200) Cont(A) SendShow(A,2)"},
    });
}

void test_cap_lru()
{
    const char *t = "cap 4: LRU frozen app closed, refused when all hidden apps run";
    World w;
    launch_and_hide(w, "A", Background::Freeze, 0, 100);
    launch_and_hide(w, "B", Background::Run, 100, 200);
    launch_and_hide(w, "C", Background::Freeze, 200, 300);
    launch_and_hide(w, "D", Background::Freeze, 300, 400);
    expect(t, "residents", std::to_string(w.m.resident_count()), "4");
    run_table(t, w, {
        {"launch E: A (LRU frozen) closed", [](World &w) { return w.launch("E", Background::Freeze, 1000); },
         cat({"Notice(Closed A (4 apps open)) Cont(A) SendClose(A,2) AppFront(E)", kToAppGrabbed, "Spawn(E)"})},
    });
    expect(t, "residents after", std::to_string(w.m.resident_count()), "4");

    const char *t2 = "cap 2, every hidden app runs: refused";
    Config c;
    c.cap = 2;
    World v(c);
    launch_and_hide(v, "A", Background::Run, 0, 100);
    launch_and_hide(v, "B", Background::Run, 100, 200);
    run_table(t2, v, {
        {"launch C refused", [](World &w) { return w.launch("C", Background::Freeze, 1000); },
         "Notice(2 apps open: close one first)"},
    });
    expect(t2, "front", std::to_string(v.m.front()), std::to_string(kNoApp));
}

void test_memory_floor()
{
    const char *t = "memory floors 64 / 32 MB with fake readings";
    World w;
    launch_and_hide(w, "A", Background::Freeze, 0, 100); // LRU
    launch_and_hide(w, "B", Background::Run, 100, 200);
    expect(t, "poll needed", w.m.needs_memory_poll() ? "yes" : "no", "yes");
    run_table(t, w, {
        {"70 MB: nothing", [](World &w) { return w.m.memory(70 * 1024, 1000); }, ""},
        {"58 MB: the LRU frozen app", [](World &w) { return w.m.memory(58 * 1024, 3000); },
         "Notice(Closed A: low memory (58 MB free)) Cont(A) SendClose(A,2)"},
        {"20 MB while A closes: one at a time", [](World &w) { return w.m.memory(20 * 1024, 5000); }, ""},
        {"A exits", [](World &w) { return w.m.exited(w.id("A"), 5500); }, "DropSnapshot(A)"},
        {"50 MB, only a run app hidden: kept", [](World &w) { return w.m.memory(50 * 1024, 7000); }, ""},
        {"20 MB: the run app too", [](World &w) { return w.m.memory(20 * 1024, 9000); },
         "Notice(Closed B: low memory (20 MB free)) SendClose(B,2) GrabKeys(0) GrabTouch(0)"},
    });
    expect(t, "last reading", std::to_string(w.m.last_memory_kb()), std::to_string(20 * 1024));
    World e;
    expect(t, "no hidden app: no poll", e.m.needs_memory_poll() ? "yes" : "no", "no");
    expect(t, "no hidden app: no action", e.render(e.m.memory(1024, 0)), "");
}

void test_restart_and_shutdown()
{
    const char *t = "launcher shutdown and restart";
    World w;
    launch_and_hide(w, "A", Background::Freeze, 0, 100);
    w.launch("B", Background::Run, 100);
    w.m.spawned(w.id("B"), 200, true, 0, 100);
    run_table(t, w, {
        {"shutdown: SIGCONT then SIGTERM, every app", [](World &w) { return w.m.shutdown(1000); },
         "Cont(A) Term(A) Cont(B) Term(B)"},
        {"tick 3 s: SIGKILL", [](World &w) { return w.m.tick(4000); }, "Kill(A) Kill(B)"},
    });
    World fresh; // the restarted launcher: systemd (or the guardian) ended the old groups
    expect(t, "fresh: no resident", std::to_string(fresh.m.resident_count()), "0");
    expect(t, "fresh: launcher in front", fresh.m.front() == kNoApp ? "launcher" : "app", "launcher");
    expect(t, "fresh: keyboard thread runs", fresh.m.derived().launcher_keys ? "on" : "off", "on");
    expect(t, "fresh: launching works", fresh.render(fresh.launch("A", Background::Freeze, 0)),
           cat({"AppFront(A)", kToApp, "Spawn(A)"}));
}

void test_legacy_app()
{
    const char *t = "legacy app (no X-Background)";
    World w;
    run_table(t, w, {
        {"launch L", [](World &w) { return w.launch("L", Background::None, 0); }, cat({"AppFront(L)", kToApp, "Spawn(L)"})},
        {"spawned without socket", [](World &w) { return w.m.spawned(w.id("L"), 100, false, 200, 0); }, ""},
        {"Super: refused", [](World &w) { return w.m.go_launcher(100); }, "Notice(L cannot stay in the background)"},
        {"Esc held 3 s", [](World &w) { return w.m.force_close_front(3000); }, "Term(L)"},
        {"Esc policy again: nothing", [](World &w) { return w.m.force_close_front(3010); }, ""},
        {"tick 3 s: SIGKILL", [](World &w) { return w.m.tick(6000); }, "Kill(L)"},
        {"exited: launcher, republish", [](World &w) { return w.m.exited(w.id("L"), 6100); },
         "LauncherFront(exit) LauncherKeys(1) RawKeys(0)"},
    });
}

void test_one_instance()
{
    const char *t = "one instance per app";
    World w;
    w.launch("A", Background::Freeze, 0);
    w.m.spawned(w.id("A"), 100, true, 0, 0);
    const AppId first = w.id("A");
    run_table(t, w, {
        {"tap A again while in front", [](World &w) { return w.m.launch(w.spec("A", Background::Freeze), 10); }, ""},
    });
    w.m.go_launcher(20);
    w.m.ack(first, 1, 21);
    run_table(t, w, {
        {"tap A while hidden: resume", [](World &w) { return w.m.launch(w.spec("A", Background::Freeze), 100); },
         cat({"AppFront(A)", kToAppGrabbed, "Restore(A) Drain(A) SetOom(A,0) Cont(A) SendShow(A,2)"})},
    });
    expect(t, "same id", w.m.front() == first ? "same" : "new", "same");
    w.m.close(first, 200);
    run_table(t, w, {
        {"tap A while closing", [](World &w) { return w.m.launch(w.spec("A", Background::Freeze), 300); },
         "Notice(A is still closing)"},
    });
}

void test_launch_while_app_in_front()
{
    const char *t = "launch from the control FIFO while an app is in front";
    World w;
    w.launch("A", Background::Run, 0);
    w.m.spawned(w.id("A"), 100, true, 0, 0);
    run_table(t, w, {
        {"launch B: A hidden first", [](World &w) { return w.launch("B", Background::Freeze, 100); }, "SendHide(A,1)"},
    });
    Actions a = w.m.ack(w.id("A"), 1, 101);
    for (const Action &x : a)
        if (x.op == Op::Spawn) {
            w.ids["B"] = x.id;
            w.names[x.id] = "B";
        }
    expect(t, "ack: B starts", w.render(a), "Snapshot(A) SetOom(A,700) AppFront(B) Spawn(B)");
}

void test_key_session_defers_keyboard_grab()
{
    const char *t = "Alt+Tab from inside an app: keyboard grab waits for the Alt release";
    World w;
    w.launch("A", Background::Freeze, 0);
    w.m.spawned(w.id("A"), 100, true, 0, 0);
    run_table(t, w, {
        {"session starts (raw)", [](World &w) { return w.m.key_session(true, 10); }, ""},
        {"hide for the session", [](World &w) { return w.m.go_launcher(10); }, "SendHide(A,1)"},
        {"ack: touch grabbed, keyboards not yet", [](World &w) { return w.m.ack(w.id("A"), 1, 11); },
         "Stop(A) Snapshot(A) SetOom(A,900) LauncherFront GrabTouch(1)"},
        {"Alt released on Home", [](World &w) { return w.m.key_session(false, 500); },
         "LauncherKeys(1) GrabKeys(1) RawKeys(0)"},
    });
}

void test_esc_hold_lifecycle_and_close_all()
{
    const char *t = "Esc hold on a lifecycle app, close all";
    World w;
    launch_and_hide(w, "A", Background::Freeze, 0, 100);
    launch_and_hide(w, "B", Background::Run, 100, 200);
    w.launch("C", Background::Freeze, 200);
    w.m.spawned(w.id("C"), 300, true, 0, 200);
    run_table(t, w, {
        {"close all: the hidden apps only", [](World &w) { return w.m.close_all(1000); },
         "Cont(A) SendClose(A,2) SendClose(B,2)"},
        {"Esc held 3 s on C", [](World &w) { return w.m.force_close_front(4000); }, "Term(C)"},
        {"tick: A and B SIGTERM", [](World &w) { return w.m.tick(4001); }, "Cont(A) Term(A) Cont(B) Term(B)"},
        {"C exits: launcher", [](World &w) { return w.m.exited(w.id("C"), 4100); },
         "LauncherFront(exit) LauncherKeys(1) RawKeys(0)"},
    });
    expect_state(t, w, "A", State::Closing);
}

void test_spawn_failed()
{
    const char *t = "spawn failed";
    World w;
    w.launch("A", Background::Freeze, 0);
    run_table(t, w, {
        {"spawn failed", [](World &w) { return w.m.spawn_failed(w.id("A"), 1); },
         "Notice(Cannot start A) LauncherFront(exit) LauncherKeys(1) RawKeys(0)"},
    });
    expect(t, "residents", std::to_string(w.m.resident_count()), "0");
}

void test_front_exit_republish()
{
    const char *t = "the app in front exits";
    World w;
    launch_and_hide(w, "A", Background::Freeze, 0, 100);
    w.launch("B", Background::Freeze, 100);
    w.m.spawned(w.id("B"), 200, true, 0, 100);
    run_table(t, w, {
        {"B quits (Esc at its root): launcher, grabs for A", [](World &w) { return w.m.exited(w.id("B"), 500); },
         cat({"LauncherFront(exit)", kToLauncherGrab})},
    });
}

void test_next_deadline()
{
    const char *t = "next deadline (the supervisor sleeps until it)";
    World w;
    expect(t, "nothing resident", std::to_string(w.m.next_deadline()), "0");
    w.launch("A", Background::Freeze, 0);
    w.m.spawned(w.id("A"), 100, true, 0, 0);
    expect(t, "in front: none", std::to_string(w.m.next_deadline()), "0");
    w.m.go_launcher(1000);
    expect(t, "hiding: the ack deadline", std::to_string(w.m.next_deadline()), "1300");
    w.m.ack(w.id("A"), 1, 1001);
    expect(t, "frozen: none", std::to_string(w.m.next_deadline()), "0");
    w.m.close(w.id("A"), 2000);
    expect(t, "closing: SIGTERM due", std::to_string(w.m.next_deadline()), "5000");
    w.m.tick(5000);
    expect(t, "SIGTERM sent: SIGKILL due", std::to_string(w.m.next_deadline()), "8000");
    w.m.tick(8000);
    expect(t, "SIGKILL sent: none", std::to_string(w.m.next_deadline()), "0");
}

void test_launch_behind_closing_front()
{
    const char *t = "a launch while the app in front is closing waits for its exit";
    World w;
    w.launch("L", Background::None, 0);
    w.m.spawned(w.id("L"), 100, false, 0, 0);
    run_table(t, w, {
        {"Esc held 3 s", [](World &w) { return w.m.force_close_front(3000); }, "Term(L)"},
        {"tap B meanwhile: queued", [](World &w) { return w.launch("B", Background::Freeze, 3100); }, ""},
    });
    Actions a = w.m.exited(w.id("L"), 3200);
    for (const Action &x : a)
        if (x.op == Op::Spawn) {
            w.ids["B"] = x.id;
            w.names[x.id] = "B";
        }
    expect(t, "L exits: B starts", w.render(a), "AppFront(B) Spawn(B)");
}

} // namespace

int main()
{
    test_next_deadline();
    test_launch_behind_closing_front();
    test_background_parse();
    test_freeze_ack_and_resume();
    test_run_ack_and_resume();
    test_no_ack_not_responding();
    test_control_closed();
    test_exit_while_hidden();
    test_killed_while_frozen();
    test_close_frozen();
    test_close_without_socket_and_not_responding_repaint();
    test_switch_between_apps();
    test_pending_last_request_wins();
    test_exit_during_hide_runs_pending();
    test_cap_lru();
    test_memory_floor();
    test_restart_and_shutdown();
    test_legacy_app();
    test_one_instance();
    test_launch_while_app_in_front();
    test_key_session_defers_keyboard_grab();
    test_esc_hold_lifecycle_and_close_all();
    test_spawn_failed();
    test_front_exit_republish();
    if (g_failures) {
        std::printf("test_multitask_model: %d of %d checks failed\n", g_failures, g_steps);
        return 1;
    }
    std::printf("test_multitask_model: %d checks passed\n", g_steps);
    return 0;
}
