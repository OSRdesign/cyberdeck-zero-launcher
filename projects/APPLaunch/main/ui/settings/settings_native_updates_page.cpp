/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates (see settings_native_updates_page.hpp).
 */

#include "settings_native_updates_page.hpp"

#include "settings_update_check.hpp"
#include "settings_update_download.hpp"
#include "settings_update_install.hpp"
#include "settings_update_install_model.hpp"
#include "settings_update_model.hpp"
#include "settings_update_platform.hpp"

#include "../native_screensaver.hpp"
#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_widgets.hpp"
#include "settings_fonts.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace launcher_update;
using cp0::ui::ListRow;
using cp0::ui::RowAffordance;
using cp0::ui::ValueTone;

constexpr uint32_t kPollMs = 200;      /* the Apps page's poll of its workers */
constexpr int kRefreshPolls = 25;      /* "Last checked" follows the clock: every 5 s */
constexpr int kHelperPolls = 5;        /* the helper's status file: read every second */
constexpr std::size_t kLogBytes = 12 * 1024; /* Show log: the end of the helper's log */
constexpr int kUpdateRgb = 0xF0B400;   /* the Apps page's tokens: amber "update offered" ... */
constexpr int kMutedRgb = 0x8A929B;    /* ... and its grey */
/* The progress bar (P2): 2.5 mm high at the panel's density (at least 6 px), the home grid's hold bar colours */
constexpr int kBarMmX10 = 25;
constexpr int kBarFloorPx = 6;
constexpr uint32_t kBarTrackRgb = 0x3A3A3A; /* native_ui.cpp: the hold bar's track (the tiles' border) ... */
constexpr uint32_t kBarFillRgb = 0x2D9CDB;  /* ... and its fill (the accent) */

enum RowId : std::intptr_t {
    kLauncher = 1, kCheckNow, kLastChecked, kWhatsNew, kDevInfo, kCheckAgain,
    kInstall, kStop, kRepair, kLastUpdate, kShowLog, kNoHelper, kStep
};

ValueTone tone_of(Tone tone)
{
    switch (tone) {
    case Tone::Update: return ValueTone::Warn; /* CP0_UI_LIST_WARN_RGB = the Apps page's amber */
    case Tone::Muted: return ValueTone::Muted;
    case Tone::Normal: break;
    }
    return ValueTone::Normal;
}

void focus_in(lv_group_t *group, lv_obj_t *obj)
{
    if (!group || !obj) return;
    if (lv_obj_get_group(obj) != group) lv_group_add_obj(group, obj);
    lv_group_focus_obj(obj);
}

void unfocus(lv_obj_t *obj)
{
    if (obj && lv_obj_get_group(obj)) lv_group_remove_obj(obj);
}

class UpdatesPanel : public SettingsNativePanel {
public:
    explicit UpdatesPanel(const SettingsNativePanelContext &context)
        : page_(context.page), group_(context.group), title_(context.title), host_back_(context.back)
    {
        // the lists start under the page's status line (Page::set_caption draws it over the top of the body); with the
        // progress bar (P2: the download, the Updating page) the bar takes that place and the list starts under it
        const cp0_ui_roller_page_t &pl = page_.layout();
        const int line_h = lv_font_get_line_height(cp0_ui_font_px(pl.caption_px));
        const int space = cp0_ui_tok(CP0_TOK_SPACE_S);
        const int top = pl.caption_y + line_h + space;
        cp0_ui_list_layout(cp0_ui_metrics_get(), CP0_UI_LIST_COMFORTABLE, pl.roller_x, top, pl.roller_w,
                           std::max(1, pl.h - top), &layout_plain_);
        const int bar_h = cp0_ui_mm_px(cp0_ui_metrics_get()->ppmm_x100, kBarMmX10, kBarFloorPx);
        const int below = top + bar_h + space;
        cp0_ui_list_layout(cp0_ui_metrics_get(), CP0_UI_LIST_COMFORTABLE, pl.roller_x, below, pl.roller_w,
                           std::max(1, pl.h - below), &layout_bar_);
        make_bar(pl.roller_x + layout_plain_.inset, top, std::max(1, pl.roller_w - 2 * layout_plain_.inset), bar_h);
        make_list(false);
        timer_ = lv_timer_create(poll_cb, kPollMs, this);

        Checker::instance().start(); /* D3: a check each time the page opens */
        snap_ = Checker::instance().snapshot();
        down_ = Downloader::instance().snapshot();
        root_ = RootJob::instance().snapshot();
        read_helper();
        seen();
        // a root update at work (this launcher started it, or the one before the restart): its progress at once
        if (phase_ == HelperPhase::Running) level_ = Level::Progress;
        show(0);
        hold_screen();
    }

