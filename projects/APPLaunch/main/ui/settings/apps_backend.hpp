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
    bool installed = false;

    bool upgradable() const { return installed && !installed_version.empty() && installed_version != version; }
};

struct Source {
    std::string url;
    std::string name;
    std::string status;        // "ok", "error", "not synced" ...
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

/* Text of the last "ERROR" record of a backend output, or a fallback. */
std::string error_text(const std::string &output, const std::string &fallback);

} // namespace apps_backend
