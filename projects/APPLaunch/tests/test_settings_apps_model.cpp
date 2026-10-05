/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps: Debian version order, upgrade detection, sync progress, failure reasons, free space, the
 * update-all queue.
 */

#include "../main/ui/settings/apps_backend.hpp"
#include "../main/ui/settings/apps_status_model.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <unistd.h>

using namespace apps_status;

static int cmp(const char *a, const char *b)
{
    return apps_backend::compare_versions(a, b);
}

static void versions()
{
    // dpkg --compare-versions semantics (each line checked against dpkg)
    assert(cmp("0.1.2", "0.1.1") > 0);
    assert(cmp("0.1.1", "0.1.2") < 0);
    assert(cmp("0.1.1", "0.1.1") == 0);
    assert(cmp("0.1.10", "0.1.9") > 0);            // numeric, not string, order
    assert(cmp("1.0", "1.0.0") < 0);
    assert(cmp("1.0~rc1", "1.0") < 0);             // "~" sorts before anything
    assert(cmp("1.0~rc2", "1.0~rc1") > 0);
    assert(cmp("1.0", "1.0+b1") < 0);
    assert(cmp("1.0a", "1.0") > 0);                // letters sort after the end
    assert(cmp("1.0", "1.0-1") < 0);               // a missing revision is "0"... and "-1" is later
    assert(cmp("1.0-2", "1.0-10") < 0);
    assert(cmp("1.0-1", "1.0-1") == 0);
    assert(cmp("1:0.1", "9.9") > 0);               // epoch wins
    assert(cmp("2:0.1", "1:9.9") > 0);
    assert(cmp("0.1.01", "0.1.1") == 0);           // leading zeros
    assert(cmp("1.0-1~bpo1", "1.0-1") < 0);
    assert(cmp("1.0-0.1", "1.0-0.1") == 0);
    assert(cmp("", "") == 0);
    assert(cmp("1", "") > 0);
    assert(cmp("a", "1") > 0 || cmp("a", "1") < 0);  // no crash on odd input
}

static void upgrades()
{
    apps_backend::App app;
    app.installed = true;
    app.installed_version = "0.1.1";
    app.version = "0.1.2";
    assert(app.upgradable() && !app.current());

    app.version = "0.1.1";
    assert(!app.upgradable() && app.current());

    app.version = "0.1.0";                          // the catalogue is older: no "update"
    assert(!app.upgradable());

    app.installed_version = "0.1.9";
    app.version = "0.1.10";                         // 0.1.10 is newer than 0.1.9
    assert(app.upgradable());

    app.installed = false;
    assert(!app.upgradable() && !app.current());

    app.installed = true;
    app.installed_version.clear();                  // unknown installed version: never offer
    assert(!app.upgradable());
}

static void parsing()
{
    const std::string summary =
        "META\t1\t2 apps\t3G\t/x\n"
        "APP\tid1\tLAN Scan\t0.1.2\tOther\t1\t0\t1454518\tdesc\tauthor\tsrc\timg\tdeps\tcode\tmine/repo\tupd\trev\t1\t0.1.1\tcat\tlanscan\n";
    const auto apps = apps_backend::parse_apps(summary);
    assert(apps.size() == 1);
    assert(apps[0].size == "1454518" && apps[0].installed_version == "0.1.1" && apps[0].upgradable());

    const std::string registries =
        "REG\thttps://x/registry.json\terror\t0\t2026-10-05 10:00\tdownload failed: https://x/registry.json\t1\tmine/repo\t0\t\n";
    const auto sources = apps_backend::parse_sources(registries);
    assert(sources.size() == 1 && sources[0].status == "error");
    assert(sources[0].error == "download failed: https://x/registry.json");
    assert(sources[0].synced_at == "2026-10-05 10:00");
}

