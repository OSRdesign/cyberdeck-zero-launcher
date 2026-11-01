/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps, the workers of the native page (see settings_apps_jobs.hpp). The jobs, their messages and the sudo
 * hand-over are the legacy page's (Shared, task_refresh, task_sync, task_package, task_finalize, start_sudo and its
 * callbacks; the legacy page was deleted in P3e), kept as they were; the native page adds only the id of the app a
 * package job is for (its row shows the job) and the sync of one source (a source's action list).
 */

#include "settings_apps_jobs.hpp"

#include "settings_apps_model.hpp"
#include "settings_apps_registry_mirror.hpp"
#include "settings_apps_registry_needs.hpp"
#include "settings_apt_upgrade.hpp"

#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <list>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#include <sys/statvfs.h>

namespace settings_apps {

namespace {
// Report 047 P4: the package jobs running now, in every Jobs (the shared busy flag, package_job_running())
std::atomic<int> g_package_jobs{0};
} // namespace

// State shared with the worker threads (which can outlive the page).
struct Jobs::Shared {
    std::mutex mutex;
    std::vector<apps_backend::App> apps;
    std::vector<apps_backend::Source> sources;
    bool apps_ready = false;
    bool sources_ready = false;
    std::string status;
    bool status_error = false;
    bool status_warn = false;          // amber: partly failed
    bool status_dirty = false;
    bool busy = false;
    std::string package_id;            // the app the package job is for (native page: its row shows the job)
    // what the worker is doing now ("Downloading X"): shown with the seconds it has taken
    std::string stage;
    std::size_t stage_position = 0, stage_total = 0;      // "update all" queue position when one runs
    std::chrono::steady_clock::time_point stage_since;
    // per-source sync progress (copied to the page when dirty) and the "update all" queue
    apps_status::SyncProgress sync;
    bool sync_dirty = false;
    apps_status::UpdateQueue queue;
    // a failure message for the page
    bool message_pending = false;
    std::string message_title;
    std::string message_detail;
    // hand-over of a prepared package job to the UI thread, which asks for the sudo password
    bool sudo_pending = false;
    std::vector<std::string> sudo_argv;
    std::string job_action;
    std::string job_id;
    std::string job_transaction;
    std::string job_name;
    std::string job_version;
    bool package_job = false;          // a package job counts in g_package_jobs (P4's busy flag) until busy ends

    // busy ends: the package job leaves the shared busy flag (call with the mutex held)
    void end_busy_locked()
    {
        busy = false;
        if (package_job) {
            package_job = false;
            --g_package_jobs;
        }
    }
    void begin_package_locked()
    {
        if (!package_job) {
            package_job = true;
            ++g_package_jobs;
        }
    }

    void say(const std::string &text)
    {
        std::lock_guard<std::mutex> lock(mutex);
        status = text;
        status_error = false;
        status_warn = false;
        status_dirty = true;
        stage.clear();
    }
    void set_stage(const std::string &text)
    {
        std::lock_guard<std::mutex> lock(mutex);
        stage = text;
        stage_position = queue.active() ? queue.position() : 0;
        stage_total = queue.active() ? queue.total() : 0;
        stage_since = std::chrono::steady_clock::now();
    }
    // An error stops an "update all" run: nothing else is attempted.
    void finish(const std::string &text, bool error = false, bool warn = false)
    {
        std::lock_guard<std::mutex> lock(mutex);
        status = text;
        status_error = error;
        status_warn = warn;
        status_dirty = true;
        stage.clear();
        end_busy_locked();
        if (error) queue.abort();
    }
    void succeed(const std::string &text)
    {
        std::lock_guard<std::mutex> lock(mutex);
        status = text;
        status_error = false;
        status_warn = false;
        status_dirty = true;
        stage.clear();
        end_busy_locked();
    }
    void fail(const apps_status::Failure &failure)
    {
        std::lock_guard<std::mutex> lock(mutex);
        status = failure.headline;
        status_error = true;
        status_warn = false;
        status_dirty = true;
        message_title = failure.headline;
        message_detail = failure.detail;
        message_pending = true;
        stage.clear();
        end_busy_locked();
        queue.abort();
    }
    void publish_sync_locked()
    {
        sync_dirty = true;
        status = sync.status();
        // the last source is in: the line is already the final one (the list reload that follows takes a while),
        // so it gets its final colour now instead of green until finish()
        const bool over = !sync.active() && sync.total() > 0 && sync.finished() == sync.total();
        status_error = over && sync.severity() == 2;
        status_warn = over && sync.severity() == 1;
        status_dirty = true;
    }
};

namespace {

using Shared = Jobs::Shared;

bool network_up()
{
    std::ifstream route("/proc/net/route");
    std::stringstream text;
    text << route.rdbuf();
    return apps_status::has_default_route(text.str());
}

unsigned long long free_bytes()
{
    struct statvfs info {};
    if (::statvfs("/", &info) != 0) return ~0ull;   // unknown: do not block the install
    return static_cast<unsigned long long>(info.f_bavail) * info.f_frsize;
}

// A failed registry download: ask curl what the server says, to tell "no network" from "HTTP 404".
apps_status::SyncProbe probe_source(const std::string &url, const std::string &backend_text)
{
    apps_status::SyncProbe probe;
    probe.network_up = network_up();
    if (!probe.network_up || backend_text.find("nvalid JSON") != std::string::npos) return probe;
    const auto result = apps_backend::run_program(
        "curl", {"-sS", "-L", "-m", "10", "-o", "/dev/null", "-w", "%{http_code}", url}, 20);
    return apps_status::curl_probe(result.rc, result.out);
}

// ---- The Store's shared source list (settings_apps_registry_mirror.hpp) and the default source -------------------
// The adapter between the pure mirror module, the backend (--registry-config) and the launcher's config service.
// The config call is made from a worker thread and waited for with a timeout (as the screen settings page does).
namespace registry_sync {

namespace mirror = apps_registry_mirror;

struct ConfigReply {
    std::mutex mutex;
    std::condition_variable condition;
    bool done = false;
    int code = -1;
    std::string data;
};

bool config_call(std::list<std::string> arguments, std::string *data = nullptr)
{
    auto reply = std::make_shared<ConfigReply>();
    try {
        cp0_signal_config_api(std::move(arguments), [reply](int code, std::string text) {
            std::lock_guard<std::mutex> lock(reply->mutex);
            if (reply->done) return;
            reply->code = code;
            reply->data = std::move(text);
            reply->done = true;
            reply->condition.notify_one();
        });
    } catch (...) {
        return false;
    }
    std::unique_lock<std::mutex> lock(reply->mutex);
    if (!reply->condition.wait_for(lock, std::chrono::seconds(3), [&] { return reply->done; })) return false;
    if (data) *data = reply->data;
    return reply->code == 0;
}

bool read_backend(mirror::Config &config)
{
    const auto result = apps_backend::run({"--registry-config"}, 30);
    return result.rc == 0 && mirror::parse_backend(result.out, config);
}

bool read_mirror(mirror::Config &config)
{
    bool reachable = true;
    const bool present = mirror::parse([&](const std::string &key) {
        std::string value;
        if (!config_call({"GetStr", key, ""}, &value)) {
            reachable = false;
            return std::string();
        }
        return value;
    }, config);
    return present && reachable;
}

bool write_mirror(const mirror::Config &config)
{
    mirror::KeyValues keys;
    int skipped = 0;
    if (!mirror::pack(config, keys, &skipped)) return false;   // never an empty list over a non-empty one
    std::list<std::string> request = {"SetManyAndSave"};
    for (const auto &pair : keys) {
        request.push_back(pair.first);
        request.push_back(pair.second);
    }
    return config_call(std::move(request));
}

bool flag_is_set()
{
    std::string value;
    return config_call({"GetStr", mirror::kDefaultFlagKey, ""}, &value) && !value.empty();
}

bool set_flag()
{
    return config_call({"SetManyAndSave", mirror::kDefaultFlagKey, "1"});
}

// Reads the backend list and the mirror and, when they differ, writes the backend's list as the mirror. Returns what
// the pure plan decided (add_default / set_flag are acted on by the caller; the mirror write is done here).
mirror::Plan reconcile(bool with_flag)
{
    mirror::PlanInput input;
    input.backend_read = read_backend(input.backend);
    if (!input.backend_read) return {};
    input.mirror_present = read_mirror(input.mirror);
    input.flag_set = with_flag ? flag_is_set() : true;
    const mirror::Plan plan = mirror::plan(input);
    if (plan.write_mirror) write_mirror(input.backend);
    return plan;
}

// After every source change and every sync (task_refresh) and when Settings > Apps opens.
void push_backend_to_mirror()
{
    reconcile(false);
}

} // namespace registry_sync

// Settings > Apps is for sideloading: only the sources the user added themselves are shown. The built-in
// CardputerZero Hub stays in the Store app.
// The catalogue the page shows, and (P3) the count the Updates page's "App updates" row shows: one place for the filter.
std::atomic<int> g_app_update_count{-1}; // -1: no catalogue read yet
std::atomic<bool> g_app_update_reading{false};

void load_catalogue(std::vector<apps_backend::App> &apps, std::vector<apps_backend::Source> &sources)
{
    const auto summary = apps_backend::run({"--summary"}, 60);
    const auto registries = apps_backend::run({"--registries"}, 30);
    apps = apps_backend::parse_apps(summary.out);
    // the screen fields are not in --summary: they come from the registries the backend caches (read only)
    const auto needs = settings_apps::read_cache_needs();
    for (apps_backend::App &app : apps) {
        const auto found = needs.find(app.id);
        if (found == needs.end()) continue;
        app.min_size = found->second.min_size;
        app.portrait_min_size = found->second.portrait_min_size;
        // an installed app: the facts of its own .desktop (what the launcher itself applies), the registry's otherwise
        settings_apps::ScreenNeeds own;
        if (app.installed && settings_apps::read_desktop_needs(found->second.desktop, own)) {
            app.min_size = own.min_size;
            app.portrait_min_size = own.portrait_min_size;
        }
    }
    sources = apps_backend::parse_sources(registries.out);
    sources.erase(std::remove_if(sources.begin(), sources.end(),
                                 [](const apps_backend::Source &x) { return x.builtin; }),
                  sources.end());
    apps.erase(std::remove_if(apps.begin(), apps.end(), [&](const apps_backend::App &app) {
        return std::none_of(sources.begin(), sources.end(),
                            [&](const apps_backend::Source &x) { return x.name == app.source; });
    }), apps.end());
    g_app_update_count = static_cast<int>(
        std::count_if(apps.begin(), apps.end(), [](const apps_backend::App &app) { return app.upgradable(); }));
}

void task_refresh(const std::shared_ptr<Shared> &s)
{
    std::vector<apps_backend::App> apps;
    std::vector<apps_backend::Source> sources;
    load_catalogue(apps, sources);
    std::unique_lock<std::mutex> lock(s->mutex);
    s->apps = std::move(apps);
    s->sources = std::move(sources);
    s->apps_ready = s->sources_ready = true;
    lock.unlock();
    registry_sync::push_backend_to_mirror();   // the Store imports its own copy at start: keep it equal to the backend
}

// The first time Settings > Apps opens (once per user, the config key apps_default_source): when the Store holds no source of
// the user's own, the project's source is added (the backend fetches it as it adds it: "Adding ..." is the page's
// normal progress) and the decision is kept, so a source the user removes later is not added back. A source of
// theirs already there: only the decision is kept. A failed add (no network) leaves the decision open for the next time.
void task_default_source(const std::shared_ptr<Shared> &s)
{
    const auto plan = registry_sync::reconcile(true);
    if (plan.set_flag) registry_sync::set_flag();
    if (!plan.add_default) {
        s->finish("");
        return;
    }
    std::string url, name, error;
    if (!apps_backend::normalize_source(apps_registry_mirror::kDefaultSource, url, name, error)) {
        s->finish("");
        return;
    }
    s->say("Adding " + name + "...");
    const auto result = apps_backend::run({"--add-registry", url, "--registry-name", name}, 90);
    // "already exists": the other writer (install.sh) was first; the list has it, which is what matters
    const bool present = result.rc == 0 || result.out.find("registry already exists") != std::string::npos;
    if (present) registry_sync::set_flag();
    task_refresh(s);
    if (present) s->finish(result.rc == 0 ? "Source added" : "");
    else s->finish(apps_backend::error_text(result.out, "Could not add the default source"), false, true);
}

void spawn(std::function<void()> work)
{
    std::thread(std::move(work)).detach();
}

void task_sync(std::shared_ptr<Shared> s, std::string only_url)
{
    // Re-read only the user's own sources (editing a registry to itself re-syncs just that one). A source that
    // fails is reported with its reason and the others are still synced. only_url: that source alone (the native
    // page's source action list).
    std::vector<apps_backend::Source> mine;
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        for (const auto &source : s->sources)
            if (source.enabled && (only_url.empty() || source.url == only_url)) mine.push_back(source);
        std::vector<std::pair<std::string, std::string>> names;
        for (const auto &source : mine) names.emplace_back(source.url, source.name);
        s->sync.reset(names);
        s->publish_sync_locked();
    }
    for (size_t i = 0; i < mine.size(); ++i) {
        {
            std::lock_guard<std::mutex> lock(s->mutex);
            s->sync.start(i);
            s->publish_sync_locked();
        }
        const auto result = apps_backend::run(
            {"--edit-registry", mine[i].url, mine[i].url, "--registry-name", mine[i].name}, 120);
        // The backend exits 0 even for a source it could not fetch (it keeps the cached list and says "cached"),
        // so what it really did is read from its own report and from the source's record afterwards.
        std::string registry_status, registry_error;
        const auto registries = apps_backend::run({"--registries"}, 30);
        for (const auto &source : apps_backend::parse_sources(registries.out)) {
            if (source.url != mine[i].url) continue;
            registry_status = source.status;
            registry_error = source.error;
        }
        const auto verdict = apps_status::decide_sync(result.rc, result.out, registry_status, registry_error);
        const std::string reason = verdict.ok ? ""
            : apps_status::sync_failure_reason(verdict.backend_text, probe_source(mine[i].url, verdict.backend_text));
        std::lock_guard<std::mutex> lock(s->mutex);
        if (verdict.ok) s->sync.succeed(i);
        else s->sync.fail(i, reason);
        s->publish_sync_locked();
    }
    task_refresh(s);
    int severity = 0;
    std::string text;
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        severity = s->sync.severity();
        text = mine.empty() ? "No source to sync" : s->sync.status();
    }
    s->finish(text, severity == 2, severity == 1);   // amber when only some sources failed, red when all did
}

void task_package(std::shared_ptr<Shared> s, std::string action, std::string id, std::string name, std::string size);

void task_finalize(std::shared_ptr<Shared> s, std::string action, std::string id, std::string tx, std::string name,
                   std::string version)
{
    s->set_stage("Finishing");
    const auto result = apps_backend::run({"--finalize-package", action, id, tx}, 120);
    task_refresh(s);
    if (result.rc != 0) {
        s->fail(apps_status::package_failure(result.out, result.rc, network_up(), true));
        return;
    }
    std::string text;
    if (action == "uninstall") text = "Removed " + name;
    else if (action == "upgrade") text = "Updated " + name + (version.empty() ? "" : " to " + version);
    else text = "Installed " + name + (version.empty() ? "" : " " + version);

    // "Update all": go on with the next app.
    std::string next_id, next_name, next_size;
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        if (s->queue.active() && !s->queue.empty()) {
            const auto next = s->queue.take();
            next_id = next.first;
            next_name = next.second;
            for (const auto &app : s->apps)
                if (app.id == next_id) next_size = app.size;
        } else if (s->queue.active()) {
            s->queue.clear();
        }
    }
    if (!next_id.empty()) {
        s->say(text);
        task_package(s, "upgrade", next_id, next_name, next_size);
        return;
    }
    s->succeed(text);
}

