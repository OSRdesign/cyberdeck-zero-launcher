/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates > Packages, P4: the rules of the upgrade (see settings_apt_upgrade_model.hpp).
 */

#include "settings_apt_upgrade_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

namespace launcher_apt {

namespace {

constexpr long long kMiB = 1024LL * 1024LL;

std::vector<std::string> lines_of(const std::string &text)
{
    std::vector<std::string> lines;
    std::string line;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (c != '\r') {
            line += c;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::string trim(const std::string &text)
{
    std::size_t a = 0, b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) --b;
    return text.substr(a, b - a);
}

bool all_digits(const std::string &text)
{
    return !text.empty() && text.size() <= 18 &&
           std::all_of(text.begin(), text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

long long number_of(const std::string &text)
{
    return all_digits(text) ? std::atoll(text.c_str()) : -1;
}

bool plain_user(const std::string &name)
{
    if (name.empty() || name[0] == '-' || name.size() > 64) return false;
    for (char c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') return false;
    return true;
}

std::set<std::string> names_of(const std::vector<Package> &list)
{
    std::set<std::string> names;
    for (const Package &p : list) names.insert(p.name);
    return names;
}

std::string plural(int n, const char *one, const char *many)
{
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

} // namespace

// ------------------------------------------------------------------ the plan

std::vector<std::string> Plan::names() const
{
    std::vector<std::string> out;
    for (const Package &p : packages) out.push_back(p.name);
    return out;
}

long long Plan::need() const
{
    return disk_needed(download, growth, largest);
}

// ------------------------------------------------------------------ the closure

std::map<std::string, Record> parse_records(const std::string &text)
{
    std::map<std::string, Record> records;
    std::string name;
    Record record;
    auto flush = [&] {
        if (!name.empty() && !records.count(name)) records[name] = record;
        name.clear();
        record = Record();
    };
    for (const std::string &line : lines_of(text)) {
        if (line.empty()) {
            flush();
            continue;
        }
        if (line.compare(0, 9, "Package: ") == 0) {
            flush();
            name = base_name(trim(line.substr(9)));
        } else if (line.compare(0, 8, "Source: ") == 0) {
            std::string source = trim(line.substr(8));
            const std::size_t paren = source.find(' '); /* "curl (8.14.1-2+deb13u7)" */
            if (paren != std::string::npos) source = source.substr(0, paren);
            record.source = source;
        } else if (line.compare(0, 9, "Depends: ") == 0 || line.compare(0, 13, "Pre-Depends: ") == 0) {
            for (const std::string &d : parse_depends(line.substr(line.find(':') + 1))) record.depends.push_back(d);
        }
    }
    flush();
    return records;
}

std::string closure_problem(const Simulation &sim, const std::vector<std::string> &names)
{
    if (!sim.valid) return "apt gave no plan";
    if (!sim.removals.empty()) return "apt would remove " + base_name(sim.removals.front().name);
    if (!sim.installs.empty()) return "apt would install the new package " + base_name(sim.installs.front().name);
    const std::set<std::string> listed(names.begin(), names.end());
    std::set<std::string> upgraded;
    for (const Change &c : sim.upgrades) upgraded.insert(base_name(c.name));
    for (const std::string &u : upgraded)
        if (!listed.count(u)) return "apt would also upgrade " + u;
    for (const std::string &n : listed)
        if (!upgraded.count(n)) return n + " is no longer offered";
    return std::string();
}

namespace {

std::vector<std::string> sorted_unique(std::vector<std::string> names)
{
    for (std::string &n : names) n = base_name(n);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    names.erase(std::remove(names.begin(), names.end(), std::string()), names.end());
    return names;
}

/* what a simulation touches beyond the set `in`: upgrades of other packages, new packages, removals */
std::vector<std::string> outside_of(const Simulation &sim, const std::set<std::string> &in)
{
    std::vector<std::string> out;
    auto add = [&](const std::string &raw) {
        const std::string n = base_name(raw);
        if (!in.count(n) && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    };
    for (const Change &c : sim.upgrades) add(c.name);
    for (const Change &c : sim.installs) {
        const std::string n = base_name(c.name);
        if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    }
    for (const Change &c : sim.removals) {
        const std::string n = base_name(c.name);
        if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    }
    return out;
}

bool upgrades_name(const Simulation &sim, const std::string &name)
{
    for (const Change &c : sim.upgrades)
        if (base_name(c.name) == name) return true;
    return false;
}

} // namespace

Closure close_plan(const std::vector<std::string> &start, const std::function<Outside(const std::string &)> &kind,
                   const ClosureHooks &hooks)
{
    Closure c;
    std::vector<std::string> plan = sorted_unique(start);
    auto stop = [&] { return hooks.cancelled && hooks.cancelled(); };
    auto simulate = [&](const std::vector<std::string> &names) {
        ++c.simulations;
        Simulation sim = parse_simulation(hooks.simulate(names));
        if (hooks.progress) hooks.progress(c.simulations);
        return sim;
    };
    for (;;) {
        if (stop()) {
            c.error = "cancelled";
            return c;
        }
        if (plan.empty()) {
            c.valid = true;
            break;
        }
        if (c.simulations >= hooks.max_simulations) {
            c.error = "apt needed too many simulations";
            return c;
        }
        const Simulation sim = simulate(plan);
        const std::string problem = closure_problem(sim, plan);
        if (problem.empty()) {
            c.valid = true;
            c.changes = sim.upgrades;
            break;
        }
        const std::set<std::string> in(plan.begin(), plan.end());
        std::vector<Pull> found;
        // listed, and not upgraded by apt now (phasing, or no newer candidate any more): not now
        if (sim.valid)
            for (const std::string &p : plan)
                if (!upgrades_name(sim, p)) found.push_back({p, {}, true});
        const std::vector<std::string> outside = sim.valid ? outside_of(sim, in) : std::vector<std::string>();
        if (found.empty()) {
            // the suspects: the same source as a package pulled in, or a Depends on one (or on another suspect)
            std::vector<std::string> suspects;
            if (!outside.empty() && hooks.records) {
                std::vector<std::string> ask = plan;
                ask.insert(ask.end(), outside.begin(), outside.end());
                const std::map<std::string, Record> records = parse_records(hooks.records(ask));
                auto source_of = [&](const std::string &n) {
                    const auto it = records.find(n);
                    return it == records.end() || it->second.source.empty() ? n : it->second.source;
                };
                std::set<std::string> hot(outside.begin(), outside.end());
                std::set<std::string> hot_sources;
                for (const std::string &o : outside) hot_sources.insert(source_of(o));
                for (bool grew = true; grew;) {
                    grew = false;
                    for (const std::string &p : plan) {
                        if (std::find(suspects.begin(), suspects.end(), p) != suspects.end()) continue;
                        bool suspect = hot_sources.count(source_of(p)) > 0;
                        const auto it = records.find(p);
                        if (!suspect && it != records.end())
                            for (const std::string &d : it->second.depends)
                                if (hot.count(d)) suspect = true;
                        if (suspect) {
                            suspects.push_back(p);
                            hot.insert(p);
                            grew = true;
                        }
                    }
                }
            }
            auto check = [&](const std::vector<std::string> &candidates) {
                for (const std::string &p : candidates) {
                    if (stop() || c.simulations >= hooks.max_simulations) return;
                    const Simulation one = simulate({p});
                    if (!one.valid || !upgrades_name(one, p)) {
                        found.push_back({p, {}, true}); /* apt refuses it alone, or will not upgrade it now */
                        continue;
                    }
                    const std::vector<std::string> needs = outside_of(one, in);
                    if (needs.empty()) continue;
                    bool terminal = !one.removals.empty() || !one.installs.empty();
                    for (const std::string &n : needs)
                        if (kind(n) == Outside::Never) terminal = true;
                    found.push_back({p, needs, terminal});
                }
            };
            check(suspects);
            if (found.empty() && !stop()) { /* no suspect explains it: every other package, one by one */
                std::vector<std::string> others;
                for (const std::string &p : plan)
                    if (std::find(suspects.begin(), suspects.end(), p) == suspects.end()) others.push_back(p);
                check(others);
            }
        }
        if (stop()) {
            c.error = "cancelled";
            return c;
        }
        if (found.empty()) {
            c.error = problem; /* apt pulls in something no single package explains */
            return c;
        }
        for (const Pull &f : found) {
            c.pulls.push_back(f);
            plan.erase(std::remove(plan.begin(), plan.end(), f.name), plan.end());
        }
    }
    c.names = plan;
    return c;
}

namespace {

/* takes the entry `name` out of whichever list holds it (safe, libraries); false when none does */
bool take_out(Report &report, const std::string &name, Package &out)
{
    for (std::vector<Package> *list : {&report.safe, &report.libraries}) {
        for (auto it = list->begin(); it != list->end(); ++it)
            if (it->name == name) {
                out = *it;
                list->erase(it);
                return true;
            }
    }
    return false;
}

std::string needs_note(const Pull &pull)
{
    if (pull.needs.empty()) return "not upgraded by apt now";
    std::string note = "needs ";
    for (std::size_t i = 0; i < pull.needs.size() && i < 3; ++i) note += (i ? ", " : "") + pull.needs[i];
    if (pull.needs.size() > 3) note += ", ...";
    return note;
}

void sort_packages(std::vector<Package> &list)
{
    std::sort(list.begin(), list.end(), [](const Package &a, const Package &b) { return a.name < b.name; });
}

Plan plan_of(const Closure &closure, const Report &report, bool libraries)
{
    Plan plan;
    plan.ready = true;
    plan.libraries = libraries;
    plan.valid = closure.valid;
    plan.error = closure.error;
    if (!closure.valid) return plan;
    for (const std::string &n : closure.names) {
        Package p{n, "", "", ""};
        for (const Change &c : closure.changes)
            if (base_name(c.name) == n) {
                p.from = c.from;
                p.to = c.to;
            }
        if (p.to.empty())
            for (const std::vector<Package> *list : {&report.safe, &report.libraries})
                for (const Package &q : *list)
                    if (q.name == n) p = q;
        plan.packages.push_back(p);
    }
    sort_packages(plan.packages);
    return plan;
}

} // namespace

Planned plan_upgrade(const Report &report, const ClosureHooks &hooks)
{
    Planned out;
    out.report = report;
    const std::set<std::string> libraries = names_of(report.libraries);
    std::vector<std::string> safe;
    for (const Package &p : report.safe) safe.push_back(p.name);

    // 1. the safe set, closed against the launcher libraries
    Closure first = close_plan(safe, [&](const std::string &n) { return libraries.count(n) ? Outside::Library : Outside::Never; },
                               hooks);
    std::vector<Pull> to_libraries, to_terminal;
    for (const Pull &p : first.pulls) (p.terminal ? to_terminal : to_libraries).push_back(p);

    // 2. with the libraries: the safe plan, the libraries and what moved to them, closed against everything else
    std::vector<std::string> all = first.names;
    all.insert(all.end(), libraries.begin(), libraries.end());
    for (const Pull &p : to_libraries) all.push_back(p.name);
    Closure second = close_plan(all, [](const std::string &) { return Outside::Never; }, hooks);
    for (const Pull &p : second.pulls) {
        to_terminal.push_back(p);
        // a library pulling the boot chain moves to the terminal; so does a safe package only the combination breaks
        to_libraries.erase(std::remove_if(to_libraries.begin(), to_libraries.end(),
                                          [&](const Pull &q) { return q.name == p.name; }),
                           to_libraries.end());
    }
    bool rerun = false;
    for (const Pull &p : second.pulls)
        if (std::find(first.names.begin(), first.names.end(), p.name) != first.names.end()) rerun = true;
    if (rerun && first.valid) { /* rare: the safe plan itself lost a package in the combination: close it again */
        std::vector<std::string> fewer;
        for (const std::string &n : first.names) {
            bool gone = false;
            for (const Pull &p : second.pulls) gone = gone || p.name == n;
            if (!gone) fewer.push_back(n);
        }
        const std::vector<Pull> earlier = first.pulls;
        first = close_plan(fewer, [&](const std::string &n) { return libraries.count(n) ? Outside::Library : Outside::Never; },
                           hooks);
        for (const Pull &p : first.pulls) (p.terminal ? to_terminal : to_libraries).push_back(p);
        first.pulls.insert(first.pulls.begin(), earlier.begin(), earlier.end());
    }

    // 3. the report as the page shows it: the moved packages in their new section
    for (const Pull &p : to_libraries) {
        Package entry;
        if (!take_out(out.report, p.name, entry)) continue;
        if (!libraries.count(p.name)) entry.note = needs_note(p);
        out.report.libraries.push_back(entry);
    }
    for (const Pull &p : to_terminal) {
        Package entry;
        if (!take_out(out.report, p.name, entry)) continue;
        entry.note = needs_note(p);
        out.report.terminal.push_back(entry);
    }
    sort_packages(out.report.libraries);
    sort_packages(out.report.terminal);

    out.plan = plan_of(first, report, false);
    out.plan.waiting_libraries = static_cast<int>(to_libraries.size());
    out.plan_libraries = plan_of(second, report, true);
    out.plan_libraries.waiting_terminal = static_cast<int>(to_terminal.size());
    return out;
}

long long parse_uris_bytes(const std::string &text, int expected)
{
    long long total = 0;
    for (const std::string &line : lines_of(text)) {
        if (line.empty() || line[0] != '\'') continue;
        const std::size_t close = line.find('\'', 1);
        if (close == std::string::npos) continue;
        // 'URI' FILE SIZE HASH
        std::string rest = trim(line.substr(close + 1));
        const std::size_t sp = rest.find(' ');
        if (sp == std::string::npos) continue;
        rest = trim(rest.substr(sp + 1));
        const std::size_t sp2 = rest.find(' ');
        const long long size = number_of(rest.substr(0, sp2));
        if (size < 0) continue;
        total += size;
    }
    if (total == 0 && expected > 0 && text.find('\'') == std::string::npos) {
        // nothing printed: everything is downloaded already, or apt failed. An E: line says which.
        return text.find("E:") != std::string::npos ? -1 : 0;
    }
    return total;
}

void parse_candidate_sizes(const std::string &text, long long &sum_kib, long long &largest_kib)
{
    sum_kib = 0;
    largest_kib = 0;
    for (const std::string &line : lines_of(text)) {
        if (line.compare(0, 15, "Installed-Size:") != 0) continue;
        const long long kib = number_of(trim(line.substr(15)));
        if (kib < 0) continue;
        sum_kib += kib;
        largest_kib = std::max(largest_kib, kib);
    }
}

long long parse_installed_kib(const std::string &text)
{
    long long sum = 0;
    for (const std::string &line : lines_of(text)) {
        const long long kib = number_of(trim(line));
        if (kib > 0) sum += kib;
    }
    return sum;
}

void apply_sizes(Plan &plan, const std::string &uris, const std::string &candidates, const std::string &installed)
{
    plan.download = parse_uris_bytes(uris, plan.count());
    long long sum = 0, largest = 0;
    parse_candidate_sizes(candidates, sum, largest);
    const long long old = parse_installed_kib(installed);
    plan.growth = std::max(0LL, sum - old) * 1024;
    plan.largest = largest * 1024;
}

long long disk_needed(long long download, long long growth, long long largest)
{
    return std::max(0LL, download) + std::max(0LL, growth) + std::max(kMarginBytes, largest);
}

std::vector<std::string> parse_depends(const std::string &field)
{
    std::vector<std::string> out;
    std::string item;
    auto flush = [&] {
        std::string name = trim(item);
        item.clear();
        const std::size_t paren = name.find('(');
        if (paren != std::string::npos) name = trim(name.substr(0, paren));
        name = base_name(name);
        if (!name.empty() && std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
    };
    for (char c : field) {
        if (c == ',' || c == '|' || c == '\n') flush();
        else item += c;
    }
    flush();
    return out;
}

// ------------------------------------------------------------------ power

Throttle parse_throttled(const std::string &raw)
{
    Throttle t;
    std::string text = trim(raw);
    const std::size_t equal = text.find('=');
    if (equal != std::string::npos) text = trim(text.substr(equal + 1));
    if (text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0) text = text.substr(2);
    if (text.empty() || text.size() > 8 ||
        !std::all_of(text.begin(), text.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
        return t;
    const unsigned long bits = std::strtoul(text.c_str(), nullptr, 16);
    t.known = true;
    t.under_now = (bits & 0x1UL) != 0;
    t.under_before = (bits & 0x10000UL) != 0;
    return t;
}

// ------------------------------------------------------------------ guards

bool battery_low(int battery_percent, bool charging)
{
    return battery_percent >= 0 && battery_percent < kBatteryFloor && !charging;
}

std::string megabytes_text(long long bytes)
{
    if (bytes <= 0) return "0 MB";
    return std::to_string(std::max(1LL, (bytes + kMiB / 2) / kMiB)) + " MB";
}

namespace {

/* the guards both an upgrade and a repair have: the helper, another job, the power */
std::string common_blocker(const UpgradeGuards &g)
{
    if (!g.helper_installed) return "No update helper: use a terminal";
    if (!g.busy.empty()) return g.busy;
    if (g.under_voltage) return "Under-voltage: use a stronger power supply";
    if (battery_low(g.battery_percent, g.charging))
        return "Battery " + std::to_string(g.battery_percent) + " %: charge it or plug it in";
    return std::string();
}

} // namespace

std::string upgrade_blocker(const UpgradeGuards &g)
{
    if (!g.helper_installed) return "No update helper: use a terminal";
    if (!g.busy.empty()) return g.busy;
    if (g.repair_needed) return "Repair the interrupted installation first";
    if (g.packages <= 0) return "Nothing to upgrade";
    if (g.lists_time <= 0) return "Lists never refreshed: refresh them first";
    const long long days = lists_age_days(g.lists_time, g.now);
    if (days > kStaleDays) return "Lists are " + std::to_string(days) + " days old: refresh them first";
    const std::string power = common_blocker(g);
    if (!power.empty()) return power;
    if (g.free_bytes >= 0 && g.free_bytes < g.need_bytes)
        return "Not enough space: need " + megabytes_text(g.need_bytes) + ", " + megabytes_text(g.free_bytes) + " free";
    if (!g.network) return "No network";
    return std::string();
}

std::string repair_blocker(const UpgradeGuards &g)
{
    return common_blocker(g);
}

// ------------------------------------------------------------------ the root command

bool valid_package(const std::string &name)
{
    if (name.empty() || name.size() > 121) return false;
    const std::size_t colon = name.find(':');
    const std::string base = name.substr(0, colon);
    if (base.empty() || base.size() > 100) return false;
    if (!(std::islower(static_cast<unsigned char>(base[0])) || std::isdigit(static_cast<unsigned char>(base[0]))))
        return false;
    for (char c : base)
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '+' ||
              c == '.' || c == '-'))
            return false;
    if (colon == std::string::npos) return true;
    const std::string arch = name.substr(colon + 1);
    if (arch.empty() || arch.size() > 20) return false;
    return std::all_of(arch.begin(), arch.end(), [](char c) {
        return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c));
    });
}

std::vector<std::string> upgrade_argv(const std::string &user, const std::vector<std::string> &packages, bool libraries,
                                      const std::string &helper)
{
    if (!plain_user(user) || helper.empty() || helper[0] != '/' || packages.empty() || packages.size() > kMaxPackages)
        return {};
    for (const std::string &p : packages)
        if (!valid_package(p) || is_boot_chain(p)) return {};
    std::vector<std::string> argv = {"/usr/bin/systemd-run", std::string("--unit=") + kRefreshUnit, "--collect", "--quiet",
                                     "--description=CyberdeckZero package upgrade", helper, "apt-upgrade", "--user", user};
    if (libraries) argv.push_back("--libraries");
    argv.push_back("--");
    argv.insert(argv.end(), packages.begin(), packages.end());
    return argv;
}

std::vector<std::string> repair_argv(const std::string &user, const std::string &helper)
{
    if (!plain_user(user) || helper.empty() || helper[0] != '/') return {};
    return {"/usr/bin/systemd-run", std::string("--unit=") + kRefreshUnit, "--collect", "--quiet",
            "--description=CyberdeckZero package repair", helper, "apt-repair", "--user", user};
}

// ------------------------------------------------------------------ the helper's status file

UpgradeStatus parse_upgrade_status(const std::string &text)
{
    UpgradeStatus s;
    bool v1 = false;
    auto small = [](const std::string &value) {
        const long long n = number_of(value);
        return n < 0 ? -1 : static_cast<int>(std::min(1000000LL, n));
    };
    for (const std::string &line : lines_of(text)) {
        const std::size_t equal = line.find('=');
        if (equal == std::string::npos || equal == 0) continue;
        const std::string key = line.substr(0, equal), value = line.substr(equal + 1);
        if (key == "v") v1 = value == "1";
        else if (key == "job") s.job = value;
        else if (key == "state") s.state = value;
        else if (key == "phase") s.phase = value;
        else if (key == "done") s.done = small(value);
        else if (key == "total") s.total = small(value);
        else if (key == "percent") s.percent = value.empty() ? -1 : std::min(100, small(value));
        else if (key == "text") s.text = value;
        else if (key == "package") s.package = value;
        else if (key == "reason") s.reason = value;
        else if (key == "reboot") s.reboot = value == "1";
        else if (key == "reboot_why") s.reboot_why = value;
        else if (key == "libraries") s.libraries = value == "1";
        else if (key == "download") s.download = number_of(value);
        else if (key == "need") s.need = number_of(value);
        else if (key == "user") s.user = value;
        else if (key == "log") s.log = value;
        else if (key == "boot") s.boot = value;
        else if (key == "pid") s.pid = std::max(0LL, number_of(value));
        else if (key == "started") s.started = std::max(0LL, number_of(value));
        else if (key == "updated") s.updated = std::max(0LL, number_of(value));
    }
    if (s.job.empty()) s.job = "upgrade";
    s.present = v1 && !s.state.empty();
    return s;
}

UpgradePhase upgrade_phase(const UpgradeStatus &s, bool alive)
{
    if (!s.present) return UpgradePhase::None;
    if (s.state == "running") {
        if (alive) return UpgradePhase::Running;
        return s.phase == "install" || s.phase == "repair" ? UpgradePhase::Interrupted : UpgradePhase::None;
    }
    if (s.state == "done") return UpgradePhase::Done;
    if (s.state == "failed") return UpgradePhase::Failed;
    if (s.state == "refused") return UpgradePhase::Refused;
    if (s.state == "cancelled") return UpgradePhase::Cancelled;
    return UpgradePhase::None;
}

bool repair_needed(UpgradePhase phase, bool dpkg_unfinished)
{
    return phase == UpgradePhase::Interrupted || dpkg_unfinished;
}

bool cancellable(const UpgradeStatus &s, UpgradePhase phase)
{
    return phase == UpgradePhase::Running && s.job == "upgrade" && (s.phase == "download" || s.phase == "check");
}

namespace {

std::string count_of(const UpgradeStatus &s)
{
    if (s.total <= 0) return std::string();
    return std::to_string(std::max(0, std::min(s.done, s.total))) + " of " + std::to_string(s.total);
}

} // namespace

std::string progress_caption(const UpgradeStatus &s, bool cancel_asked)
{
    if (s.job == "repair") return "Finishing the interrupted installation";
    if (s.phase == "download") {
        if (cancel_asked) return "Cancelling the download";
        const std::string n = count_of(s);
        return n.empty() ? std::string("Downloading") : "Downloading " + n;
    }
    if (s.phase == "install") {
        const std::string n = count_of(s);
        return n.empty() ? std::string("Installing") : "Installing " + n;
    }
    if (s.phase == "clean") return "Cleaning up";
    return cancel_asked ? "Cancelling" : "Checking the plan";
}

int progress_percent(const UpgradeStatus &s)
{
    if (s.job == "repair") return -1;
    if (s.phase == "download" || s.phase == "install") return std::max(0, std::min(100, s.percent));
    if (s.phase == "clean") return 100;
    return 0;
}

std::vector<StepLine> progress_steps(const UpgradeStatus &s, long long now)
{
    const long long seconds = s.started > 0 && now >= s.started ? now - s.started : -1;
    if (s.job == "repair") {
        StepLine line{"Finish the installation", seconds >= 0 ? std::to_string(seconds) + " s" : std::string(),
                      StepState::Current};
        if (s.total > 0) line.value = count_of(s);
        return {line};
    }
    std::vector<StepLine> steps = {{"Check the plan", "", StepState::Pending},
                                   {"Download", "", StepState::Pending},
                                   {"Install", "", StepState::Pending},
                                   {"Clean up", "", StepState::Pending}};
    int current = 0;
    if (s.phase == "download") current = 1;
    else if (s.phase == "install") current = 2;
    else if (s.phase == "clean") current = 3;
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        StepLine &step = steps[static_cast<std::size_t>(i)];
        if (i < current) {
            step.state = StepState::Done;
            step.value = "done";
        } else if (i == current) {
            step.state = StepState::Current;
            if (i == 1 || i == 2) step.value = count_of(s);
        }
    }
    return steps;
}

const char *do_not_switch_off()
{
    return "Do not switch off";
}

std::string last_lines(const std::string &text, std::size_t count)
{
    const std::vector<std::string> lines = lines_of(text);
    const std::size_t from = lines.size() > count ? lines.size() - count : 0;
    std::string out;
    for (std::size_t i = from; i < lines.size(); ++i) out += lines[i] + "\n";
    return out;
}

std::string result_text(const UpgradeStatus &s, UpgradePhase phase, const std::string &log_tail)
{
    const bool repair = s.job == "repair";
    const int total = std::max(0, s.total);
    std::string text;
    switch (phase) {
    case UpgradePhase::Done:
        text = repair ? std::string("The interrupted installation is finished.")
                      : "Upgraded " + plural(total, "package", "packages") + ".";
        if (s.reboot)
            text += "\n\nRestart the board when it suits you (" +
                    std::string(s.reboot_why == "a package asked for it" || s.reboot_why.empty()
                                    ? "a package asks for it"
                                    : s.reboot_why + " was upgraded") +
                    "): Settings > System > Reboot. The launcher never restarts the board by itself.";
        else
            text += "\n\nYour apps keep running; a program uses its new version the next time it starts.";
        break;
    case UpgradePhase::Cancelled:
        text = s.reason.empty() ? std::string("Cancelled: nothing was installed.") : "Cancelled: " + s.reason + ".";
        break;
    case UpgradePhase::Refused:
        text = std::string(repair ? "The repair did not start: " : "The upgrade did not start: ") +
               (s.reason.empty() ? std::string("see the log") : s.reason) + ". Nothing was changed.";
        break;
    case UpgradePhase::Failed:
        text = std::string(repair ? "The repair failed: " : "The upgrade failed: ") +
               (s.reason.empty() ? std::string("see the log") : s.reason) + ".";
        text += repair ? "\n\nFinish it in a terminal: sudo dpkg --configure -a, then sudo apt-get -f install."
                       : "\n\nPackages that were installed stay installed: apt cannot undo an upgrade. Packages offers "
                         "Repair when dpkg left something unfinished.";
        break;
    case UpgradePhase::Interrupted:
        text = "The installation was interrupted (the power, a crash): Repair finishes it.";
        break;
    case UpgradePhase::Running:
    case UpgradePhase::None:
        text = "No result yet.";
        break;
    }
    // a failure shows the end of the log (a refusal changed nothing: its reason says it all)
    if ((phase == UpgradePhase::Failed || phase == UpgradePhase::Interrupted) && !log_tail.empty())
        text += "\n\nThe end of the log" + (s.log.empty() ? std::string() : " (" + s.log + ")") + ":\n\n" + log_tail;
    else if (!s.log.empty())
        text += "\n\nLog: " + s.log;
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    return text;
}

// ------------------------------------------------------------------ the Packages page and the confirmation page

std::string upgrade_label(const Plan &plan)
{
    if (plan.count() <= 0) return "Upgrade packages";
    return "Upgrade " + plural(plan.count(), "package", "packages");
}

std::string upgrade_value(const Plan &plan)
{
    if (!plan.valid || plan.count() <= 0 || plan.download < 0) return std::string();
    // the download alone: beside "Upgrade 234 packages" a longer value ("N, XX MB download") cuts the label at 640 px
    // (a list value is never cut, the label is)
    return megabytes_text(plan.download);
}

std::string packages_line(const Report &report)
{
    if (report.boot.empty() && report.terminal.empty()) return std::string();
    return "Kernel and kept-back ones: in a terminal";
}

std::string libraries_label(std::size_t libraries)
{
    // short enough for the dense row beside "Off" and the chevron at every size (480x320 included)
    return "Launcher libraries (" + std::to_string(libraries) + ")";
}

const char *switch_value(bool on)
{
    return on ? "On" : "Off";
}

const char *libraries_warning()
{
    // one status line at 480 px; the text block says the rest (D14)
    return "Included: the launcher runs on these";
}

const char *libraries_off_line()
{
    return "Launcher libraries are not included";
}

std::string confirm_text(const Plan &plan, bool battery_board, bool libraries_on, std::size_t libraries)
{
    std::string text;
    if (plan.count() <= 0) {
        text = "Nothing to upgrade with these settings.";
        if (!libraries_on && libraries > 0) text += " Include the launcher libraries to upgrade them.";
        return text;
    }
    text = plural(plan.count(), "package", "packages") + ": " +
           (plan.download < 0 ? std::string("download size unknown") : megabytes_text(plan.download) + " to download") +
           ", " + megabytes_text(plan.need()) + " of disk needed.\n\n";
    text += battery_board ? "Keep the battery above 50 % or plug it in until it is done.\n\n"
                          : "Keep the board plugged in until it is done.\n\n";
    text += "Your apps keep running until the next restart; some updates need a reboot afterwards.\n\n";
    text += "The download can be cancelled; once installing starts it cannot, and apt cannot undo an upgrade.";
    if (libraries_on && libraries > 0)
        text += "\n\nLauncher libraries included (" + std::to_string(libraries) +
                "): Debian keeps them compatible and they carry security fixes, but the launcher and its services run "
                "on them. If one breaks, the launcher may only start again after a repair from a terminal.";
    const int waiting = libraries_on ? plan.waiting_terminal : plan.waiting_libraries;
    if (waiting > 0) {
        const bool one = waiting == 1;
        text += "\n\n" + plural(waiting, "more package waits", "more packages wait") +
                (libraries_on ? std::string(" for the kernel, the firmware or a package apt keeps back: see Needs the "
                                            "terminal.")
                              : std::string(" for a launcher library: include the libraries to upgrade ") +
                                    (one ? "it" : "them") + " too.");
    }
    return text;
}

} // namespace launcher_apt