    ~UpdatesPanel() override
    {
        if (timer_) lv_timer_delete(timer_);
        timer_ = nullptr;
        lv_async_call_cancel(run_posted_cb, this);
        posted_.clear();
        dialog_.reset();
        text_.reset();
        list_.reset(); /* a check or a download still running finishes on its own: the next page or the background reads it */
        if (bar_) lv_obj_delete(bar_);
        bar_ = nullptr;
        page_.set_caption("", false);
        if (held_) native_screensaver::hold(false);
    }

    bool back() override
    {
        if (dialog_) {
            close_dialog(true);
            return true;
        }
        switch (level_) {
        case Level::Progress: return true; /* report 047 section 4.2: the progress page stays while the root work runs */
        case Level::WhatsNew:
        case Level::Log: set_level(Level::Launcher); return true;
        case Level::Launcher: set_level(Level::Root); return true;
        case Level::Root: break;
        }
        return false;
    }

private:
    enum class Level { Root, Launcher, WhatsNew, Progress, Log };

    void post(std::function<void()> callback)
    {
        posted_.push_back(std::move(callback));
        if (!post_armed_) post_armed_ = lv_async_call(run_posted_cb, this) == LV_RESULT_OK;
    }

    static void run_posted_cb(void *data)
    {
        auto *self = static_cast<UpdatesPanel *>(data);
        if (!self) return;
        self->post_armed_ = false;
        std::vector<std::function<void()>> callbacks;
        callbacks.swap(self->posted_);
        for (auto &callback : callbacks) callback();
    }

    // ---------------------------------------------------------------- what the page shows

    bool development() const { return development_build(snap_.build); }

    /* The release to install: the one offered, while a download of it is under way or done. */
    const std::string &offered() const { return snap_.offer.available; }
    bool download_is_offered() const
    {
        return !down_.download.version.empty() && down_.download.version == offered();
    }

    std::vector<NotesSource> sources() const
    {
        return notes_sources(snap_.releases, snap_.offer, snap_.state, snap_.build,
                             platform::shipped_notes(snap_.build.version));
    }

    /* The Launcher level's install rows (P2): Install, the download's progress and Stop, Repair, the last result. */
    void install_rows(std::vector<ListRow> &rows) const
    {
        if (phase_ == HelperPhase::Interrupted) {
            rows.push_back(ListRow::item("Repair the update", "", ValueTone::Normal, kRepair,
                                         root_.asking ? RowAffordance::ActionRunning : RowAffordance::Action));
        } else if (!offered().empty()) {
            const std::string &v = offered();
            if (!helper_installed_) {
                rows.push_back(ListRow::item("Install " + v, "by hand", ValueTone::Normal, kNoHelper,
                                             RowAffordance::Info));
            } else if (download_is_offered() && down_.phase == DownloadPhase::Downloading) {
                // no version in these labels: beside the value it is cut up to 640 px (the status line names it)
                rows.push_back(ListRow::item("Downloading", progress_text(down_.done, down_.download.size),
                                             ValueTone::Normal, kInstall, RowAffordance::ActionRunning));
                rows.push_back(ListRow::item("Stop the download", "", ValueTone::Normal, kStop, RowAffordance::Action));
            } else if (download_is_offered() && down_.phase == DownloadPhase::Verifying) {
                rows.push_back(ListRow::item("Checking the download", "sha256", ValueTone::Normal, kInstall,
                                             RowAffordance::ActionRunning));
            } else {
                const bool ready = download_is_offered() && down_.phase == DownloadPhase::Ready;
                Download d;
                const long long size = download_of(snap_.releases, v, snap_.build.arch, d) ? d.size : 0;
                rows.push_back(ListRow::item("Install " + v, ready ? "downloaded" : megabytes(size), ValueTone::Normal,
                                             kInstall,
                                             root_.asking ? RowAffordance::ActionRunning : RowAffordance::Action));
            }
        }
    }