static void sync_progress()
{
    SyncProgress progress;
    assert(progress.status().empty() && !progress.active());
    progress.reset({{"u1", "a/one"}, {"u2", "b/two"}, {"u3", ""}});
    assert(progress.total() == 3 && !progress.active());
    assert(progress.find("u3")->name == "u3");
    assert(progress.find("nope") == nullptr);

    progress.start(0);
    assert(progress.active() && progress.status() == "Syncing 1/3 a/one");
    progress.succeed(0);
    assert(!progress.active());
    progress.start(1);
    assert(progress.status() == "Syncing 2/3 b/two");
    progress.fail(1, "HTTP 404");                    // a failed source does not stop the next one
    assert(progress.find("u2")->state == SyncState::Failed && progress.find("u2")->reason == "HTTP 404");
    assert(progress.find("u1")->state == SyncState::Done);
    progress.start(2);
    assert(progress.status() == "Syncing 3/3 u3");
    progress.succeed(2);
    assert(progress.finished() == 3 && progress.failed() == 1);
    assert(progress.status() == "Synced 2 of 3 sources" && progress.severity() == 1);

    progress.reset({{"u1", "a/one"}});
    progress.start(0);
    progress.succeed(0);
    assert(progress.status() == "Synced 1 source" && progress.severity() == 0);
    progress.reset({{"u1", "a/one"}});
    progress.start(0);
    progress.fail(0, "");
    assert(progress.status() == "Sync failed" && progress.find("u1")->reason == "Sync failed");
    progress.reset({{"u1", "a"}, {"u2", "b"}});
    progress.start(0);
    progress.fail(0, "x");
    progress.start(1);
    progress.fail(1, "y");
    assert(progress.status() == "Sync failed (2)" && progress.severity() == 2);

    // out of range calls are ignored
    progress.start(9);
    progress.succeed(9);
    progress.fail(9, "x");
    assert(progress.total() == 2);

    progress.reset({{"u", "a very long source name indeed"}});
    progress.start(0);
    assert(progress.status().size() <= 8 + 4 + 16 + 1);
}

static void sync_verdicts()
{
    const std::string url = "https://x/registry.json";
    const std::string ok_line = "INFO sync\nREGISTRY\tUPDATED\t" + url + "\t" + url + "\tok\t4\tmine/repo\n";
    const std::string cached_line = "REGISTRY\tUPDATED\t" + url + "\t" + url + "\tcached\t4\tmine/repo\n";
    assert(update_registry_status(ok_line) == "ok");
    assert(update_registry_status(cached_line) == "cached");
    assert(update_registry_status("").empty());
    assert(update_registry_status("REGISTRY\tADDED\tu\tok\t1\tn\n").empty());   // not the UPDATED record

    // a clean fetch
    SyncVerdict v = decide_sync(0, ok_line, "", "");
    assert(v.ok);
    // the backend exits 0 but could not fetch the source: a failure with the stored reason
    v = decide_sync(0, cached_line, "cached", "download failed: " + url);
    assert(!v.ok && v.backend_text == "download failed: " + url);
    v = decide_sync(0, "REGISTRY\tUPDATED\ta\tb\terror\t0\tn\n", "error", "invalid JSON response: u");
    assert(!v.ok && v.backend_text == "invalid JSON response: u");
    // no UPDATED line: the source's own record decides
    v = decide_sync(0, "", "ok", "");
    assert(v.ok);
    v = decide_sync(0, "", "cached", "download failed: u");
    assert(!v.ok);
    v = decide_sync(0, "", "", "");                         // nothing known: never claim success
    assert(!v.ok);
    // the UPDATED line wins over a stale record
    v = decide_sync(0, ok_line, "cached", "old error");
    assert(v.ok);
    // a non-zero exit is a failure whatever the record says
    v = decide_sync(1, "ERROR\tregistry unavailable\t" + url + "\n", "ok", "");
    assert(!v.ok && v.backend_text == "registry unavailable");
    v = decide_sync(-2, "", "ok", "");
    assert(!v.ok && v.backend_text == "timed out");

    // with the probe: an unreachable source shows its reason, never "done"
    SyncProbe http404;
    http404.http_code = 404;
    v = decide_sync(0, cached_line, "cached", "download failed: " + url);
    assert(sync_failure_reason(v.backend_text, http404) == "HTTP 404");
    SyncProbe refused;
    refused.curl_rc = 7;
    assert(sync_failure_reason(v.backend_text, refused) == "Connection refused");
    SyncProbe offline;
    offline.network_up = false;
    assert(sync_failure_reason(v.backend_text, offline) == "No network");
    v = decide_sync(0, cached_line, "cached", "invalid JSON response: " + url);
    assert(sync_failure_reason(v.backend_text, SyncProbe()) == "Bad JSON");

    // three of four sources good: amber "Synced 3 of 4 sources"
    SyncProgress progress;
    progress.reset({{"a", "a"}, {"b", "b"}, {"c", "c"}, {"d", "d"}});
    for (size_t i = 0; i < 4; ++i) {
        progress.start(i);
        if (i == 2) progress.fail(i, "HTTP 404");
        else progress.succeed(i);
    }
    assert(progress.status() == "Synced 3 of 4 sources" && progress.severity() == 1);
    assert(progress.find("c")->reason == "HTTP 404" && progress.find("a")->state == SyncState::Done);
}