void task_package(std::shared_ptr<Shared> s, std::string action, std::string id, std::string name, std::string size)
{
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        s->package_id = id;
    }
    // report 047 P4: Settings > Updates runs a package job (an upgrade, a repair, a launcher update, a password asked):
    // one dpkg at a time
    const std::string busy = launcher_apt::system_job_busy();
    if (!busy.empty()) {
        s->fail({"Another installation is running", busy + ": try again when it is done"});
        return;
    }
    if (action != "uninstall") {
        const std::string problem = apps_status::space_problem(apps_status::parse_size_bytes(size), free_bytes());
        if (!problem.empty()) {
            s->fail({"Not enough space", problem});
            return;
        }
    }
    // A failed dpkg step of this same app was set aside (see sudo_complete_cb): bring it back so the retry resumes.
    apps_backend::restore_pending_transaction(id, action);
    s->set_stage(action == "uninstall" ? "Preparing removal" : "Downloading " + shorten(name, 16));
    const auto prepared = apps_backend::run({"--prepare-package", action, id}, 600);
    apps_backend::PackageJob job;
    if (prepared.rc != 0 || !apps_backend::parse_package_job(prepared.out, job)) {
        s->fail(apps_status::package_failure(prepared.out, prepared.rc, network_up(), true));
        return;
    }
    std::lock_guard<std::mutex> lock(s->mutex);
    std::string version;
    for (const auto &app : s->apps)
        if (app.id == id) version = app.version;
    s->sudo_argv = apps_backend::privileged_argv(job);
    s->job_action = action;
    s->job_id = id;
    s->job_transaction = job.transaction;
    s->job_name = name;
    s->job_version = version;
    s->sudo_pending = true;
    s->stage.clear();
    s->status = "Type your password";
    s->status_error = false;
    s->status_warn = false;
    s->status_dirty = true;
}