    void result_rows(std::vector<ListRow> &rows) const
    {
        const std::string value = result_value(helper_, phase_, snap_.build.version);
        if (value.empty()) return;
        rows.push_back(ListRow::item("Last update", value, ValueTone::Warn, kLastUpdate, RowAffordance::Info));
        rows.push_back(ListRow::item("Show log", "", ValueTone::Normal, kShowLog, RowAffordance::Open));
    }

    /* The helper's run as the progress page shows it: the status file, or (the second between systemd-run and the
     * helper's first write) the run this launcher just started. */
    HelperStatus progress_status() const
    {
        if (phase_ == HelperPhase::Running) return helper_;
        HelperStatus starting;
        starting.present = true;
        starting.state = "running";
        starting.step = "check";
        starting.from = snap_.build.version;
        starting.to = down_.download.version;
        return starting;
    }

    std::vector<ListRow> rows() const
    {
        std::vector<ListRow> rows;
        const RowAffordance check = snap_.running ? RowAffordance::ActionRunning : RowAffordance::Action;
        switch (level_) {
        case Level::Root: {
            const RowValue value = root_value(snap_.state, snap_.build);
            // "Launcher" alone: with its version ("Launcher 0.8.12", the study's mockup) the label is cut beside
            // "0.8.13 available" up to 640 px; the running version is in the Launcher level's status line
            rows.push_back(ListRow::item("Launcher", value.text, tone_of(value.tone), kLauncher, RowAffordance::Open));
            rows.push_back(ListRow::item("Check now", "", ValueTone::Normal, kCheckNow, check));
            rows.push_back(ListRow::item("Last checked", last_checked_text(snap_.state.last_check, platform::now()),
                                         ValueTone::Normal, kLastChecked, RowAffordance::Info));
            break;
        }
        case Level::Progress: {
            for (const StepRow &step : progress_rows(progress_status(), platform::now())) {
                const ValueTone tone = step.mark == StepMark::Done      ? ValueTone::Ok
                                       : step.mark == StepMark::Current ? ValueTone::Normal
                                                                         : ValueTone::Muted;
                rows.push_back(ListRow::item(step.label, step.value, tone, kStep, RowAffordance::Info));
            }
            break;
        }
        case Level::Launcher:
        case Level::WhatsNew:
        case Level::Log: {
            const std::vector<NotesSource> notes = sources();
            rows.push_back(ListRow::item(notes.empty() ? std::string("What's new")
                                                       : "What's new in " + notes.front().version,
                                         "", ValueTone::Normal, kWhatsNew, RowAffordance::Open));
            if (development())
                rows.push_back(ListRow::item("Development build", "install by hand", ValueTone::Normal, kDevInfo,
                                             RowAffordance::Info)); /* D4: why there is no Install */
            else
                install_rows(rows);
            rows.push_back(ListRow::item("Check again", "", ValueTone::Normal, kCheckAgain, check));
            result_rows(rows);
            break;
        }
        }
        return rows;
    }

