/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps, the rules of the native page (task 014 P3d): pure (no LVGL, no thread, no I/O;
 * tests/test_settings_apps_model.cpp). The legacy page's behaviour (settings_apps_page.cpp) without its drawing, plus
 * the native page's action lists:
 *
 *   tabs      Apps (the catalogue of the user's own sources) and Sources. A pending confirmation and an open action list
 *             are dropped when the tab changes.
 *   rows      Apps: "Update all (N)" first when more than one update is offered, then every app (title; value: its
 *             installed version, "installed>offered" when an update is offered, "v<offered>" when it is not
 *             installed), "Loading..." / "No apps: add your source in Sources" / "The Store is needed (backend
 *             missing)" while the list is empty, and a "Sources" row (the native page's way to the Sources tab by
 *             touch). Sources: every source (name; value: "off", "waiting", "done", "failed", "<N> apps", "not
 *             synced"), "+ Add a GitHub source", "Sync my sources". The app a package job runs for and the source being
 *             synced are busy rows. Values carry the legacy page's colours as tones (update amber, installed / synced
 *             green, failed red, the rest muted).
 *   actions   an app's or a source's action list (a level of its own): Install (not installed) / Update (an update is
 *             offered) / Remove (installed), Cancel; Enable or Disable, Sync (enabled), Remove, Cancel. A tap on an app
 *             or a source opens it; Enter (the keyboard) keeps the legacy page's direct action (install, update, a
 *             source on / off) and opens the list only for an app that is up to date (nothing to install or update).
 *             Nothing starts while a job runs or without the Store backend; going to the Sources tab, opening and
 *             leaving a list always work.
 *   confirm   Remove (the list's row, or D / Del on the row) asks first (pending); confirm() gives the removal.
 *   caption   the status line: the reason of the highlighted (or listed) failed source while nothing runs
 *             ("owner/repo: HTTP 404", "(cached list)" when its last good list is still used), else the workers' status
 *             with its severity (amber when only some sources failed, as the legacy page).
 *
 * The page feeds it what the workers report (settings_apps_jobs.hpp) and draws rows().
 */
#pragma once

#include "apps_backend.hpp"
#include "apps_status_model.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace settings_apps {

enum class Tab { Apps, Sources };

enum class RowType {
    UpdateAll,   /* "Update all (N)" */
    App,         /* key: the app's id */
    Placeholder, /* the empty catalogue's text (busy while the first list loads) */
    Sources,     /* the Sources tab */
    Source,      /* key: the source's URL */
    AddSource,   /* "+ Add a GitHub source" */
    Sync,        /* "Sync my sources" */
    /* an action list's rows (key: the app's id / the source's URL) */
    Install,
    Update,
    Remove,
    Enable,
    Disable,
    SyncSource,
    Cancel,
};

/* A value's colour, the legacy page's: Good green (installed, synced), Update amber (an update), Bad red (failed),
 * Muted grey (not installed, off, waiting, not synced), Normal the roller's own. */
enum class Tone { Normal, Good, Update, Bad, Muted };

/* The status line's colour: Warn amber (some sources failed), Error red. */
enum class Severity { Normal, Warn, Error };

struct Row {
    RowType type = RowType::Placeholder;
    std::string key;
    std::string label;
    std::string value;
    bool busy = false; /* a job runs for it: the label and a spinner */
    Tone tone = Tone::Normal;
};

enum class ActionKind {
    None,
    Say,          /* a status message only (message) */
    Install,      /* id, name, size */
    Upgrade,      /* id, name, size */
    Uninstall,    /* id, name, size: a confirmed removal */
    UpdateAll,    /* items; size: the first item's download size */
    Sync,         /* id: one source's URL, empty: every enabled source */
    ToggleSource, /* id: the URL; enable: the new state */
    RemoveSource, /* id: the URL: a confirmed removal */
    AddSource,    /* open the add-source entry */
    ShowSources,  /* the Sources tab */
    OpenActions,  /* the row's action list (open_actions) */
    CloseActions, /* back from the action list (Cancel) */
    AskRemove,    /* the confirmation of a removal (ask_remove on the same row) */
};

struct Action {
    ActionKind kind = ActionKind::None;
    std::string id;
    std::string name;
    std::string size;
    bool enable = false;
    std::vector<std::pair<std::string, std::string>> items; /* UpdateAll: (id, title) of every app with an update */
    std::string message;
};

enum class Confirm { None, RemoveApp, RemoveSource };

/* The legacy page's cut for narrow texts: "Weather Sta~". */
std::string shorten(const std::string &text, std::size_t limit);

class Model {
public:
    /* ---- what the page knows */

    /* No Store backend: every action is refused, the status says "Store backend not installed". */
    void set_backend_missing(bool missing);
    bool backend_missing() const { return backend_missing_; }
    /* A fresh catalogue and source list from the workers (the first one ends "Loading..."). */
    void set_lists(std::vector<apps_backend::App> apps, std::vector<apps_backend::Source> sources);
    bool loaded() const { return loaded_; }
    void set_sync(const apps_status::SyncProgress &sync) { sync_ = sync; }
    /* A job runs (nothing else starts); package_id: the app the running package job is for ("" none). */
    void set_busy(bool busy, const std::string &package_id = std::string());
    bool busy() const { return busy_; }
    const std::string &package_id() const { return package_id_; }
    /* The status line: the workers' latest status (error: red; warn: some sources failed) or a message of the page. */
    void set_status(const std::string &text, bool error = false, bool warn = false);
    const std::string &status() const { return status_; }
    bool status_warn() const { return status_warn_; }

    const std::vector<apps_backend::App> &apps() const { return apps_; }
    const std::vector<apps_backend::Source> &sources() const { return sources_; }

    /* ---- tabs and action lists */

    Tab tab() const { return tab_; }
    void set_tab(Tab tab);
    /* Row `index` (an app or a source) shows its action list; false for any other row. */
    bool open_actions(int index);
    void close_actions();
    bool in_actions() const { return actions_; }
    /* The app's title / the source's name the list is for. */
    std::string actions_title() const;
    /* The list's app or source is still there (a refreshed list may have lost it). */
    bool actions_valid() const;
    /* The row of the list's app or source on its tab (where the highlight goes back to), -1 when it is gone. */
    int actions_row() const;

    /* ---- the rows shown */

    std::vector<Row> rows() const;
    int upgradable_count() const;
    bool update_all_row() const; /* the "Update all (N)" row heads the list when N > 1 */
    /* The index in rows() of the row with this type and key, -1 when there is none. */
    int find_row(RowType type, const std::string &key = std::string()) const;

    /* ---- actions; index: a row of rows() */

    Action activate(int index) const; /* Enter (the keyboard) */
    Action tap(int index) const;      /* a tap on the highlighted row: an app or a source opens its action list */
    Action update_all() const;        /* U (Apps tab), the "Update all" row */
    Action sync() const;              /* S */
    Action add_source() const;        /* A (Sources tab) */
    /* D / Del on an app or a source, or an action list's Remove: an installed app or a source asks for a confirmation
     * (pending()); false when the row has nothing to remove or a job runs. */
    bool ask_remove(int index);
    Confirm pending() const { return pending_; }
    const std::string &pending_name() const { return pending_name_; } /* the app's title / the source's name */
    /* The confirmed removal; the confirmation is cleared. */
    Action confirm();
    void cancel();

    /* ---- texts */

    /* The status line with row `index` highlighted. */
    std::string caption(int index, Severity &severity) const;
    /* An app row's value: "0.1.2", "0.1.2>0.1.3", "v0.4.0", and its tone. */
    static std::string app_value(const apps_backend::App &app);
    static Tone app_tone(const apps_backend::App &app);
    std::string source_value(const apps_backend::Source &source) const;
    Tone source_tone(const apps_backend::Source &source) const;
    /* Why an enabled source failed: "owner/repo: HTTP 404", " (cached list)" added when its last good list is kept;
     * empty when it did not fail. */
    std::string source_problem(const apps_backend::Source &source) const;
    /* The add-source entry's text (apps_backend::normalize_source): the registry URL and a short name, or the error. */
    static bool parse_source(const std::string &text, std::string &url, std::string &name, std::string &error);

private:
    const apps_backend::App *app(const std::string &id) const;
    const apps_backend::Source *source(const std::string &url) const;
    Row row(int index) const;
    std::vector<Row> list_rows() const;
    std::vector<Row> action_rows() const;
    Action package_action(ActionKind kind, const apps_backend::App &app) const;

    std::vector<apps_backend::App> apps_;
    std::vector<apps_backend::Source> sources_;
    apps_status::SyncProgress sync_;
    Tab tab_ = Tab::Apps;
    bool loaded_ = false;
    bool backend_missing_ = false;
    bool busy_ = false;
    std::string package_id_;
    std::string status_;
    bool status_error_ = false;
    bool status_warn_ = false;
    bool actions_ = false;
    std::string actions_key_; /* the app's id (Apps tab) / the source's URL (Sources tab) */
    Confirm pending_ = Confirm::None;
    std::string pending_key_;
    std::string pending_name_;
    std::string pending_size_;
};

} // namespace settings_apps
