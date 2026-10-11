/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps, the rules of the native page (see settings_apps_model.hpp). Each rule of the lists is the legacy
 * page's (settings_apps_page.cpp: row_count, render, activate, request_remove_app, start_update_all, shortcut_key);
 * the action lists are the native page's (task 014 P3d, Controller decision).
 */

#include "settings_apps_model.hpp"

#include <algorithm>
#include <utility>

namespace settings_apps {

std::string shorten(const std::string &text, std::size_t limit)
{
    if (text.size() <= limit) return text;
    return text.substr(0, limit > 1 ? limit - 1 : 0) + "~";
}

void Model::set_backend_missing(bool missing)
{
    backend_missing_ = missing;
    if (missing) set_status("Store backend not installed");
}

void Model::set_lists(std::vector<apps_backend::App> apps, std::vector<apps_backend::Source> sources)
{
    apps_ = std::move(apps);
    sources_ = std::move(sources);
    loaded_ = true;
}

void Model::set_busy(bool busy, const std::string &package_id)
{
    busy_ = busy;
    package_id_ = busy ? package_id : std::string();
}

void Model::set_status(const std::string &text, bool error, bool warn)
{
    status_ = text;
    status_error_ = error;
    status_warn_ = warn;
}

void Model::set_tab(Tab tab)
{
    cancel();
    close_actions();
    tab_ = tab;
}

const apps_backend::App *Model::app(const std::string &id) const
{
    for (const apps_backend::App &entry : apps_)
        if (entry.id == id) return &entry;
    return nullptr;
}

const apps_backend::Source *Model::source(const std::string &url) const
{
    for (const apps_backend::Source &entry : sources_)
        if (entry.url == url) return &entry;
    return nullptr;
}

/* ---- action lists ------------------------------------------------------------------------------------------ */

bool Model::open_actions(int index)
{
    const Row target = row(index);
    if (actions_ || (target.type != RowType::App && target.type != RowType::Source)) return false;
    actions_ = true;
    actions_key_ = target.key;
    return true;
}

void Model::close_actions()
{
    actions_ = false;
    actions_key_.clear();
}

std::string Model::actions_title() const
{
    if (!actions_) return {};
    if (tab_ == Tab::Apps) {
        const apps_backend::App *chosen = app(actions_key_);
        return chosen ? chosen->title : std::string();
    }
    const apps_backend::Source *chosen = source(actions_key_);
    return chosen ? chosen->name : std::string();
}

bool Model::actions_valid() const
{
    if (!actions_) return false;
    return tab_ == Tab::Apps ? app(actions_key_) != nullptr : source(actions_key_) != nullptr;
}

int Model::actions_row() const
{
    const std::vector<Row> all = list_rows();
    const RowType type = tab_ == Tab::Apps ? RowType::App : RowType::Source;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].type == type && all[i].key == actions_key_) return static_cast<int>(i);
    return -1;
}

/* ---- rows -------------------------------------------------------------------------------------------------- */

int Model::upgradable_count() const
{
    return static_cast<int>(std::count_if(apps_.begin(), apps_.end(),
                                          [](const apps_backend::App &entry) { return entry.upgradable(); }));
}

bool Model::update_all_row() const
{
    return upgradable_count() > 1;
}

std::string Model::app_value(const apps_backend::App &app)
{
    // installed > available when an update is offered; a current app shows its version only
    if (app.upgradable()) return shorten(app.installed_version, 8) + ">" + shorten(app.version, 8);
    if (app.installed) return shorten(app.installed_version.empty() ? app.version : app.installed_version, 14);
    return "v" + shorten(app.version, 12);
}

Tone Model::app_tone(const apps_backend::App &app)
{
    return app.upgradable() ? Tone::Update : (app.installed ? Tone::Good : Tone::Muted);
}

std::string Model::source_value(const apps_backend::Source &source) const
{
    if (!source.enabled) return "off";
    const apps_status::SyncEntry *sync = sync_.find(source.url);
    if (sync && sync->state == apps_status::SyncState::Running) return "syncing...";
    if (sync && sync->state == apps_status::SyncState::Waiting) return "waiting";
    if (sync && sync->state == apps_status::SyncState::Failed) return "failed";
    if (sync && sync->state == apps_status::SyncState::Done && sync_.active()) return "done";
    if (source.status == "ok") return std::to_string(source.apps) + " apps";
    if (source.status == "error" || source.status == "cached") return "failed";
    return "not synced";
}

Tone Model::source_tone(const apps_backend::Source &source) const
{
    if (!source.enabled) return Tone::Muted;
    const apps_status::SyncEntry *sync = sync_.find(source.url);
    if (sync && sync->state == apps_status::SyncState::Running) return Tone::Update;
    if (sync && sync->state == apps_status::SyncState::Waiting) return Tone::Muted;
    if (sync && sync->state == apps_status::SyncState::Failed) return Tone::Bad;
    if (sync && sync->state == apps_status::SyncState::Done && sync_.active()) return Tone::Good;
    if (source.status == "ok") return Tone::Good;
    if (source.status == "error" || source.status == "cached") return Tone::Bad;
    return Tone::Muted;
}

