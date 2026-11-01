/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates > System packages: the rules (see settings_apt_model.hpp).
 */

#include "settings_apt_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace launcher_apt {

namespace {

std::string trim(const std::string &text)
{
    std::size_t a = 0, b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) --b;
    return text.substr(a, b - a);
}

bool starts_with(const std::string &text, const char *prefix)
{
    return text.rfind(prefix, 0) == 0;
}

std::vector<std::string> split_lines(const std::string &text)
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

std::vector<std::string> words(const std::string &text)
{
    std::vector<std::string> out;
    std::string word;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!word.empty()) out.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) out.push_back(word);
    return out;
}

/* "Inst NAME [OLD] (NEW origin [arch]) []" and "Remv NAME [OLD] []": the fields after the verb. */
bool parse_change(const std::string &line, std::size_t verb_len, Change &out)
{
    std::size_t i = verb_len;
    while (i < line.size() && line[i] == ' ') ++i;
    std::size_t j = i;
    while (j < line.size() && line[j] != ' ') ++j;
    if (j == i) return false;
    out = Change();
    out.name = line.substr(i, j - i);
    i = j;
    while (i < line.size() && line[i] == ' ') ++i;
    if (i < line.size() && line[i] == '[') {
        const std::size_t close = line.find(']', i);
        if (close == std::string::npos) return false;
        out.from = line.substr(i + 1, close - i - 1);
        i = close + 1;
        while (i < line.size() && line[i] == ' ') ++i;
    }
    if (i < line.size() && line[i] == '(') {
        std::size_t k = i + 1;
        while (k < line.size() && line[k] != ' ' && line[k] != ')') ++k;
        out.to = line.substr(i + 1, k - i - 1);
    }
    return true;
}

bool name_char(char c)
{
    return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '+' ||
           c == '.' || c == '-';
}

bool valid_package_name(const std::string &name)
{
    if (name.size() < 2 || !(std::islower(static_cast<unsigned char>(name[0])) || std::isdigit(static_cast<unsigned char>(name[0]))))
        return false;
    return std::all_of(name.begin(), name.end(), name_char);
}

/* name == base, or the same with Debian's "t64" suffix (libglib2.0-0 / libglib2.0-0t64) */
bool is_base(const std::string &name, const char *base)
{
    const std::string b = base;
    return name == b || name == b + "t64";
}

bool has_prefix(const std::string &name, const char *prefix)
{
    return starts_with(name, prefix);
}

void sort_by_name(std::vector<Package> &list)
{
    std::sort(list.begin(), list.end(), [](const Package &a, const Package &b) { return a.name < b.name; });
}

const Change *find_change(const std::vector<Change> &list, const std::string &name)
{
    for (const Change &c : list)
        if (base_name(c.name) == name) return &c;
    return nullptr;
}

} // namespace

// ------------------------------------------------------------------ the simulation

std::string base_name(const std::string &name)
{
    const std::size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(0, colon);
}

Simulation parse_simulation(const std::string &text)
{
    Simulation sim;
    bool kept_section = false;
    bool phased_section = false;
    bool any = false;
    for (const std::string &line : split_lines(text)) {
        if (kept_section || phased_section) {
            if (!line.empty() && line[0] == ' ') {
                for (const std::string &name : words(line))
                    (kept_section ? sim.kept_back : sim.phased).push_back(base_name(name));
                continue;
            }
            kept_section = phased_section = false;
        }
        if (starts_with(line, "The following packages have been kept back:") ||
            starts_with(line, "The following package has been kept back:")) {
            kept_section = true;
            continue;
        }
        // apt 2.7+ (Ubuntu 26.04): phased updates are deferred, not kept back; they come by themselves later
        if (starts_with(line, "The following upgrades have been deferred due to phasing:") ||
            starts_with(line, "The following upgrade has been deferred due to phasing:")) {
            phased_section = true;
            continue;
        }
        Change change;
        if (starts_with(line, "Inst ") && parse_change(line, 5, change)) {
            (change.from.empty() ? sim.installs : sim.upgrades).push_back(change);
            any = true;
        } else if (starts_with(line, "Remv ") && parse_change(line, 5, change)) {
            sim.removals.push_back(change);
            any = true;
        } else {
            int up = 0, added = 0, remove = 0, kept = 0;
            if (std::sscanf(line.c_str(), "%d upgraded, %d newly installed, %d to remove and %d not upgraded.", &up,
                            &added, &remove, &kept) == 4) {
                sim.summary_upgraded = up;
                sim.summary_new = added;
                sim.summary_remove = remove;
                sim.summary_kept = kept;
                any = true;
            }
        }
    }
    sim.valid = any;
    return sim;
}