struct SudoContext {
    std::shared_ptr<Shared> shared;
    std::string action, id, transaction, name, version;
    std::string output;      // what the privileged helper printed (progress lines, dpkg's complaints)
};

void sudo_output_cb(const char *data, size_t size, void *user)
{
    auto *context = static_cast<SudoContext *>(user);
    if (!context || !data) return;
    if (context->output.size() < (64u << 10)) context->output.append(data, size);
    const std::string stage = apps_status::progress_text(context->output);
    if (!stage.empty()) context->shared->set_stage(stage);
}

void sudo_complete_cb(cp0_sudo_result_t result, int exit_code, void *user)
{
    std::unique_ptr<SudoContext> context(static_cast<SudoContext *>(user));
    if (!context) return;
    if (result == CP0_SUDO_RESULT_SUCCESS) {
        auto c = *context;
        spawn([c] { task_finalize(c.shared, c.action, c.id, c.transaction, c.name, c.version); });
        return;
    }
    if (result == CP0_SUDO_RESULT_AUTH_FAILED) context->shared->finish("Wrong password", true);
    else if (result == CP0_SUDO_RESULT_CANCELLED) context->shared->finish("Cancelled", true);
    else if (result == CP0_SUDO_RESULT_TIMED_OUT) context->shared->finish("Timed out", true);
    else {
        // The backend keeps this half-done transaction and would refuse any other app ("another package
        // transaction is pending"): set it aside. Retrying this app brings it back (task_package).
        apps_backend::park_pending_transaction(context->id, context->action);
        context->shared->fail(apps_status::package_failure(context->output, exit_code, true, false));
    }
}

} // namespace

