/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps: Debian version order, upgrade detection, sync progress, failure reasons, free space, the
 * update-all queue; the native page's rules (settings_apps_model: rows, actions, confirmations, status line).
 */

#include "../main/ui/settings/apps_backend.hpp"
#include "../main/ui/settings/apps_status_model.hpp"
#include "../main/ui/settings/settings_apps_model.hpp"

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

/* ---- the native page's rules (settings_apps_model) ----------------------------------------------------------- */

static apps_backend::App make_app(const char *id, const char *title, const char *version, const char *installed,
                                  const char *size = "1454518")
{
    apps_backend::App app;
    app.id = id;
    app.title = title;
    app.version = version;
    app.installed = installed != nullptr;
    app.installed_version = installed ? installed : "";
    app.size = size;
    app.source = "deck-lab/apps";
    return app;
}

static apps_backend::Source make_source(const char *url, const char *name, const char *status, int apps,
                                        bool enabled = true, const char *error = "")
{
    apps_backend::Source source;
    source.url = url;
    source.name = name;
    source.status = status;
    source.apps = apps;
    source.enabled = enabled;
    source.error = error;
    return source;
}

/* Two updates (Update all), a current app, an available one. */
static settings_apps::Model catalogue_model()
{
    settings_apps::Model model;
    model.set_lists({make_app("radar", "Packet Radar", "0.3.1", "0.3.0", "2 MB"),
                     make_app("notes", "Field Notes", "0.9.2", "0.9.2"),
                     make_app("meter", "Signal Meter", "0.4.0", nullptr),
                     make_app("weather", "Weather Station", "1.2.0", "1.1.4")},
                    {make_source("u1", "deck-lab/apps", "ok", 4),
                     make_source("u2", "field-kit/tools", "cached", 2, true, "Invalid JSON at line 1"),
                     make_source("u3", "old/archive", "ok", 1, false)});
    return model;
}

static void model_rows()
{
    using settings_apps::RowType;
    assert(settings_apps::shorten("Weather Station", 12) == "Weather Sta~");
    assert(settings_apps::shorten("short", 12) == "short");

    // before the first list: "Loading..." (busy), then the Sources row
    settings_apps::Model loading;
    auto rows = loading.rows();
    assert(rows.size() == 2 && rows[0].type == RowType::Placeholder && rows[0].label == "Loading..." && rows[0].busy);
    assert(rows[1].type == RowType::Sources && rows[1].label == "Sources");
    loading.set_lists({}, {});
    rows = loading.rows();
    assert(rows[0].label == "No apps: add your source in Sources" && !rows[0].busy && loading.loaded());
    settings_apps::Model missing;
    missing.set_backend_missing(true);
    assert(missing.status() == "Store backend not installed");
    assert(missing.rows()[0].label == "The Store is needed (backend missing)" && !missing.rows()[0].busy);

    // the catalogue: Update all (2) first, versions as the legacy page shows them, Sources last
    settings_apps::Model model = catalogue_model();
    rows = model.rows();
    assert(model.upgradable_count() == 2 && model.update_all_row());
    assert(rows.size() == 6);
    assert(rows[0].type == RowType::UpdateAll && rows[0].label == "Update all (2)");
    assert(rows[1].key == "radar" && rows[1].label == "Packet Radar" && rows[1].value == "0.3.0>0.3.1");
    assert(rows[2].value == "0.9.2");                // current: its version only
    assert(rows[3].value == "v0.4.0");               // not installed: the offered version
    // the legacy page's colours: an update amber, installed green, not installed muted
    using settings_apps::Tone;
    assert(rows[1].tone == Tone::Update && rows[2].tone == Tone::Good && rows[3].tone == Tone::Muted);
    assert(rows[0].tone == Tone::Normal && rows[5].tone == Tone::Normal);
    assert(rows[5].type == RowType::Sources);
    assert(model.find_row(RowType::App, "meter") == 3 && model.find_row(RowType::Sources) == 5);
    assert(model.find_row(RowType::App, "nope") == -1);
    // one update only: no Update all row
    settings_apps::Model single;
    single.set_lists({make_app("radar", "Packet Radar", "0.3.1", "0.3.0")}, {});
    assert(!single.update_all_row() && single.rows()[0].type == RowType::App);
    // long versions are cut as on the legacy page
    assert(settings_apps::Model::app_value(make_app("x", "X", "1.2.3-beta4", "1.2.3-beta3")) == "1.2.3-b~>1.2.3-b~");
    // the app a package job runs for is busy
    model.set_busy(true, "notes");
    assert(model.rows()[2].busy && !model.rows()[1].busy);
    model.set_busy(false, "notes");
    assert(!model.rows()[2].busy && model.package_id().empty());

    // Sources: values, then Add and Sync
    model.set_tab(settings_apps::Tab::Sources);
    rows = model.rows();
    assert(rows.size() == 5);
    assert(rows[0].type == RowType::Source && rows[0].key == "u1" && rows[0].value == "4 apps");
    assert(rows[1].value == "failed");
    assert(rows[2].value == "off");
    assert(rows[0].tone == Tone::Good && rows[1].tone == Tone::Bad && rows[2].tone == Tone::Muted);
    assert(rows[3].type == RowType::AddSource && rows[3].label == "+ Add a GitHub source");
    assert(rows[4].type == RowType::Sync && rows[4].label == "Sync my sources");
    // a sync run: waiting, syncing (busy row), done while it runs, failed
    apps_status::SyncProgress progress;
    progress.reset({{"u1", "deck-lab/apps"}, {"u2", "field-kit/tools"}});
    progress.start(0);
    model.set_sync(progress);
    rows = model.rows();
    assert(rows[0].value == "syncing..." && rows[0].busy && rows[1].value == "waiting" && !rows[1].busy);
    assert(rows[0].tone == Tone::Update && rows[1].tone == Tone::Muted);
    progress.succeed(0);
    progress.start(1);
    model.set_sync(progress);
    assert(model.rows()[0].value == "done" && model.rows()[0].tone == Tone::Good && model.rows()[1].busy);
    progress.fail(1, "HTTP 404");
    model.set_sync(progress);
    rows = model.rows();
    assert(rows[0].value == "4 apps" && rows[1].value == "failed" && !rows[1].busy && rows[1].tone == Tone::Bad);
    // not synced yet: muted
    assert(model.source_tone(make_source("u9", "x/y", "", 0)) == Tone::Muted);
    assert(model.source_value(make_source("u9", "x/y", "", 0)) == "not synced");
}

