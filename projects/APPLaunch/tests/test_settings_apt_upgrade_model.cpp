// SPDX-License-Identifier: MIT
//
// Settings > Updates > Packages, P4 of report 047 (the launcher upgrades the safe set): the plan (the pins of the protected
// packages, the pinned simulation read back), the sizes, the power, the guards, the root command, the helper's status file
// (phases, captions, steps, the bar, cancel), the result page and the confirmation page's texts. The plans are built on the
// real P0 captures of the boards (tests/fixtures/apt/<board>/), apt's pinned simulation emulated by dropping the held
// packages (and what needs them) from the capture.
//
//   test_settings_apt_upgrade_model FIXTURE_DIR

#include "settings_apt_upgrade_model.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace launcher_apt;

namespace {

std::string g_fixtures;

std::string load(const std::string &path, bool must = true)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (!must) return std::string();
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        std::abort();
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool contains(const std::vector<Package> &list, const std::string &name)
{
    return std::any_of(list.begin(), list.end(), [&](const Package &p) { return p.name == name; });
}

const Package *find(const std::vector<Package> &list, const std::string &name)
{
    for (const Package &p : list)
        if (p.name == name) return &p;
    return nullptr;
}

std::string fx(const std::string &board, const char *name)
{
    return load(g_fixtures + "/" + board + "/" + name);
}

std::vector<std::string> lines(const std::string &text)
{
    std::vector<std::string> out;
    std::stringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

Board board_of(const std::string &name)
{
    Board board;
    for (const std::string &n : lines(fx(name, "closure.txt"))) board.libraries.insert(n);
    for (const std::string &n : parse_installed_packages(fx(name, "installed.json"))) board.apps.insert(n);
    for (const std::string &n : lines(fx(name, "owners.txt"))) board.apps.insert(n);
    return board;
}

Report report_of(const std::string &name)
{
    return classify(parse_simulation(fx(name, "upgrade.txt")), parse_simulation(fx(name, "dist-upgrade.txt")),
                    board_of(name));
}

bool has(const std::string &text, const std::string &part)
{
    return text.find(part) != std::string::npos;
}

/* apt as the helper and the launcher run it: `apt-get -s install --only-upgrade --no-remove --no-install-recommends NAMES`
 * on a board's capture, the closure of NAMES through its pulls.txt ("a: b c": installing a also upgrades b and c: a
 * dependency, or since apt 2.7 the other installed packages of the same source). A name with no newer version is left out
 * (apt does not upgrade it); a pulled name that is not installed is a new package. Counts its simulations. */
struct FakeApt {
    std::map<std::string, std::pair<std::string, std::string>> versions; /* name -> installed, candidate */
    std::map<std::string, std::vector<std::string>> pulls;
    std::string records_text;
    std::set<std::string> refuse;  /* apt refuses these alone ("E: ...") */
    int simulations = 0;

    explicit FakeApt(const std::string &board)
    {
        for (const Change &c : parse_simulation(fx(board, "upgrade.txt")).upgrades) versions[base_name(c.name)] = {c.from, c.to};
        for (const std::string &line : lines(load(g_fixtures + "/" + board + "/pulls.txt", false))) {
            if (line.empty() || line[0] == '#') continue;
            const std::size_t colon = line.find(':');
            std::stringstream rest(line.substr(colon + 1));
            std::string n;
            while (rest >> n) pulls[line.substr(0, colon)].push_back(n);
        }
        records_text = load(g_fixtures + "/" + board + "/records.txt", false);
    }
    std::string simulate(const std::vector<std::string> &names)
    {
        ++simulations;
        for (const std::string &n : names)
            if (refuse.count(n)) return "E: Unable to correct problems, you have held broken packages.\n";
        std::vector<std::string> todo(names.begin(), names.end());
        std::set<std::string> all;
        while (!todo.empty()) {
            const std::string n = todo.back();
            todo.pop_back();
            if (!all.insert(n).second) continue;
            const auto it = pulls.find(n);
            if (it != pulls.end()) todo.insert(todo.end(), it->second.begin(), it->second.end());
        }
        std::string out = "NOTE: This is only a simulation!\n";
        int up = 0, added = 0;
        for (const std::string &n : all) {
            const auto v = versions.find(n);
            if (v != versions.end()) {
                out += "Inst " + n + " [" + v->second.first + "] (" + v->second.second + " Debian:13/stable [arm64])\n";
                ++up;
            } else if (std::find(names.begin(), names.end(), n) == names.end()) {
                out += "Inst " + n + " (1.0-1 Debian:13/stable [arm64])\n";
                ++added;
            }
        }
        out += std::to_string(up) + " upgraded, " + std::to_string(added) + " newly installed, 0 to remove and 0 not upgraded.\n";
        return out;
    }
    ClosureHooks hooks(bool with_records = true)
    {
        ClosureHooks h;
        h.simulate = [this](const std::vector<std::string> &n) { return simulate(n); };
        if (with_records) h.records = [this](const std::vector<std::string> &) { return records_text; };
        return h;
    }
};

std::vector<std::string> names_in(const std::vector<Package> &list)
{
    std::vector<std::string> out;
    for (const Package &p : list) out.push_back(p.name);
    return out;
}

bool in(const std::vector<std::string> &list, const std::string &name)
{
    return std::find(list.begin(), list.end(), name) != list.end();
}

void plan_cases()
{
    // the deck case of the first board test: the naive safe set is what the helper refused ("apt would also upgrade curl")
    const Report r = report_of("deck-p4");
    assert(r.safe.size() == 25 && r.libraries.size() == 3 && r.boot.size() == 1 && r.terminal.empty());
    assert(contains(r.safe, "libcurl3t64-gnutls") && contains(r.libraries, "curl") && contains(r.libraries, "libcurl4t64"));
    FakeApt apt("deck-p4");
    {
        const std::vector<std::string> naive = names_in(r.safe);
        const std::string problem = closure_problem(parse_simulation(apt.simulate(naive)), naive);
        assert(problem == "apt would also upgrade curl"); /* the refusal of the board test, reproduced */
    }
    apt.simulations = 0;
    const Planned planned = plan_upgrade(r, apt.hooks());
    const Plan &off = planned.plan;
    const Plan &on = planned.plan_libraries;
    // the safe plan: 21 (the 22 of the deck less libcurl3t64-gnutls), closed: the helper's rule accepts it as it is
    assert(off.ready && off.valid && off.error.empty() && off.count() == 21 && !off.libraries);
    for (const char *gone : {"libcurl3t64-gnutls", "chain-tool", "libchain1", "raspberrypi-sys-mods", "curl", "raspi-firmware"})
        assert(!in(off.names(), gone));
    assert(closure_problem(parse_simulation(apt.simulate(off.names())), off.names()).empty());
    assert(off.waiting_libraries == 3 && contains(off.packages, "tzdata") && find(off.packages, "tzdata")->to == "2026e-0+deb13u1");
    // the moved group: libcurl3t64-gnutls (same source as curl), chain-tool and libchain1 (the chain to libssl3t64) under
    // Launcher libraries; raspberrypi-sys-mods (it needs new boot firmware) under Needs the terminal
    const Report &shown = planned.report;
    assert((names_in(shown.libraries) ==
            std::vector<std::string>{"chain-tool", "curl", "libchain1", "libcurl3t64-gnutls", "libcurl4t64", "libssl3t64"}));
    assert(shown.safe.size() == 21 && shown.boot.size() == 1 && shown.terminal.size() == 1);
    assert(shown.terminal[0].name == "raspberrypi-sys-mods" && shown.terminal[0].note == "needs raspi-firmware");
    assert(find(shown.libraries, "libcurl3t64-gnutls")->note == "needs curl, libcurl4t64");
    assert(shown.shown() == r.shown()); /* nothing lost: the same packages in other sections */
    // with the switch on: the 21, the 3 libraries and the 3 moved to them, closed too; the boot pull stays out
    assert(on.valid && on.libraries && on.count() == 27 && on.waiting_terminal == 1);
    assert(in(on.names(), "libcurl3t64-gnutls") && in(on.names(), "curl") && in(on.names(), "libchain1") &&
           !in(on.names(), "raspberrypi-sys-mods") && !in(on.names(), "raspi-firmware"));
    assert(closure_problem(parse_simulation(apt.simulate(on.names())), on.names()).empty());
    // the same state never refuses twice: planning it again gives the same plans (and so does planning the shown report)
    const Planned again = plan_upgrade(r, apt.hooks());
    assert(again.plan.names() == off.names() && again.plan_libraries.names() == on.names());
    const Planned shown_again = plan_upgrade(shown, apt.hooks());
    assert(shown_again.plan.names() == off.names() && shown_again.plan_libraries.names() == on.names());
    // without the records (no suspects): every package checked by its own simulation, the same result, more simulations
    FakeApt slow("deck-p4");
    const Planned blind = plan_upgrade(r, slow.hooks(false));
    assert(blind.plan.names() == off.names() && blind.plan_libraries.names() == on.names());
    FakeApt fast("deck-p4");
    plan_upgrade(r, fast.hooks(true));
    assert(fast.simulations < slow.simulations);
    std::printf("deck-p4: plan %d, with the libraries %d; %d simulations with the records, %d without\n", off.count(),
                on.count(), fast.simulations, slow.simulations);

    // the chain alone: chain-tool -> libchain1 -> libssl3t64 (a library): both taken out, the plan closes
    {
        FakeApt chain("deck-p4");
        const auto kind = [](const std::string &n) { return n == "libssl3t64" ? Outside::Library : Outside::Never; };
        const Closure c = close_plan({"chain-tool", "libchain1", "tzdata"}, kind, chain.hooks(false));
        assert(c.valid && c.names == std::vector<std::string>{"tzdata"} && c.pulls.size() == 2);
        for (const Pull &p : c.pulls) assert(!p.terminal && (p.name == "chain-tool" || p.name == "libchain1"));
        // a package pulling the boot chain is Never: the terminal
        const Closure b = close_plan({"raspberrypi-sys-mods", "tzdata"}, kind, chain.hooks());
        assert(b.valid && b.names == std::vector<std::string>{"tzdata"} && b.pulls.size() == 1 && b.pulls[0].terminal &&
               b.pulls[0].needs == std::vector<std::string>{"raspi-firmware"});
        // a package apt refuses alone (held broken packages): out, the terminal's, no need named
        FakeApt broken("deck-p4");
        broken.refuse.insert("vim");
        const Closure v = close_plan({"vim", "tzdata"}, kind, broken.hooks());
        assert(v.valid && v.names == std::vector<std::string>{"tzdata"} && v.pulls.size() == 1 && v.pulls[0].terminal &&
               v.pulls[0].needs.empty());
        // a package with no newer version any more (the lists moved on): not upgraded now, out
        const Closure gone = close_plan({"tzdata", "not-upgradable"}, kind, chain.hooks());
        assert(gone.valid && gone.names == std::vector<std::string>{"tzdata"} && gone.pulls[0].name == "not-upgradable");
        // the progress callback sees every simulation; a cancel stops at once and the result is not valid
        int seen = 0;
        ClosureHooks h = chain.hooks();
        h.progress = [&seen](int n) { seen = n; };
        const Closure counted = close_plan({"chain-tool", "libchain1", "tzdata"}, kind, h);
        assert(seen == counted.simulations && seen >= 3);
        h.cancelled = [&seen] { return seen >= 1; };
        seen = 0;
        const Closure stopped = close_plan({"chain-tool", "libchain1", "tzdata"}, kind, h);
        assert(!stopped.valid && stopped.error == "cancelled" && stopped.simulations == 1);
        // a whole-plan difference no single package explains: invalid, the difference in words
        FakeApt odd("deck-p4");
        ClosureHooks oh;
        oh.simulate = [&odd](const std::vector<std::string> &n) {
            std::string out = odd.simulate(n);
            if (n.size() > 1) out = "Inst mystery [1] (2 Debian:13/stable [arm64])\n" + out;
            return out;
        };
        const Closure m = close_plan({"tzdata", "xxd"}, [](const std::string &) { return Outside::Never; }, oh);
        assert(!m.valid && m.error == "apt would also upgrade mystery");
    }
    // the rule itself (the helper's)
    {
        Simulation s = parse_simulation("Inst a [1] (2 x [arm64])\nInst b:armhf [1] (2 x [armhf])\nInst b [1] (2 x [arm64])\n");
        assert(closure_problem(s, {"a", "b"}).empty());
        assert(closure_problem(s, {"a"}) == "apt would also upgrade b");
        assert(closure_problem(s, {"a", "b", "c"}) == "c is no longer offered");
        assert(closure_problem(parse_simulation("Inst a [1] (2 x)\nRemv old [1]\n"), {"a"}) == "apt would remove old");
        assert(closure_problem(parse_simulation("Inst a [1] (2 x)\nInst new (1 x)\n"), {"a"}) ==
               "apt would install the new package new");
        assert(closure_problem(Simulation(), {"a"}) == "apt gave no plan");
    }
    // the records: Source (with a version), Depends and Pre-Depends names
    {
        const std::map<std::string, Record> rec = parse_records(fx("deck-p4", "records.txt"));
        assert(rec.at("libcurl3t64-gnutls").source == "curl" && rec.at("curl").source.empty());
        assert(in(rec.at("libchain1").depends, "libssl3t64") && in(rec.at("libchain1").depends, "libc6"));
        assert(in(rec.at("chain-tool").depends, "libchain1"));
    }
    // the Pi 4 B and the Pi 5 (their captures, the curl source group): libcurl3t64-gnutls moves to the libraries
    {
        FakeApt a4("pi4b");
        const Planned p4 = plan_upgrade(report_of("pi4b"), a4.hooks(false));
        assert(p4.plan.valid && p4.plan.count() == 15 && p4.plan_libraries.count() == 20 && p4.report.libraries.size() == 5);
        assert(!in(p4.plan.names(), "libcurl3t64-gnutls") && in(p4.plan_libraries.names(), "libcurl3t64-gnutls"));
        assert(!in(p4.plan_libraries.names(), "rpi-eeprom")); /* the boot chain: never */
        FakeApt a5("pi5");
        const Planned p5 = plan_upgrade(report_of("pi5"), a5.hooks(false));
        assert(p5.plan.valid && p5.plan.count() == 233);
        for (const Package &p : report_of("pi5").terminal) assert(!in(p5.plan_libraries.names(), p.name));
        std::printf("pi4b: plan %d (+%d with the libraries); pi5: plan %d, %d simulations\n", p4.plan.count(),
                    p4.plan_libraries.count() - p4.plan.count(), p5.plan.count(), a5.simulations);
    }
    // the deck's capture: Debian's 2048 is the Store app's name: never in a plan
    {
        FakeApt deck("deck");
        const Planned pd = plan_upgrade(report_of("deck"), deck.hooks(false));
        assert(pd.plan.valid && pd.plan.count() == 0 && upgrade_label(pd.plan) == "Upgrade packages");
    }
    // phased updates (apt 2.7+, Ubuntu): deferred, not kept back: listed apart, never offered, never an error
    {
        const Simulation s = parse_simulation(
            "The following upgrades have been deferred due to phasing:\n  libfoo1 foo-bin\n"
            "The following packages will be upgraded:\n  tzdata\n1 upgraded, 0 newly installed, 0 to remove and 2 not upgraded.\n"
            "Inst tzdata [1] (2 Ubuntu:26.04/resolute-updates [all])\n");
        assert(s.valid && s.phased == (std::vector<std::string>{"libfoo1", "foo-bin"}) && s.kept_back.empty());
        const Report rp = classify(s, Simulation(), Board());
        assert(rp.phased.size() == 2 && rp.safe.size() == 1 && rp.terminal.empty() && !rp.needs_dist_upgrade);
    }
}

void size_cases()
{
    const std::string uris = "'http://deb.debian.org/debian/pool/main/c/curl/curl_8.14.1-2%2bdeb13u7_arm64.deb' "
                             "curl_8.14.1-2+deb13u7_arm64.deb 285432 SHA256:abcd\n"
                             "'http://archive.raspberrypi.com/debian/pool/main/r/rpi/x_1_arm64.deb' x_1_arm64.deb 1000000 SHA256:00\n";
    assert(parse_uris_bytes(uris, 2) == 1285432);
    assert(parse_uris_bytes("", 0) == 0);
    assert(parse_uris_bytes("", 3) == 0);                                   /* everything downloaded already */
    assert(parse_uris_bytes("E: Unable to locate package foo\n", 3) == -1); /* apt failed: unknown */
    long long sum = 0, largest = 0;
    parse_candidate_sizes("Package: a\nInstalled-Size: 1100\n\nPackage: b\nInstalled-Size: 70000\nSize: 9\n\n", sum, largest);
    assert(sum == 71100 && largest == 70000);
    assert(parse_installed_kib("1000\n2000\n\nbogus\n") == 3000);
    Plan plan;
    plan.packages = {{"a", "1", "2", ""}, {"b", "1", "2", ""}};
    plan.valid = true;
    apply_sizes(plan, uris, "Installed-Size: 1100\nInstalled-Size: 70000\n", "1000\n69000\n");
    assert(plan.download == 1285432 && plan.growth == 1100 * 1024 && plan.largest == 70000LL * 1024);
    assert(plan.need() == 1285432 + 1100 * 1024 + 70000LL * 1024); /* the largest package beats the 64 MB margin */
    apply_sizes(plan, uris, "Installed-Size: 100\n", "5000\n");      /* the packages shrink: no growth */
    assert(plan.growth == 0 && plan.need() == 1285432 + kMarginBytes);
    assert(disk_needed(-1, 0, 0) == kMarginBytes);
    assert(upgrade_value(plan) == "1 MB" && upgrade_label(plan) == "Upgrade 2 packages");
    plan.download = -1;
    assert(upgrade_value(plan).empty());
    plan.packages.resize(1);
    assert(upgrade_label(plan) == "Upgrade 1 package");
    assert(megabytes_text(0) == "0 MB" && megabytes_text(1) == "1 MB" && megabytes_text(41LL << 20) == "41 MB");

    const std::vector<std::string> deps = parse_depends("ca-certificates, curl, libc6 (>= 2.38), libinput10:arm64 | x, curl");
    assert((deps == std::vector<std::string>{"ca-certificates", "curl", "libc6", "libinput10", "x"}));
    assert(parse_depends("").empty());
}

void power_cases()
{
    Throttle t = parse_throttled("throttled=0x50005\n");
    assert(t.known && t.under_now && t.under_before);
    t = parse_throttled("50000");
    assert(t.known && !t.under_now && t.under_before);
    t = parse_throttled("0x0");
    assert(t.known && !t.under_now && !t.under_before);
    assert(!parse_throttled("").known && !parse_throttled("error=1 vchiq").known);
    assert(battery_low(30, false) && !battery_low(30, true) && !battery_low(50, false) && !battery_low(-1, false));
}

void guard_cases()
{
    const long long now = 1800000000;
    UpgradeGuards g;
    g.packages = 16;
    g.lists_time = now - 3600;
    g.now = now;
    g.need_bytes = 100LL << 20;
    g.free_bytes = 4096LL << 20;
    assert(upgrade_blocker(g).empty());
    UpgradeGuards x = g;
    x.network = false;
    assert(upgrade_blocker(x) == "No network");
    x.free_bytes = 50LL << 20;
    assert(upgrade_blocker(x) == "Not enough space: need 100 MB, 50 MB free");
    x.battery_percent = 30;
    assert(upgrade_blocker(x) == "Battery 30 %: charge it or plug it in");
    x.charging = true;
    assert(upgrade_blocker(x) == "Not enough space: need 100 MB, 50 MB free");
    x.under_voltage = true;
    assert(upgrade_blocker(x) == "Under-voltage: use a stronger power supply");
    x.lists_time = now - 9 * 86400;
    assert(upgrade_blocker(x) == "Lists are 9 days old: refresh them first");
    x.lists_time = 0;
    assert(upgrade_blocker(x) == "Lists never refreshed: refresh them first");
    x.packages = 0;
    assert(upgrade_blocker(x) == "Nothing to upgrade");
    x.repair_needed = true;
    assert(upgrade_blocker(x) == "Repair the interrupted installation first");
    x.busy = "Settings > Apps is installing an app";
    assert(upgrade_blocker(x) == "Settings > Apps is installing an app");
    x.helper_installed = false;
    assert(upgrade_blocker(x) == "No update helper: use a terminal");
    // the repair needs no lists, space or network, but the same power and the helper
    UpgradeGuards rep;
    rep.network = false;
    rep.free_bytes = 0;
    rep.need_bytes = 1;
    assert(repair_blocker(rep).empty());
    rep.battery_percent = 10;
    assert(repair_blocker(rep) == "Battery 10 %: charge it or plug it in");
    rep.under_voltage = true;
    assert(repair_blocker(rep) == "Under-voltage: use a stronger power supply");
    rep.busy = "A launcher update is running";
    assert(repair_blocker(rep) == "A launcher update is running");
}

void argv_cases()
{
    const std::vector<std::string> argv = upgrade_argv("pi", {"curl", "libc6:armhf"}, true, "/h");
    assert((argv == std::vector<std::string>{"/usr/bin/systemd-run", "--unit=cyberdeckzero-apt", "--collect", "--quiet",
                                             "--description=CyberdeckZero package upgrade", "/h", "apt-upgrade", "--user",
                                             "pi", "--libraries", "--", "curl", "libc6:armhf"}));
    const std::vector<std::string> plain = upgrade_argv("pi", {"tzdata"}, false);
    assert(plain[5] == kHelperPath && plain[9] == "--" && plain[10] == "tzdata" && plain.size() == 11);
    assert(upgrade_argv("pi", {}, false).empty());
    assert(upgrade_argv("-pi", {"curl"}, false).empty() && upgrade_argv("p i", {"curl"}, false).empty());
    assert(upgrade_argv("pi", {"curl;reboot"}, false).empty() && upgrade_argv("pi", {"Curl"}, false).empty());
    assert(upgrade_argv("pi", {"--purge"}, false).empty() && upgrade_argv("pi", {"curl", ""}, false).empty());
    assert(upgrade_argv("pi", {"rpi-eeprom"}, false).empty());          /* D15: never the boot chain */
    assert(upgrade_argv("pi", {"linux-image-rpi-v8"}, true).empty());
    assert(upgrade_argv("pi", {"curl"}, false, "relative").empty());
    assert(upgrade_argv("pi", std::vector<std::string>(kMaxPackages + 1, "curl"), false).empty());
    assert(valid_package("libstdc++6") && valid_package("libc6:arm64") && valid_package("2048") && !valid_package("a:b:c") &&
           !valid_package("libc6:") && !valid_package(":arm64"));
    const std::vector<std::string> repair = repair_argv("pi");
    assert(repair.size() == 9 && repair[6] == "apt-repair" && repair[8] == "pi" && repair[1] == "--unit=cyberdeckzero-apt");
    assert(repair_argv("").empty());
}

void status_cases()
{
    const std::string text = "v=1\njob=upgrade\nstate=running\nphase=download\ndone=12\ntotal=40\npercent=31\n"
                             "text=Retrieving file 12 of 40\npackage=\nreason=\nreboot=0\nlibraries=1\ndownload=43000000\n"
                             "need=120000000\nuser=pi\npid=4000\nboot=b\nstarted=100\nupdated=130\nlog=/var/log/x.log\n";
    UpgradeStatus s = parse_upgrade_status(text);
    assert(s.present && s.job == "upgrade" && s.state == "running" && s.phase == "download" && s.done == 12 && s.total == 40 &&
           s.percent == 31 && s.libraries && s.download == 43000000 && s.need == 120000000 && s.pid == 4000 &&
           s.started == 100 && s.log == "/var/log/x.log");
    assert(!parse_upgrade_status("state=done\n").present && !parse_upgrade_status("v=1\n").present);
    assert(parse_upgrade_status("v=1\nstate=done\n").job == "upgrade");

    assert(upgrade_phase(s, true) == UpgradePhase::Running);
    assert(upgrade_phase(s, false) == UpgradePhase::None); /* stopped while downloading: nothing changed */
    assert(cancellable(s, UpgradePhase::Running) && !cancellable(s, UpgradePhase::Done));
    assert(progress_caption(s, false) == "Downloading 12 of 40" && progress_caption(s, true) == "Cancelling the download");
    assert(progress_percent(s) == 31);
    std::vector<StepLine> steps = progress_steps(s, 160);
    assert(steps.size() == 4 && steps[0].state == StepState::Done && steps[0].value == "done" &&
           steps[1].state == StepState::Current && steps[1].value == "12 of 40" && steps[2].state == StepState::Pending &&
           steps[2].value.empty());

    s.phase = "install";
    s.done = 3;
    s.percent = 8;
    assert(upgrade_phase(s, false) == UpgradePhase::Interrupted); /* stopped while dpkg worked */
    assert(!cancellable(s, UpgradePhase::Running));                 /* never while installing */
    assert(progress_caption(s, true) == "Installing 3 of 40" && progress_percent(s) == 8);
    steps = progress_steps(s, 160);
    assert(steps[1].state == StepState::Done && steps[2].state == StepState::Current && steps[2].value == "3 of 40");
    s.done = 99; /* never above the total */
    assert(progress_caption(s, false) == "Installing 40 of 40");
    s.phase = "clean";
    assert(progress_caption(s, false) == "Cleaning up" && progress_percent(s) == 100);
    s.phase = "check";
    assert(progress_caption(s, false) == "Checking the plan" && progress_percent(s) == 0 && cancellable(s, UpgradePhase::Running));
    s.total = -1;
    s.phase = "download";
    assert(progress_caption(s, false) == "Downloading");
    assert(std::string(do_not_switch_off()) == "Do not switch off");

    UpgradeStatus rep = parse_upgrade_status("v=1\njob=repair\nstate=running\nphase=repair\ntotal=0\nstarted=100\n");
    assert(progress_caption(rep, false) == "Finishing the interrupted installation" && progress_percent(rep) == -1);
    steps = progress_steps(rep, 145);
    assert(steps.size() == 1 && steps[0].label == "Finish the installation" && steps[0].value == "45 s");
    assert(upgrade_phase(rep, false) == UpgradePhase::Interrupted && !cancellable(rep, UpgradePhase::Running));

    assert(upgrade_phase(parse_upgrade_status("v=1\nstate=done\n"), false) == UpgradePhase::Done);
    assert(upgrade_phase(parse_upgrade_status("v=1\nstate=failed\n"), false) == UpgradePhase::Failed);
    assert(upgrade_phase(parse_upgrade_status("v=1\nstate=refused\n"), false) == UpgradePhase::Refused);
    assert(upgrade_phase(parse_upgrade_status("v=1\nstate=cancelled\n"), false) == UpgradePhase::Cancelled);
    assert(repair_needed(UpgradePhase::Interrupted, false) && repair_needed(UpgradePhase::Done, true) &&
           !repair_needed(UpgradePhase::Failed, false));
}

void result_cases()
{
    UpgradeStatus s = parse_upgrade_status("v=1\nstate=done\ntotal=40\nreboot=0\nlog=/var/log/cyberdeckzero/apt-upgrade-1.log\n");
    std::string t = result_text(s, UpgradePhase::Done, "tail");
    assert(t.find("Upgraded 40 packages.") == 0 && has(t, "Your apps keep running") && !has(t, "Restart") && !has(t, "tail") &&
           has(t, "Log: /var/log/cyberdeckzero/apt-upgrade-1.log"));
    s.reboot = true;
    s.reboot_why = "libc6";
    t = result_text(s, UpgradePhase::Done, "");
    assert(has(t, "Restart the board when it suits you (libc6 was upgraded)") && has(t, "Settings > System > Reboot") &&
           has(t, "never restarts the board by itself"));
    s.reboot_why = "a package asked for it";
    assert(has(result_text(s, UpgradePhase::Done, ""), "(a package asks for it)"));
    s.total = 1;
    assert(result_text(s, UpgradePhase::Done, "").find("Upgraded 1 package.") == 0);

    UpgradeStatus f = parse_upgrade_status("v=1\nstate=failed\nreason=E: Sub-process /usr/bin/dpkg returned an error code (1)\n"
                                           "log=/var/log/cyberdeckzero/apt-upgrade-2.log\n");
    t = result_text(f, UpgradePhase::Failed, "Setting up foo ...\ndpkg: error processing package foo\n");
    assert(t.find("The upgrade failed: E: Sub-process /usr/bin/dpkg returned an error code (1).") == 0 &&
           has(t, "apt cannot undo an upgrade") && has(t, "Repair") &&
           has(t, "The end of the log (/var/log/cyberdeckzero/apt-upgrade-2.log):\n\nSetting up foo") &&
           has(t, "dpkg: error processing package foo"));
    UpgradeStatus c = parse_upgrade_status("v=1\nstate=cancelled\nreason=the download was cancelled: nothing was installed\n");
    assert(result_text(c, UpgradePhase::Cancelled, "x").find("Cancelled: the download was cancelled: nothing was installed.") == 0);
    UpgradeStatus r = parse_upgrade_status("v=1\nstate=refused\nreason=battery 30 %: charge it or plug it in\n");
    assert(result_text(r, UpgradePhase::Refused, "").find(
               "The upgrade did not start: battery 30 %: charge it or plug it in. Nothing was changed.") == 0);
    UpgradeStatus rd = parse_upgrade_status("v=1\njob=repair\nstate=done\n");
    assert(result_text(rd, UpgradePhase::Done, "").find("The interrupted installation is finished.") == 0);
    UpgradeStatus rf = parse_upgrade_status("v=1\njob=repair\nstate=failed\nreason=dpkg still reports unfinished packages: see the log\n");
    assert(has(result_text(rf, UpgradePhase::Failed, ""), "sudo dpkg --configure -a"));

    assert(last_lines("a\nb\nc\nd\n", 2) == "c\nd\n" && last_lines("a\nb", 5) == "a\nb\n" && last_lines("", 3).empty());
}

void text_cases()
{
    Plan plan;
    plan.valid = true;
    plan.packages = std::vector<Package>(40, Package{"p", "1", "2", ""});
    plan.download = 41LL << 20;
    plan.growth = 15LL << 20;
    const std::string t = confirm_text(plan, false, false, 4);
    assert(t.find("40 packages: 41 MB to download, 120 MB of disk needed.") == 0);
    assert(has(t, "Keep the board plugged in until it is done."));
    assert(has(t, "Your apps keep running until the next restart; some updates need a reboot afterwards."));
    assert(has(t, "once installing starts it cannot, and apt cannot undo an upgrade"));
    assert(!has(t, "Launcher libraries included") && !has(t, "waits"));
    const std::string battery = confirm_text(plan, true, false, 4);
    assert(has(battery, "Keep the battery above 50 % or plug it in") && !has(battery, "plugged in until"));
    plan.waiting_libraries = 3;
    plan.waiting_terminal = 3;
    std::string on = confirm_text(plan, false, true, 4);
    assert(has(on, "Launcher libraries included (4): ") && has(on, "repair from a terminal") &&
           has(on, "3 more packages wait for the kernel, the firmware or a package apt keeps back"));
    std::string off = confirm_text(plan, false, false, 4);
    assert(has(off, "3 more packages wait for a launcher library: include the libraries to upgrade them too."));
    plan.waiting_libraries = 1;
    assert(has(confirm_text(plan, false, false, 4), "1 more package waits for a launcher library: include the libraries to upgrade it too."));
    plan.download = -1;
    assert(confirm_text(plan, false, false, 0).find("40 packages: download size unknown, 79 MB of disk needed.") == 0);
    Plan none;
    none.valid = true;
    assert(confirm_text(none, false, false, 4) == "Nothing to upgrade with these settings. Include the launcher libraries to upgrade them.");
    assert(confirm_text(none, false, true, 4) == "Nothing to upgrade with these settings.");
    assert(libraries_label(4) == "Launcher libraries (4)");
    assert(std::string(switch_value(true)) == "On" && std::string(switch_value(false)) == "Off");
    /* one status line at 480 px each; the warning says the libraries are included */
    assert(std::string(libraries_warning()).size() <= 40 && std::string(libraries_warning()).compare(0, 8, "Included") == 0);
    assert(std::string(libraries_off_line()).size() <= 40 && has(libraries_off_line(), "not included"));
}

} // namespace

int main(int argc, char **argv)
{
    g_fixtures = argc > 1 ? argv[1] : "fixtures/apt";
    plan_cases();
    size_cases();
    power_cases();
    guard_cases();
    argv_cases();
    status_cases();
    result_cases();
    text_cases();
    std::printf("settings_apt_upgrade_model: all passed\n");
    return 0;
}
