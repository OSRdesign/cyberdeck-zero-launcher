/* SPDX-License-Identifier: MIT
 *
 * Render harness: the Store backend of Settings > Apps (task 014 P3d), in process. The native page's workers
 * (settings_apps_jobs.cpp) run the backend program through apps_backend::run / run_program and check it with
 * apps_backend::available; the Makefile renames those three references in settings_apps_jobs.o to the functions below
 * (objcopy, as the display manager's open / ioctl / mmap), so the real jobs run and get their answers at once and the
 * same every time. They answer in the backend's own record format (APP / REG / REGISTRY / ERROR lines, parsed by the
 * real apps_backend). Fake names and addresses only.
 *
 * The scene picks the case with `env HARNESS_APPS <case>` (default catalogue):
 *   catalogue  three sources of the user (deck-lab/apps: four apps, two of them with an update; field-kit/tools: its
 *              last sync failed, the cached list is kept; old/archive: off) and the built-in hub (left out by the page)
 *   none       no source yet: the empty catalogue; a source added through the page brings deck-lab/apps' apps
 *   hold       the first list never comes ("Loading...")
 *   missing    the backend is not installed
 * Installs: --prepare-package answers that an earlier install is unfinished (no sudo in the harness); with
 * `env HARNESS_APPS_PACKAGE hold` it never answers (the app's row keeps its spinner: updating / installing).
 */
#include "settings/apps_backend.hpp"

#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct FakeSource {
    std::string url;
    std::string name;
    std::string status;
    int apps;
    std::string error;
    bool enabled;
    bool builtin;
};

struct FakeApp {
    const char *id, *title, *version, *installed, *source;
};

const char *kDeckLab = "https://raw.githubusercontent.com/deck-lab/apps/main/registry.json";

/* deck-lab/apps: two updates, a current app, one not installed; the hub's app is left out by the page */
const FakeApp kApps[] = {
    {"radar", "Packet Radar", "0.3.1", "0.3.0", "deck-lab/apps"},
    {"notes", "Field Notes", "0.9.2", "0.9.2", "deck-lab/apps"},
    {"meter", "Signal Meter", "0.4.0", nullptr, "deck-lab/apps"},
    {"weather", "Weather Station with a long name", "1.2.0", "1.1.4", "deck-lab/apps"},
    {"hubapp", "Hub Game", "1.0.0", nullptr, "CardputerZero Hub"},
    {"tools1", "Bench Tools", "2.0.0", nullptr, "field-kit/tools"},
};

std::mutex g_mutex;
bool g_ready = false;
std::vector<FakeSource> g_sources;

std::string fixture_case()
{
    const char *value = std::getenv("HARNESS_APPS");
    return value && value[0] ? value : "catalogue";
}

void ensure_sources()
{
    if (g_ready) return;
    g_ready = true;
    g_sources.push_back({"https://hub.invalid/registry.json", "CardputerZero Hub", "ok", 1, "", true, true});
    if (fixture_case() != "catalogue") return;
    g_sources.push_back({kDeckLab, "deck-lab/apps", "ok", 4, "", true, false});
    g_sources.push_back({"https://raw.githubusercontent.com/field-kit/tools/main/registry.json", "field-kit/tools",
                         "cached", 1, "registry is not valid: Invalid JSON at line 1", true, false});
    g_sources.push_back({"https://raw.githubusercontent.com/old/archive/main/registry.json", "old/archive", "ok", 0, "",
                         false, false});
}

FakeSource *find_source(const std::string &url)
{
    for (FakeSource &source : g_sources)
        if (source.url == url) return &source;
    return nullptr;
}

const FakeSource *source_named(const std::string &name)
{
    for (const FakeSource &source : g_sources)
        if (source.name == name) return &source;
    return nullptr;
}

std::string arg(const std::vector<std::string> &args, std::size_t index)
{
    return index < args.size() ? args[index] : std::string();
}

/* The --summary APP record: id title version category installed ? size description author ... source(14) ...
 * installed_version(18) ... package(20). */