Jobs::Jobs() : shared_(std::make_shared<Shared>()) {}

bool Jobs::available()
{
    return apps_backend::available();
}

bool Jobs::begin()
{
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (shared_->busy) return false;
    shared_->busy = true;
    shared_->package_id.clear();
    return true;
}

void Jobs::load()
{
    if (!begin()) return;
    auto shared = shared_;
    shared->say("Loading...");
    spawn([shared] {
        task_refresh(shared);
        task_default_source(shared);
    });
}

bool Jobs::package(const std::string &action, const std::string &id, const std::string &name, const std::string &size)
{
    if (!begin()) return false;
    auto shared = shared_;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->package_id = id;
        shared->begin_package_locked();
    }
    spawn([shared, action, id, name, size] { task_package(shared, action, id, name, size); });
    return true;
}

bool Jobs::update_all(const std::vector<std::pair<std::string, std::string>> &items, const std::string &first_size)
{
    if (items.empty()) return false;
    auto shared = shared_;
    std::pair<std::string, std::string> first;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->busy) return false;
        shared->busy = true;
        shared->begin_package_locked();
        shared->queue.reset(items);
        first = shared->queue.take();
        shared->package_id = first.first;
    }
    spawn([shared, first, first_size] { task_package(shared, "upgrade", first.first, first.second, first_size); });
    return true;
}

