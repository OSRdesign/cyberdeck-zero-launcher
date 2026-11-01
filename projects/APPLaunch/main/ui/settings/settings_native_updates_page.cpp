/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates (see settings_native_updates_page.hpp).
 */

#include "settings_native_updates_page.hpp"

#include "settings_apps_jobs.hpp"
#include "settings_apt_model.hpp"
#include "settings_apt_platform.hpp"
#include "settings_apt_scan.hpp"
#include "settings_update_check.hpp"
#include "settings_update_download.hpp"
#include "settings_update_install.hpp"
#include "settings_update_install_model.hpp"
#include "settings_update_model.hpp"
#include "settings_update_platform.hpp"

#include "../native_screensaver.hpp"
#include "cp0_font_service.hpp"
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
    kInstall, kStop, kRepair, kLastUpdate, kShowLog, kNoHelper, kStep,
    kSystem, kAppUpdates, kRefreshLists, kNothing
};
/* System packages (P3): a package row's id says which row it is; the ones that open the terminal-command page are
 * kOpenBase and up, the information rows kInfoBase and up (a unique id each: a rebuilt list keeps the highlight on it). */
constexpr std::intptr_t kInfoBase = 1000;
constexpr std::intptr_t kOpenBase = 5000;
constexpr int kStaleRefreshS = 2; /* a status written this many seconds before the page opened still counts as this visit's */

ValueTone tone_of(launcher_apt::Tone tone)
{
    switch (tone) {
    case launcher_apt::Tone::Ok: return ValueTone::Ok;
    case launcher_apt::Tone::Warn: return ValueTone::Warn;
    case launcher_apt::Tone::Muted: return ValueTone::Muted;
    case launcher_apt::Tone::Normal: break;
    }
    return ValueTone::Normal;
}

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
        // System packages (P3): a dense list (the study's mockups: Compact, about 7 rows on the deck): a package row is a name
        // and "old > new"
        cp0_ui_list_layout(cp0_ui_metrics_get(), CP0_UI_LIST_COMPACT, pl.roller_x, top, pl.roller_w, std::max(1, pl.h - top),
                           &layout_dense_plain_);
        cp0_ui_list_layout(cp0_ui_metrics_get(), CP0_UI_LIST_COMPACT, pl.roller_x, below, pl.roller_w,
                           std::max(1, pl.h - below), &layout_dense_bar_);
        make_bar(pl.roller_x + layout_plain_.inset, top, std::max(1, pl.roller_w - 2 * layout_plain_.inset), bar_h);
        make_list(false);
        timer_ = lv_timer_create(poll_cb, kPollMs, this);

        Checker::instance().start(); /* D3: a check each time the page opens */
        opened_at_ = platform::now();
        launcher_apt::Scanner::instance().start(); /* P3: the plan of the system packages, read here, never in the background */
        settings_apps::refresh_app_updates();      /* D19: the "App updates" row's count (local, no network) */
        open_apps_installed_ = context.open_apps_installed;
        scan_ = launcher_apt::Scanner::instance().snapshot();
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
        case Level::Terminal: set_level(Level::System); return true;
        case Level::Launcher:
        case Level::System: set_level(Level::Root); return true;
        case Level::Root: break;
        }
        return false;
    }

