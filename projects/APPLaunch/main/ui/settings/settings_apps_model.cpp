/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps, the rules of the native page (see settings_apps_model.hpp). Each rule of the lists is the legacy
 * page's (deleted in P3e: row_count, render, activate, request_remove_app, start_update_all, shortcut_key);
 * the action lists are the native page's (task 014 P3d, Controller decision), the levels P3e's (Installed / Available
 * split, Controller decision after the Pi 3A+ test).
 */

#include "settings_apps_model.hpp"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace settings_apps {

std::string shorten(const std::string &text, std::size_t limit)
{
    if (text.size() <= limit) return text;
    return text.substr(0, limit > 1 ? limit - 1 : 0) + "~";
}

std::string first_line(const std::string &text)
{
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::size_t from = start, to = end;
        while (from < to && std::isspace(static_cast<unsigned char>(text[from]))) ++from;
        while (to > from && std::isspace(static_cast<unsigned char>(text[to - 1]))) --to;
        if (to > from) return text.substr(from, to - from);
        start = end + 1;
    }
    return {};
}

std::string elide(const std::string &text, std::size_t limit)
{
    std::size_t chars = 0, cut = text.size();
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue; /* a continuation byte */
        if (chars++ == limit) {
            cut = i;
            break;
        }
    }
    return cut == text.size() ? text : text.substr(0, cut) + "...";
}

std::string cap_description(const std::string &text, std::size_t limit)
{
    constexpr std::size_t kDots = 3;
    constexpr std::size_t kMinSentence = 300; /* a sentence end earlier than this leaves too little: cut at a space */
    std::vector<std::size_t> starts; /* byte offset of every character, then the end */
    for (std::size_t i = 0; i < text.size(); ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) starts.push_back(i);
    const std::size_t chars = starts.size();
    if (chars <= limit) return text;
    starts.push_back(text.size());
    const std::size_t room = limit > kDots ? limit - kDots : 0; /* characters of text kept before the dots */
    auto is_space = [&](std::size_t c) {
        const char ch = text[starts[c]];
        return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
    };
    std::size_t keep = 0; /* characters kept; 0: not found */
    bool sentence_cut = false;
    // a sentence end: ". ", "! ", "? " keep the mark; a line break ends the text before it
    for (std::size_t c = room; c > kMinSentence && !keep; --c) {
        const char last = text[starts[c - 1]];
        if (c < chars && (last == '.' || last == '!' || last == '?') && is_space(c)) keep = c;
        else if (c < chars && text[starts[c]] == '\n') keep = c;
    }
    sentence_cut = keep != 0;
    for (std::size_t c = room; c > 0 && !keep; --c)
        if (c < chars && is_space(c)) keep = c; /* the cut stands on a space: the word before it is whole */
    if (!keep) keep = room;
    std::string out = text.substr(0, starts[keep]);
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) out.pop_back();
    if (sentence_cut && !out.empty() && out.back() == '.') out.pop_back(); /* "end." + "..." would make four dots */
    return out + "...";
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

void Model::set_level(Level level)
{
    cancel();
    close_actions();
    level_ = level;
}

int Model::installed_count() const
{
    return static_cast<int>(
        std::count_if(apps_.begin(), apps_.end(), [](const apps_backend::App &entry) { return entry.installed; }));
}

int Model::available_count() const
{
    return static_cast<int>(apps_.size()) - installed_count();
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
    if (level_ == Level::Installed || level_ == Level::Available) {
        const apps_backend::App *chosen = app(actions_key_);
        return chosen ? chosen->title : std::string();
    }
    const apps_backend::Source *chosen = source(actions_key_);
    return chosen ? chosen->name : std::string();
}

std::string Model::actions_description() const
{
    if (!actions_ || level_ == Level::Sources) return {};
    const apps_backend::App *chosen = app(actions_key_);
    return chosen ? cap_description(chosen->description) : std::string();
}

bool Model::actions_valid() const
{
    if (!actions_) return false;
    if (level_ == Level::Installed) {
        const apps_backend::App *chosen = app(actions_key_);
        return chosen && chosen->installed; /* removed: its list has nothing left to do */
    }
    if (level_ == Level::Available) {
        const apps_backend::App *chosen = app(actions_key_);
        return chosen && !chosen->installed; /* installed meanwhile: nothing left to install */
    }
    return source(actions_key_) != nullptr;
}

