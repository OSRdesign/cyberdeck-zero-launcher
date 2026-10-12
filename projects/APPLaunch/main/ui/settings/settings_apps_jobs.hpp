/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps, the workers of the native page (task 014 P3d). The legacy page's jobs (the page was deleted in P3e),
 * unchanged in what they run and say: the catalogue and source list (--summary, --registries; the built-in source
 * and its apps left out), the per-source sync with its failure reasons, enable / disable / remove / add a source,
 * install / update / remove an app (free space check, --prepare-package, the sudo password, --finalize-package, the
 * parked transaction of a failed dpkg step) and "update all" (one app after the other, a failure stops the run).
 *
 * The jobs run on detached threads holding their own reference to the shared state, so they may outlive the page;
 * the page reads what changed with take() from a timer (the legacy page's 200 ms poll) and hands a prepared package
 * job to the sudo prompt with start_sudo() on the LVGL thread (cp0_sudo_run_argv_async_ex, as the legacy page).
 * The sudo password never passes through here.
 */
#pragma once

#include "apps_backend.hpp"
#include "apps_status_model.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace settings_apps {

/* What changed since the last take(). */
struct JobsUpdate {
    bool lists = false;                        /* a fresh catalogue and source list */
    std::vector<apps_backend::App> apps;
    std::vector<apps_backend::Source> sources;
    bool sync = false;                         /* the per-source sync progress */
    apps_status::SyncProgress progress;
    bool status = false;                       /* the status line: a worker's message, or the running step and its
                                                  seconds ("Downloading Packet Radar (1/2) 5s") */
    std::string status_text;
    bool status_error = false;
    bool status_warn = false;                  /* some sources failed */
    bool failure = false;                      /* a failed install / update / remove: headline and the reason */
    std::string failure_title;
    std::string failure_detail;
    bool sudo = false;                         /* a prepared package job waits for the password: start_sudo() */
    bool busy = false;                         /* a job runs */
    std::string package_id;                    /* the app the running package job is for ("" none) */
};

class Jobs {
public:
    struct Shared;

    Jobs();
    Jobs(const Jobs &) = delete;
    Jobs &operator=(const Jobs &) = delete;

    /* The Store backend is installed (apps_backend::available()). */
    static bool available();

    /* The first list ("Loading..." until it comes). */
    void load();
    /* Each returns false (nothing started) while another job runs. */
    bool package(const std::string &action, const std::string &id, const std::string &name, const std::string &size);
    /* items: (id, title) of every app with an update; first_size: the first one's download size. */
    bool update_all(const std::vector<std::pair<std::string, std::string>> &items, const std::string &first_size);
    /* url: that source alone (a source's action list); empty: every enabled source (the legacy page's sync) */
    bool sync(const std::string &url = std::string());
    bool toggle_source(const std::string &url, bool enable);
    bool remove_source(const std::string &url);
    bool add_source(const std::string &url, const std::string &name);

    /* Once at launcher start (native_ui::show_home, the first time): the backend's source list is mirrored into the
     * Store's shared keys on a detached thread a moment later. Local only (--registry-config and the config write):
     * no sync, no add. A source seeded by install.sh then survives the Store being opened before Settings > Apps. */
    static void mirror_at_start();

    JobsUpdate take();
    /* LVGL thread: the prepared package job asks for the sudo password (the shared prompt); the privileged step and
     * --finalize-package follow on the workers. */
    void start_sudo();

private:
    bool begin(); /* busy, unless a job runs already */

    std::shared_ptr<Shared> shared_;
};

} // namespace settings_apps