// ------------------------------------------------------------------ owners, libraries, apps

std::vector<std::string> parse_owners(const std::string &output)
{
    std::vector<std::string> names;
    for (const std::string &line : split_lines(output)) {
        const std::size_t colon = line.find(": /");
        if (colon == std::string::npos || starts_with(line, "diversion") || starts_with(line, "dpkg-query")) continue;
        std::string head = line.substr(0, colon);
        std::size_t start = 0;
        for (;;) {
            const std::size_t comma = head.find(", ", start);
            std::string name = trim(head.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            name = base_name(name);
            if (valid_package_name(name) && std::find(names.begin(), names.end(), name) == names.end())
                names.push_back(name);
            if (comma == std::string::npos) break;
            start = comma + 2;
        }
    }
    return names;
}

std::vector<std::string> ldd_paths(const std::string &output)
{
    std::vector<std::string> paths;
    for (const std::string &line : split_lines(output)) {
        const std::size_t arrow = line.find("=> ");
        if (arrow == std::string::npos) continue;
        std::size_t i = arrow + 3;
        std::size_t j = i;
        while (j < line.size() && line[j] != ' ') ++j;
        const std::string path = line.substr(i, j - i);
        if (!path.empty() && path[0] == '/' && std::find(paths.begin(), paths.end(), path) == paths.end())
            paths.push_back(path);
    }
    return paths;
}

std::string dpkg_path(const std::string &real_path)
{
    if (starts_with(real_path, "/lib/")) return "/usr" + real_path;
    return real_path;
}

std::vector<std::string> parse_installed_packages(const std::string &json)
{
    // the record's own "package" keys, as uninstall.sh reads them: no JSON parser needed for one string field
    std::vector<std::string> names;
    const std::string key = "\"package\"";
    std::size_t at = 0;
    while ((at = json.find(key, at)) != std::string::npos) {
        std::size_t i = at + key.size();
        at = i;
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
        if (i >= json.size() || json[i] != ':') continue;
        ++i;
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
        if (i >= json.size() || json[i] != '"') continue;
        const std::size_t close = json.find('"', i + 1);
        if (close == std::string::npos) break;
        const std::string name = json.substr(i + 1, close - i - 1);
        if (valid_package_name(name) && std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(name);
        at = close + 1;
    }
    return names;
}

bool is_boot_chain(const std::string &raw)
{
    const std::string name = base_name(raw);
    static const char *const prefixes[] = {
        "linux-image-", "linux-modules-", "linux-headers-", "linux-kbuild-", "linux-base", "linux-firmware",
        "firmware-", "armbian-firmware", "armbian-bsp", "grub-", "shim", "raspi-firmware", "raspberrypi-bootloader", "raspberrypi-kernel",
        "rpi-eeprom", "rpi-update", "initramfs-tools", "dracut", "u-boot-", "flash-kernel", "intel-microcode",
        "amd64-microcode", "bluez-firmware",
    };
    if (name == "u-boot-tools") return false; /* userland tools (mkimage), not the boot loader */
    for (const char *prefix : prefixes)
        if (has_prefix(name, prefix)) return true;
    return false;
}

bool is_static_library(const std::string &raw)
{
    const std::string name = base_name(raw);
    // what the binaries need on any board (report 047 section 7.4.1: the DT_NEEDED of the launcher, the Store, the shim)
    static const char *const bases[] = {
        "libc6", "libc-bin", "libstdc++6", "libgcc-s1", "libinput10", "libudev1", "libevdev2", "libmtdev1", "libwacom9",
        "libxkbcommon0", "libglib2.0-0", "libfreetype6", "libpng16-16", "zlib1g", "libbrotli1", "libbz2-1.0",
        "libssl3", "libcurl4", "libgudev-1.0-0", "libatomic1", "libffi8", "libpcre2-8-0", "libmount1", "libblkid1",
        "libselinux1", "libcap2",
    };
    for (const char *base : bases)
        if (is_base(name, base)) return true;
    // curl (the launcher runs it), the tools and services it calls, the session (class N), the display managers
    static const char *const exact[] = {
        "curl", "ca-certificates", "sudo", "systemd", "systemd-sysv", "libsystemd0", "libsystemd-shared", "udev", "dbus",
        "dbus-bin", "dbus-daemon", "dbus-system-bus-common", "libdbus-1-3", "polkitd", "policykit-1",
        "libpolkit-gobject-1-0", "libpolkit-agent-1-0", "network-manager", "libnm0", "wpasupplicant", "bluez", "rfkill",
        "alsa-utils", "libasound2", "pulseaudio-utils", "kbd", "dpkg", "apt", "libapt-pkg7.0", "device-tree-compiler",
        "lightdm", "gdm3", "sddm", "labwc", "xserver-xorg-core",
    };
    for (const char *e : exact)
        if (is_base(name, e)) return true;
    return false;
}

// ------------------------------------------------------------------ the classified plan

int Report::shown() const
{
    return static_cast<int>(safe.size() + libraries.size() + boot.size() + terminal.size());
}

Report classify(const Simulation &upgrade, const Simulation &dist, const Board &board)
{
    Report report;
    report.valid = upgrade.valid;
    if (!upgrade.valid) return report;

    auto is_app = [&](const std::string &name) { return board.apps.count(base_name(name)) > 0; };
    auto is_library = [&](const std::string &name) {
        return board.libraries.count(base_name(name)) > 0 || is_static_library(name);
    };
    auto add_store = [&](const std::string &name) {
        const std::string base = base_name(name);
        if (std::find(report.store.begin(), report.store.end(), base) == report.store.end()) report.store.push_back(base);
    };

    auto place = [&](const Change &c) {
        if (is_app(c.name)) {
            add_store(c.name);
            if (!c.from.empty()) report.store_versions.push_back({base_name(c.name), c.from, c.to, ""});
            return;
        }
        Package p;
        p.name = base_name(c.name);
        p.from = c.from;
        p.to = c.to;
        if (is_boot_chain(c.name)) report.boot.push_back(p);
        else if (is_library(c.name)) report.libraries.push_back(p);
        else report.safe.push_back(p);
    };
    for (const Change &c : upgrade.upgrades) place(c);
    for (const Change &c : upgrade.installs) place(c);
    for (const std::string &name : upgrade.phased)
        if (!is_app(name)) report.phased.push_back(base_name(name));

    // what `upgrade` keeps back (a new package is needed, or one would go): the terminal's job. `dist-upgrade` tells the
    // versions and the removals; it is only read when something was kept back.
    const bool kept = !upgrade.kept_back.empty();
    for (const std::string &raw : upgrade.kept_back) {
        const std::string name = base_name(raw);
        if (is_app(name)) {
            add_store(name);
            continue;
        }
        Package p;
        p.name = name;
        p.note = "kept back";
        if (const Change *c = dist.valid ? find_change(dist.upgrades, name) : nullptr) {
            p.from = c->from;
            p.to = c->to;
        }
        (is_boot_chain(name) ? report.boot : report.terminal).push_back(p);
        report.needs_dist_upgrade = true;
    }
    if (kept && dist.valid) {
        for (const Change &c : dist.installs) {
            if (is_app(c.name) || !is_boot_chain(c.name)) continue; /* a new kernel package: part of the boot chain */
            Package p;
            p.name = base_name(c.name);
            p.to = c.to;
            p.note = "new package";
            report.boot.push_back(p);
            report.needs_dist_upgrade = true;
        }
        for (const Change &c : dist.removals) {
            const std::string name = base_name(c.name);
            Package *existing = nullptr; /* kept back and removed by dist-upgrade: one row, the removal is what matters */
            for (Package &t : report.terminal)
                if (t.name == name) existing = &t;
            Package p;
            Package &row = existing ? *existing : p;
            row.name = name;
            row.from = c.from;
            row.to.clear();
            row.note = "will be removed";
            if (!existing) report.terminal.push_back(p);
            report.needs_dist_upgrade = true;
        }
    }
    sort_by_name(report.safe);
    sort_by_name(report.libraries);
    sort_by_name(report.boot);
    sort_by_name(report.terminal);
    std::sort(report.store.begin(), report.store.end());
    return report;
}

// ------------------------------------------------------------------ detection

std::string automatic_updates(const Detection &d)
{
    if (!d.unattended_installed) return "not installed";
    const bool periodic = !d.unattended_periodic.empty() && d.unattended_periodic != "0";
    return d.unattended_enabled || periodic ? "on" : "off";
}

// ------------------------------------------------------------------ the refresh of the lists

namespace {

bool plain_name(const std::string &name)
{
    if (name.empty() || name[0] == '-') return false;
    for (char c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') return false;
    return true;
}

long long number_of(const std::string &text)
{
    if (text.empty() || text.size() > 18 ||
        !std::all_of(text.begin(), text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return 0;
    return std::atoll(text.c_str());
}

} // namespace

std::vector<std::string> refresh_argv(const std::string &user, const std::string &helper)
{
    if (!plain_name(user) || helper.empty() || helper[0] != '/' || helper.find('\n') != std::string::npos) return {};
    return {"/usr/bin/systemd-run", std::string("--unit=") + kRefreshUnit, "--collect", "--quiet",
            "--description=CyberdeckZero package list refresh", helper, "apt-refresh", "--user", user};
}

RefreshStatus parse_refresh_status(const std::string &text)
{
    RefreshStatus status;
    bool v1 = false;
    for (const std::string &line : split_lines(text)) {
        const std::size_t equal = line.find('=');
        if (equal == std::string::npos || equal == 0) continue;
        const std::string key = line.substr(0, equal);
        const std::string value = line.substr(equal + 1);
        if (key == "v") v1 = value == "1";
        else if (key == "state") status.state = value;
        else if (key == "text") status.text = value;
        else if (key == "reason") status.reason = value;
        else if (key == "boot") status.boot = value;
        else if (key == "percent") status.percent = value.empty() ? -1 : static_cast<int>(std::min(100LL, number_of(value)));
        else if (key == "pid") status.pid = number_of(value);
        else if (key == "started") status.started = number_of(value);
        else if (key == "updated") status.updated = number_of(value);
    }
    status.present = v1 && !status.state.empty();
    return status;
}

RefreshPhase refresh_phase(const RefreshStatus &status, bool alive)
{
    if (!status.present) return RefreshPhase::None;
    if (status.state == "running") return alive ? RefreshPhase::Running : RefreshPhase::Interrupted;
    if (status.state == "done") return RefreshPhase::Done;
    if (status.state == "failed") return RefreshPhase::Failed;
    return RefreshPhase::None;
}

int refresh_percent(const RefreshStatus &status)
{
    return status.percent < 0 ? 0 : status.percent;
}

std::string refresh_value(const RefreshStatus &status)
{
    return status.percent < 0 ? std::string("starting") : std::to_string(status.percent) + " %";
}

std::string refresh_failure(const RefreshStatus &status, RefreshPhase phase)
{
    if (phase == RefreshPhase::Interrupted) return "The refresh was interrupted: refresh again";
    if (phase != RefreshPhase::Failed) return std::string();
    std::string reason = status.reason;
    if (reason.size() > 120) reason = reason.substr(0, 117) + "...";
    return reason.empty() ? std::string("Refresh failed: journalctl -u cyberdeckzero-apt") : "Refresh failed: " + reason;
}

std::string refresh_blocker(bool helper_installed, bool network, bool update_running, bool refresh_running)
{
    if (!helper_installed) return "No update helper: sudo apt-get update";
    if (update_running) return "A launcher update is running";
    if (refresh_running) return "A refresh is already running";
    if (!network) return "No network";
    return std::string();
}

bool package_installed(const std::string &output, const std::string &name)
{
    for (const std::string &line : split_lines(output)) {
        const std::vector<std::string> w = words(line);
        // "unattended-upgrades ii " (db:Status-Abbrev: want, status, error flag)
        if (w.size() >= 2 && w[0] == name && w[1].size() >= 2 && w[1][1] == 'i') return true;
    }
    return false;
}

std::string periodic_value(const std::string &dump, const std::string &key)
{
    for (const std::string &line : split_lines(dump)) {
        if (!starts_with(line, key.c_str())) continue;
        const std::size_t open = line.find('"', key.size());
        const std::size_t close = open == std::string::npos ? open : line.find('"', open + 1);
        if (close != std::string::npos) return line.substr(open + 1, close - open - 1);
    }
    return std::string();
}

// ------------------------------------------------------------------ what the page shows

long long lists_age_days(long long lists_time, long long now)
{
    if (lists_time <= 0 || now <= lists_time) return 0;
    return (now - lists_time) / (24 * 60 * 60);
}

Value row_value(const Overview &o, long long now)
{
    if (!o.scanned) return {"", Tone::Normal};
    if (o.failed || !o.report.valid) return {"Check failed", Tone::Muted};
    if (o.lists_time <= 0) return {"not checked", Tone::Muted};
    const long long days = lists_age_days(o.lists_time, now);
    if (days > kStaleDays) return {"Lists are " + std::to_string(days) + " days old", Tone::Muted};
    const int n = o.report.shown();
    if (n > 0) return {std::to_string(n) + (n == 1 ? " update" : " updates"), Tone::Warn};
    return {"Up to date", Tone::Ok};
}

std::string section_title(const char *name, std::size_t count)
{
    return std::string(name) + " (" + std::to_string(count) + ")";
}

PackageRow package_row(const Package &p)
{
    PackageRow row;
    row.label = p.name;
    // the end of a version is what differs: a narrow row keeps that ("...12+deb13u1"), never the start
    auto tail = [](const std::string &version) {
        constexpr std::size_t kKeep = 14;
        return version.size() <= kKeep + 3 ? version : "..." + version.substr(version.size() - kKeep);
    };
    if (!p.from.empty() && !p.to.empty()) row.values = {p.from + " > " + p.to, "> " + p.to, tail(p.to)};
    else if (!p.to.empty()) row.values = {"new " + p.to, tail(p.to)};
    else row.values = {p.note.empty() ? p.from : p.note}; /* a removal, or kept back with no version known */
    row.value = row.values[0];
    return row;
}

const char *info_line()
{
    // one line of the page's status line: at 480 px the end is what survives, so the command is the end
    return "From a terminal: sudo apt-get upgrade";
}

std::string terminal_text(const Report &r)
{
    // (P4: Upgrade on the Packages page installs the safe set; these stay the terminal's)
    std::string text = "The launcher never installs these updates. Run them in a terminal and read what apt says before "
                       "you answer yes:\n\n  sudo apt-get upgrade\n";
    if (r.needs_dist_upgrade)
        text += "\nSome packages are kept back (apt needs a new package for them, or one is removed). Only this installs "
                "them:\n\n  sudo apt-get dist-upgrade\n";
    auto names = [](const std::vector<Package> &list) {
        std::string out;
        for (const Package &p : list) out += "\n  " + p.name + (p.note.empty() ? "" : "  (" + p.note + ")");
        return out;
    };
    if (!r.boot.empty())
        text += "\nKernel, firmware and boot (" + std::to_string(r.boot.size()) + "):" + names(r.boot) +
                "\n\nKeep the board plugged in while they install: a power cut can leave it unable to start.\n";
    if (!r.terminal.empty()) text += "\nNeeds the terminal (" + std::to_string(r.terminal.size()) + "):" + names(r.terminal) + "\n";
    if (!r.store.empty()) {
        std::string list;
        for (const std::string &n : r.store) list += (list.empty() ? "" : ", ") + n;
        text += "\nCareful: apt also offers Debian's own " + list +
                ", which would replace the Store app of the same name. Update Store apps in Settings > Apps.\n";
    }
    return text;
}

} // namespace launcher_apt