private:
    enum class Level { Root, Launcher, WhatsNew, Progress, Log, System, Terminal };

    /* the levels that show a TextBlock instead of the list */
    bool text_level() const { return level_ == Level::WhatsNew || level_ == Level::Log || level_ == Level::Terminal; }

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

    // ---------------------------------------------------------------- System packages (P3, list only)

    /* The refresh of the lists is under way: the helper runs, or it was started a moment ago and has not written yet. */
    bool refreshing() const { return refresh_phase_ == launcher_apt::RefreshPhase::Running || refresh_waiting_; }

    /* The lists' own age and whether the list below says little (D12: older than a week, or never refreshed). */
    bool lists_stale() const
    {
        const launcher_apt::Overview &o = scan_.overview;
        return o.scanned && (o.lists_time <= 0 || launcher_apt::lists_age_days(o.lists_time, launcher_apt::platform::now()) >
                                                      launcher_apt::kStaleDays);
    }

    /* A package row's value that leaves its name room: "old > new", else "> new", else the end of the new version. The
     * list never cuts a value (it takes the label's room), so a long Debian version must be chosen here, by its width in
     * the list's own fonts. The name keeps its natural width or 40 % of the row, whichever is less. */
    std::string fit_value(const launcher_apt::PackageRow &row, bool opens) const
    {
        const cp0_ui_list_layout_t &l = layout_dense_plain_;
        auto width = [](const std::string &text, const lv_font_t *font) {
            if (text.empty() || !font) return 0;
            lv_point_t size{};
            lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            return static_cast<int>(size.x);
        };
        const lv_font_t *label_font = cp0_fonts().get("Montserrat-Medium.ttf", static_cast<uint16_t>(l.text_px));
        const lv_font_t *value_font = cp0_fonts().get("Montserrat-Medium.ttf", static_cast<uint16_t>(l.value_px));
        const int inner = l.w - 2 * l.inset - (opens ? l.row_h : 0); /* the chevron's slot */
        const int label_w = std::min(width(row.label, label_font), inner * 40 / 100);
        const int room = inner - l.gap - label_w;
        for (const std::string &candidate : row.values)
            if (width(candidate, value_font) <= room) return candidate;
        return row.values.back();
    }

    void system_rows(std::vector<ListRow> &rows) const
    {
        const launcher_apt::Overview &o = scan_.overview;
        const long long now = launcher_apt::platform::now();
        if (refreshing())
            rows.push_back(ListRow::item("Refresh lists", launcher_apt::refresh_value(refresh_), ValueTone::Normal,
                                         kRefreshLists, RowAffordance::ActionRunning));
        else
            rows.push_back(ListRow::item("Refresh lists", o.scanned ? last_checked_text(o.lists_time, now) : std::string(),
                                         lists_stale() ? ValueTone::Warn : ValueTone::Normal, kRefreshLists,
                                         root_.asking ? RowAffordance::ActionRunning : RowAffordance::Action));
        if (!o.scanned || o.failed || !o.report.valid) return;
        const launcher_apt::Report &r = o.report;
        if (r.shown() == 0)
            rows.push_back(ListRow::item("Nothing to upgrade", "", ValueTone::Muted, kNothing, RowAffordance::Info));
        std::intptr_t info = kInfoBase;
        std::intptr_t open = kOpenBase;
        std::intptr_t detect = kInfoBase + 500;
        auto section = [&](const char *title, const std::vector<launcher_apt::Package> &list, bool terminal) {
            if (list.empty()) return;
            rows.push_back(ListRow::section(launcher_apt::section_title(title, list.size())));
            for (const launcher_apt::Package &p : list) {
                const launcher_apt::PackageRow row = launcher_apt::package_row(p);
                // a package apt would install: grey, information only (no upgrade from here before P4); the boot chain and
                // what apt keeps back open the page with the command to run in a terminal
                rows.push_back(ListRow::item(row.label, fit_value(row, terminal), ValueTone::Muted, terminal ? open++ : info++,
                                             terminal ? RowAffordance::Open : RowAffordance::Info));
            }
        };
        section("Safe to upgrade", r.safe, false);
        section("Launcher libraries", r.libraries, false);
        section("Kernel, firmware and boot", r.boot, true);
        section("Needs the terminal", r.terminal, true);
        // D18: what else updates this board, shown and never changed
        rows.push_back(ListRow::section("On this board"));
        rows.push_back(ListRow::item("Automatic updates", launcher_apt::automatic_updates(o.detection), ValueTone::Muted,
                                     detect++, RowAffordance::Info));
        if (o.detection.packagekit)
            rows.push_back(ListRow::item("PackageKit", "installed", ValueTone::Muted, detect++, RowAffordance::Info));
        if (o.detection.snapd)
            rows.push_back(ListRow::item("Snap", "installed", ValueTone::Muted, detect++, RowAffordance::Info));
        if (o.detection.holds > 0)
            rows.push_back(ListRow::item("Held packages", std::to_string(o.detection.holds), ValueTone::Muted, detect++,
                                         RowAffordance::Info));
        if (!r.store.empty())
            rows.push_back(ListRow::item("Store apps left out", std::to_string(r.store.size()), ValueTone::Muted, detect++,
                                         RowAffordance::Info));
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
            // P3 (the study's mockups: System packages and App updates between Launcher and Check now)
            launcher_apt::Value system = launcher_apt::row_value(scan_.overview, launcher_apt::platform::now());
            if (!scan_.overview.scanned && scan_.running) system = {"checking", launcher_apt::Tone::Muted};
            rows.push_back(ListRow::item("Packages", system.text, tone_of(system.tone), kSystem, RowAffordance::Open));
            if (settings_apps::Jobs::available()) {
                const settings_apps::AppUpdates apps = settings_apps::app_updates();
                std::string text;
                ValueTone tone = ValueTone::Muted;
                if (apps.known && apps.count > 0) {
                    text = std::to_string(apps.count) + (apps.count == 1 ? " update" : " updates");
                    tone = ValueTone::Warn; /* the Apps page's amber "update offered" */
                } else if (apps.known) {
                    text = "None"; /* "No app updates": the row stays, it opens Installed (D19) */
                }
                rows.push_back(ListRow::item("App updates", text, tone, kAppUpdates, RowAffordance::Open));
            }
            rows.push_back(ListRow::item("Check now", "", ValueTone::Normal, kCheckNow, check));
            rows.push_back(ListRow::item("Last checked", last_checked_text(snap_.state.last_check, platform::now()),
                                         ValueTone::Normal, kLastChecked, RowAffordance::Info));
            break;
        }
        case Level::System:
            system_rows(rows);
            break;
        case Level::Terminal:
            break;
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
        if (text_level()) {
            text.clear(); /* the TextBlock starts at the top of the body (the log's first line says how it ended) */
        } else if (level_ == Level::System) {
            text = system_caption(tone);
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

    /* The System level's status line, most urgent first: the password prompt, why a refresh did not start or how it ended,
     * what the scan is doing, lists that say little, and otherwise the grey line: upgrading comes later, the terminal now. */
    std::string system_caption(cp0::ui::CaptionTone &tone) const
    {
        tone = cp0::ui::CaptionTone::Warn;
        const launcher_apt::Overview &o = scan_.overview;
        if (root_.asking) return "Type your password";
        if (!root_.error.empty()) return root_.error;
        if (!note_.empty()) return note_;
        if (refreshing()) {
            tone = cp0::ui::CaptionTone::Normal;
            return refresh_.text.empty() ? std::string("Refreshing the package lists...") : refresh_.text;
        }
        const launcher_apt::RefreshPhase phase = refresh_phase_;
        if (refresh_.updated + kStaleRefreshS >= opened_at_) {
            const std::string failure = launcher_apt::refresh_failure(refresh_, phase);
            if (!failure.empty()) return failure;
        }
        if (!o.scanned || scan_.running) {
            tone = cp0::ui::CaptionTone::Normal;
            return "Reading the package lists...";
        }
        if (o.failed || !o.report.valid) return "No plan from apt: try apt-get -s upgrade";
        if (o.lists_time <= 0) return "Lists never refreshed: refresh them first";
        if (lists_stale())
            return "Lists are " + std::to_string(launcher_apt::lists_age_days(o.lists_time, launcher_apt::platform::now())) +
                   " days old: refresh them first";
        tone = cp0::ui::CaptionTone::Normal;
        return launcher_apt::info_line();
    }

    void show_title()
    {
        std::vector<std::string> title = title_;
        if (level_ == Level::System || level_ == Level::Terminal) title.push_back("Packages");
        if (level_ == Level::Terminal) title.push_back("Terminal");
        if (level_ != Level::Root && level_ != Level::System && level_ != Level::Terminal) title.push_back("Launcher");
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
        list_dense_ = level_ == Level::System;
        list_ = std::make_unique<cp0::ui::CompactList>(page_.root(),
                                                       list_dense_ ? (with_bar ? layout_dense_bar_ : layout_dense_plain_)
                                                                   : (with_bar ? layout_bar_ : layout_plain_),
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
        if (level_ == Level::System && refreshing()) return launcher_apt::refresh_percent(refresh_); /* P3: apt-get update */
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
        if (want != list_with_bar_ && list_ && !text_level()) {
            const int selected = std::max(0, list_->selected());
            const std::vector<ListRow> current = rows();
            make_list(want);
            list_->reset_rows(current, selected);
            shown_ = current;
        }
        if (!bar_) return;
        if (want) {
            lv_bar_set_value(bar_, value, LV_ANIM_OFF);
            lv_obj_remove_flag(bar_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    static bool rows_equal(const std::vector<ListRow> &a, const std::vector<ListRow> &b)
    {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].kind != b[i].kind || a[i].label != b[i].label || a[i].value != b[i].value || a[i].tone != b[i].tone ||
                a[i].id != b[i].id || a[i].affordance != b[i].affordance)
                return false;
        return true;
    }

    /* selected >= 0: new content (a level), that row highlighted; -1: the live refresh (texts in place). */
    void show(int selected)
    {
        update_bar();
        if (!text_level() && list_) {
            const std::vector<ListRow> next = rows();
            bool same = selected < 0 && static_cast<int>(next.size()) == list_->count();
            for (int i = 0; same && i < list_->count(); ++i) same = list_->row(i).id == next[static_cast<std::size_t>(i)].id;
            if (same) {
                if (!rows_equal(next, shown_)) list_->set_rows(next); /* nothing new: the widget is left alone */
            } else {
                int target = std::max(0, selected);
                if (selected < 0) { /* rows came or went: the highlight stays on the same row when it is still there */
                    const std::intptr_t id = list_->row(list_->selected()).id;
                    for (std::size_t i = 0; i < next.size(); ++i)
                        if (next[i].id == id) target = static_cast<int>(i);
                }
                list_->reset_rows(next, target);
            }
            shown_ = next;
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
        const Level from = level_;
        level_ = level;
        note_.clear();
        if (level == Level::WhatsNew || level == Level::Log || level == Level::Terminal) {
            unfocus(list_->focus_object());
            list_->set_hidden(true);
            open_text(level == Level::Log ? log_text()
                      : level == Level::Terminal ? launcher_apt::terminal_text(scan_.overview.report)
                                                 : std::string());
            show(-1);
            hold_screen();
            return;
        }
        if (text_) {
            unfocus(text_->focus_object());
            text_.reset();
        }
        if (list_dense_ != (level == Level::System)) make_list(list_with_bar_); /* the System level's list is the dense one */
        list_->set_hidden(false);
        focus_in(group_, list_->focus_object());
        // a level opens on its first row, which is also the row it was opened from (Launcher, What's new); back from the
        // terminal page the highlight returns to the package it was opened from
        show(from == Level::Terminal && level == Level::System ? terminal_from_ : 0);
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
        case kSystem: set_level(Level::System); break;
        case kAppUpdates: open_apps(); break;
        case kRefreshLists: refresh_lists(); break;
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
        default:
            if (id >= kOpenBase && level_ == Level::System) { /* a boot-chain or kept-back package: the command to run */
                terminal_from_ = list_ ? std::max(0, list_->selected()) : 0;
                set_level(Level::Terminal);
            }
            break; /* otherwise information */
        }
    }

    /* D19: Settings > Apps on its Installed level. */
    void open_apps()
    {
        if (open_apps_installed_ && open_apps_installed_()) return;
        note_ = "Settings > Apps is not available";
        show(-1);
    }

    // ---------------------------------------------------------------- Refresh package lists (P3: the one root step)

    void read_refresh()
    {
        refresh_ = launcher_apt::parse_refresh_status(launcher_apt::platform::refresh_status());
        const bool alive = refresh_.present && refresh_.boot == platform::boot_id() && platform::process_alive(refresh_.pid);
        refresh_phase_ = launcher_apt::refresh_phase(refresh_, alive);
    }

    /* The password (the shared prompt), then systemd-run ... cyberdeckzero-update apt-refresh: apt-get update in a transient
     * unit. D12: only on this request. */
    void refresh_lists()
    {
        if (root_.asking || refreshing()) return;
        note_ = launcher_apt::refresh_blocker(helper_installed_ || platform::helper_installed(), platform::network_up(),
                                              phase_ == HelperPhase::Running, false);
        RootJob::instance().clear_error();
        root_ = RootJob::instance().snapshot();
        if (note_.empty()) {
            RootJob::instance().refresh_lists(platform::user_name());
            root_ = RootJob::instance().snapshot();
        }
        show(-1);
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
        // the system packages: the scan's result, the refresh's status
        const launcher_apt::ScanSnapshot scan = launcher_apt::Scanner::instance().snapshot();
        if (scan.done != scan_.done || scan.running != scan_.running) changed = true;
        scan_ = scan;
        // the root step: the prompt, its failure, the helper's unit started
        const RootSnapshot root = RootJob::instance().snapshot();
        const bool started = root.started != root_.started;
        if (root.apt_started != root_.apt_started) { /* systemd-run returned: apt-refresh writes its first status within a second */
            refresh_waiting_ = true;
            refresh_since_ = platform::now();
            read_refresh();
            changed = true;
        }
        if (root.asking != root_.asking || root.error != root_.error) changed = true;
        root_ = root;
        // the helper's status file
        const HelperPhase before = phase_;
        if (started || ++helper_polls_ >= kHelperPolls) {
            helper_polls_ = 0;
            read_helper();
            if (phase_ != before || level_ == Level::Progress) changed = true;
        }
        if (refreshing() || level_ == Level::System) {
            const launcher_apt::RefreshPhase was = refresh_phase_;
            const bool was_refreshing = refreshing();
            const int percent = refresh_.percent;
            read_refresh();
            if (refresh_waiting_ && (refresh_phase_ == launcher_apt::RefreshPhase::Running || refresh_.started >= refresh_since_ - 2 ||
                                     platform::now() - refresh_since_ >= 15))
                refresh_waiting_ = false; /* its run is in the status file (or it never wrote one) */
            if (refresh_phase_ != was || refresh_.percent != percent) changed = true;
            if (was_refreshing && !refreshing()) { /* the lists changed: read the plan again */
                launcher_apt::Scanner::instance().start();
                changed = true;
            }
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
    launcher_apt::ScanSnapshot scan_;                                   /* P3: the system packages */
    launcher_apt::RefreshStatus refresh_;                               /* the lists' refresh (apt-status) */
    launcher_apt::RefreshPhase refresh_phase_ = launcher_apt::RefreshPhase::None;
    bool refresh_waiting_ = false;                                      /* systemd-run returned, no status read yet */
    long long refresh_since_ = 0;
    long long opened_at_ = 0;                                           /* a refresh status older than this visit is not shown */
    int terminal_from_ = 0;                                             /* the row the terminal page was opened from */
    std::function<bool()> open_apps_installed_;
    bool ask_when_ready_ = false; /* the download was started here: the question opens when it is checked */
    bool held_ = false;
    std::string note_; /* why Install was refused (space, battery, ...) */
    cp0_ui_list_layout_t layout_plain_ {}; /* the list under the status line ... */
    cp0_ui_list_layout_t layout_bar_ {};   /* ... and under the progress bar */
    cp0_ui_list_layout_t layout_dense_plain_ {}; /* System packages: the dense list under the status line ... */
    cp0_ui_list_layout_t layout_dense_bar_ {};   /* ... and under the progress bar */
    bool list_dense_ = false;
    std::vector<ListRow> shown_; /* the rows the list was given last (a live refresh with nothing new skips set_rows) */
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