static void model_actions()
{
    using settings_apps::ActionKind;
    settings_apps::Model model = catalogue_model();

    // Update all (row 0): every app with an update, the first one's size
    auto action = model.activate(0);
    assert(action.kind == ActionKind::UpdateAll && action.items.size() == 2);
    assert(action.items[0].first == "radar" && action.items[1].second == "Weather Station" && action.size == "2 MB");
    assert(model.update_all().kind == ActionKind::UpdateAll);
    action = model.activate(1);
    assert(action.kind == ActionKind::Upgrade && action.id == "radar" && action.name == "Packet Radar");
    assert(model.activate(2).kind == ActionKind::OpenActions);  // up to date: nothing to start, its list (Remove)
    action = model.activate(3);
    assert(action.kind == ActionKind::Install && action.id == "meter" && action.size == "1454518");
    assert(model.activate(5).kind == ActionKind::ShowSources);
    assert(model.activate(99).kind == ActionKind::None);
    assert(model.sync().kind == ActionKind::Sync);
    assert(model.add_source().kind == ActionKind::None);        // A works on the Sources tab only

    // nothing starts while a job runs; the Sources tab still opens
    model.set_busy(true, "radar");
    assert(model.activate(3).kind == ActionKind::None && model.activate(0).kind == ActionKind::None);
    assert(model.update_all().kind == ActionKind::None && model.sync().kind == ActionKind::None);
    assert(model.activate(5).kind == ActionKind::ShowSources);
    assert(!model.ask_remove(2));
    model.set_busy(false);

    // nothing to update: a message
    settings_apps::Model current;
    current.set_lists({make_app("notes", "Field Notes", "0.9.2", "0.9.2")}, {});
    action = current.update_all();
    assert(action.kind == ActionKind::Say && action.message == "Everything is up to date");

    // without the backend nothing runs
    settings_apps::Model missing = catalogue_model();
    missing.set_backend_missing(true);
    assert(missing.activate(3).kind == ActionKind::None && missing.update_all().kind == ActionKind::None);
    assert(missing.sync().kind == ActionKind::None);

    // Sources: Enter toggles a source, Add, Sync
    model.set_tab(settings_apps::Tab::Sources);
    action = model.activate(0);
    assert(action.kind == ActionKind::ToggleSource && action.id == "u1" && !action.enable);
    action = model.activate(2);
    assert(action.kind == ActionKind::ToggleSource && action.id == "u3" && action.enable);
    assert(model.activate(3).kind == ActionKind::AddSource && model.add_source().kind == ActionKind::AddSource);
    assert(model.activate(4).kind == ActionKind::Sync);
    model.set_busy(true);
    assert(model.add_source().kind == ActionKind::None && model.activate(0).kind == ActionKind::None);
    model.set_busy(false);

    // the add-source entry: the backend's address rules
    std::string url, name, error;
    assert(settings_apps::Model::parse_source("deck-lab/apps", url, name, error));
    assert(url == "https://raw.githubusercontent.com/deck-lab/apps/main/registry.json" && name == "deck-lab/apps");
    assert(!settings_apps::Model::parse_source("", url, name, error) && error == "Enter owner/repo");
    assert(!settings_apps::Model::parse_source("lonely", url, name, error) && error == "Use owner/repo");
}

