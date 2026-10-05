/*
 * Settings > Apps: access to the app catalogue through the Store's native backend
 * (M5CardputerZero-AppStore): sources (registries), the app list, install and remove.
 *
 * Nothing here touches LVGL, so it can run on worker threads.
 */

#pragma once

#include <string>
#include <vector>

namespace apps_backend {

struct App {
    std::string id;            // uuid / share code understood by the backend
    std::string title;
    std::string version;       // version offered by the catalogue
    std::string installed_version;
    std::string category;
    std::string author;
    std::string description;
    std::string source;        // name of the registry the entry comes from
    std::string package;
    std::string size;          // download size as the catalogue states it (bytes, "1.4 MB" or "online")
    bool installed = false;

    /* True when the catalogue offers a newer version than the installed one (Debian version order). */
    bool upgradable() const;
    /* True when the app is installed and shows its own version only (nothing newer on offer). */
    bool current() const { return installed && !upgradable(); }
};

struct Source {
    std::string url;
    std::string name;
    std::string status;        // "ok", "cached", "error", "not synced" ...
    std::string synced_at;
    std::string error;         // why the last sync failed (empty when it did not)
    int apps = 0;
    bool enabled = true;
    bool builtin = false;
};

struct Result {
    int rc = -1;
    std::string out;           // stdout and stderr, in order
};

struct PackageJob {
    std::string action;        // install / uninstall / ...
    std::string value;
    std::string desktop;
    std::string transaction;
    std::string pending_path;
    bool reinstall = false;
    std::vector<std::string> executables;
};

/* Path of the backend executable (APPLAUNCH_APPSTORE_BIN overrides the default). */
std::string backend_path();
bool available();

/* Debian version order (dpkg --compare-versions): epoch, upstream version, revision, "~" sorts before
 * anything. Returns <0, 0 or >0. */
int compare_versions(const std::string &a, const std::string &b);

/* Runs any program with `args` (same capture and timeout rules as run()). */
Result run_program(const std::string &binary, const std::vector<std::string> &args, int timeout_s);

/* Runs the backend with `args`, waits for it (at most timeout_s seconds) and captures its output. */
Result run(const std::vector<std::string> &args, int timeout_s);

std::vector<App> parse_apps(const std::string &summary_output);
std::vector<Source> parse_sources(const std::string &registries_output);

/* "owner/repo", "github.com/owner/repo[/...]", "https://github.com/owner/repo[/tree/branch]" or a direct
 * https URL to a registry.json -> the URL of its registry.json and a short name. */
bool normalize_source(const std::string &input, std::string &url, std::string &name, std::string &error);

bool parse_package_job(const std::string &prepare_output, PackageJob &job);
/* The privileged command (run through sudo) that applies a prepared package job. */
std::vector<std::string> privileged_argv(const PackageJob &job);

/* The backend keeps one "pending package transaction" file in its state directory (M5APPSTORE_STATE_DIR, else
 * ~/.local/share/cardputerzero-appstore). After a failed dpkg step the half-installed package stays in that file
 * and the backend refuses every install of a different app until it is finished. These two calls keep the
 * Store backend unchanged: `park_pending_transaction` moves the file aside under a name that holds the app id and
 * action, `restore_pending_transaction` puts it back before that same app is retried (so the retry resumes from
 * the saved download as before). Both return true when a file was moved. */
std::string state_dir();
bool park_pending_transaction(const std::string &app_id, const std::string &action);
bool restore_pending_transaction(const std::string &app_id, const std::string &action);

/* Text of the last "ERROR" record of a backend output, or a fallback. */
std::string error_text(const std::string &output, const std::string &fallback);

} // namespace apps_backend