static void stage_lines()
{
    assert(stage_line("Downloading LAN Scan", 0, 0, 12) == "Downloading LAN Scan 12s");
    assert(stage_line("Verifying package", 0, 1, -1) == "Verifying package");
    // the counters survive a long name, the name is cut
    const std::string line = stage_line("Downloading Wi-Fi Survey Extended Edition", 2, 2, 14);
    assert(line.size() <= 30);
    assert(line.size() >= 10 && line.substr(line.size() - 10) == " (2/2) 14s");
    assert(line.find('~') != std::string::npos);
    assert(stage_line("Finishing", 1, 3, 0) == "Finishing (1/3) 0s");
    assert(stage_line("x", 1, 2, 123456, 8).size() >= 8);     // tiny width: still the counters
}

static void parked_transactions()
{
    char dir[] = "/tmp/apps-state-XXXXXX";
    assert(mkdtemp(dir));
    setenv("M5APPSTORE_STATE_DIR", dir, 1);
    assert(apps_backend::state_dir() == dir);
    const std::string pending = std::string(dir) + "/pending-package.json";
    auto write = [&](const std::string &text) { std::ofstream(pending) << text; };
    auto exists = [&](const std::string &path) { return std::ifstream(path).good(); };

    assert(!apps_backend::park_pending_transaction("app-a", "install"));          // nothing pending
    write("{\"app_id\":\"app-a\"}");
    assert(apps_backend::park_pending_transaction("app-a", "install"));
    assert(!exists(pending));                                                      // a different app can go on
    assert(!apps_backend::restore_pending_transaction("app-b", "install"));        // another app: nothing to bring back
    assert(!exists(pending));
    assert(apps_backend::restore_pending_transaction("app-a", "install"));         // the retry of the same app
    assert(exists(pending));
    assert(!apps_backend::restore_pending_transaction("app-a", "install"));        // already back

    // never overwrite a live transaction with an old one
    assert(apps_backend::park_pending_transaction("app-a", "install"));
    write("{\"app_id\":\"app-c\"}");
    assert(!apps_backend::restore_pending_transaction("app-a", "install"));
    assert(exists(pending));
    std::remove(pending.c_str());
    assert(apps_backend::restore_pending_transaction("app-a", "install"));
    std::remove(pending.c_str());
    unsetenv("M5APPSTORE_STATE_DIR");
}

static void sync_reasons()
{
    SyncProbe up;
    SyncProbe down;
    down.network_up = false;
    assert(sync_failure_reason("download failed: u", down) == "No network");

    assert(sync_failure_reason("invalid JSON response: u", up) == "Bad JSON");
    assert(sync_failure_reason("registry md5 mismatch", up) == "MD5 mismatch");

    SyncProbe http;
    http.http_code = 404;
    assert(sync_failure_reason("download failed: u", http) == "HTTP 404");
    http.http_code = 503;
    assert(sync_failure_reason("download failed: u", http) == "HTTP 503");
    http.http_code = 200;                            // the server answers fine: do not blame it
    assert(sync_failure_reason("download failed: u", http) == "Download failed");

    SyncProbe dns;
    dns.curl_rc = 6;
    assert(sync_failure_reason("download failed: u", dns) == "No network");
    SyncProbe slow;
    slow.curl_rc = 28;
    assert(sync_failure_reason("download failed: u", slow) == "Timed out");
    SyncProbe other;
    other.curl_rc = 35;
    assert(sync_failure_reason("download failed: u", other) == "Connection failed");

    // curl exit code + http code -> reason, through the probe parser
    const std::string bf = "download failed: u";
    auto reason = [&](int rc, const std::string &out) { return sync_failure_reason(bf, curl_probe(rc, out)); };
    assert(reason(0, "404") == "HTTP 404");
    assert(reason(0, "503") == "HTTP 503");
    assert(reason(0, "200") == "Download failed");
    assert(reason(7, "curl: (7) Failed to connect to 127.0.0.1 port 8098\n000") == "Connection refused");
    assert(reason(6, "curl: (6) Could not resolve host: x\n000") == "No network");
    assert(reason(28, "curl: (28) Operation timed out\n000") == "Timed out");
    assert(reason(-2, "") == "Timed out");                                   // our own deadline
    assert(reason(52, "curl: (52) Empty reply from server\n000") == "Connection failed");
    assert(reason(56, "curl: (56) Recv failure: Connection reset by peer\n000") == "Connection failed");
    assert(reason(127, "") == "Connection failed");                          // curl could not be started
    SyncProbe offline_probe = curl_probe(0, "404");
    offline_probe.network_up = false;
    assert(sync_failure_reason(bf, offline_probe) == "No network");
    assert(sync_failure_reason("invalid JSON response: u", curl_probe(0, "200")) == "Bad JSON");
    assert(std::string("Connection refused").size() <= 24);

    assert(sync_failure_reason("", up) == "Sync failed");
    assert(sync_failure_reason("something odd happened", up) == "something odd happened");
    assert(sync_failure_reason(std::string(100, 'x'), up).size() <= 40);

    const std::string route =
        "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n"
        "wlan0\t00000000\t0144A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0\n"
        "wlan0\t0044A8C0\t00000000\t0001\t0\t0\t600\t00FFFFFF\t0\t0\t0\n";
    assert(has_default_route(route));
    assert(!has_default_route("Iface\tDestination\tGateway\tFlags\n"
                              "wlan0\t0044A8C0\t00000000\t0001\t0\t0\t600\t00FFFFFF\t0\t0\t0\n"));
    assert(!has_default_route("Iface\tDestination\tGateway\tFlags\nwlan0\t00000000\t0144A8C0\t0002\n"));  // not up
    assert(!has_default_route(""));
}

