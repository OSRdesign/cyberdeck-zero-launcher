/*
 * Settings > Apps (see settings_apps_page.hpp).
 */

#include "settings_apps_page.hpp"

#include "settings_fonts.hpp"

#include "cp0_display.h"
#include "cp0_lvgl_app.h"
#include "input_keys.h"
#include "keyboard_input.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#include <sys/statvfs.h>

namespace {

constexpr int kWidth = 320;
constexpr int kRowY = 24;
constexpr int kRowH = 17;

constexpr uint32_t kBlue = 0x58A6FF;
constexpr uint32_t kText = 0xE0E0E0;
constexpr uint32_t kMuted = 0x8A929B;
constexpr uint32_t kGreen = 0x46DC87;
constexpr uint32_t kYellow = 0xF0B400;
constexpr uint32_t kRed = 0xFF6B6B;

std::string shorten(const std::string &text, size_t limit)
{
    if (text.size() <= limit) return text;
    return text.substr(0, limit > 1 ? limit - 1 : 0) + "~";
}

} // namespace

// State shared with the worker threads (which can outlive the page).
struct LvSettingAppsPage3::Shared {
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
    // what the worker is doing now ("Downloading X"): shown with the seconds it has taken
    std::string stage;
    std::size_t stage_position = 0, stage_total = 0;      // "update all" queue position when one runs
    std::chrono::steady_clock::time_point stage_since;
    // per-source sync progress (copied to the page when dirty) and the "update all" queue
    apps_status::SyncProgress sync;
    bool sync_dirty = false;
    apps_status::UpdateQueue queue;
    // final messages: a success line for the footer, or a failure panel
    std::string notice;
    bool notice_dirty = false;
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
        busy = false;
        if (error) queue.abort();
    }
    void succeed(const std::string &text)
    {
        std::lock_guard<std::mutex> lock(mutex);
        status = text;
        status_error = false;
        status_warn = false;
        status_dirty = true;
        notice = text;
        notice_dirty = true;
        stage.clear();
        busy = false;
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
        busy = false;
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

using Shared = LvSettingAppsPage3::Shared;

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

// Settings > Apps is for sideloading: only the sources the user added themselves are shown. The built-in
// CardputerZero Hub stays in the Store app.
void task_refresh(const std::shared_ptr<Shared> &s)
{
    const auto summary = apps_backend::run({"--summary"}, 60);
    const auto registries = apps_backend::run({"--registries"}, 30);
    auto apps = apps_backend::parse_apps(summary.out);
    auto sources = apps_backend::parse_sources(registries.out);
    sources.erase(std::remove_if(sources.begin(), sources.end(),
                                 [](const apps_backend::Source &x) { return x.builtin; }),
                  sources.end());
    apps.erase(std::remove_if(apps.begin(), apps.end(), [&](const apps_backend::App &app) {
        return std::none_of(sources.begin(), sources.end(),
                            [&](const apps_backend::Source &x) { return x.name == app.source; });
    }), apps.end());
    std::lock_guard<std::mutex> lock(s->mutex);
    s->apps = std::move(apps);
    s->sources = std::move(sources);
    s->apps_ready = s->sources_ready = true;
}

void spawn(std::function<void()> work)
{
    std::thread(std::move(work)).detach();
}

void task_sync(std::shared_ptr<Shared> s)
{
    // Re-read only the user's own sources (editing a registry to itself re-syncs just that one). A source that
    // fails is reported with its reason and the others are still synced.
    std::vector<apps_backend::Source> mine;
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        for (const auto &source : s->sources)
            if (source.enabled) mine.push_back(source);
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

} // namespace

LvSettingAppsPage3::LvSettingAppsPage3(lv_obj_t *parent, const NodeIter &, std::function<void()> back_callback)
    : shared_(std::make_shared<Shared>())
{
    LeaveSelfPage = std::move(back_callback);
    create_ui(parent);
}

LvSettingAppsPage3::~LvSettingAppsPage3()
{
    cp0_display_set_list_drag_inverted(0);
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (keyboard_root_ && keyboard_dsc_) {
        lv_obj_remove_event_dsc(keyboard_root_, keyboard_dsc_);
        keyboard_dsc_ = nullptr;
    }
    leave_text_mode();
    if (ComponensObj) {
        lv_obj_delete(ComponensObj);
        ComponensObj = nullptr;
    }
}

void LvSettingAppsPage3::AnimateNextIn(std::function<void()> callback)
{
    if (callback) callback();
}

void LvSettingAppsPage3::AnimateNextOut(std::function<void()> callback)
{
    if (callback) callback();
}

void LvSettingAppsPage3::LoadNextPage() {}

void LvSettingAppsPage3::LeaveNextPage()
{
    if (LeaveSelfPage) LeaveSelfPage();
}

void LvSettingAppsPage3::create_ui(lv_obj_t *parent)
{
    if (!parent) return;
    cp0_display_set_list_drag_inverted(1);     // the highlight moves here, so the drag direction is reversed
    ComponensObj = lv_obj_create(parent);
    if (!ComponensObj) return;
    lv_obj_set_size(ComponensObj, kWidth, 150);
    lv_obj_set_pos(ComponensObj, 0, 0);
    lv_obj_set_style_bg_color(ComponensObj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ComponensObj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLLABLE);

    auto make_label = [&](lv_obj_t *where, int x, int y, uint32_t color, const lv_font_t *font) {
        lv_obj_t *label = lv_label_create(where);
        lv_label_set_text(label, "");
        lv_obj_set_pos(label, x, y);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        if (font) lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        return label;
    };

    tab_apps_ = make_label(ComponensObj, 8, 3, kBlue, settings_fonts::sans(14, LV_FREETYPE_FONT_STYLE_BOLD));
    lv_label_set_text(tab_apps_, "Apps");
    tab_sources_ = make_label(ComponensObj, 64, 3, kMuted, settings_fonts::sans(14, LV_FREETYPE_FONT_STYLE_BOLD));
    lv_label_set_text(tab_sources_, "Sources");
    for (lv_obj_t *tab : {tab_apps_, tab_sources_}) {
        lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(tab, 6);
    }
    lv_obj_add_event_cb(tab_apps_, [](lv_event_t *e) {
        static_cast<LvSettingAppsPage3 *>(lv_event_get_user_data(e))->set_tab(Tab::Apps);
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(tab_sources_, [](lv_event_t *e) {
        static_cast<LvSettingAppsPage3 *>(lv_event_get_user_data(e))->set_tab(Tab::Sources);
    }, LV_EVENT_CLICKED, this);

    status_ = make_label(ComponensObj, 124, 5, kGreen, settings_fonts::sans(11));
    lv_obj_set_size(status_, 188, 14);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    for (int i = 0; i < kRows; ++i) {
        rows_[i] = lv_obj_create(ComponensObj);
        lv_obj_remove_style_all(rows_[i]);
        lv_obj_remove_flag(rows_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(rows_[i], kWidth, kRowH);
        lv_obj_set_pos(rows_[i], 0, kRowY + i * kRowH);
        lv_obj_set_style_bg_color(rows_[i], lv_color_hex(0x2A2F35), LV_PART_MAIN);
        left_[i] = make_label(rows_[i], 8, 1, kText, settings_fonts::cjk_sans(13));
        lv_obj_set_size(left_[i], 188, 15);
        lv_label_set_long_mode(left_[i], LV_LABEL_LONG_MODE_DOTS);
        right_[i] = make_label(rows_[i], 198, 2, kMuted, settings_fonts::sans(11));
        lv_obj_set_size(right_[i], 114, 14);
        lv_label_set_long_mode(right_[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(right_[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_add_flag(rows_[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(rows_[i], reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(rows_[i], [](lv_event_t *e) {
            auto *self = static_cast<LvSettingAppsPage3 *>(lv_event_get_user_data(e));
            const int row = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e))));
            if (self->offset_ + row >= self->row_count()) return;
            self->pending_ = Pending::None;       // a tap elsewhere cancels a pending remove
            self->selected_ = self->offset_ + row;
            self->activate();
        }, LV_EVENT_CLICKED, this);
    }

    footer_ = make_label(ComponensObj, 8, 132, kGreen, settings_fonts::sans(11));
    lv_obj_set_size(footer_, 304, 14);
    lv_label_set_long_mode(footer_, LV_LABEL_LONG_MODE_DOTS);

    // Add-source editor (shown over the lists).
    edit_panel_ = lv_obj_create(ComponensObj);
    lv_obj_remove_style_all(edit_panel_);
    lv_obj_remove_flag(edit_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(edit_panel_, kWidth, 150);
    lv_obj_set_style_bg_color(edit_panel_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(edit_panel_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(edit_panel_, LV_OBJ_FLAG_HIDDEN);
    edit_title_ = make_label(edit_panel_, 10, 6, kBlue, settings_fonts::sans(14, LV_FREETYPE_FONT_STYLE_BOLD));
    lv_label_set_text(edit_title_, "Add a GitHub source");
    edit_hint_ = make_label(edit_panel_, 10, 30, kMuted, settings_fonts::sans(11));
    lv_obj_set_width(edit_hint_, 300);
    lv_label_set_long_mode(edit_hint_, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(edit_hint_, "owner/repo (or a registry.json address)");
    edit_value_ = make_label(edit_panel_, 10, 62, 0xFFFFFF, settings_fonts::sans(14));
    lv_obj_set_size(edit_value_, 300, 24);
    lv_label_set_long_mode(edit_value_, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_bg_color(edit_value_, lv_color_hex(0x181818), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(edit_value_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(edit_value_, 3, LV_PART_MAIN);

    // Failure panel (shown over the lists): what went wrong, in words.
    msg_panel_ = lv_obj_create(ComponensObj);
    lv_obj_remove_style_all(msg_panel_);
    lv_obj_remove_flag(msg_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(msg_panel_, kWidth, 150);
    lv_obj_set_style_bg_color(msg_panel_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(msg_panel_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(msg_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(msg_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(msg_panel_, [](lv_event_t *e) {
        static_cast<LvSettingAppsPage3 *>(lv_event_get_user_data(e))->dismiss_message();
    }, LV_EVENT_CLICKED, this);
    msg_title_ = make_label(msg_panel_, 10, 8, kRed, settings_fonts::sans(14, LV_FREETYPE_FONT_STYLE_BOLD));
    msg_detail_ = make_label(msg_panel_, 10, 34, kText, settings_fonts::sans(11));
    lv_obj_set_width(msg_detail_, 300);
    lv_label_set_long_mode(msg_detail_, LV_LABEL_LONG_MODE_WRAP);
    msg_hint_ = make_label(msg_panel_, 10, 132, kMuted, settings_fonts::sans(11));
    lv_label_set_text(msg_hint_, "Enter, Esc or tap: close");

    DComponens::lvgl_bind_event(ComponensObj, LV_EVENT_KEY, nullptr,
                                [this](lv_event_t *event) { handle_key(event); });
    // The page's own screen: it is the one that receives the raw keyboard event once it is loaded.
    keyboard_root_ = lv_obj_get_screen(ComponensObj);
    if (!keyboard_root_) keyboard_root_ = lv_screen_active();
    if (keyboard_root_ && LV_EVENT_KEYBOARD != 0)
        keyboard_dsc_ = lv_obj_add_event_cb(keyboard_root_, &LvSettingAppsPage3::keyboard_event_cb,
                                            static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), this);
    timer_ = lv_timer_create(&LvSettingAppsPage3::poll_cb, 200, this);

    backend_missing_ = !apps_backend::available();
    if (backend_missing_) {
        set_status("Store backend not installed");
    } else {
        auto shared = shared_;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->busy = true;
        }
        shared->say("Loading...");
        spawn([shared] {
            task_refresh(shared);
            shared->finish("");
        });
    }
    render();
}

int LvSettingAppsPage3::row_count() const
{
    if (tab_ == Tab::Apps) return static_cast<int>(apps_.size()) + (update_all_row() ? 1 : 0);
    return static_cast<int>(sources_.size()) + 2;   // + "Add" and "Sync" rows
}

int LvSettingAppsPage3::upgradable_count() const
{
    return static_cast<int>(std::count_if(apps_.begin(), apps_.end(),
                                          [](const apps_backend::App &app) { return app.upgradable(); }));
}

bool LvSettingAppsPage3::update_all_row() const
{
    return upgradable_count() > 1;
}

const apps_backend::App *LvSettingAppsPage3::selected_app() const
{
    if (tab_ != Tab::Apps) return nullptr;
    const int index = selected_ - (update_all_row() ? 1 : 0);
    if (index < 0 || index >= static_cast<int>(apps_.size())) return nullptr;
    return &apps_[static_cast<size_t>(index)];
}

void LvSettingAppsPage3::set_status(const std::string &text, bool error, bool warn)
{
    if (!status_) return;
    const std::string shown = (error ? "!" : (warn ? "~" : "=")) + text;
    if (shown == shown_status_) return;
    shown_status_ = shown;
    lv_label_set_text(status_, text.c_str());
    lv_obj_set_style_text_color(status_, lv_color_hex(error ? kRed : (warn ? kYellow : kGreen)), LV_PART_MAIN);
}

void LvSettingAppsPage3::show_message(const std::string &title, const std::string &detail)
{
    if (!msg_panel_) return;
    lv_label_set_text(msg_title_, title.c_str());
    lv_label_set_text(msg_detail_, detail.c_str());
    lv_obj_remove_flag(msg_panel_, LV_OBJ_FLAG_HIDDEN);
    message_showing_ = true;
}

void LvSettingAppsPage3::dismiss_message()
{
    if (!message_showing_) return;
    message_showing_ = false;
    if (msg_panel_) lv_obj_add_flag(msg_panel_, LV_OBJ_FLAG_HIDDEN);
}

void LvSettingAppsPage3::start_update_all()
{
    if (backend_missing_) return;
    std::vector<std::pair<std::string, std::string>> items;
    for (const apps_backend::App &app : apps_)
        if (app.upgradable()) items.emplace_back(app.id, app.title);
    if (items.empty()) {
        set_status("Everything is up to date");
        return;
    }
    auto shared = shared_;
    std::pair<std::string, std::string> first;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->busy) return;
        shared->busy = true;
        shared->queue.reset(items);
        first = shared->queue.take();
    }
    std::string size;
    for (const apps_backend::App &app : apps_)
        if (app.id == first.first) size = app.size;
    pending_ = Pending::None;
    notice_.clear();
    spawn([shared, first, size] { task_package(shared, "upgrade", first.first, first.second, size); });
    render();
}

void LvSettingAppsPage3::set_tab(Tab tab)
{
    if (editing_) return;
    pending_ = Pending::None;
    notice_.clear();
    if (tab == tab_) return;
    tab_ = tab;
    selected_ = 0;
    offset_ = 0;
    render();
}

void LvSettingAppsPage3::move_selection(int delta)
{
    pending_ = Pending::None;
    notice_.clear();
    const int count = row_count();
    if (count <= 0) return;
    selected_ = std::clamp(selected_ + delta, 0, count - 1);
    if (selected_ < offset_) offset_ = selected_;
    if (selected_ >= offset_ + kRows) offset_ = selected_ - kRows + 1;
    render();
}

void LvSettingAppsPage3::render()
{
    const bool apps_tab = tab_ == Tab::Apps;
    lv_obj_set_style_text_color(tab_apps_, lv_color_hex(apps_tab ? kBlue : kMuted), LV_PART_MAIN);
    lv_obj_set_style_text_color(tab_sources_, lv_color_hex(apps_tab ? kMuted : kBlue), LV_PART_MAIN);

    const int count = row_count();
    if (selected_ >= count) selected_ = std::max(0, count - 1);
    for (int i = 0; i < kRows; ++i) {
        const int index = offset_ + i;
        if (index >= count) {
            lv_obj_add_flag(rows_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(rows_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(rows_[i], index == selected_ ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        std::string left, right;
        uint32_t left_color = kText, right_color = kMuted;
        if (apps_tab) {
            const int app_index = index - (update_all_row() ? 1 : 0);
            if (app_index < 0) {
                left = "Update all (" + std::to_string(upgradable_count()) + ")";
                left_color = kYellow;
            } else {
                const apps_backend::App &app = apps_[static_cast<size_t>(app_index)];
                left = app.title;
                // installed > available when an update is offered; a current app shows its version only
                if (app.upgradable()) right = shorten(app.installed_version, 8) + ">" + shorten(app.version, 8);
                else if (app.installed) right = shorten(app.installed_version.empty() ? app.version : app.installed_version, 14);
                else right = "v" + shorten(app.version, 12);
                right_color = app.upgradable() ? kYellow : (app.installed ? kGreen : kMuted);
                left_color = app.installed ? kGreen : kText;
            }
        } else if (index < static_cast<int>(sources_.size())) {
            const apps_backend::Source &source = sources_[static_cast<size_t>(index)];
            const apps_status::SyncEntry *sync = sync_.find(source.url);
            left = source.name;
            left_color = source.enabled ? kText : kMuted;
            if (!source.enabled) {
                right = "off";
                right_color = kMuted;
            } else if (sync && sync->state == apps_status::SyncState::Running) {
                right = "syncing...";
                right_color = kYellow;
            } else if (sync && sync->state == apps_status::SyncState::Waiting) {
                right = "waiting";
            } else if (sync && sync->state == apps_status::SyncState::Failed) {
                right = "failed";
                right_color = kRed;
            } else if (sync && sync->state == apps_status::SyncState::Done && sync_.active()) {
                right = "done";
                right_color = kGreen;
            } else if (source.status == "ok") {
                right = std::to_string(source.apps) + " apps";
                right_color = kGreen;
            } else if (source.status == "error" || source.status == "cached") {
                right = "failed";
                right_color = kRed;
            } else {
                right = "not synced";
            }
        } else if (index == static_cast<int>(sources_.size())) {
            left = "+ Add a GitHub source";
            left_color = kBlue;
        } else {
            left = "Sync my sources";
            left_color = kBlue;
        }
        lv_label_set_text(left_[i], left.c_str());
        lv_obj_set_style_text_color(left_[i], lv_color_hex(left_color), LV_PART_MAIN);
        lv_label_set_text(right_[i], right.c_str());
        lv_obj_set_style_text_color(right_[i], lv_color_hex(right_color), LV_PART_MAIN);
    }
    if (apps_tab && apps_.empty()) {
        lv_obj_remove_flag(rows_[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(rows_[0], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_width(left_[0], 304);      // the empty-list text uses the whole row
        lv_label_set_text(left_[0], backend_missing_ ? "The Store is needed (backend missing)"
                                                      : (loaded_ ? "No apps: add your source in Sources" : "Loading..."));
        lv_obj_set_style_text_color(left_[0], lv_color_hex(kMuted), LV_PART_MAIN);
        lv_label_set_text(right_[0], "");
    } else {
        lv_obj_set_width(left_[0], 188);
    }

    std::string hint;
    uint32_t hint_color = kGreen;
    const apps_backend::App *chosen = selected_app();
    std::string source_problem;
    if (!apps_tab && selected_ < static_cast<int>(sources_.size())) {
        const apps_backend::Source &source = sources_[static_cast<size_t>(selected_)];
        const apps_status::SyncEntry *sync = sync_.find(source.url);
        if (source.enabled && sync && sync->state == apps_status::SyncState::Failed)
            source_problem = sync->reason;
        else if (source.enabled && (source.status == "error" || source.status == "cached"))
            source_problem = apps_status::sync_failure_reason(source.error, {});
        if (!source_problem.empty())
            source_problem = shorten(source.name, 22) + ": " + source_problem +
                             (source.status == "cached" ? " (cached list)" : "");
    }
    if (pending_ == Pending::Remove && chosen) {
        hint = "Remove " + shorten(chosen->title, 20) + "?  Enter: yes   Esc: no";
        hint_color = kYellow;
    } else if (pending_ == Pending::RemoveSource) {
        hint = "Remove this source?  Enter: yes   Esc: no";
        hint_color = kYellow;
    } else if (!notice_.empty()) {
        hint = notice_;
    } else if (!source_problem.empty()) {
        hint = source_problem;
        hint_color = kRed;
    } else if (apps_tab) {
        if (apps_.empty()) hint = "Right: sources   Esc: back";
        else if (!chosen) hint = "Enter: update all   Right: sources   Esc: back";
        else if (!chosen->installed) hint = "Enter: install   Right: sources   Esc: back";
        else if (chosen->upgradable())
            hint = "Enter: update   D: remove" + std::string(update_all_row() ? "   U: update all" : "");
        else hint = "D: remove   Right: sources   Esc: back";
    } else {
        hint = "Enter: on/off   D: delete   A: add   S: sync";
    }
    lv_label_set_text(footer_, hint.c_str());
    lv_obj_set_style_text_color(footer_, lv_color_hex(hint_color), LV_PART_MAIN);

    if (edit_panel_) {
        if (editing_) lv_obj_remove_flag(edit_panel_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(edit_panel_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LvSettingAppsPage3::request_remove_app()
{
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (shared_->busy) return;
    }
    const apps_backend::App *chosen = update_all_row() && selected_ == 0 ? nullptr : selected_app();
    if (!chosen || !chosen->installed) return;
    pending_ = Pending::Remove;
    render();
}

void LvSettingAppsPage3::activate()
{
    auto shared = shared_;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->busy) return;
    }
    if (backend_missing_) return;

    if (tab_ == Tab::Apps) {
        if (update_all_row() && selected_ == 0) {
            start_update_all();
            return;
        }
        const apps_backend::App *chosen = selected_app();
        if (!chosen) return;
        const apps_backend::App &app = *chosen;
        std::string action;
        if (!app.installed) action = "install";
        else if (app.upgradable()) action = "upgrade";
        else if (pending_ == Pending::Remove) action = "uninstall";
        else {
            render();                 // an up-to-date app: Enter and a tap do nothing, D asks to remove it
            return;
        }
        pending_ = Pending::None;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->busy = true;
        }
        notice_.clear();
        spawn([shared, action, id = app.id, name = app.title, size = app.size] {
            task_package(shared, action, id, name, size);
        });
        render();
        return;
    }

    // Sources tab
    const int sources = static_cast<int>(sources_.size());
    if (selected_ < sources) {
        toggle_source();
    } else if (selected_ == sources) {
        begin_edit();
    } else {
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->busy = true;
        }
        spawn([shared] { task_sync(shared); });
    }
}

void LvSettingAppsPage3::toggle_source()
{
    if (selected_ >= static_cast<int>(sources_.size())) return;
    const apps_backend::Source &source = sources_[static_cast<size_t>(selected_)];
    if (source.builtin) {
        set_status("Built-in source");
        return;
    }
    auto shared = shared_;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->busy = true;
    }
    spawn([shared, url = source.url, enable = !source.enabled] {
        const auto result = apps_backend::run({enable ? "--enable-registry" : "--disable-registry", url}, 30);
        task_refresh(shared);
        shared->finish(result.rc == 0 ? "" : apps_backend::error_text(result.out, "Failed"));
    });
}

void LvSettingAppsPage3::remove_selected_source()
{
    if (selected_ >= static_cast<int>(sources_.size())) return;
    const apps_backend::Source &source = sources_[static_cast<size_t>(selected_)];
    if (source.builtin) {
        set_status("Built-in source");
        return;
    }
    auto shared = shared_;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->busy = true;
    }
    spawn([shared, url = source.url] {
        const auto result = apps_backend::run({"--remove-registry", url}, 30);
        task_refresh(shared);
        shared->finish(result.rc == 0 ? "Source removed" : apps_backend::error_text(result.out, "Failed"));
    });
    selected_ = 0;
    offset_ = 0;
}

void LvSettingAppsPage3::enter_text_mode()
{
    if (!text_mode_saved_) {
        previous_context_ = static_cast<int>(cp0_keyboard_get_input_context());
        previous_intercept_ = cp0_keyboard_get_lvgl_keypad_intercept();
        text_mode_saved_ = true;
    }
    cp0_keyboard_set_input_context(KBD_INPUT_CONTEXT_TEXT);
    cp0_keyboard_set_lvgl_keypad_intercept(1);
}

void LvSettingAppsPage3::leave_text_mode()
{
    if (!text_mode_saved_) return;
    cp0_keyboard_set_input_context(static_cast<cp0_keyboard_input_context_t>(previous_context_));
    cp0_keyboard_set_lvgl_keypad_intercept(previous_intercept_);
    text_mode_saved_ = false;
}

void LvSettingAppsPage3::begin_edit()
{
    editing_ = true;
    edit_text_.clear();
    edit_error_.clear();
    enter_text_mode();
    lv_label_set_text(edit_value_, "_");
    lv_label_set_text(edit_hint_, "owner/repo (or a registry.json address)\nEnter: add   Esc: cancel");
    render();
}

void LvSettingAppsPage3::end_edit(bool submit)
{
    if (submit) {
        std::string url, name, error;
        if (!apps_backend::normalize_source(edit_text_, url, name, error)) {
            lv_label_set_text(edit_hint_, (error + "\nEnter: add   Esc: cancel").c_str());
            return;
        }
        auto shared = shared_;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->busy = true;
        }
        shared->say("Adding " + shorten(name, 20) + "...");
        spawn([shared, url, name] {
            const auto result = apps_backend::run({"--add-registry", url, "--registry-name", name}, 90);
            task_refresh(shared);
            shared->finish(result.rc == 0 ? "Source added" : apps_backend::error_text(result.out, "Could not add the source"));
        });
    }
    editing_ = false;
    leave_text_mode();
    render();
}

void LvSettingAppsPage3::edit_key(const key_item *item)
{
    if (item->key_state == 0) return;
    if (item->key_state == 1 && item->key_code == KEY_ESC) {
        end_edit(false);
        return;
    }
    if (item->key_state == 1 && (item->key_code == KEY_ENTER || item->key_code == KEY_KPENTER)) {
        end_edit(true);
        return;
    }
    if (item->key_code == KEY_BACKSPACE) {
        while (!edit_text_.empty() && (static_cast<unsigned char>(edit_text_.back()) & 0xC0) == 0x80) edit_text_.pop_back();
        if (!edit_text_.empty()) edit_text_.pop_back();
    } else if (item->utf8[0] && static_cast<unsigned char>(item->utf8[0]) >= 0x20 && item->utf8[0] != 0x7f &&
               edit_text_.size() < 200 && !(item->mods & (KBD_MOD_CTRL | KBD_MOD_ALT))) {
        edit_text_ += item->utf8;
    } else {
        return;
    }
    // keep the end of long input visible
    const std::string shown = edit_text_.size() > 40 ? "~" + edit_text_.substr(edit_text_.size() - 39) : edit_text_;
    lv_label_set_text(edit_value_, (shown + "_").c_str());
}

void LvSettingAppsPage3::keyboard_event_cb(lv_event_t *event)
{
    auto *self = static_cast<LvSettingAppsPage3 *>(lv_event_get_user_data(event));
    auto *item = static_cast<const key_item *>(lv_event_get_param(event));
    if (!self || !item) return;
    if (!self->editing_) {
        self->shortcut_key(item);
        return;
    }
    self->edit_key(item);
    lv_event_stop_processing(event);
}

void LvSettingAppsPage3::start_sync()
{
    auto shared = shared_;
    bool start = false;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (!shared->busy && !backend_missing_) shared->busy = start = true;
    }
    if (start) spawn([shared] { task_sync(shared); });
}

// Letter shortcuts come from the raw keyboard event: the native LVGL key for a letter is its evdev code
// (KEY_S = 31), not its ASCII value, and some letter codes equal an arrow's LVGL code (KEY_R = 19 = LV_KEY_RIGHT),
// so the letters are told apart here by their physical key code.
void LvSettingAppsPage3::shortcut_key(const key_item *item)
{
    if (item->key_code == KEY_W || item->key_code == KEY_E || item->key_code == KEY_R || item->key_code == KEY_T) {
        raw_letter_ = true;                  // handle_key must not take the matching LVGL key for an arrow
        raw_letter_tick_ = lv_tick_get();
        return;
    }
    if (item->key_state != KBD_KEY_PRESSED || message_showing_ || (item->mods & (KBD_MOD_CTRL | KBD_MOD_ALT))) return;
    if (item->key_code == KEY_U && tab_ == Tab::Apps) {
        start_update_all();
    } else if (item->key_code == KEY_S) {
        pending_ = Pending::None;
        start_sync();
    } else if (tab_ == Tab::Apps && item->key_code == KEY_D) {
        request_remove_app();
    } else if (tab_ == Tab::Sources && item->key_code == KEY_A) {
        begin_edit();
    } else if (tab_ == Tab::Sources && item->key_code == KEY_D) {
        if (selected_ < static_cast<int>(sources_.size()) && !sources_[static_cast<size_t>(selected_)].builtin) {
            pending_ = Pending::RemoveSource;
            render();
        }
    } else {
        return;
    }
    if (!notice_.empty()) {
        notice_.clear();
        render();
    }
}

void LvSettingAppsPage3::handle_key(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY || editing_) return;
    const uint32_t key = lv_event_get_key(event);
    // W, E, R, T arrive as 17..20 = LV_KEY_UP/DOWN/RIGHT/LEFT: a real letter press (seen just before) is not an arrow.
    // The window is not cleared on use: a held letter repeats.
    if (key >= LV_KEY_UP && key <= LV_KEY_LEFT && raw_letter_ && lv_tick_elaps(raw_letter_tick_) < 300) return;
    if (message_showing_) {            // any key closes the failure panel
        dismiss_message();
        lv_event_stop_processing(event);
        return;
    }
    if (!notice_.empty()) {
        notice_.clear();
        render();
    }
    if (key == LV_KEY_ESC) {
        if (pending_ != Pending::None) {
            pending_ = Pending::None;
            render();
        } else if (LeaveSelfPage) {
            LeaveSelfPage();
        }
    } else if (key == LV_KEY_UP) {
        move_selection(-1);
    } else if (key == LV_KEY_DOWN) {
        move_selection(1);
    } else if (key == LV_KEY_LEFT) {
        set_tab(Tab::Apps);
    } else if (key == LV_KEY_RIGHT) {
        set_tab(Tab::Sources);
    } else if (key == LV_KEY_ENTER) {
        if (pending_ == Pending::RemoveSource) {
            pending_ = Pending::None;
            remove_selected_source();
            render();
        } else {
            activate();
        }
    } else if (tab_ == Tab::Apps && key == LV_KEY_DEL) {
        request_remove_app();
    } else if (tab_ == Tab::Sources && key == LV_KEY_DEL) {
        if (selected_ < static_cast<int>(sources_.size()) && !sources_[static_cast<size_t>(selected_)].builtin) {
            pending_ = Pending::RemoveSource;
            render();
        }
    } else {
        return;
    }
    lv_event_stop_processing(event);
}

void LvSettingAppsPage3::poll_cb(lv_timer_t *timer)
{
    static_cast<LvSettingAppsPage3 *>(lv_timer_get_user_data(timer))->poll();
}

void LvSettingAppsPage3::poll()
{
    auto shared = shared_;
    bool changed = false;
    bool start_sudo_now = false;
    std::string status;
    bool status_dirty = false;
    bool status_error = false;
    bool status_warn = false;
    bool show_failure = false;
    std::string failure_title, failure_detail;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->apps_ready) {
            loaded_ = true;
            apps_ = shared->apps;
            shared->apps_ready = false;
            changed = true;
        }
        if (shared->sources_ready) {
            sources_ = shared->sources;
            shared->sources_ready = false;
            changed = true;
        }
        if (shared->sync_dirty) {
            sync_ = shared->sync;
            shared->sync_dirty = false;
            changed = true;
        }
        if (shared->notice_dirty) {
            notice_ = shared->notice;
            shared->notice_dirty = false;
            changed = true;
        }
        if (shared->message_pending) {
            failure_title = shared->message_title;
            failure_detail = shared->message_detail;
            shared->message_pending = false;
            show_failure = true;
        }
        if (shared->status_dirty) {
            status = shared->status;
            status_error = shared->status_error;
            status_warn = shared->status_warn;
            status_dirty = true;
            shared->status_dirty = false;
        } else if (shared->busy && !shared->stage.empty()) {
            // a running step shows how long it has been going: the sign that nothing is stuck
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - shared->stage_since).count();
            status = apps_status::stage_line(shared->stage, shared->stage_position, shared->stage_total,
                                             static_cast<long>(seconds));
            status_dirty = true;
        }
        if (shared->sudo_pending) start_sudo_now = true;
    }
    if (show_failure) {
        show_message(failure_title, failure_detail);
        changed = true;
    }
    if (status_dirty) set_status(status, status_error, status_warn);
    if (changed) render();
    if (start_sudo_now) start_sudo();
}

void LvSettingAppsPage3::sudo_output_cb(const char *data, size_t size, void *user)
{
    auto *context = static_cast<SudoContext *>(user);
    if (!context || !data) return;
    if (context->output.size() < (64u << 10)) context->output.append(data, size);
    const std::string stage = apps_status::progress_text(context->output);
    if (!stage.empty()) context->shared->set_stage(stage);
}

void LvSettingAppsPage3::sudo_complete_cb(cp0_sudo_result_t result, int exit_code, void *user)
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

void LvSettingAppsPage3::start_sudo()
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
        argv.data(), CP0_SUDO_CALLBACK_LVGL, &LvSettingAppsPage3::sudo_output_cb,
        &LvSettingAppsPage3::sudo_complete_cb, raw,
        60 * 1000, 15 * 60 * 1000, &request_id);
    if (rc != 0) {
        delete raw;
        shared->finish("Could not ask for the password");
    }
}

std::unique_ptr<DComponens::LvglComponensBase> settings_apps_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback)
{
    return std::make_unique<LvSettingAppsPage3>(parent, page_node, std::move(back_callback));
}