int Model::actions_row() const
{
    const std::vector<Row> all = list_rows();
    const RowType type = level_ == Level::Sources ? RowType::Source : RowType::App;
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

void Model::add_apps(std::vector<Row> &rows, bool installed) const
{
    bool any = false;
    for (const apps_backend::App &entry : apps_) {
        if (entry.installed != installed) continue;
        rows.push_back({RowType::App, entry.id, entry.title, app_value(entry), busy_ && package_id_ == entry.id,
                        app_tone(entry)});
        any = true;
    }
    if (any) return;
    Row placeholder;
    if (backend_missing_) {
        placeholder.label = "The Store is needed (backend missing)";
    } else if (loaded_) {
        placeholder.label = installed ? "No apps installed" : "Nothing to install";
    } else {
        placeholder.label = "Loading...";
        placeholder.busy = true;
    }
    rows.push_back(std::move(placeholder));
}

std::vector<Row> Model::list_rows() const
{
    std::vector<Row> rows;
    const bool counted = loaded_ && !backend_missing_;
    auto count = [&](const char *label, int n) {
        return std::string(label) + (counted ? " (" + std::to_string(n) + ")" : std::string());
    };
    switch (level_) {
    case Level::Root:
        rows.push_back({RowType::Installed, {}, count("Installed", installed_count()), {}, false, Tone::Normal});
        rows.push_back({RowType::Available, {}, count("Available", available_count()), {}, false, Tone::Normal});
        rows.push_back({RowType::Sources, {}, "Sources", {}, false, Tone::Normal});
        return rows;
    case Level::Installed:
        if (update_all_row())
            rows.push_back({RowType::UpdateAll, {}, "Update all (" + std::to_string(upgradable_count()) + ")", {}, false,
                            Tone::Normal});
        add_apps(rows, true);
        return rows;
    case Level::Available:
        add_apps(rows, false);
        return rows;
    case Level::Sources:
        break;
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
    if (level_ == Level::Installed) {
        if (const apps_backend::App *chosen = app(actions_key_)) {
            const std::string line = first_line(chosen->description); /* no description: no row */
            if (!line.empty()) rows.push_back({RowType::Description, actions_key_, "Description", line, false, Tone::Muted});
            if (chosen->upgradable()) add(RowType::Update, "Update");
            if (chosen->installed) add(RowType::Remove, "Remove");
        }
    } else if (level_ == Level::Available) {
        if (const apps_backend::App *chosen = app(actions_key_)) {
            const std::string line = first_line(chosen->description);
            if (!line.empty()) rows.push_back({RowType::Description, actions_key_, "Description", line, false, Tone::Muted});
            if (!chosen->installed) add(RowType::Install, "Install");
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
    case RowType::Installed:
    case RowType::Available:
    case RowType::Sources:
        action.kind = ActionKind::ShowLevel;
        action.level = target.type == RowType::Installed
                           ? Level::Installed
                           : (target.type == RowType::Available ? Level::Available : Level::Sources);
        return action;
    case RowType::Cancel:
        action.kind = ActionKind::CloseActions;
        return action;
    case RowType::Description: /* reading works while a job runs or without the backend */
        action.kind = ActionKind::ShowDescription;
        action.id = target.key;
        return action;
    case RowType::App:
        // an installed app: its action list (Update, Remove), never a direct update
        if ((level_ == Level::Installed || level_ == Level::Available) && !actions_) {
            action.kind = ActionKind::OpenActions;
            return action;
        }
        break;
    default:
        break;
    }
    if (busy_ || backend_missing_) return action;
    switch (target.type) {
    case RowType::UpdateAll:
        return update_all();
    case RowType::Install: { /* the Available app's list: the install the one-tap row ran before */
        const apps_backend::App *chosen = app(target.key);
        if (chosen && !chosen->installed) return package_action(ActionKind::Install, *chosen);
        break;
    }
    case RowType::Update: {
        const apps_backend::App *chosen = app(target.key);
        if (chosen && chosen->upgradable()) return package_action(ActionKind::Upgrade, *chosen);
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
    case RowType::Installed:
    case RowType::Available:
    case RowType::Sources:
    case RowType::App:
    case RowType::Description:
    case RowType::Cancel:
        break;
    }
    return action;
}

Action Model::tap(int index) const
{
    const Row target = row(index);
    if (!actions_ && level_ == Level::Sources && target.type == RowType::Source) {
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
    if (level_ == Level::Sources && !actions_ && !busy_ && !backend_missing_) action.kind = ActionKind::AddSource;
    return action;
}

bool Model::ask_remove(int index)
{
    if (busy_) return false;
    const Row target = row(index);
    const bool listed = target.type == RowType::App || target.type == RowType::Source;
    if (!listed && target.type != RowType::Remove) return false;
    if (level_ == Level::Installed && (target.type == RowType::App || target.type == RowType::Remove)) {
        const apps_backend::App *chosen = app(target.key);
        if (!chosen || !chosen->installed) return false;
        pending_ = Confirm::RemoveApp;
        pending_key_ = chosen->id;
        pending_name_ = chosen->title;
        pending_size_ = chosen->size;
        return true;
    }
    if (level_ == Level::Sources && (target.type == RowType::Source || target.type == RowType::Remove)) {
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
    if (!busy_ && level_ == Level::Sources) {
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