static void model_confirmations()
{
    using settings_apps::ActionKind;
    using settings_apps::Confirm;
    settings_apps::Model model = catalogue_model();

    // D: an installed app asks first; the update-all row, an app that is not installed and the Sources row do not
    assert(!model.ask_remove(0) && !model.ask_remove(3) && !model.ask_remove(5));
    assert(model.pending() == Confirm::None);
    assert(model.ask_remove(2) && model.pending() == Confirm::RemoveApp && model.pending_name() == "Field Notes");
    auto action = model.confirm();
    assert(action.kind == ActionKind::Uninstall && action.id == "notes" && action.name == "Field Notes");
    assert(model.pending() == Confirm::None && model.confirm().kind == ActionKind::None);
    // cancelled, or dropped by a tab change
    assert(model.ask_remove(1));
    model.cancel();
    assert(model.pending() == Confirm::None);
    assert(model.ask_remove(1));
    model.set_tab(settings_apps::Tab::Sources);
    assert(model.pending() == Confirm::None);

    // a source asks first; the built-in one (never listed in practice) and the Add / Sync rows do not
    assert(model.ask_remove(1) && model.pending() == Confirm::RemoveSource && model.pending_name() == "field-kit/tools");
    action = model.confirm();
    assert(action.kind == ActionKind::RemoveSource && action.id == "u2");
    assert(!model.ask_remove(3) && !model.ask_remove(4));
    settings_apps::Model builtin;
    apps_backend::Source hub = make_source("hub", "CardputerZero Hub", "ok", 9);
    hub.builtin = true;
    builtin.set_lists({}, {hub});
    builtin.set_tab(settings_apps::Tab::Sources);
    assert(!builtin.ask_remove(0));
    assert(builtin.activate(0).kind == ActionKind::Say && builtin.activate(0).message == "Built-in source");

    // a job started meanwhile: the confirmed removal does not run
    assert(model.ask_remove(0));
    model.set_busy(true);
    assert(model.confirm().kind == ActionKind::None && model.pending() == Confirm::None);
}