std::string Model::source_problem(const apps_backend::Source &source) const
{
    if (!source.enabled) return {};
    const apps_status::SyncEntry *sync = sync_.find(source.url);
    std::string problem;
    if (sync && sync->state == apps_status::SyncState::Failed)
        problem = sync->reason;
    else if (source.status == "error" || source.status == "cached")
        problem = apps_status::sync_failure_reason(source.error, {});
    if (problem.empty()) return {};
    return shorten(source.name, 22) + ": " + problem + (source.status == "cached" ? " (cached list)" : "");
}

std::vector<Row> Model::list_rows() const
{
    std::vector<Row> rows;
    if (tab_ == Tab::Apps) {
        if (update_all_row())
            rows.push_back({RowType::UpdateAll, {}, "Update all (" + std::to_string(upgradable_count()) + ")", {}, false,
                            Tone::Normal});
        for (const apps_backend::App &entry : apps_)
            rows.push_back({RowType::App, entry.id, entry.title, app_value(entry), busy_ && package_id_ == entry.id,
                            app_tone(entry)});
        if (apps_.empty()) {
            Row placeholder;
            if (backend_missing_) {
                placeholder.label = "The Store is needed (backend missing)";
            } else if (loaded_) {
                placeholder.label = "No apps: add your source in Sources";
            } else {
                placeholder.label = "Loading...";
                placeholder.busy = true;
            }
            rows.push_back(std::move(placeholder));
        }
        rows.push_back({RowType::Sources, {}, "Sources", {}, false, Tone::Normal});
        return rows;
    }
    for (const apps_backend::Source &entry : sources_) {
        const apps_status::SyncEntry *sync = sync_.find(entry.url);
        const bool syncing = entry.enabled && sync && sync->state == apps_status::SyncState::Running;
        rows.push_back({RowType::Source, entry.url, entry.name, source_value(entry), syncing, source_tone(entry)});
    }
    rows.push_back({RowType::AddSource, {}, "+ Add a GitHub source", {}, false, Tone::Normal});
    rows.push_back({RowType::Sync, {}, "Sync my sources", {}, false, Tone::Normal});
    return rows;
}

std::vector<Row> Model::action_rows() const
{
    std::vector<Row> rows;
    auto add = [&](RowType type, const char *label) { rows.push_back({type, actions_key_, label, {}, false, Tone::Normal}); };
    if (tab_ == Tab::Apps) {
        if (const apps_backend::App *chosen = app(actions_key_)) {
            if (!chosen->installed) add(RowType::Install, "Install");
            if (chosen->upgradable()) add(RowType::Update, "Update");
            if (chosen->installed) add(RowType::Remove, "Remove");
        }
    } else if (const apps_backend::Source *chosen = source(actions_key_)) {
        add(chosen->enabled ? RowType::Disable : RowType::Enable, chosen->enabled ? "Disable" : "Enable");
        if (chosen->enabled) add(RowType::SyncSource, "Sync");
        if (!chosen->builtin) add(RowType::Remove, "Remove");
    }
    add(RowType::Cancel, "Cancel");
    return rows;
}

std::vector<Row> Model::rows() const
{
    return actions_ ? action_rows() : list_rows();
}

Row Model::row(int index) const
{
    const std::vector<Row> all = rows();
    if (index < 0 || index >= static_cast<int>(all.size())) return Row{};
    return all[static_cast<std::size_t>(index)];
}

int Model::find_row(RowType type, const std::string &key) const
{
    const std::vector<Row> all = rows();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].type == type && all[i].key == key) return static_cast<int>(i);
    return -1;
}

/* ---- actions ----------------------------------------------------------------------------------------------- */

Action Model::package_action(ActionKind kind, const apps_backend::App &chosen) const
{
    Action action;
    action.kind = kind;
    action.id = chosen.id;
    action.name = chosen.title;
    action.size = chosen.size;
    return action;
}