    /* The status line: a check on its way, why the last one failed, what stops or ends an install, a release without
     * this board's bundle; on the Launcher level otherwise the versions, the channel and the architecture. */
    void show_caption()
    {
        std::string text;
        cp0::ui::CaptionTone tone = cp0::ui::CaptionTone::Normal;
        const std::string result = result_caption(helper_, phase_, snap_.build.version);
        if (level_ == Level::WhatsNew || level_ == Level::Log) {
            text.clear(); /* the TextBlock starts at the top of the body (the log's first line says how it ended) */
        } else if (level_ == Level::Progress) {
            text = root_.error.empty() ? progress_caption(progress_status()) : root_.error;
            tone = root_.error.empty() ? cp0::ui::CaptionTone::Warn : cp0::ui::CaptionTone::Error;
        } else if (root_.asking) {
            text = "Type your password";
        } else if (!root_.error.empty() && level_ == Level::Launcher) {
            text = root_.error;
            tone = cp0::ui::CaptionTone::Warn;
        } else if (!note_.empty() && level_ == Level::Launcher) {
            text = note_;
            tone = cp0::ui::CaptionTone::Warn;
        } else if (!result.empty() && level_ == Level::Launcher && phase_ == HelperPhase::Interrupted) {
            text = result; /* before anything else: nothing installs until it is repaired */
            tone = cp0::ui::CaptionTone::Warn;
        } else if (level_ == Level::Launcher && download_is_offered() && down_.phase == DownloadPhase::Failed) {
            text = down_.error;
            tone = cp0::ui::CaptionTone::Warn;
        } else if (snap_.running) {
            text = "Checking for updates...";
        } else if (snap_.done > 0 && failed(snap_.outcome)) {
            text = outcome_text(snap_.outcome, snap_.http_status, clock_text(snap_.state.rate_reset));
            tone = cp0::ui::CaptionTone::Warn;
        } else if (!result.empty() && level_ == Level::Launcher) {
            text = result;
            tone = phase_ == HelperPhase::Failed ? cp0::ui::CaptionTone::Error : cp0::ui::CaptionTone::Warn;
        } else if (!snap_.offer.missing_asset.empty() && !development()) {
            text = "No build for " + snap_.build.arch + " in " + snap_.offer.missing_asset;
            tone = cp0::ui::CaptionTone::Warn;
        } else if (level_ == Level::Launcher) {
            const Build &b = snap_.build;
            text = b.version + (snap_.offer.available.empty() ? "" : " -> " + snap_.offer.available) + ", " +
                   b.channel + ", " + b.arch;
        }
        if (text == caption_ && tone == caption_tone_) return;
        caption_ = text;
        caption_tone_ = tone;
        page_.set_caption(text, tone);
        if (dialog_) dialog_->raise();
    }

    void show_title()
    {
        std::vector<std::string> title = title_;
        if (level_ != Level::Root) title.push_back("Launcher");
        if (level_ == Level::WhatsNew) title.push_back("What's new");
        if (level_ == Level::Progress) title.push_back("Updating");
        if (level_ == Level::Log) title.push_back("Log");
        page_.set_title(title);
    }

    // ---------------------------------------------------------------- the progress bar (P2)