bool Jobs::sync(const std::string &url)
{
    if (!begin()) return false;
    auto shared = shared_;
    spawn([shared, url] { task_sync(shared, url); });
    return true;
}

bool Jobs::toggle_source(const std::string &url, bool enable)
{
    if (!begin()) return false;
    auto shared = shared_;
    spawn([shared, url, enable] {
        const auto result = apps_backend::run({enable ? "--enable-registry" : "--disable-registry", url}, 30);
        task_refresh(shared);
        shared->finish(result.rc == 0 ? "" : apps_backend::error_text(result.out, "Failed"));
    });
    return true;
}

bool Jobs::remove_source(const std::string &url)
{
    if (!begin()) return false;
    auto shared = shared_;
    spawn([shared, url] {
        const auto result = apps_backend::run({"--remove-registry", url}, 30);
        task_refresh(shared);
        shared->finish(result.rc == 0 ? "Source removed" : apps_backend::error_text(result.out, "Failed"));
    });
    return true;
}

bool Jobs::add_source(const std::string &url, const std::string &name)
{
    if (!begin()) return false;
    auto shared = shared_;
    shared->say("Adding " + shorten(name, 20) + "...");
    spawn([shared, url, name] {
        const auto result = apps_backend::run({"--add-registry", url, "--registry-name", name}, 90);
        task_refresh(shared);
        shared->finish(result.rc == 0 ? "Source added" : apps_backend::error_text(result.out, "Could not add the source"));
    });
    return true;
}

