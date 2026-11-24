/*
 * SPDX-License-Identifier: MIT
 *
 * Quick controls model: the state replies, what a button shows and does, the adapters, the focus moves, and the
 * Controller's one rule: opening the page and moving the focus only READ; only activate() switches a radio or starts a
 * shutdown / reboot (and never twice, never while busy, never on a button that is not available).
 */

#include "../main/ui/model/quick_controls_model.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace quick_controls;
namespace fs = std::filesystem;

namespace {

struct Counters {
    int adapters = 0, reads = 0, sets = 0, powers = 0;
    bool wifi_on = true, bt_on = false;
    bool wifi_adapter = true, bt_adapter = true;
    bool set_ok = true, power_ok = true, read_ok = true;
    Control last_set = Control::Wifi;
    bool last_set_on = false;
    Control last_power = Control::Shutdown;
};

Backend fake(Counters &c)
{
    Backend b;
    b.adapters = [&c] {
        ++c.adapters;
        return Adapters{c.wifi_adapter, c.bt_adapter};
    };
    b.read = [&c](Control control) {
        ++c.reads;
        if (!c.read_ok) return Radio::Unknown;
        return (control == Control::Wifi ? c.wifi_on : c.bt_on) ? Radio::On : Radio::Off;
    };
    b.set = [&c](Control control, bool on) {
        ++c.sets;
        c.last_set = control;
        c.last_set_on = on;
        if (!c.set_ok) return false;
        (control == Control::Wifi ? c.wifi_on : c.bt_on) = on;
        return true;
    };
    b.power = [&c](Control control) {
        ++c.powers;
        c.last_power = control;
        return c.power_ok;
    };
    return b;
}

Controller::Spawn inline_spawn()
{
    return [](std::function<void()> job) { job(); };
}

State settled(Radio wifi, Radio bt)
{
    State s;
    s.wifi = wifi;
    s.bluetooth = bt;
    s.wifi_busy = s.bluetooth_busy = false;
    return s;
}

void test_parse()
{
    assert(parse_wifi_reply(1) == Radio::On);
    assert(parse_wifi_reply(0) == Radio::Off);
    assert(parse_wifi_reply(-1) == Radio::Unknown);
    assert(parse_wifi_reply(2) == Radio::Unknown);
    assert(parse_bluetooth_status(0, "1\t00:11:22:33:44:55\t0\tzero7") == Radio::On);
    assert(parse_bluetooth_status(0, "0\t00:11:22:33:44:55\t0\tzero7") == Radio::Off);
    assert(parse_bluetooth_status(0, "1") == Radio::On);
    assert(parse_bluetooth_status(0, "") == Radio::Unknown);
    assert(parse_bluetooth_status(0, "x\ta\tb\tc") == Radio::Unknown);
    assert(parse_bluetooth_status(-1, "1\ta\t0\tz") == Radio::Unknown); // a failed call is never "on"
}

void test_texts_and_decisions()
{
    State s; // just opened: still checking
    assert(state_text(Control::Wifi, s) == "Checking...");
    assert(decide(Control::Wifi, s) == ActionKind::None);
    assert(!active(Control::Bluetooth, s));
    assert(decide(Control::Shutdown, s) == ActionKind::Shutdown); // the power buttons need no state
    assert(state_text(Control::Shutdown, s) == "Power off now");
    assert(state_text(Control::Reboot, s) == "Restart now");

    s = settled(Radio::On, Radio::Off);
    assert(state_text(Control::Wifi, s) == "On" && state_text(Control::Bluetooth, s) == "Off");
    assert(decide(Control::Wifi, s) == ActionKind::WifiOff);
    assert(decide(Control::Bluetooth, s) == ActionKind::BluetoothOn);

    s = settled(Radio::Unavailable, Radio::Unavailable);
    assert(state_text(Control::Wifi, s) == "Not available" && state_text(Control::Bluetooth, s) == "Not available");
    assert(decide(Control::Wifi, s) == ActionKind::None && decide(Control::Bluetooth, s) == ActionKind::None);
    assert(!active(Control::Wifi, s));

    s = settled(Radio::Unknown, Radio::On); // the read failed: "Unknown", a tap reads again
    assert(state_text(Control::Wifi, s) == "Unknown" && decide(Control::Wifi, s) == ActionKind::Refresh);

    s = settled(Radio::On, Radio::On);
    s.wifi_busy = s.wifi_switching = true;
    assert(state_text(Control::Wifi, s) == "Switching..." && decide(Control::Wifi, s) == ActionKind::None);
    assert(decide(Control::Bluetooth, s) == ActionKind::BluetoothOff); // the other one is free

    s = settled(Radio::On, Radio::On);
    s.power_started = true;
    s.power = Control::Reboot;
    assert(state_text(Control::Reboot, s) == "Restarting...");
    assert(state_text(Control::Shutdown, s) == "Power off now");
    for (int i = 0; i < kControlCount; ++i) assert(decide(static_cast<Control>(i), s) == ActionKind::None);

    assert(std::string(label(Control::Wifi)) == "Wi-Fi" && std::string(label(Control::Bluetooth)) == "Bluetooth");
    assert(std::string(label(Control::Shutdown)) == "Shutdown" && std::string(label(Control::Reboot)) == "Reboot");
}

void test_focus_moves()
{
    // 2 x 2: 0 1 / 2 3
    assert(move_focus(0, 2, 4, Move::Right) == 1 && move_focus(1, 2, 4, Move::Right) == 1);
    assert(move_focus(1, 2, 4, Move::Left) == 0 && move_focus(0, 2, 4, Move::Left) == 0);
    assert(move_focus(0, 2, 4, Move::Down) == 2 && move_focus(2, 2, 4, Move::Down) == 2);
    assert(move_focus(3, 2, 4, Move::Up) == 1 && move_focus(1, 2, 4, Move::Up) == 1);
    // 1 x 4: left and right stay, up and down walk
    assert(move_focus(1, 1, 4, Move::Left) == 1 && move_focus(1, 1, 4, Move::Right) == 1);
    assert(move_focus(1, 1, 4, Move::Down) == 2 && move_focus(3, 1, 4, Move::Down) == 3);
    assert(move_focus(0, 1, 4, Move::Up) == 0);
}

void test_adapters()
{
    Adapters a;
    assert(!parse_adapters_hook(nullptr, a) && !parse_adapters_hook("", a));
    assert(parse_adapters_hook("none", a) && !a.wifi && !a.bluetooth);
    assert(parse_adapters_hook("wifi", a) && a.wifi && !a.bluetooth);
    assert(parse_adapters_hook("bt", a) && !a.wifi && a.bluetooth);
    assert(parse_adapters_hook("wifi,bluetooth", a) && a.wifi && a.bluetooth);
    assert(parse_adapters_hook("all", a) && a.wifi && a.bluetooth);

    const fs::path root = fs::temp_directory_path() / "quickctl-sysfs-test";
    fs::remove_all(root);
    fs::create_directories(root / "class" / "net" / "eth0");
    fs::create_directories(root / "class" / "net" / "lo");
    assert(!probe_adapters(root.string()).wifi && !probe_adapters(root.string()).bluetooth); // wired only
    fs::create_directories(root / "class" / "net" / "wlan0" / "wireless");
    assert(probe_adapters(root.string()).wifi && !probe_adapters(root.string()).bluetooth);
    fs::create_directories(root / "class" / "bluetooth");
    assert(!probe_adapters(root.string()).bluetooth); // the class folder alone is no adapter
    fs::create_directories(root / "class" / "bluetooth" / "hci0");
    assert(probe_adapters(root.string()).bluetooth);
    fs::remove_all(root);
    fs::create_directories(root / "class" / "net" / "wlan1" / "phy80211");
    assert(probe_adapters(root.string()).wifi); // a cfg80211 interface
    fs::remove_all(root);
    assert(!probe_adapters((root / "missing").string()).wifi); // no sysfs tree at all: nothing found, no crash
}

/* Opening (refresh), polling, looking at the state, deciding and moving the focus run no action. */
void test_open_and_focus_only_read()
{
    Counters c;
    Controller controller(fake(c), inline_spawn());
    controller.refresh();
    assert(controller.pending());
    assert(controller.poll());
    assert(!controller.pending());
    assert(c.adapters == 1 && c.reads == 2);
    assert(c.sets == 0 && c.powers == 0);
    assert(controller.state().wifi == Radio::On && controller.state().bluetooth == Radio::Off);

    for (int i = 0; i < kControlCount; ++i) {
        (void)decide(static_cast<Control>(i), controller.state()); // painting and focus look, never act
        (void)state_text(static_cast<Control>(i), controller.state());
    }
    for (int i = 0, at = 0; i < 12; ++i) at = move_focus(at, 2, 4, static_cast<Move>(i % 4));
    controller.poll();
    assert(c.sets == 0 && c.powers == 0 && c.reads == 2);
}

void test_activate_power()
{
    Counters c;
    Controller controller(fake(c), inline_spawn());
    controller.refresh();
    controller.poll();
    assert(controller.activate(Control::Reboot) == ActionKind::Reboot);
    assert(c.powers == 1 && c.last_power == Control::Reboot && c.sets == 0);
    assert(controller.state().power_started && controller.message() == "Restarting...");
    controller.poll();
    assert(controller.state().power_started && !controller.message_error());
    // no second request, from any button
    assert(controller.activate(Control::Reboot) == ActionKind::None);
    assert(controller.activate(Control::Shutdown) == ActionKind::None);
    assert(controller.activate(Control::Wifi) == ActionKind::None);
    assert(c.powers == 1 && c.sets == 0);

    Counters d;
    Controller shutdown(fake(d), inline_spawn());
    shutdown.refresh();
    shutdown.poll();
    assert(shutdown.activate(Control::Shutdown) == ActionKind::Shutdown);
    assert(d.powers == 1 && d.last_power == Control::Shutdown && shutdown.message() == "Shutting down...");

    // a refused command: the buttons come back and the page says so
    Counters f;
    f.power_ok = false;
    Controller failing(fake(f), inline_spawn());
    failing.refresh();
    failing.poll();
    failing.activate(Control::Shutdown);
    failing.poll();
    assert(!failing.state().power_started && failing.message_error() && failing.message() == "Shutdown failed");
    assert(decide(Control::Shutdown, failing.state()) == ActionKind::Shutdown);
}

void test_activate_radios()
{
    Counters c;
    Controller controller(fake(c), inline_spawn());
    controller.refresh();
    controller.poll();
    assert(controller.activate(Control::Wifi) == ActionKind::WifiOff);
    assert(c.sets == 1 && c.last_set == Control::Wifi && !c.last_set_on && c.powers == 0);
    assert(controller.state().wifi_busy && state_text(Control::Wifi, controller.state()) == "Switching...");
    assert(controller.poll());
    assert(controller.state().wifi == Radio::Off && !controller.state().wifi_busy);
    assert(state_text(Control::Wifi, controller.state()) == "Off" && controller.message().empty());

    assert(controller.activate(Control::Bluetooth) == ActionKind::BluetoothOn);
    assert(c.sets == 2 && c.last_set == Control::Bluetooth && c.last_set_on);
    controller.poll();
    assert(controller.state().bluetooth == Radio::On);
    assert(controller.activate(Control::Bluetooth) == ActionKind::BluetoothOff);
    controller.poll();
    assert(controller.state().bluetooth == Radio::Off && c.sets == 3);

    // a refused switch: the real state is shown again and the page says so
    c.set_ok = false;
    assert(controller.activate(Control::Wifi) == ActionKind::WifiOn);
    controller.poll();
    assert(controller.state().wifi == Radio::Off && controller.message_error());
    assert(controller.message() == "Wi-Fi did not switch");
}

void test_unavailable_and_busy()
{
    Counters c;
    c.wifi_adapter = false;
    c.bt_adapter = false;
    Controller controller(fake(c), inline_spawn());
    controller.refresh();
    controller.poll();
    assert(controller.state().wifi == Radio::Unavailable && controller.state().bluetooth == Radio::Unavailable);
    assert(c.reads == 0); // nothing asked of a service that has no adapter
    assert(controller.activate(Control::Wifi) == ActionKind::None);
    assert(controller.activate(Control::Bluetooth) == ActionKind::None);
    assert(c.sets == 0);

    // a switch still running: a second tap does nothing
    Counters d;
    std::vector<std::function<void()>> jobs;
    Controller slow(fake(d), [&jobs](std::function<void()> job) { jobs.push_back(std::move(job)); });
    slow.refresh();
    assert(slow.activate(Control::Wifi) == ActionKind::None); // still checking: not acted on
    for (auto &job : jobs) job();
    jobs.clear();
    slow.poll();
    assert(slow.activate(Control::Wifi) == ActionKind::WifiOff);
    assert(slow.activate(Control::Wifi) == ActionKind::None);
    assert(d.sets == 0); // the worker has not run yet
    for (auto &job : jobs) job();
    assert(d.sets == 1);
    slow.poll();
    assert(slow.activate(Control::Wifi) == ActionKind::WifiOn);

    // a failed read: "Unknown", a tap reads again and still switches nothing
    Counters e;
    e.read_ok = false;
    Controller unknown(fake(e), inline_spawn());
    unknown.refresh();
    unknown.poll();
    assert(unknown.state().wifi == Radio::Unknown);
    const int reads = e.reads;
    assert(unknown.activate(Control::Wifi) == ActionKind::Refresh);
    unknown.poll();
    assert(e.reads > reads && e.sets == 0 && e.powers == 0);
}

} // namespace

int main()
{
    test_parse();
    test_texts_and_decisions();
    test_focus_moves();
    test_adapters();
    test_open_and_focus_only_read();
    test_activate_power();
    test_activate_radios();
    test_unavailable_and_busy();
    std::printf("test_quick_controls_model ok\n");
    return 0;
}
