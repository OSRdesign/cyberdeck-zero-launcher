/*
 * Settings > Apps (see settings_apps_page.hpp).
 */

#include "settings_apps_page.hpp"

#include "settings_fonts.hpp"

#include "cp0_lvgl_app.h"
#include "input_keys.h"
#include "keyboard_input.h"

#include <algorithm>
#include <mutex>
#include <thread>
#include <utility>

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
    bool status_dirty = false;
    bool busy = false;
    // hand-over of a prepared package job to the UI thread, which asks for the sudo password
    bool sudo_pending = false;
    std::vector<std::string> sudo_argv;
    std::string job_action;
    std::string job_id;
    std::string job_transaction;
    std::string job_name;

    void say(const std::string &text)
    {
        std::lock_guard<std::mutex> lock(mutex);
        status = text;
        status_dirty = true;
    }
    void finish(const std::string &text)
    {
        std::lock_guard<std::mutex> lock(mutex);
        status = text;
        status_dirty = true;
        busy = false;
    }
};

namespace {

using Shared = LvSettingAppsPage3::Shared;

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
    // Re-read only the user's own sources (editing a registry to itself re-syncs just that one).
    std::vector<apps_backend::Source> mine;
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        mine = s->sources;
    }
    std::string failure;
    int done = 0;
    for (const apps_backend::Source &source : mine) {
        if (!source.enabled) continue;
        s->say("Syncing " + shorten(source.name, 22) + "...");
        const auto result = apps_backend::run({"--edit-registry", source.url, source.url, "--registry-name", source.name}, 120);
        if (result.rc != 0) failure = apps_backend::error_text(result.out, "Sync failed");
        ++done;
    }
    task_refresh(s);
    s->finish(!failure.empty() ? failure : (done == 0 ? "No source to sync" : "Sources synced"));
}

void task_finalize(std::shared_ptr<Shared> s, std::string action, std::string id, std::string tx, std::string name)
{
    s->say("Finishing...");
    const auto result = apps_backend::run({"--finalize-package", action, id, tx}, 120);
    task_refresh(s);
    if (result.rc != 0) {
        s->finish(apps_backend::error_text(result.out, "Failed"));
        return;
    }
    s->finish((action == "uninstall" ? "Removed " : "Installed ") + name);
}

void task_package(std::shared_ptr<Shared> s, std::string action, std::string id, std::string name)
{
    s->say(action == "uninstall" ? "Preparing removal..." : "Downloading " + shorten(name, 18) + "...");
    const auto prepared = apps_backend::run({"--prepare-package", action, id}, 600);
    apps_backend::PackageJob job;
    if (prepared.rc != 0 || !apps_backend::parse_package_job(prepared.out, job)) {
        s->finish(apps_backend::error_text(prepared.out, "Could not prepare the package"));
        return;
    }
    std::lock_guard<std::mutex> lock(s->mutex);
    s->sudo_argv = apps_backend::privileged_argv(job);
    s->job_action = action;
    s->job_id = id;
    s->job_transaction = job.transaction;
    s->job_name = name;
    s->sudo_pending = true;
    s->status = "Type your password";
    s->status_dirty = true;
}

struct SudoContext {
    std::shared_ptr<Shared> shared;
    std::string action, id, transaction, name;
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