Action Model::activate(int index) const
{
    const Row target = row(index);
    Action action;
    switch (target.type) { /* what works while a job runs or without the backend */
    case RowType::Sources:
        action.kind = ActionKind::ShowSources;
        return action;
    case RowType::Cancel:
        action.kind = ActionKind::CloseActions;
        return action;
    case RowType::App: {
        // an up-to-date app: no install or update to start, its action list (Remove) instead
        const apps_backend::App *chosen = app(target.key);
        if (chosen && chosen->current()) action.kind = ActionKind::OpenActions;
        if (chosen && chosen->current()) return action;
        break;
    }
    default:
        break;
    }
    if (busy_ || backend_missing_) return action;
    switch (target.type) {
    case RowType::UpdateAll:
        return update_all();
    case RowType::App:
    case RowType::Install:
    case RowType::Update: {
        const apps_backend::App *chosen = app(target.key);
        if (!chosen) break;
        if (!chosen->installed && target.type != RowType::Update) return package_action(ActionKind::Install, *chosen);
        if (chosen->upgradable() && target.type != RowType::Install) return package_action(ActionKind::Upgrade, *chosen);
        break;
    }
    case RowType::Remove:
        action.kind = ActionKind::AskRemove;
        break;
    case RowType::Source:
    case RowType::Enable:
    case RowType::Disable: {
        const apps_backend::Source *chosen = source(target.key);
        if (!chosen) break;
        if (chosen->builtin) {
            action.kind = ActionKind::Say;
            action.message = "Built-in source";
            break;
        }
        if (target.type == RowType::Enable && chosen->enabled) break;
        if (target.type == RowType::Disable && !chosen->enabled) break;
        action.kind = ActionKind::ToggleSource;
        action.id = chosen->url;
        action.enable = !chosen->enabled;
        break;
    }
    case RowType::SyncSource: {
        const apps_backend::Source *chosen = source(target.key);
        if (!chosen || !chosen->enabled) break;
        action.kind = ActionKind::Sync;
        action.id = chosen->url;
        break;
    }
    case RowType::AddSource:
        action.kind = ActionKind::AddSource;
        break;
    case RowType::Sync:
        return sync();
    case RowType::Placeholder:
    case RowType::Sources:
    case RowType::Cancel:
        break;
    }
    return action;
}

Action Model::tap(int index) const
{
    const Row target = row(index);
    if (!actions_ && (target.type == RowType::App || target.type == RowType::Source)) {
        Action action;
        action.kind = ActionKind::OpenActions;
        return action;
    }
    return activate(index);
}

Action Model::update_all() const
{
    Action action;
    if (backend_missing_) return action;
    for (const apps_backend::App &entry : apps_)
        if (entry.upgradable()) action.items.emplace_back(entry.id, entry.title);
    if (action.items.empty()) {
        action.kind = ActionKind::Say;
        action.message = "Everything is up to date";
        return action;
    }
    if (busy_) return Action{};
    action.kind = ActionKind::UpdateAll;
    if (const apps_backend::App *first = app(action.items.front().first)) action.size = first->size;
    return action;
}

Action Model::sync() const
{
    Action action;
    if (!busy_ && !backend_missing_) action.kind = ActionKind::Sync;
    return action;
}

Action Model::add_source() const
{
    Action action;
    if (tab_ == Tab::Sources && !actions_ && !busy_ && !backend_missing_) action.kind = ActionKind::AddSource;
    return action;
}

bool Model::ask_remove(int index)
{
    if (busy_) return false;
    const Row target = row(index);
    const bool listed = target.type == RowType::App || target.type == RowType::Source;
    if (!listed && target.type != RowType::Remove) return false;
    if (tab_ == Tab::Apps && (target.type == RowType::App || target.type == RowType::Remove)) {
        const apps_backend::App *chosen = app(target.key);
        if (!chosen || !chosen->installed) return false;
        pending_ = Confirm::RemoveApp;
        pending_key_ = chosen->id;
        pending_name_ = chosen->title;
        pending_size_ = chosen->size;
        return true;
    }
    if (tab_ == Tab::Sources && (target.type == RowType::Source || target.type == RowType::Remove)) {
        const apps_backend::Source *chosen = source(target.key);
        if (!chosen || chosen->builtin) return false;
        pending_ = Confirm::RemoveSource;
        pending_key_ = chosen->url;
        pending_name_ = chosen->name;
        pending_size_.clear();
        return true;
    }
    return false;
}

Action Model::confirm()
{
    Action action;
    const Confirm what = pending_;
    action.id = pending_key_;
    action.name = pending_name_;
    action.size = pending_size_;
    cancel();
    if (busy_ || backend_missing_) return Action{};
    if (what == Confirm::RemoveApp) action.kind = ActionKind::Uninstall;
    else if (what == Confirm::RemoveSource) action.kind = ActionKind::RemoveSource;
    else return Action{};
    return action;
}

void Model::cancel()
{
    pending_ = Confirm::None;
    pending_key_.clear();
    pending_name_.clear();
    pending_size_.clear();
}

std::string Model::caption(int index, Severity &severity) const
{
    // a running job's progress first; when idle, the failure of the highlighted source (or of the listed one)
    if (!busy_ && tab_ == Tab::Sources) {
        const std::string key = actions_ ? actions_key_ : (row(index).type == RowType::Source ? row(index).key : "");
        if (const apps_backend::Source *chosen = key.empty() ? nullptr : source(key)) {
            const std::string problem = source_problem(*chosen);
            if (!problem.empty()) {
                severity = Severity::Error;
                return problem;
            }
        }
    }
    severity = status_error_ ? Severity::Error : (status_warn_ ? Severity::Warn : Severity::Normal);
    return status_;
}

bool Model::parse_source(const std::string &text, std::string &url, std::string &name, std::string &error)
{
    return apps_backend::normalize_source(text, url, name, error);
}

} // namespace settings_apps