    /* An lv_bar under the status line: never focused or clicked (keys and taps stay the list's), hidden until a
     * download or the Updating page needs it. */
    void make_bar(int x, int y, int w, int h)
    {
        bar_ = lv_bar_create(page_.root());
        if (!bar_) return;
        lv_obj_add_event_cb(bar_, bar_delete_cb, LV_EVENT_DELETE, this);
        if (lv_obj_get_group(bar_)) lv_group_remove_obj(bar_);
        lv_obj_remove_flag(bar_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(bar_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_set_pos(bar_, x, y);
        lv_obj_set_size(bar_, w, h);
        lv_bar_set_range(bar_, 0, 100);
        lv_bar_set_value(bar_, 0, LV_ANIM_OFF);
        lv_obj_set_style_radius(bar_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(bar_, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(bar_, lv_color_hex(kBarTrackRgb), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar_, lv_color_hex(kBarFillRgb), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_anim_duration(bar_, 0, LV_PART_MAIN); /* values jump: no time-dependent pixels */
        lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
    }

    static void bar_delete_cb(lv_event_t *event)
    {
        auto *self = static_cast<UpdatesPanel *>(lv_event_get_user_data(event));
        if (self) self->bar_ = nullptr;
    }

    /* The list under the status line, or under the bar. */
    void make_list(bool with_bar)
    {
        cp0::ui::CompactListEvents events;
        events.activate = [this](int, std::intptr_t id) { post([this, id] { activate(id); }); };
        events.back = host_back_;
        if (list_) unfocus(list_->focus_object());
        list_.reset();
        list_ = std::make_unique<cp0::ui::CompactList>(page_.root(), with_bar ? layout_bar_ : layout_plain_,
                                                       std::move(events));
        list_with_bar_ = with_bar;
        if (dialog_) dialog_->raise(); /* the question keeps the keys; close_dialog() gives them to the list */
        else focus_in(group_, list_->focus_object());
    }

    /* What the bar shows now (0..100), -1 for no bar: the download of the offered release (its bytes; the check of
     * the finished file: full), the Updating page (the run's steps). */
    int bar_value() const
    {
        if (level_ == Level::Progress) return step_percent(progress_status());
        if (level_ == Level::Launcher && !development() && download_is_offered()) {
            if (down_.phase == DownloadPhase::Downloading) return download_percent(down_.done, down_.download.size);
            if (down_.phase == DownloadPhase::Verifying) return 100;
        }
        return -1;
    }

    /* The bar shown or hidden with its value; the list moves under it (or back) when that changes: rebuilt with the
     * other layout, the highlight kept on the same row. */
    void update_bar()
    {
        const int value = bar_value();
        const bool want = value >= 0 && bar_ != nullptr;
        if (want != list_with_bar_ && list_ && level_ != Level::WhatsNew && level_ != Level::Log) {
            const int selected = std::max(0, list_->selected());
            const std::vector<ListRow> current = rows();
            make_list(want);
            list_->reset_rows(current, selected);
        }
        if (!bar_) return;
        if (want) {
            lv_bar_set_value(bar_, value, LV_ANIM_OFF);
            lv_obj_remove_flag(bar_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* selected >= 0: new content (a level), that row highlighted; -1: the live refresh (texts in place). */
    void show(int selected)
    {
        update_bar();
        if (level_ != Level::WhatsNew && level_ != Level::Log && list_) {
            const std::vector<ListRow> next = rows();
            bool same = selected < 0 && static_cast<int>(next.size()) == list_->count();
            for (int i = 0; same && i < list_->count(); ++i) same = list_->row(i).id == next[static_cast<std::size_t>(i)].id;
            if (same) {
                list_->set_rows(next);
            } else {
                int target = std::max(0, selected);
                if (selected < 0) { /* rows came or went: the highlight stays on the same row when it is still there */
                    const std::intptr_t id = list_->row(list_->selected()).id;
                    for (std::size_t i = 0; i < next.size(); ++i)
                        if (next[i].id == id) target = static_cast<int>(i);
                }
                list_->reset_rows(next, target);
            }
            if (level_ == Level::Progress) { /* the highlight on the running step */
                const std::vector<StepRow> steps = progress_rows(progress_status(), platform::now());
                for (std::size_t i = 0; i < steps.size(); ++i)
                    if (steps[i].mark == StepMark::Current && list_->selected() != static_cast<int>(i))
                        list_->select(static_cast<int>(i));
            }
        }
        show_title();
        show_caption();
    }

    void set_level(Level level)
    {
        if (level == Level::Launcher && phase_ == HelperPhase::Running) level = Level::Progress;
        level_ = level;
        note_.clear();
        if (level == Level::WhatsNew || level == Level::Log) {
            unfocus(list_->focus_object());
            list_->set_hidden(true);
            open_text(level == Level::Log ? log_text() : std::string());
            show(-1);
            hold_screen();
            return;
        }
        if (text_) {
            unfocus(text_->focus_object());
            text_.reset();
        }
        list_->set_hidden(false);
        focus_in(group_, list_->focus_object());
        // a level opens on its first row, which is also the row it was opened from (Launcher, What's new)
        show(0);
        hold_screen();
    }

    /* The lock screen stays away while the progress page shows (the helper's own systemd-inhibit covers the system). */
    void hold_screen()
    {
        const bool hold = level_ == Level::Progress;
        if (hold == held_) return;
        held_ = hold;
        native_screensaver::hold(hold);
    }

    std::string log_text() const
    {
        std::string text = result_text(helper_, phase_, snap_.build.version);
        if (!text.empty()) text += ".\n\n";
        const std::string log = platform::read_log(helper_.log, kLogBytes);
        text += log.empty() ? std::string("(no log: ") + (helper_.log.empty() ? std::string("none written") : helper_.log) + ")"
                            : helper_.log + "\n" + log;
        return text;
    }

    /* What's new (text empty: the notes) or the log, in a TextBlock. */
    void open_text(const std::string &text_in)
    {
        const cp0_ui_roller_page_t &layout = page_.layout();
        const int px = cp0_ui_metrics_get()->text_px[CP0_TEXT_BODY];
        const lv_font_t *font = settings_fonts::cjk_sans(static_cast<uint16_t>(px));
        if (!font) font = cp0_ui_font(CP0_TEXT_BODY);
        std::string text = text_in;
        if (text.empty()) {
            const MeasureText measure = [font](const std::string &line) {
                lv_point_t size;
                lv_text_get_size(&size, line.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                return static_cast<int>(size.x);
            };
            const int width = layout.roller_w - 2 * layout.inset - 2; /* the TextBlock's label: its box less the insets */
            text = blocks_text(whats_new_blocks(sources(), snap_.build.version), measure, width);
        }
        text_ = std::make_unique<cp0::ui::TextBlock>(page_.root(), layout, text, font, host_back_);
        focus_in(group_, text_->focus_object());
    }

    // ---------------------------------------------------------------- the install (P2)

    void read_helper()
    {
        helper_ = parse_helper_status(platform::helper_status());
        const bool alive = helper_.present && helper_.boot == platform::boot_id() && platform::process_alive(helper_.pid);
        phase_ = helper_phase(helper_, alive);
        helper_installed_ = platform::helper_installed();
    }

    /* Install: the guards, then the download (as the user); a bundle already downloaded and checked: the question. */
    void install()
    {
        if (root_.asking || development() || offered().empty()) return;
        if (download_is_offered() && (down_.phase == DownloadPhase::Downloading || down_.phase == DownloadPhase::Verifying))
            return;
        if (download_is_offered() && down_.phase == DownloadPhase::Ready) {
            open_confirm();
            return;
        }
        Download d;
        if (!download_of(snap_.releases, offered(), snap_.build.arch, d)) {
            note_ = "No build for " + snap_.build.arch + " in " + offered();
            show(-1);
            return;
        }
        InstallGuards guards;
        guards.helper_installed = helper_installed_;
        guards.helper = phase_;
        guards.battery_percent = platform::battery(guards.charging);
        guards.bundle_size = d.size;
        guards.free_bytes = platform::free_bytes(platform::download_dir());
        guards.network = platform::network_up();
        note_ = install_blocker(guards);
        RootJob::instance().clear_error();
        root_ = RootJob::instance().snapshot();
        if (note_.empty()) {
            if (Downloader::instance().start(d, "cyberdeckzero-launcher/" + snap_.build.version)) ask_when_ready_ = true;
            down_ = Downloader::instance().snapshot();
        }
        show(-1);
    }

    void open_confirm()
    {
        if (dialog_ || !download_is_offered() || down_.phase != DownloadPhase::Ready) return;
        const std::string &to = down_.download.version;
        const std::string &from = snap_.build.version;
        bool charging = false;
        const bool battery = platform::battery(charging) >= 0;
        cp0::ui::DialogSpec spec;
        spec.title = "Install " + to + "?";
        spec.page = &page_;
        spec.buttons = {{"Cancel", cp0::ui::ButtonRole::Cancel, [this] { post([this] { close_dialog(true); }); }},
                        {"Install", cp0::ui::ButtonRole::Primary, [this] { post([this] { confirm_install(); }); }}};
        dialog_ = std::make_unique<cp0::ui::Dialog>(page_.root(), page_.layout(), std::move(spec));
        // the study's text (report 047 section 4.3), open apps closing said as the PDA-mode question says it; no
        // version number or file name inside the text: LVGL breaks lines at '.', "0." / "8.13" (the title names it)
        (void)from;
        dialog_->add_text("Downloaded and checked (" + megabytes(down_.download.size) +
                          "). The launcher restarts and closes open apps. Settings, apps and the board profile are "
                          "kept. If the new version does not start, the old one comes back by itself.");
        if (!battery) dialog_->add_text("Keep the board plugged in until it is done."); /* D16 */
        dialog_->relayout();
        unfocus(list_->focus_object());
        focus_in(group_, dialog_->focus_object());
    }

    void close_dialog(bool cancelled)
    {
        if (!dialog_) return;
        dialog_.reset();
        if (cancelled) Downloader::instance().reset(); /* the checked bundle stays on disk for the next Install */
        down_ = Downloader::instance().snapshot();
        focus_in(group_, list_->focus_object());
        caption_.clear();
        show(-1);
    }

    void confirm_install()
    {
        const DownloadSnapshot ready = down_;
        close_dialog(false);
        bool charging = false;
        const int battery = platform::battery(charging);
        if (battery_too_low(battery, charging)) { /* D16, again: the battery may have run down while downloading */
            note_ = "Battery " + std::to_string(battery) + " %: charge it or plug it in";
            show(-1);
            return;
        }
        ApplyRequest request;
        request.version = ready.download.version;
        request.from = snap_.build.version;
        request.sha256 = ready.sha256;
        request.file = ready.file;
        request.user = platform::user_name();
        request.cache_dir = platform::download_dir();
        RootJob::instance().apply(request);
        root_ = RootJob::instance().snapshot();
        show(-1);
    }

    void repair()
    {
        if (root_.asking) return;
        RootJob::instance().resume(platform::user_name());
        root_ = RootJob::instance().snapshot();
        show(-1);
    }

    // ---------------------------------------------------------------- rows and the workers

    /* A release this page shows needs no background toast any more (one notice per version, D3). */
    void seen()
    {
        if (snap_.state.available.empty() || compare_versions(snap_.state.available, snap_.state.notified) <= 0) return;
        Checker::instance().update_state([](State &state) { take_available_toast(state); });
        snap_ = Checker::instance().snapshot();
    }

    void activate(std::intptr_t id)
    {
        if (dialog_) return;
        switch (id) {
        case kLauncher: set_level(Level::Launcher); break;
        case kWhatsNew: set_level(Level::WhatsNew); break;
        case kShowLog: set_level(Level::Log); break;
        case kInstall: install(); break;
        case kStop: Downloader::instance().cancel(); break;
        case kRepair: repair(); break;
        case kCheckNow:
        case kCheckAgain:
            note_.clear();
            Checker::instance().start();
            poll(true);
            break;
        default: break; /* information */
        }
    }

    static void poll_cb(lv_timer_t *timer)
    {
        auto *self = static_cast<UpdatesPanel *>(lv_timer_get_user_data(timer));
        if (self) self->poll(false);
    }

    void poll(bool force)
    {
        Snapshot snap = Checker::instance().snapshot();
        bool changed = snap.running != snap_.running || snap.done != snap_.done;
        if (changed) {
            snap_ = std::move(snap);
            seen();
        }
        // the download: its progress every poll while it runs, the question once it is checked
        DownloadSnapshot down = Downloader::instance().snapshot();
        if (down.generation != down_.generation || down.done != down_.done) changed = true;
        const bool now_ready = down.phase == DownloadPhase::Ready && down.generation != down_.generation;
        down_ = std::move(down);
        // the root step: the prompt, its failure, the helper's unit started
        const RootSnapshot root = RootJob::instance().snapshot();
        const bool started = root.started != root_.started;
        if (root.asking != root_.asking || root.error != root_.error) changed = true;
        root_ = root;
        // the helper's status file
        const HelperPhase before = phase_;
        if (started || ++helper_polls_ >= kHelperPolls) {
            helper_polls_ = 0;
            read_helper();
            if (phase_ != before || level_ == Level::Progress) changed = true;
        }
        if (++polls_ >= kRefreshPolls) {
            polls_ = 0;
            force = true;
        }
        if (started) { /* systemd-run returned: the helper writes its first status within a second */
            waiting_for_helper_ = true;
            waiting_since_ = platform::now();
        }
        if (waiting_for_helper_ && (phase_ == HelperPhase::Running || helper_.started >= waiting_since_ - 2 ||
                                    platform::now() - waiting_since_ >= 15))
            waiting_for_helper_ = false; /* its run is in the status file (or it never wrote one) */
        if (started && level_ != Level::Root) {
            set_level(Level::Progress);
            return;
        }
        if (level_ == Level::Progress && !waiting_for_helper_ && phase_ != HelperPhase::Running) {
            set_level(Level::Launcher); /* done, refused, rolled back: the Launcher level says how it ended */
            return;
        }
        if (phase_ == HelperPhase::Running && before != HelperPhase::Running && level_ == Level::Launcher) {
            set_level(Level::Progress);
            return;
        }
        if (now_ready && ask_when_ready_ && level_ == Level::Launcher) {
            ask_when_ready_ = false;
            open_confirm();
        }
        if (changed || force) show(-1);
    }

    cp0::ui::Page &page_;
    lv_group_t *group_;
    std::vector<std::string> title_;
    std::function<void()> host_back_;

    Level level_ = Level::Root;
    Snapshot snap_;
    DownloadSnapshot down_;
    RootSnapshot root_;
    HelperStatus helper_;
    HelperPhase phase_ = HelperPhase::None;
    bool helper_installed_ = false;
    bool ask_when_ready_ = false; /* the download was started here: the question opens when it is checked */
    bool held_ = false;
    std::string note_; /* why Install was refused (space, battery, ...) */
    cp0_ui_list_layout_t layout_plain_ {}; /* the list under the status line ... */
    cp0_ui_list_layout_t layout_bar_ {};   /* ... and under the progress bar */
    bool list_with_bar_ = false;
    lv_obj_t *bar_ = nullptr;              /* the progress bar (lv_bar), not focusable */
    std::unique_ptr<cp0::ui::CompactList> list_;
    std::unique_ptr<cp0::ui::TextBlock> text_;
    std::unique_ptr<cp0::ui::Dialog> dialog_;
    std::string caption_;
    cp0::ui::CaptionTone caption_tone_ = cp0::ui::CaptionTone::Normal;
    lv_timer_t *timer_ = nullptr;
    int polls_ = 0;
    int helper_polls_ = 0;
    bool waiting_for_helper_ = false; /* the helper's unit started, its first status not read yet */
    long long waiting_since_ = 0;
    std::vector<std::function<void()>> posted_;
    bool post_armed_ = false;
};

} // namespace

std::unique_ptr<SettingsNativePanel> settings_updates_panel_create(const SettingsNativePanelContext &context)
{
    return std::make_unique<UpdatesPanel>(context);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_updates_page_factory(lv_obj_t *, const NodeIter &,
                                                                             std::function<void()>)
{
    return nullptr; /* native only: settings_native_panel_create() */
}

std::string settings_updates_row_value(int &value_rgb)
{
    const Snapshot snap = Checker::instance().snapshot();
    const RowValue value = root_value(snap.state, snap.build);
    switch (value.tone) {
    case Tone::Update: value_rgb = kUpdateRgb; break;
    case Tone::Muted: value_rgb = kMutedRgb; break;
    case Tone::Normal: value_rgb = -1; break; /* the roller's green */
    }
    return value.text;
}