    status_ = make_label(ComponensObj, 140, 5, kGreen, settings_fonts::sans(11));
    lv_obj_set_size(status_, 172, 14);
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
        lv_obj_set_size(left_[i], 214, 15);
        lv_label_set_long_mode(left_[i], LV_LABEL_LONG_MODE_DOTS);
        right_[i] = make_label(rows_[i], 224, 2, kMuted, settings_fonts::sans(11));
        lv_obj_set_size(right_[i], 88, 14);
        lv_label_set_long_mode(right_[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(right_[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_add_flag(rows_[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(rows_[i], reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(rows_[i], [](lv_event_t *e) {
            auto *self = static_cast<LvSettingAppsPage3 *>(lv_event_get_user_data(e));
            const int row = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e))));
            if (self->offset_ + row >= self->row_count()) return;
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

    DComponens::lvgl_bind_event(ComponensObj, LV_EVENT_KEY, nullptr,
                                [this](lv_event_t *event) { handle_key(event); });
    keyboard_root_ = lv_screen_active();
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
    if (tab_ == Tab::Apps) return static_cast<int>(apps_.size());
    return static_cast<int>(sources_.size()) + 2;   // + "Add" and "Sync" rows
}

void LvSettingAppsPage3::set_status(const std::string &text)
{
    if (status_) lv_label_set_text(status_, text.c_str());
}

void LvSettingAppsPage3::set_tab(Tab tab)
{
    if (editing_) return;
    pending_ = Pending::None;
    if (tab == tab_) return;
    tab_ = tab;
    selected_ = 0;
    offset_ = 0;
    render();
}

void LvSettingAppsPage3::move_selection(int delta)
{
    pending_ = Pending::None;
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
            const apps_backend::App &app = apps_[static_cast<size_t>(index)];
            left = app.title;
            right = app.upgradable() ? "update " + shorten(app.version, 8) : (app.installed ? "installed" : "v" + shorten(app.version, 9));
            right_color = app.upgradable() ? kYellow : (app.installed ? kGreen : kMuted);
            left_color = app.installed ? kGreen : kText;
        } else if (index < static_cast<int>(sources_.size())) {
            const apps_backend::Source &source = sources_[static_cast<size_t>(index)];
            left = source.name;
            right = !source.enabled ? "off" : (source.status == "ok" ? std::to_string(source.apps) + " apps" : "error");
            right_color = !source.enabled ? kMuted : (source.status == "ok" ? kGreen : kRed);
            left_color = source.enabled ? kText : kMuted;
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
        lv_label_set_text(left_[0], backend_missing_ ? "The Store is needed (backend missing)"
                                                      : "No apps: add your source in Sources");
        lv_obj_set_style_text_color(left_[0], lv_color_hex(kMuted), LV_PART_MAIN);
        lv_label_set_text(right_[0], "");
    }

    std::string hint;
    if (pending_ == Pending::Remove && selected_ < static_cast<int>(apps_.size()))
        hint = "Remove " + shorten(apps_[static_cast<size_t>(selected_)].title, 20) + "?  Enter: yes   Esc: no";
    else if (pending_ == Pending::RemoveSource)
        hint = "Remove this source?  Enter: yes   Esc: no";
    else if (apps_tab)
        hint = "Enter: install/remove   Right: sources   Esc: back";
    else
        hint = "Enter: on/off   D: delete   A: add   S: sync";
    lv_label_set_text(footer_, hint.c_str());
    lv_obj_set_style_text_color(footer_, lv_color_hex(pending_ != Pending::None ? kYellow : kGreen), LV_PART_MAIN);

    if (edit_panel_) {
        if (editing_) lv_obj_remove_flag(edit_panel_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(edit_panel_, LV_OBJ_FLAG_HIDDEN);
    }
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
        if (selected_ >= static_cast<int>(apps_.size())) return;
        const apps_backend::App &app = apps_[static_cast<size_t>(selected_)];
        std::string action;
        if (!app.installed) action = "install";
        else if (app.upgradable()) action = "upgrade";
        else if (pending_ == Pending::Remove) action = "uninstall";
        else {
            pending_ = Pending::Remove;
            render();
            return;
        }
        pending_ = Pending::None;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->busy = true;
        }
        spawn([shared, action, id = app.id, name = app.title] { task_package(shared, action, id, name); });
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
    if (!self || !item || !self->editing_) return;
    self->edit_key(item);
    lv_event_stop_processing(event);
}

void LvSettingAppsPage3::handle_key(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY || editing_) return;
    const uint32_t key = lv_event_get_key(event);
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
    } else if (tab_ == Tab::Sources && (key == 'a' || key == 'A')) {
        begin_edit();
    } else if (tab_ == Tab::Sources && (key == 'd' || key == 'D' || key == LV_KEY_DEL)) {
        if (selected_ < static_cast<int>(sources_.size()) && !sources_[static_cast<size_t>(selected_)].builtin) {
            pending_ = Pending::RemoveSource;
            render();
        }
    } else if (key == 's' || key == 'S') {
        auto shared = shared_;
        bool start = false;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (!shared->busy && !backend_missing_) shared->busy = start = true;
        }
        if (start) spawn([shared] { task_sync(shared); });
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
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->apps_ready) {
            apps_ = shared->apps;
            shared->apps_ready = false;
            changed = true;
        }
        if (shared->sources_ready) {
            sources_ = shared->sources;
            shared->sources_ready = false;
            changed = true;
        }
        if (shared->status_dirty) {
            status = shared->status;
            status_dirty = true;
            shared->status_dirty = false;
        }
        if (shared->sudo_pending) start_sudo_now = true;
    }
    if (status_dirty) set_status(status);
    if (changed) render();
    if (start_sudo_now) start_sudo();
}

void LvSettingAppsPage3::sudo_output_cb(const char *, size_t, void *) {}

void LvSettingAppsPage3::sudo_complete_cb(cp0_sudo_result_t result, int exit_code, void *user)
{
    std::unique_ptr<SudoContext> context(static_cast<SudoContext *>(user));
    if (!context) return;
    if (result == CP0_SUDO_RESULT_SUCCESS) {
        auto c = *context;
        spawn([c] { task_finalize(c.shared, c.action, c.id, c.transaction, c.name); });
        return;
    }
    std::string message = "Failed";
    if (result == CP0_SUDO_RESULT_AUTH_FAILED) message = "Wrong password";
    else if (result == CP0_SUDO_RESULT_CANCELLED) message = "Cancelled";
    else if (result == CP0_SUDO_RESULT_TIMED_OUT) message = "Timed out";
    else if (exit_code != 0) message = "Install failed (" + std::to_string(exit_code) + ")";
    context->shared->finish(message);
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