static void package_failures()
{
    // md5
    Failure f = package_failure("ERROR\tmd5 mismatch\t1\n", 1, true, true);
    assert(f.headline == "MD5 mismatch" && !f.detail.empty());
    f = package_failure("PROGRESS\tverify\t0\t0\t-1\tVerifying package\nERROR\tprepared Debian package failed MD5 verification\n",
                        1, true, false);
    assert(f.headline == "MD5 mismatch");

    // space
    f = package_failure("ERROR\tdownload failed (curl 23): https://x/a.deb\t1\n", 1, true, true);
    assert(f.headline == "Not enough space");
    f = package_failure("dpkg: error processing archive /x.deb (--install):\n cannot copy extracted data: No space left on device\n",
                        1, true, false);
    assert(f.headline == "Not enough space");

    // network
    f = package_failure("ERROR\tdownload failed (curl 6): https://x/a.deb\t1\n", 1, true, true);
    assert(f.headline == "No network");
    f = package_failure("ERROR\tdownload failed (curl 7): https://x/a.deb\t1\n", 1, true, true);
    assert(f.headline == "No network");
    f = package_failure("ERROR\tdownload failed (curl 56): u\t1\n", 1, false, true);
    assert(f.headline == "No network");
    f = package_failure("ERROR\tdownload failed (curl 22): u\t1\n", 1, true, true);
    assert(f.headline == "Download failed");
    f = package_failure("ERROR\tdownload failed (curl 18): u\t1\n", 1, true, true);   // other curl failure, route is up
    assert(f.headline == "Download failed" && f.detail.find("curl 18") != std::string::npos);

    // dpkg: the first dpkg line is the detail
    f = package_failure("PROGRESS\tpackage-manager\t0\t0\t-1\tInstalling package\n"
                        "dpkg: error processing archive /x.deb (--install):\n trying to overwrite '/usr/bin/y'\n"
                        "dpkg: error processing package z (--install):\nERROR\tpackage manager failed with exit code 1\n",
                        1, true, false);
    assert(f.headline == "dpkg error");
    assert(f.detail == "trying to overwrite '/usr/bin/y'");      // the reason line, not the announcement
    f = package_failure("dpkg: dependency problems prevent configuration of z:\n", 1, true, false);
    assert(f.headline == "dpkg error");
    f = package_failure("E: Unable to locate package foo\n", 100, true, false);
    assert(f.headline == "apt error" && f.detail == "E: Unable to locate package foo");

    // the real dpkg report: the announcement line carries no reason, the next lines do
    f = package_failure("v008fail: postinst says no\n"
                        "dpkg: error processing package v008fail (--install):\n"
                        " installed v008fail package post-installation script subprocess returned error exit status 1\n"
                        "Errors were encountered while processing:\n v008fail\n"
                        "ERROR\tpackage manager failed with exit code 1\n",
                        1, true, false);
    assert(f.headline == "dpkg error");
    assert(f.detail == "installed v008fail package post-installation script subprocess returned error exit status 1");
    f = package_failure("dpkg: error processing package z (--install):\n first reason\n second reason\n third reason\n",
                        1, true, false);
    assert(f.detail == "first reason\nsecond reason");                            // at most two lines
    f = package_failure("dpkg: error processing package z (--install):\n " + std::string(300, 'r') + "\n", 1, true, false);
    assert(f.detail.size() <= 96);                                                // cut to fit
    f = package_failure("dpkg: error processing package z (--install):\nERROR\tpackage manager failed\n", 1, true, false);
    assert(f.headline == "dpkg error" && f.detail == "dpkg: error processing package z (--install):");   // nothing follows
    f = package_failure("dpkg: dependency problems prevent configuration of z:\n z depends on foo; however:\n"
                        "  Package foo is not installed.\n", 1, true, false);
    assert(f.detail == "z depends on foo; however:\nPackage foo is not installed.");

    // a pending transaction of another app is not a download problem
    f = package_failure("PENDING_CONFLICT\tid1\tinstall\tv008fail\n"
                        "ERROR\tanother package transaction is pending; finish or retry its original operation\t1\n",
                        1, true, true);
    assert(f.headline == "Unfinished install" && f.detail.find("v008fail") != std::string::npos);
    assert(f.headline != "Download failed");
    f = package_failure("ERROR\tanother package transaction is pending; finish or retry its original operation\t1\n",
                        1, true, true);
    assert(f.headline == "Unfinished install");

    // fallbacks
    f = package_failure("ERROR\tapp not found: x\t1\n", 1, true, true);
    assert(f.headline == "Could not prepare" && f.detail == "app not found: x");    // not a download problem
    f = package_failure("ERROR\tsomething\n", 1, true, false);
    assert(f.headline == "Install failed" && f.detail == "something");
    f = package_failure("", 3, true, false);
    assert(f.headline == "Failed" && f.detail.find("exit 3") != std::string::npos);
    f = package_failure("ERROR\t" + std::string(500, 'e') + "\n", 1, true, false);
    assert(f.detail.size() <= 200);

    // progress lines
    assert(progress_text("").empty());
    assert(progress_text("PROGRESS\tverify\t0\t0\t-1\tVerifying package\n") == "Verifying package");
    assert(progress_text("PROGRESS\tverify\t0\t0\t-1\tVerifying package\nPROGRESS\tpackage-manager\t0\t0\t-1\tInstalling package\n")
           == "Installing package");
    assert(progress_text("PROGRESS\tx\n") == "x");
    assert(progress_text("noise\n") .empty());
}