void Jobs::mirror_at_start()
{
    if (!available()) return;
    spawn([] {
        std::this_thread::sleep_for(std::chrono::seconds(3));   // after the home screen is up
        registry_sync::push_backend_to_mirror();
    });
}

JobsUpdate Jobs::take()
{
    JobsUpdate update;
    Shared &s = *shared_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.apps_ready || s.sources_ready) {
        update.lists = true;
        update.apps = s.apps;
        update.sources = s.sources;
        s.apps_ready = s.sources_ready = false;
    }
    if (s.sync_dirty) {
        update.sync = true;
        update.progress = s.sync;
        s.sync_dirty = false;
    }
    if (s.message_pending) {
        update.failure = true;
        update.failure_title = s.message_title;
        update.failure_detail = s.message_detail;
        s.message_pending = false;
    }
    if (s.status_dirty) {
        update.status = true;
        update.status_text = s.status;
        update.status_error = s.status_error;
        update.status_warn = s.status_warn;
        s.status_dirty = false;
    } else if (s.busy && !s.stage.empty()) {
        // a running step shows how long it has been going: the sign that nothing is stuck
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - s.stage_since).count();
        update.status = true;
        update.status_text = apps_status::stage_line(s.stage, s.stage_position, s.stage_total, static_cast<long>(seconds));
    }
    update.sudo = s.sudo_pending;
    update.busy = s.busy;
    if (s.busy) update.package_id = s.package_id;
    return update;
}

void Jobs::start_sudo()
{
    auto shared = shared_;
    std::vector<std::string> argv_storage;
    auto context = std::make_unique<SudoContext>();
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (!shared->sudo_pending) return;
        shared->sudo_pending = false;
        argv_storage = shared->sudo_argv;
        context->shared = shared;
        context->action = shared->job_action;
        context->id = shared->job_id;
        context->transaction = shared->job_transaction;
        context->name = shared->job_name;
        context->version = shared->job_version;
        shared->stage = "Type your password";
        shared->stage_since = std::chrono::steady_clock::now();
    }
    std::vector<const char *> argv;
    for (const std::string &arg : argv_storage) argv.push_back(arg.c_str());
    argv.push_back(nullptr);
    uint64_t request_id = 0;
    SudoContext *raw = context.release();
    const int rc = cp0_sudo_run_argv_async_ex(
        argv.data(), CP0_SUDO_CALLBACK_LVGL, &sudo_output_cb, &sudo_complete_cb, raw,
        60 * 1000, 15 * 60 * 1000, &request_id);
    if (rc != 0) {
        delete raw;
        shared->finish("Could not ask for the password");
    }
}

bool package_job_running()
{
    return g_package_jobs.load() > 0;
}

AppUpdates app_updates()
{
    AppUpdates updates;
    const int count = g_app_update_count.load();
    updates.known = count >= 0;
    updates.count = count < 0 ? 0 : count;
    return updates;
}

void refresh_app_updates()
{
    if (!Jobs::available() || g_app_update_reading.exchange(true)) return;
    std::thread([] {
        std::vector<apps_backend::App> apps;
        std::vector<apps_backend::Source> sources;
        load_catalogue(apps, sources);
        g_app_update_reading = false;
    }).detach();
}

} // namespace settings_apps
