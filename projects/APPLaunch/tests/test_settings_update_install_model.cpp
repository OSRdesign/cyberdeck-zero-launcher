// SPDX-License-Identifier: MIT
//
// Settings > Updates P2 (report 047 section 11): the pure rules of the download and the install
// (settings_update_install_model.hpp): the bundle and its checksums from the release list, the .sha256 file, the hash
// check, the space and battery guards, the one root command and every value it refuses, the helper's status file, its
// phases, the exit-76 request of desktop boards, the progress page's steps, the result lines and the rollback toast
// (once per helper run), the stale downloads.
//
//   test_settings_update_install_model FIXTURE_DIR

#include "settings_update_install_model.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace launcher_update;

namespace {

std::string g_fixtures;

std::vector<Release> releases_of(const char *name)
{
    std::string text;
    if (!read_file(g_fixtures + "/" + name, text)) {
        std::fprintf(stderr, "cannot read %s\n", name);
        std::abort();
    }
    std::vector<Release> releases;
    assert(parse_releases(text, releases));
    return releases;
}

bool has(const std::string &text, const char *part)
{
    return text.find(part) != std::string::npos;
}

const std::string kArmDigest = "ffd7c4972546fe32fbd480e39054dcd772f53b5cbd30e5fd8990e6700b133202";
const std::string kAmdDigest = "5920b019053801bace7747231c9afb83e8920939965d1696d61df979dae77313";

void test_download_of()
{
    const std::vector<Release> releases = releases_of("releases-0.8.13.json");
    Download d;
    assert(download_of(releases, "0.8.13", "arm64", d));
    assert(d.version == "0.8.13" && d.name == "cyberdeckzero-launcher-0.8.13-arm64.tar.gz" && d.size == 57332103);
    assert(d.digest == kArmDigest);
    assert(d.url.find("/v0.8.13/cyberdeckzero-launcher-0.8.13-arm64.tar.gz") != std::string::npos);
    assert(d.checksum_url == d.url + ".sha256");
    assert(download_of(releases, "0.8.13", "amd64", d) && d.digest == kAmdDigest && d.size == 57311828);
    // a draft (0.8.15) and a pre-release (0.8.14-rc.1) are never downloaded; an unknown version neither
    assert(!download_of(releases, "0.8.15", "arm64", d));
    assert(!download_of(releases, "0.8.14-rc.1", "arm64", d));
    assert(!download_of(releases, "0.9.0", "arm64", d));
    // a release without this architecture's bundle
    const std::vector<Release> arm_only = releases_of("releases-0.8.13-arm64-only.json");
    assert(!download_of(arm_only, "0.8.13", "amd64", d));
    assert(download_of(arm_only, "0.8.13", "arm64", d));
    std::puts("download_of OK");
}

void test_checksums()
{
    assert(plain_version("0.8.13") && plain_version("10.0.1"));
    assert(!plain_version("v0.8.13") && !plain_version("0.8.13-rc.1") && !plain_version("0.8") && !plain_version("") &&
           !plain_version("0.8.13 ") && !plain_version("0.8.x"));
    assert(valid_sha256(kArmDigest) && !valid_sha256("ABC") && !valid_sha256(std::string(64, 'A')) &&
           !valid_sha256(std::string(63, 'a')));
    assert(digest_hex("sha256:" + kArmDigest) == kArmDigest);
    assert(digest_hex("sha256:FFD7C4972546FE32FBD480E39054DCD772F53B5CBD30E5FD8990E6700B133202") == kArmDigest);
    assert(digest_hex("sha512:" + kArmDigest).empty() && digest_hex(kArmDigest).empty() && digest_hex("").empty());
    const std::string name = "cyberdeckzero-launcher-0.8.13-arm64.tar.gz";
    // the release's .sha256 asset: "<hash> <file>" with one space (build: sha256sum's two spaces, '*', CRLF also fine)
    assert(parse_checksum_file(kArmDigest + " " + name + "\n", name) == kArmDigest);
    assert(parse_checksum_file(kArmDigest + "  " + name + "\r\n", name) == kArmDigest);
    assert(parse_checksum_file(kArmDigest + " *" + name, name) == kArmDigest);
    assert(parse_checksum_file(kArmDigest + "  ./out/" + name, name) == kArmDigest);
    assert(parse_checksum_file(kArmDigest + "\n", name) == kArmDigest);
    assert(parse_checksum_file(kArmDigest + " cyberdeckzero-launcher-0.8.13-amd64.tar.gz", name).empty());
    assert(parse_checksum_file("<html>captive portal</html>", name).empty());
    assert(parse_checksum_file("", name).empty());
    // the check: the file's hash against the .sha256 asset and GitHub's digest (D5)
    assert(check_hash(kArmDigest, kArmDigest, kArmDigest) == HashCheck::Ok);
    assert(check_hash(kArmDigest, kArmDigest, "") == HashCheck::Ok);
    assert(check_hash(kArmDigest, "", kArmDigest) == HashCheck::Ok);
    assert(check_hash(kAmdDigest, kArmDigest, kArmDigest) == HashCheck::Mismatch);
    assert(check_hash("", kArmDigest, "") == HashCheck::Mismatch); /* sha256sum failed */
    assert(check_hash(kArmDigest, kArmDigest, kAmdDigest) == HashCheck::Disagree);
    assert(check_hash(kArmDigest, "", "") == HashCheck::NoReference);
    std::puts("checksums OK");
}

void test_guards()
{
    const long long MB = 1024LL * 1024;
    assert(space_needed(57 * MB) == 300 * MB);         /* 57 + 96 + 30 MB and a margin: 300 MB */
    assert(space_needed(100 * MB) == 400 * MB);        /* a bigger bundle: four times it */
    assert(space_problem(57 * MB, 301 * MB).empty());
    assert(space_problem(57 * MB, -1).empty());        /* unknown: not refused */
    assert(space_problem(57 * MB, 120 * MB) == "Not enough space: need 300 MB, 120 MB free");
    assert(!battery_too_low(-1, false));               /* no battery (the Pi boards) */
    assert(battery_too_low(49, false) && !battery_too_low(49, true) && !battery_too_low(50, false));
    assert(megabytes(57332103) == "55 MB" && megabytes(57 * MB) == "57 MB" && megabytes(1) == "1 MB" &&
           megabytes(0) == "0 MB");
    assert(progress_text(23 * MB, 57 * MB) == "23 of 57 MB");
    assert(progress_text(80 * MB, 57 * MB) == "57 of 57 MB");
    assert(progress_text(-5, 57 * MB) == "0 of 57 MB");
    assert(progress_text(3 * MB, 0) == "3 MB");
    assert(download_failure_text(22, 404) == "The server refused it (HTTP 404)");
    assert(download_failure_text(6, 0) == "No network name for the server (DNS)");
    assert(download_failure_text(28, 0) == download_failure_text(-2, 0));
    assert(has(download_failure_text(60, 0), "clock"));
    assert(download_failure_text(99, 0) == "The download failed (curl 99)");
    InstallGuards g;
    g.bundle_size = 57 * MB;
    g.free_bytes = 4096 * MB;
    assert(install_blocker(g).empty());
    g.network = false;
    assert(install_blocker(g) == "No network");
    g.free_bytes = 100 * MB;
    assert(install_blocker(g) == "Not enough space: need 300 MB, 100 MB free");
    g.battery_percent = 34;
    assert(install_blocker(g) == "Battery 34 %: charge it or plug it in");
    g.charging = true;
    assert(has(install_blocker(g), "Not enough space"));
    g.helper = HelperPhase::Interrupted;
    assert(install_blocker(g) == "Repair the interrupted update first");
    g.helper = HelperPhase::Running;
    assert(install_blocker(g) == "An update is already running");
    g.helper_installed = false;
    assert(install_blocker(g) == "The update helper is missing");
    std::puts("guards OK");
}

void test_root_command()
{
    ApplyRequest r;
    r.version = "0.8.13";
    r.from = "0.8.12";
    r.sha256 = kArmDigest;
    r.cache_dir = "/home/pi/.cache/cyberdeckzero/update";
    r.file = r.cache_dir + "/cyberdeckzero-launcher-0.8.13-arm64.tar.gz";
    r.user = "pi";
    const std::vector<std::string> want = {
        "/usr/bin/systemd-run", "--unit=cyberdeckzero-update", "--collect", "--quiet",
        "--description=CyberdeckZero launcher update", "/usr/share/APPLaunch/bin/cyberdeckzero-update", "apply",
        "--version", "0.8.13", "--from-version", "0.8.12", "--sha256", kArmDigest, "--file", r.file, "--user", "pi"};
    assert(apply_argv(r) == want);
    // every value is checked: nothing else ever reaches the root command
    auto refused = [&](void (*change)(ApplyRequest &)) {
        ApplyRequest bad = r;
        change(bad);
        return apply_argv(bad).empty();
    };
    assert(refused([](ApplyRequest &x) { x.version = "0.8.13-rc.1"; }));
    assert(refused([](ApplyRequest &x) { x.version = "0.8.13; reboot"; }));
    assert(refused([](ApplyRequest &x) { x.from = "0.0.0-dev"; }));
    assert(refused([](ApplyRequest &x) { x.sha256 = "--force"; }));
    assert(refused([](ApplyRequest &x) { x.sha256 = std::string(64, 'g'); }));
    assert(refused([](ApplyRequest &x) { x.file = "/etc/shadow"; }));
    assert(refused([](ApplyRequest &x) { x.file = x.cache_dir + "/../../../etc/shadow"; }));
    assert(refused([](ApplyRequest &x) { x.file = x.cache_dir + "/"; }));
    assert(refused([](ApplyRequest &x) { x.file = x.cache_dir + "-other/a.tar.gz"; }));
    assert(refused([](ApplyRequest &x) { x.file = x.cache_dir + "/a\n--user root"; }));
    assert(refused([](ApplyRequest &x) { x.cache_dir = "relative/dir"; x.file = "relative/dir/a.tar.gz"; }));
    assert(refused([](ApplyRequest &x) { x.user = "-root"; }));
    assert(refused([](ApplyRequest &x) { x.user = "pi user"; }));
    assert(refused([](ApplyRequest &x) { x.user = ""; }));
    assert(refused([](ApplyRequest &x) { x.helper = "cyberdeckzero-update"; }));
    const std::vector<std::string> resume = resume_argv("pi");
    assert(resume.size() == 9 && resume[0] == "/usr/bin/systemd-run" && resume[5] == kHelperPath &&
           resume[6] == "resume" && resume[7] == "--user" && resume[8] == "pi");
    assert(resume_argv("pi;rm").empty() && resume_argv("").empty());
    std::puts("root command OK");
}

HelperStatus status_of(const std::string &text)
{
    return parse_helper_status(text);
}

void test_helper_status()
{
    const std::string running = "v=1\nstate=running\nstep=install\nfrom=0.8.12\nto=0.8.13\nvariant=appliance\nuser=pi\n"
                                "pid=812\nboot=b1\nstarted=1000\nupdated=1040\nlauncher_pid=700\nrestart=\nbackup=1\n"
                                "log=/var/log/cyberdeckzero/update-20261104-101500.log\nreason=\nunknown=kept quiet\n";
    HelperStatus s = status_of(running);
    assert(s.present && s.state == "running" && s.step == "install" && s.from == "0.8.12" && s.to == "0.8.13" &&
           s.variant == "appliance" && s.user == "pi" && s.pid == 812 && s.boot == "b1" && s.started == 1000 &&
           s.updated == 1040 && s.launcher_pid == 700 && s.backup && s.log == "/var/log/cyberdeckzero/update-20261104-101500.log");
    assert(!status_of("").present && !status_of("state=running\n").present /* no v=1 */);
    assert(!status_of("v=2\nstate=running\n").present && !status_of("v=1\n").present);
    assert(status_of("v=1\r\nstate=done\r\n").state == "done");
    assert(status_of("v=1\nstate=done\npid=12x\n").pid == 0);
    // phases: a running status whose helper is gone (a power cut, a kill) is interrupted
    assert(helper_phase(s, true) == HelperPhase::Running);
    assert(helper_phase(s, false) == HelperPhase::Interrupted);
    assert(helper_phase(status_of("v=1\nstate=done\n"), false) == HelperPhase::Done);
    assert(helper_phase(status_of("v=1\nstate=refused\n"), false) == HelperPhase::Refused);
    assert(helper_phase(status_of("v=1\nstate=rolled-back\n"), false) == HelperPhase::RolledBack);
    assert(helper_phase(status_of("v=1\nstate=failed\n"), false) == HelperPhase::Failed);
    assert(helper_phase(status_of("v=1\nstate=odd\n"), true) == HelperPhase::None);
    assert(helper_phase(HelperStatus(), true) == HelperPhase::None);
    std::puts("helper status OK");
}

void test_restart_request()
{
    HelperStatus s = status_of("v=1\nstate=running\nstep=restart\nfrom=0.8.12\nto=0.8.13\nvariant=desktop\n"
                               "launcher_pid=700\nrestart=launcher\n");
    assert(restart_requested(s, true, "0.8.12", 700));
    assert(!restart_requested(s, false, "0.8.12", 700)); /* the helper is gone */
    assert(!restart_requested(s, true, "0.8.13", 700)); /* the new launcher: never a loop */
    assert(!restart_requested(s, true, "0.8.12", 701)); /* another launcher process */
    s.restart.clear();
    assert(!restart_requested(s, true, "0.8.12", 700));
    s.restart = "launcher";
    s.launcher_pid = 0;
    assert(!restart_requested(s, true, "0.8.12", 0));
    std::puts("restart request OK");
}

void test_progress()
{
    HelperStatus s = status_of("v=1\nstate=running\nstep=check\nfrom=0.8.12\nto=0.8.13\nupdated=1000\n");
    std::vector<StepRow> rows = progress_rows(s, 1012);
    assert(rows.size() == 4);
    assert(rows[0].label == "Checking the download" && rows[0].mark == StepMark::Current && rows[0].value == "12 s");
    assert(rows[1].label == "Backup of 0.8.12" && rows[1].mark == StepMark::Pending && rows[1].value.empty());
    assert(rows[2].label == "Installing 0.8.13" && rows[3].label == "Restart and check");
    s.step = "install";
    rows = progress_rows(s, 1045);
    assert(rows[0].mark == StepMark::Done && rows[0].value == "done" && rows[1].mark == StepMark::Done);
    assert(rows[2].mark == StepMark::Current && rows[2].value == "45 s" && rows[3].mark == StepMark::Pending);
    s.step = "health";
    rows = progress_rows(s, 999); /* a clock before the status: no seconds */
    assert(rows[3].mark == StepMark::Current && rows[3].value.empty() && rows[2].value == "done");
    assert(progress_caption(s) == "Checking that 0.8.13 runs. Do not switch off.");
    s.step = "rollback";
    rows = progress_rows(s, 1001);
    assert(rows.size() == 5 && rows[4].label == "Putting 0.8.12 back" && rows[4].mark == StepMark::Current);
    assert(progress_caption(s) == "Putting 0.8.12 back. Do not switch off.");
    s.step = "install";
    assert(progress_caption(s) == "Do not switch off: it restarts by itself");
    // the bars: the download's bytes; the run's steps (no percentage inside a step)
    assert(download_percent(0, 57332103) == 0 && download_percent(57332103 * 35 / 100, 57332103) == 35);
    assert(download_percent(57332103, 57332103) == 100 && download_percent(70000000, 57332103) == 100);
    assert(download_percent(57332102, 57332103) == 99); /* full only when the file is whole */
    assert(download_percent(1000, 0) == 0 && download_percent(-1, 100) == 0 && download_percent(35, 100) == 35);
    const char *steps[][2] = {{"check", "25"}, {"plan", "25"}, {"", "25"}, {"backup", "50"}, {"install", "75"},
                              {"restart", "100"}, {"health", "100"}, {"rollback", "100"}};
    for (const auto &step : steps) {
        s.step = step[0];
        assert(step_percent(s) == std::atoi(step[1]));
    }
    std::puts("progress OK");
}

void test_results()
{
    const HelperStatus rolled = status_of("v=1\nstate=rolled-back\nstep=rollback\nfrom=0.8.12\nto=0.8.13\n"
                                          "started=5000\nreason=0.8.13 did not start\n");
    assert(result_caption(rolled, HelperPhase::RolledBack, "0.8.12") == "Update to 0.8.13 failed: 0.8.12 put back");
    assert(result_text(rolled, HelperPhase::RolledBack, "0.8.12") ==
           "Update to 0.8.13 failed (0.8.13 did not start): 0.8.12 was put back");
    assert(result_value(rolled, HelperPhase::RolledBack, "0.8.12") == "rolled back");
    // installed by hand since: nothing to say about it any more
    assert(result_caption(rolled, HelperPhase::RolledBack, "0.8.13").empty());
    assert(result_value(rolled, HelperPhase::RolledBack, "0.8.14").empty());
    const HelperStatus refused = status_of("v=1\nstate=refused\nstep=plan\nfrom=0.8.12\nto=0.8.13\nreason=needs "
                                           "install.sh by hand: a desktop was installed\n");
    assert(result_caption(refused, HelperPhase::Refused, "0.8.12") == "0.8.13 not installed: see Show log");
    assert(result_text(refused, HelperPhase::Refused, "0.8.12") ==
           "0.8.13 not installed: needs install.sh by hand: a desktop was installed");
    assert(result_value(refused, HelperPhase::Refused, "0.8.12") == "not installed");
    const HelperStatus failed = status_of("v=1\nstate=failed\nfrom=0.8.12\nto=0.8.13\nreason=x\n");
    assert(result_caption(failed, HelperPhase::Failed, "0.8.12") == "Update to 0.8.13 failed: see Show log");
    assert(result_value(failed, HelperPhase::Failed, "0.8.12") == "failed");
    const HelperStatus interrupted = status_of("v=1\nstate=running\nstep=install\nfrom=0.8.12\nto=0.8.13\n");
    assert(result_caption(interrupted, HelperPhase::Interrupted, "0.8.12") == "Update to 0.8.13 interrupted: Repair it");
    assert(result_value(interrupted, HelperPhase::Interrupted, "0.8.12") == "interrupted");
    const HelperStatus done = status_of("v=1\nstate=done\nfrom=0.8.12\nto=0.8.13\n");
    assert(result_caption(done, HelperPhase::Done, "0.8.13").empty() && result_value(done, HelperPhase::Done, "0.8.12").empty());
    assert(result_caption(interrupted, HelperPhase::Running, "0.8.12").empty());

    // the rollback toast: once per helper run, only in the launcher it rolled back to
    State state;
    assert(take_rollback_notice(state, rolled, HelperPhase::RolledBack, "0.8.12") ==
           "Update to 0.8.13 failed\nRolled back to 0.8.12");
    assert(state.rollback_seen == 5000);
    assert(take_rollback_notice(state, rolled, HelperPhase::RolledBack, "0.8.12").empty());
    HelperStatus again = rolled;
    again.started = 6000;
    assert(!take_rollback_notice(state, again, HelperPhase::RolledBack, "0.8.12").empty() && state.rollback_seen == 6000);
    State fresh;
    assert(take_rollback_notice(fresh, rolled, HelperPhase::RolledBack, "0.8.13").empty()); /* not the version put back */
    assert(take_rollback_notice(fresh, refused, HelperPhase::Refused, "0.8.12").empty());
    assert(fresh.rollback_seen == 0);
    // the state file keeps it
    assert(parse_state(format_state(state)).rollback_seen == 6000);
    std::puts("results OK");
}

void test_stale_downloads()
{
    const std::vector<std::string> names = {"cyberdeckzero-launcher-0.8.12-arm64.tar.gz",
                                            "cyberdeckzero-launcher-0.8.13-arm64.tar.gz",
                                            "cyberdeckzero-launcher-0.8.13-arm64.tar.gz.part",
                                            "cyberdeckzero-launcher-0.8.11-arm64.tar.gz.part",
                                            "releases.json", "fetch.headers.12", "notes"};
    const std::vector<std::string> stale = stale_downloads(names, "cyberdeckzero-launcher-0.8.13-arm64.tar.gz");
    assert((stale == std::vector<std::string>{"cyberdeckzero-launcher-0.8.11-arm64.tar.gz.part",
                                               "cyberdeckzero-launcher-0.8.12-arm64.tar.gz"}));
    assert(stale_downloads(names, "").size() == 4);
    std::puts("stale downloads OK");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s FIXTURE_DIR\n", argv[0]);
        return 2;
    }
    g_fixtures = argv[1];
    test_download_of();
    test_checksums();
    test_guards();
    test_root_command();
    test_helper_status();
    test_restart_request();
    test_progress();
    test_results();
    test_stale_downloads();
    std::puts("settings update install model OK");
    return 0;
}