static void model_action_lists()
{
    using settings_apps::ActionKind;
    using settings_apps::Confirm;
    using settings_apps::RowType;
    settings_apps::Model model = catalogue_model();

    // a tap on an app or a source opens its list; other rows do what Enter does
    assert(model.tap(1).kind == ActionKind::OpenActions && model.tap(3).kind == ActionKind::OpenActions);
    assert(model.tap(0).kind == ActionKind::UpdateAll && model.tap(5).kind == ActionKind::ShowSources);
    assert(!model.open_actions(0) && !model.open_actions(5) && !model.in_actions());

    // an app with an update: Update, Remove, Cancel; the title is the app's
    assert(model.open_actions(1) && model.in_actions() && model.actions_title() == "Packet Radar");
    auto rows = model.rows();
    assert(rows.size() == 3 && rows[0].type == RowType::Update && rows[0].label == "Update");
    assert(rows[1].type == RowType::Remove && rows[1].label == "Remove" && rows[2].type == RowType::Cancel);
    assert(!model.open_actions(0));                              // one list at a time
    auto action = model.activate(0);
    assert(action.kind == ActionKind::Upgrade && action.id == "radar" && action.size == "2 MB");
    assert(model.tap(0).kind == ActionKind::Upgrade);           // the list's rows: a tap is Enter
    assert(model.activate(1).kind == ActionKind::AskRemove);
    assert(model.ask_remove(1) && model.pending() == Confirm::RemoveApp && model.pending_name() == "Packet Radar");
    action = model.confirm();
    assert(action.kind == ActionKind::Uninstall && action.id == "radar");
    assert(!model.ask_remove(0) && !model.ask_remove(2));      // Update and Cancel remove nothing
    assert(model.activate(2).kind == ActionKind::CloseActions);
    assert(model.actions_row() == 1);                            // the highlight goes back to the app's row
    model.close_actions();
    assert(!model.in_actions() && model.rows().size() == 6 && model.actions_title().empty());

    // not installed: Install, Cancel; current: Remove, Cancel
    assert(model.open_actions(3));
    rows = model.rows();
    assert(rows.size() == 2 && rows[0].type == RowType::Install && rows[1].type == RowType::Cancel);
    action = model.activate(0);
    assert(action.kind == ActionKind::Install && action.id == "meter");
    model.close_actions();
    assert(model.open_actions(2));
    rows = model.rows();
    assert(rows.size() == 2 && rows[0].type == RowType::Remove && rows[1].type == RowType::Cancel);

    // a job runs: nothing starts, Cancel still closes
    model.set_busy(true, "weather");
    assert(model.activate(0).kind == ActionKind::None && !model.ask_remove(0));
    assert(model.activate(1).kind == ActionKind::CloseActions);
    model.set_busy(false);
    // the app went away (removed elsewhere): the list is no longer valid
    assert(model.actions_valid());
    model.set_lists({make_app("radar", "Packet Radar", "0.3.1", "0.3.0")}, model.sources());
    assert(!model.actions_valid() && model.actions_row() == -1);
    model.close_actions();

    // a source: Disable, Sync, Remove, Cancel; a disabled one: Enable, Remove, Cancel
    model = catalogue_model();
    model.set_tab(settings_apps::Tab::Sources);
    assert(model.tap(0).kind == ActionKind::OpenActions && model.tap(3).kind == ActionKind::AddSource);
    assert(model.activate(0).kind == ActionKind::ToggleSource);  // Enter keeps the legacy on / off
    assert(model.open_actions(1) && model.actions_title() == "field-kit/tools");
    rows = model.rows();
    assert(rows.size() == 4 && rows[0].type == RowType::Disable && rows[0].label == "Disable");
    assert(rows[1].type == RowType::SyncSource && rows[1].label == "Sync" && rows[2].type == RowType::Remove);
    action = model.activate(0);
    assert(action.kind == ActionKind::ToggleSource && action.id == "u2" && !action.enable);
    action = model.activate(1);
    assert(action.kind == ActionKind::Sync && action.id == "u2"); /* this source only */
    assert(model.activate(2).kind == ActionKind::AskRemove && model.ask_remove(2));
    assert(model.pending() == Confirm::RemoveSource && model.confirm().id == "u2");
    // the listed source's failure shows while its list is open
    settings_apps::Severity severity = settings_apps::Severity::Normal;
    assert(model.caption(0, severity) == "field-kit/tools: Bad JSON (cached list)" &&
           severity == settings_apps::Severity::Error);
    assert(model.actions_row() == 1);
    model.set_tab(settings_apps::Tab::Sources);                  // a tab change closes it
    assert(!model.in_actions());
    assert(model.open_actions(2));
    rows = model.rows();
    assert(rows.size() == 3 && rows[0].type == RowType::Enable && rows[0].label == "Enable");
    assert(rows[1].type == RowType::Remove);
    action = model.activate(0);
    assert(action.kind == ActionKind::ToggleSource && action.id == "u3" && action.enable);
    assert(model.add_source().kind == ActionKind::None);         // A: only on the list

    // without the backend: nothing but Cancel
    settings_apps::Model missing = catalogue_model();
    missing.set_backend_missing(true);
    assert(missing.open_actions(3) && missing.activate(0).kind == ActionKind::None);
    assert(missing.activate(1).kind == ActionKind::CloseActions);
}

static void model_caption()
{
    using settings_apps::Severity;
    settings_apps::Model model = catalogue_model();
    Severity severity = Severity::Error;
    model.set_status("Updated Packet Radar to 0.3.1");
    assert(model.caption(0, severity) == "Updated Packet Radar to 0.3.1" && severity == Severity::Normal);
    model.set_status("Wrong password", true);
    assert(model.caption(0, severity) == "Wrong password" && severity == Severity::Error);
    // only some sources failed: amber, as the legacy page
    model.set_status("Synced 2 of 3 sources", false, true);
    assert(model.caption(0, severity) == "Synced 2 of 3 sources" && severity == Severity::Warn && model.status_warn());

    // Sources: the highlighted failed source says why (its stored error; "cached": the last good list is kept)
    model.set_tab(settings_apps::Tab::Sources);
    assert(model.caption(1, severity) == "field-kit/tools: Bad JSON (cached list)" && severity == Severity::Error);
    assert(model.caption(0, severity) == "Synced 2 of 3 sources" && severity == Severity::Warn);
    // a failure of this run: its reason
    apps_status::SyncProgress progress;
    progress.reset({{"u1", "deck-lab/apps"}});
    progress.start(0);
    progress.fail(0, "HTTP 404");
    model.set_sync(progress);
    assert(model.caption(0, severity) == "deck-lab/apps: HTTP 404" && severity == Severity::Error);
    // a job runs: its progress wins
    model.set_busy(true);
    model.set_status("Syncing 1/2 deck-lab/apps");
    assert(model.caption(0, severity) == "Syncing 1/2 deck-lab/apps" && severity == Severity::Normal);
    // a disabled source has no problem
    model.set_busy(false);
    assert(model.source_problem(model.sources()[2]).empty());
    assert(model.source_problem(make_source("u", "a-very-long-owner-name/and-repo", "error", 0, true,
                                            "download failed (curl 22): x"))
           == "a-very-long-owner-nam~: Download failed");
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
    model_rows();
    model_actions();
    model_confirmations();
    model_action_lists();
    model_caption();
    return 0;
}