std::string app_record(const FakeApp &app)
{
    std::string fields[21];
    fields[0] = "APP";
    fields[1] = app.id;
    fields[2] = app.title;
    fields[3] = app.version;
    fields[4] = "Tools";
    fields[5] = app.installed ? "1" : "0";
    fields[6] = "0";
    fields[7] = "1454518";
    fields[8] = "A fake app of the render harness";
    fields[9] = "harness";
    fields[14] = app.source;
    fields[18] = app.installed ? app.installed : "";
    fields[20] = app.id;
    std::string line;
    for (int i = 0; i < 21; ++i) line += (i ? "\t" : "") + fields[i];
    return line + "\n";
}

std::string reg_record(const FakeSource &source)
{
    return "REG\t" + source.url + "\t" + source.status + "\t" + std::to_string(source.apps) + "\t2026-10-10 20:00\t" +
           source.error + "\t" + (source.enabled ? "1" : "0") + "\t" + source.name + "\t" + (source.builtin ? "1" : "0") +
           "\n";
}

apps_backend::Result answer(int rc, std::string out)
{
    apps_backend::Result result;
    result.rc = rc;
    result.out = std::move(out);
    return result;
}

} // namespace

namespace harness_apps {

bool available()
{
    return fixture_case() != "missing";
}

apps_backend::Result run(const std::vector<std::string> &args, int)
{
    const std::string command = arg(args, 0);
    if (fixture_case() == "hold" && command == "--summary")
        for (;;) usleep(100 * 1000); /* the first list never comes */
    if (command == "--prepare-package") {
        const char *package = std::getenv("HARNESS_APPS_PACKAGE");
        if (package && std::string(package) == "hold")
            for (;;) usleep(100 * 1000); /* the package job never ends */
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    ensure_sources();
    if (command == "--summary") {
        std::string out = "META\t1\tharness\t3G\t/harness\n";
        for (const FakeApp &app : kApps) {
            const FakeSource *source = source_named(app.source);
            if (source && source->enabled) out += app_record(app);
        }
        return answer(0, out);
    }
    if (command == "--registries") {
        std::string out;
        for (const FakeSource &source : g_sources) out += reg_record(source);
        return answer(0, out);
    }
    if (command == "--add-registry") {
        const std::string url = arg(args, 1);
        if (find_source(url)) return answer(1, "ERROR\tsource already added\n");
        const std::string name = arg(args, 3);
        g_sources.push_back({url, name, "ok", url == kDeckLab ? 4 : 0, "", true, false});
        return answer(0, "REGISTRY\tADDED\t" + url + "\tok\t0\t" + name + "\n");
    }
    if (command == "--remove-registry" || command == "--enable-registry" || command == "--disable-registry") {
        const std::string url = arg(args, 1);
        for (std::size_t i = 0; i < g_sources.size(); ++i) {
            if (g_sources[i].url != url) continue;
            if (command == "--remove-registry") g_sources.erase(g_sources.begin() + static_cast<long>(i));
            else g_sources[i].enabled = command == "--enable-registry";
            return answer(0, "");
        }
        return answer(1, "ERROR\tno such source\n");
    }
    if (command == "--edit-registry") {
        FakeSource *source = find_source(arg(args, 1));
        if (!source) return answer(1, "ERROR\tno such source\n");
        const std::string status = source->status == "cached" ? "cached" : "ok";
        return answer(0, "REGISTRY\tUPDATED\t" + source->url + "\t" + source->url + "\t" + status + "\t" +
                             std::to_string(source->apps) + "\t" + source->name + "\n");
    }
    /* The source list the Store imports (apps_registry_mirror): not modelled, the workers then leave the mirror and the
     * default source alone. */
    if (command == "--registry-config") return answer(1, "");
    if (command == "--prepare-package")
        return answer(1, "ERROR\tanother package transaction is pending\nPENDING_CONFLICT\tbench\t1\tBench Tools\n");
    std::fprintf(stderr, "[harness] apps backend: no fixture for %s\n", command.c_str());
    return answer(-1, "");
}

/* The curl probe of a failed source: the server says 404. */
apps_backend::Result run_program(const std::string &, const std::vector<std::string> &, int)
{
    return answer(0, "404");
}

} // namespace harness_apps