static void space()
{
    assert(parse_size_bytes("1454518") == 1454518);
    assert(parse_size_bytes("1.5 MB") == 1572864);
    assert(parse_size_bytes("2 KB") == 2048);
    assert(parse_size_bytes("1G") == 1073741824ull);
    assert(parse_size_bytes("online") == 0);
    assert(parse_size_bytes("") == 0);
    assert(parse_size_bytes("12 parsecs") == 0);

    assert(format_bytes(512) == "1 KB" || format_bytes(512) == "0 KB");
    assert(format_bytes(340 * 1024) == "340 KB");
    assert(format_bytes(12u << 20) == "12 MB");
    assert(format_bytes(3ull << 30) == "3.0 GB");

    assert(space_problem(0, 0).empty());                        // unknown size: no check
    assert(space_problem(1u << 20, 100u << 20).empty());
    assert(space_problem(10u << 20, 30u << 20).empty());        // exactly three times is enough
    const std::string problem = space_problem(10u << 20, 29u << 20);
    assert(problem == "Not enough space: need 30 MB, 29 MB free");
    assert(!space_problem(1454518, 0).empty());
}

static void queue()
{
    UpdateQueue update;
    assert(update.empty() && !update.active());
    update.reset({{"a", "A"}, {"b", "B"}, {"c", "C"}});
    assert(update.active() && update.total() == 3 && !update.empty() && update.position() == 0);
    auto item = update.take();
    assert(item.first == "a" && item.second == "A" && update.position() == 1);
    item = update.take();
    assert(item.first == "b" && update.position() == 2);
    update.abort();                                              // a failed update stops the run
    assert(update.empty() && !update.active());
    assert(update.take().first.empty());

    update.reset({{"x", "X"}});
    update.take();
    assert(update.empty() && update.active());                   // last one taken, still running
    update.clear();
    assert(!update.active());
}

int main()
{
    versions();
    upgrades();
    parsing();
    sync_progress();
    sync_reasons();
    sync_verdicts();
    stage_lines();
    parked_transactions();
    package_failures();
    space();
    queue();
    return 0;
}
