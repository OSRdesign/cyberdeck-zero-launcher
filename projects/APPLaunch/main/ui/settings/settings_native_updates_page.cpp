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
#include "settings_apt_upgrade.hpp"
#include "settings_apt_upgrade_model.hpp"
#include "settings_update_check.hpp"
#include "settings_update_download.hpp"
#include "settings_update_install.hpp"
#include "settings_update_install_model.hpp"
#include "settings_update_model.hpp"
#include "settings_update_platform.hpp"

#include "settings_wait_box.hpp"
#include "settings_wait_model.hpp"

#include "../native_screensaver.hpp"
#include "sample_log.h"
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
    kSystem, kAppUpdates, kRefreshLists, kNothing,
    /* P4: the Packages page's action row, the confirmation page, the upgrade's progress page */
    kUpgrade, kAptRepair, kAptRunning, kSwitch, kUpgradeNow, kConfirmCancel, kAptCancel, kAptStep, kNoSwitchOff, kReassure
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
        // P4: the "working" box of a long wait (settings_wait_box.hpp), in the list's place under the status line
        {
            settings_wait::BoxLook look;
            look.x = pl.roller_x + layout_dense_plain_.inset / 2;
            look.y = top;
            look.w = std::max(1, pl.roller_w - layout_dense_plain_.inset);
            look.title_font = cp0_fonts().get("Montserrat-Medium.ttf", static_cast<uint16_t>(layout_dense_plain_.text_px));
            look.text_font = cp0_fonts().get("Montserrat-Medium.ttf", static_cast<uint16_t>(layout_dense_plain_.value_px));
            look.bar_h = bar_h;
            look.pad = cp0_ui_tok(CP0_TOK_SPACE_M);
            look.gap = cp0_ui_tok(CP0_TOK_SPACE_S);
            look.max_h = std::max(1, pl.h - top - space); /* the list's room: 640x480's large fonts take the small one */
            look.small_font = cp0_ui_font_px(pl.caption_px);
            wait_box_ = std::make_unique<settings_wait::WaitBox>(page_.root(), look);
        }
        slow_ = launcher_apt::platform::slow_board();
        timer_ = lv_timer_create(poll_cb, kPollMs, this);

        Checker::instance().start(); /* D3: a check each time the page opens */
        check_tick_ = lv_tick_get();
        opened_at_ = platform::now();
        begin_scan(); /* P3: the plan of the system packages, read here, never in the background */
        settings_apps::refresh_app_updates();      /* D19: the "App updates" row's count (local, no network) */
        open_apps_installed_ = context.open_apps_installed;
        scan_ = launcher_apt::Scanner::instance().snapshot();
        snap_ = Checker::instance().snapshot();
        down_ = Downloader::instance().snapshot();
        root_ = RootJob::instance().snapshot();
        read_helper();
        read_apt();
        seen();
        // a root update at work (this launcher started it, or the one before the restart): its progress at once
        if (phase_ == HelperPhase::Running) level_ = Level::Progress;
        // P4: a package upgrade or repair at work: its progress page (Packages under it)
        else if (upgrade_phase_ == launcher_apt::UpgradePhase::Running) level_ = Level::AptProgress;
        if (level_ == Level::AptProgress) make_list(false);
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
        confirm_text_.reset();
        wait_box_.reset();
        list_.reset(); /* a check or a download still running finishes on its own: the next page or the background reads it */
        // P4: a package scan left behind is stopped (its programs killed): the next visit scans again, never a half result
        if (launcher_apt::Scanner::instance().snapshot().running) launcher_apt::Scanner::instance().cancel();
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
        case Level::AptProgress:
            // P4: Esc only in the phase that can be cancelled (the download): it cancels; ignored while installing
            if (launcher_apt::cancellable(upgrade_, upgrade_phase_)) cancel_upgrade();
            return true;
        case Level::WhatsNew:
        case Level::Log: set_level(Level::Launcher); return true;
        case Level::Terminal:
        case Level::Confirm:
        case Level::AptResult: set_level(Level::System); return true;
        case Level::Launcher:
            // P4: Esc cancels a download of the release (nothing installed, the partial file removed by the downloader)
            if (download_is_offered() && down_.phase == DownloadPhase::Downloading) {
                Downloader::instance().cancel();
                SLOGI("[UPDATE] download cancelled by Esc");
            }
            set_level(Level::Root);
            return true;
        case Level::System:
            // P4: Esc in the box of a plan the user waits for: back to the list (the plan goes on); else leaving Packages
            // stops a scan or a plan at work (its programs killed): the next visit scans again
            if (wait_plan_) {
                wait_plan_ = false;
                show(-1);
                return true;
            }
            if (scan_.running) {
                launcher_apt::Scanner::instance().cancel();
                SLOGI("[UPDATE] package scan cancelled by Esc");
            }
            set_level(Level::Root);
            return true;
        case Level::Root: break;
        }
        return false;
    }

private:
    /* P4: Confirm (the text block, the switch, Upgrade now, Cancel), AptProgress (the upgrade's or the repair's steps and
     * bar), AptResult (how it ended, the end of the log for a failure) */
    enum class Level { Root, Launcher, WhatsNew, Progress, Log, System, Terminal, Confirm, AptProgress, AptResult };

    /* the levels that show a TextBlock instead of the list */
    bool text_level() const
    {
        return level_ == Level::WhatsNew || level_ == Level::Log || level_ == Level::Terminal || level_ == Level::AptResult;
    }
    /* the levels under Packages, with its dense list (the upgrade's progress page too: its four steps and the Cancel or
     * "Do not switch off" row fit under the bar at every size, 480x320 and the deck's 640x480 included) */
    static bool dense_level(Level level)
    {
        return level == Level::System || level == Level::Confirm || level == Level::AptProgress;
    }

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
        if (box_wanted()) return; /* P4: nothing listed yet (or the plan the user waits for): the working box instead */
        if (refreshing())
            rows.push_back(ListRow::item("Refresh lists", launcher_apt::refresh_value(refresh_), ValueTone::Normal,
                                         kRefreshLists, RowAffordance::ActionRunning));
        else
            rows.push_back(ListRow::item("Refresh lists", o.scanned ? last_checked_text(o.lists_time, now) : std::string(),
                                         lists_stale() ? ValueTone::Warn : ValueTone::Normal, kRefreshLists,
                                         root_.asking ? RowAffordance::ActionRunning : RowAffordance::Action));
        upgrade_row(rows); /* P4: the first action row below Refresh (or Repair after an interruption) */
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
        // P4: phased updates (Ubuntu): apt offers them later by itself; never in a plan, never an error
        if (!r.phased.empty())
            rows.push_back(ListRow::item("Deferred by apt (phasing)", std::to_string(r.phased.size()), ValueTone::Muted,
                                         detect++, RowAffordance::Info));
    }

    // ---------------------------------------------------------------- the visible waits (P4: "is it frozen?")

    /* A scan this page starts (opening Updates, entering Packages after a cancel, a refresh that ended): its elapsed
     * counter and its bar start again. */
    void begin_scan()
    {
        launcher_apt::Scanner::instance().start();
        scan_tick_ = stage_tick_ = lv_tick_get();
        stage_seen_ = checks_seen_ = -1;
        wait_bar_.reset();
    }

    /* The working box shows on Packages while nothing is listed yet (the first scan), or while the user waits for the
     * plan (Upgrade pressed before it was ready). */
    bool box_wanted() const
    {
        if (level_ != Level::System) return false;
        if (!scan_.overview.scanned) return scan_.running;
        return wait_plan_ && !planned();
    }

    void update_wait_box()
    {
        if (!wait_box_) return;
        if (!box_wanted()) {
            wait_box_->hide();
            return;
        }
        const int stage = std::max(1, scan_.stage);
        if (stage != stage_seen_ || scan_.checks != checks_seen_) { /* a step changed: the 20 s line waits again */
            stage_seen_ = stage;
            checks_seen_ = scan_.checks;
            stage_tick_ = lv_tick_get();
        }
        settings_wait::BoxState s;
        s.title = scan_.overview.scanned ? "Preparing the upgrade" : "Preparing the updates";
        s.step = launcher_apt::scan_stage_text(stage);
        s.percent = wait_bar_.value(settings_wait::stage_percent(
            stage, launcher_apt::kScanStages, stage == 4 ? settings_wait::unknown_fraction(scan_.checks) : 0.0));
        s.elapsed_ms = lv_tick_elaps(scan_tick_);
        s.since_change_ms = lv_tick_elaps(stage_tick_);
        s.slow = slow_;
        s.cancellable = true; /* Esc: the scan stopped (its programs killed), or back to the list (the plan goes on) */
        wait_box_->show(s);
        if (dialog_) dialog_->raise();
    }

    /* A wait whose status line carries an elapsed counter is under way (the line is redrawn when its second changes). */
    bool timed_wait() const
    {
        return scan_.running || refreshing() || snap_.running || apt_running() || waiting_for_helper_ ||
               level_ == Level::Progress ||
               (download_is_offered() && (down_.phase == DownloadPhase::Downloading || down_.phase == DownloadPhase::Verifying));
    }

    // ---------------------------------------------------------------- the upgrade of the safe set (P4)

    /* the plan the confirmation page shows and Upgrade now runs: with or without the launcher libraries (D14) */
    const launcher_apt::Plan &chosen_plan() const { return libraries_on_ ? scan_.plan_libraries : scan_.plan; }
    std::size_t library_count() const { return scan_.overview.report.libraries.size(); }
    bool repair_offered() const { return launcher_apt::repair_needed(upgrade_phase_, scan_.overview.dpkg_unfinished); }
    /* the scan reads the lists (the overview is not out yet); its planning part does not count */
    bool scanning() const { return scan_.running && !scan_.planning; }
    bool planned() const { return scan_.plan.ready && !scan_.planning; }
    bool apt_running() const { return upgrade_phase_ == launcher_apt::UpgradePhase::Running || upgrade_waiting_; }

    /* The helper's run as the progress page shows it: the status file, or (the second between systemd-run and the helper's
     * first write) the run this launcher just started. */
    launcher_apt::UpgradeStatus apt_status() const
    {
        if (upgrade_phase_ == launcher_apt::UpgradePhase::Running || !upgrade_waiting_) return upgrade_;
        launcher_apt::UpgradeStatus starting;
        starting.present = true;
        starting.state = "running";
        starting.job = asked_repair_ ? "repair" : "upgrade";
        starting.phase = asked_repair_ ? "repair" : "check";
        starting.total = asked_repair_ ? -1 : asked_count_;
        starting.started = upgrade_since_;
        return starting;
    }

    /* The Packages page's first action row below Refresh: Repair after an interruption, the run at work, or "Upgrade N
     * packages" with its download in amber (the Apps page's "update offered" token); "checking" while the plan is made. */
    void upgrade_row(std::vector<ListRow> &rows) const
    {
        const launcher_apt::Overview &o = scan_.overview;
        if (apt_running()) {
            const launcher_apt::UpgradeStatus s = apt_status();
            std::string value = s.total > 0 && s.done >= 0 ? std::to_string(std::min(s.done, s.total)) + " of " +
                                                                  std::to_string(s.total)
                                                            : std::string();
            rows.push_back(ListRow::item(s.job == "repair" ? "Repairing" : "Upgrading", value, ValueTone::Normal, kAptRunning,
                                         RowAffordance::ActionRunning));
            return;
        }
        if (o.scanned && repair_offered()) {
            rows.push_back(ListRow::item("Repair the installation", "", ValueTone::Normal, kAptRepair,
                                         root_.asking && asked_ == kAptRepair ? RowAffordance::ActionRunning
                                                                              : RowAffordance::Action));
            return;
        }
        if (!o.scanned || o.failed || !o.report.valid) return;
        if (o.report.safe.empty() && o.report.libraries.empty()) return; /* only the terminal's: nothing to offer */
        if (!planned()) {
            rows.push_back(ListRow::item("Upgrade packages", "checking", ValueTone::Muted, kUpgrade, RowAffordance::Info));
            return;
        }
        const launcher_apt::Plan &plan = scan_.plan;
        const launcher_apt::Plan &all = scan_.plan_libraries;
        if (!plan.valid && !all.valid) {
            rows.push_back(ListRow::item("Upgrade packages", "no plan", ValueTone::Muted, kUpgrade, RowAffordance::Info));
            return;
        }
        if (plan.count() == 0 && all.count() == 0) return;
        rows.push_back(ListRow::item(launcher_apt::upgrade_label(plan),
                                     plan.count() > 0 ? launcher_apt::upgrade_value(plan) : std::string("libraries only"),
                                     ValueTone::Warn, kUpgrade, RowAffordance::Action));
    }

    /* Another package job: Settings > Apps installing an app (the shared busy flag), a launcher update, a refresh. */
    std::string busy_reason() const
    {
        if (settings_apps::package_job_running()) return "Settings > Apps is installing an app";
        if (phase_ == HelperPhase::Running) return "A launcher update is running";
        if (refreshing()) return "The package lists are being refreshed";
        if (upgrade_phase_ == launcher_apt::UpgradePhase::Running) return "A package job is running";
        return std::string();
    }

    launcher_apt::UpgradeGuards guards(const launcher_apt::Plan &plan, int packages) const
    {
        launcher_apt::UpgradeGuards g;
        g.helper_installed = helper_installed_ || platform::helper_installed();
        g.busy = busy_reason();
        g.repair_needed = repair_offered();
        g.packages = packages;
        g.lists_time = scan_.overview.lists_time;
        g.now = launcher_apt::platform::now();
        g.under_voltage = launcher_apt::platform::under_voltage();
        g.battery_percent = platform::battery(g.charging);
        g.need_bytes = plan.need();
        g.free_bytes = launcher_apt::platform::upgrade_free();
        g.network = platform::network_up();
        return g;
    }

    /* "Upgrade N packages": the guards before anything (refused in the status line), then the confirmation page. */
    void open_upgrade()
    {
        if (root_.asking || apt_running()) return;
        RootJob::instance().clear_error();
        root_ = RootJob::instance().snapshot();
        if (!planned()) {
            // P4: the box with the plan's checks until it is ready, then this again (poll): never a silent wait
            if (!wait_plan_) {
                wait_plan_ = true;
                if (!scan_.running) { /* no plan is being made (a scan was stopped): make it */
                    begin_scan();
                    scan_ = launcher_apt::Scanner::instance().snapshot();
                }
                wait_bar_.reset();
                stage_seen_ = checks_seen_ = -1;
                reselect_ = kUpgrade; /* Esc on the box: the list again, on this row */
                list_->reset_rows(std::vector<ListRow>(), 0);
                shown_.clear();
            }
            show(-1);
            return;
        } else if (!scan_.plan.valid && !scan_.plan_libraries.valid) {
            note_ = "No plan from apt: " + scan_.plan.error;
        } else {
            const launcher_apt::Plan &plan = scan_.plan.count() > 0 ? scan_.plan : scan_.plan_libraries;
            note_ = launcher_apt::upgrade_blocker(guards(plan, std::max(scan_.plan.count(), scan_.plan_libraries.count())));
        }
        if (!note_.empty()) {
            show(-1);
            return;
        }
        libraries_on_ = false; /* D14: the launcher libraries are left out unless the switch is turned on, each time */
        set_level(Level::Confirm);
    }

    std::string confirm_text() const
    {
        bool charging = false;
        const bool battery = platform::battery(charging) >= 0;
        return launcher_apt::confirm_text(chosen_plan(), battery, libraries_on_, library_count());
    }

    const lv_font_t *body_font() const
    {
        const int px = cp0_ui_metrics_get()->text_px[CP0_TEXT_BODY];
        const lv_font_t *font = settings_fonts::cjk_sans(static_cast<uint16_t>(px));
        return font ? font : cp0_ui_font(CP0_TEXT_BODY);
    }

    /* The confirmation page: the read-only text at the top (a TextBlock in the area above the rows: its natural height
     * while the rows fit under it, else it scrolls: a drag, or Up / Down while the first row is highlighted), the dense
     * list of its rows under it (the switch, Upgrade now, Cancel). selected: the row to highlight. */
    void build_confirm(int selected)
    {
        const cp0_ui_roller_page_t &pl = page_.layout();
        const int line_h = lv_font_get_line_height(cp0_ui_font_px(pl.caption_px));
        const int space = cp0_ui_tok(CP0_TOK_SPACE_S);
        const int top = pl.caption_y + line_h + space;
        const lv_font_t *font = body_font();
        const std::string text = confirm_text();
        lv_point_t size{};
        lv_text_get_size(&size, text.c_str(), font, 0, 0, std::max(1, pl.roller_w - 2 * pl.inset - 2), LV_TEXT_FLAG_NONE);
        const int rows = library_count() > 0 ? 3 : 2;
        const int rows_h = rows * layout_dense_plain_.row_h;
        const int natural = static_cast<int>(size.y) + 2 * pl.gap;
        const int room = std::max(2 * lv_font_get_line_height(font) + 2 * pl.gap, pl.h - top - rows_h);
        const int text_h = std::min(natural, room);
        cp0_ui_roller_page_t text_layout = pl;
        text_layout.body_y = top;
        text_layout.body_h = text_h;
        if (confirm_text_) unfocus(confirm_text_->focus_object());
        confirm_text_.reset();
        confirm_text_ = std::make_unique<cp0::ui::TextBlock>(page_.root(), text_layout, text, font, host_back_);
        cp0_ui_list_layout(cp0_ui_metrics_get(), CP0_UI_LIST_COMPACT, pl.roller_x, top + text_h, pl.roller_w,
                           std::max(1, pl.h - top - text_h), &layout_confirm_);
        make_list(false);
        list_->reset_rows(rows_now(), std::max(0, selected));
        shown_ = rows_now();
    }

    /* Up / Down while the first row is highlighted scroll the text above it first (the page reads as one column); Page
     * Up / Down always scroll the text. Runs before the list's own key handler (LV_EVENT_PREPROCESS). */
    static void confirm_key_cb(lv_event_t *event)
    {
        auto *self = static_cast<UpdatesPanel *>(lv_event_get_user_data(event));
        if (!self || self->level_ != Level::Confirm || !self->confirm_text_ || !self->list_) return;
        lv_obj_t *box = self->confirm_text_->focus_object();
        if (!box) return;
        const uint32_t key = lv_event_get_key(event);
        const int step = std::max(1, self->page_.layout().row_h);
        const int page = std::max(step, static_cast<int>(lv_obj_get_height(box)) - step);
        const bool first = self->list_->selected() <= 0;
        int dy = 0;
        if (key == LV_KEY_DOWN && first && lv_obj_get_scroll_bottom(box) > 0) dy = -step;
        else if (key == LV_KEY_UP && first && lv_obj_get_scroll_y(box) > 0) dy = step;
        else if (key == LV_KEY_NEXT) dy = -page;
        else if (key == LV_KEY_PREV) dy = page;
        if (dy == 0) return;
        lv_obj_scroll_by_bounded(box, 0, dy, LV_ANIM_ON);
        lv_event_stop_processing(event);
    }

    void toggle_libraries()
    {
        if (root_.asking) return;
        libraries_on_ = !libraries_on_;
        note_.clear();
        build_confirm(list_ ? list_->selected() : 0);
        show(-1);
    }

    /* Upgrade now: the guards again with the chosen plan, then the shared sudo prompt runs systemd-run ... apt-upgrade. */
    void upgrade_now()
    {
        if (root_.asking || apt_running()) return;
        const launcher_apt::Plan &plan = chosen_plan();
        RootJob::instance().clear_error();
        root_ = RootJob::instance().snapshot();
        note_ = launcher_apt::upgrade_blocker(guards(plan, plan.count()));
        if (note_.empty() && !plan.valid) note_ = "No plan from apt: " + plan.error;
        if (note_.empty()) {
            asked_ = kUpgradeNow;
            asked_repair_ = false;
            asked_count_ = plan.count();
            RootJob::instance().upgrade_packages(platform::user_name(), plan.names(), libraries_on_);
            root_ = RootJob::instance().snapshot();
        }
        show(-1);
    }

    /* Repair the installation: dpkg --configure -a through the helper (the same prompt). */
    void start_repair()
    {
        if (root_.asking || apt_running()) return;
        RootJob::instance().clear_error();
        root_ = RootJob::instance().snapshot();
        note_ = launcher_apt::repair_blocker(guards(scan_.plan, 0));
        if (note_.empty()) {
            asked_ = kAptRepair;
            asked_repair_ = true;
            asked_count_ = 0;
            RootJob::instance().repair_packages(platform::user_name());
            root_ = RootJob::instance().snapshot();
        }
        show(-1);
    }

    /* Cancel (the row, Esc) while the download runs: the flag file the helper reads in that phase only. */
    void cancel_upgrade()
    {
        if (cancel_asked_ || upgrade_phase_ != launcher_apt::UpgradePhase::Running ||
            !launcher_apt::cancellable(upgrade_, upgrade_phase_))
            return;
        cancel_asked_ = launcher_apt::platform::request_cancel();
        if (!cancel_asked_) note_ = "Could not ask the helper to stop";
        show(-1);
    }

    void read_apt()
    {
        const launcher_apt::UpgradeRead read = launcher_apt::read_upgrade();
        upgrade_ = read.status;
        upgrade_phase_ = read.phase;
    }

    /* The result page's text: how the run ended, the end of the log for a failure. */
    std::string apt_result_text() const
    {
        if (upgrade_since_ > 0 && upgrade_.started + 2 < upgrade_since_) /* the helper wrote nothing for this run */
            return std::string(asked_repair_ ? "The repair" : "The upgrade") +
                   " did not start: the helper wrote no status. See journalctl -u cyberdeckzero-apt in a terminal.";
        const std::string log = upgrade_.log.empty() ? std::string() : platform::read_log(upgrade_.log, kLogBytes);
        return launcher_apt::result_text(upgrade_, upgrade_phase_, launcher_apt::last_lines(log, 40));
    }

    std::vector<ListRow> rows_now() const { return rows(); }

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
        case Level::AptResult:
            break;
        case Level::Confirm: {
            // the switch (D14, default off; only when there are launcher libraries to include), Upgrade now, Cancel
            if (library_count() > 0)
                rows.push_back(ListRow::item(launcher_apt::libraries_label(library_count()),
                                             launcher_apt::switch_value(libraries_on_),
                                             libraries_on_ ? ValueTone::Warn : ValueTone::Muted, kSwitch,
                                             RowAffordance::Edit));
            rows.push_back(ListRow::item("Upgrade now", "", ValueTone::Normal, kUpgradeNow,
                                         root_.asking && asked_ == kUpgradeNow ? RowAffordance::ActionRunning
                                                                               : RowAffordance::Action));
            rows.push_back(ListRow::item("Cancel", "", ValueTone::Normal, kConfirmCancel, RowAffordance::Action));
            break;
        }
        case Level::AptProgress: {
            const launcher_apt::UpgradeStatus s = apt_status();
            for (const launcher_apt::StepLine &step : launcher_apt::progress_steps(s, launcher_apt::platform::now())) {
                const ValueTone tone = step.state == launcher_apt::StepState::Done      ? ValueTone::Ok
                                       : step.state == launcher_apt::StepState::Current ? ValueTone::Normal
                                                                                        : ValueTone::Muted;
                rows.push_back(ListRow::item(step.label, step.value, tone, kAptStep, RowAffordance::Info));
            }
            if (upgrade_phase_ == launcher_apt::UpgradePhase::Running && launcher_apt::cancellable(s, upgrade_phase_))
                // the download, or (P4) the check before it: both stop with nothing installed
                rows.push_back(ListRow::item(s.phase == "download" ? (cancel_asked_ ? "Cancelling the download"
                                                                                    : "Cancel the download")
                                                                   : (cancel_asked_ ? "Cancelling" : "Cancel"),
                                             "", ValueTone::Normal, kAptCancel,
                                             cancel_asked_ ? RowAffordance::ActionRunning : RowAffordance::Action));
            else if (s.phase == "install" || s.job == "repair")
                rows.push_back(ListRow::item(launcher_apt::do_not_switch_off(), "", ValueTone::Normal, kNoSwitchOff,
                                             RowAffordance::Info));
            // P4: a phase with no count (the check, the clean-up, the repair, the start): the reassurance on a slow board,
            // and on any board once 20 s pass with no change in the helper's status
            const bool counted = (s.phase == "download" || s.phase == "install") && s.total > 0;
            const bool still = settings_wait::still(lv_tick_elaps(apt_change_tick_));
            if (!counted && (slow_ || still))
                rows.push_back(ListRow::item(still ? settings_wait::still_row() : settings_wait::slow_row(), "",
                                             ValueTone::Muted, kReassure, RowAffordance::Info));
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
        if (text_level()) {
            text.clear(); /* the TextBlock starts at the top of the body (the log's first line says how it ended) */
        } else if (level_ == Level::System) {
            text = system_caption(tone);
        } else if (level_ == Level::Confirm) {
            text = confirm_caption(tone);
        } else if (level_ == Level::AptProgress) {
            // P4: what the helper does now ("Downloading 12 of 40"), amber as the update's progress line, with the run's
            // elapsed time; between systemd-run and the helper's first status: "Starting the helper..."
            const std::string problem = !root_.error.empty() ? root_.error : note_;
            if (!problem.empty()) text = problem;
            else if (upgrade_waiting_ && upgrade_phase_ != launcher_apt::UpgradePhase::Running)
                text = settings_wait::with_elapsed("Starting the helper...", lv_tick_elaps(apt_start_tick_));
            else
                text = settings_wait::with_elapsed(launcher_apt::progress_caption(apt_status(), cancel_asked_),
                                                   upgrade_.started > 0 ? (platform::now() - upgrade_.started) * 1000 : 0);
            tone = problem.empty() ? cp0::ui::CaptionTone::Warn : cp0::ui::CaptionTone::Error;
        } else if (level_ == Level::Progress) {
            if (!root_.error.empty()) text = root_.error;
            else if (waiting_for_helper_ && phase_ != HelperPhase::Running)
                text = settings_wait::with_elapsed("Starting the helper...", lv_tick_elaps(helper_start_tick_));
            else text = progress_caption(progress_status());
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
        } else if (level_ == Level::Launcher && download_is_offered() &&
                   (down_.phase == DownloadPhase::Downloading || down_.phase == DownloadPhase::Verifying)) {
            text = settings_wait::with_elapsed(down_.phase == DownloadPhase::Downloading ? "Downloading the update"
                                                                                          : "Checking the download",
                                               lv_tick_elaps(download_tick_));
        } else if (snap_.running) {
            text = settings_wait::with_elapsed("Checking for updates...", lv_tick_elaps(check_tick_));
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

    /* The confirmation page's status line: the password prompt, a refusal, D14's warning while the switch is on. */
    std::string confirm_caption(cp0::ui::CaptionTone &tone) const
    {
        tone = cp0::ui::CaptionTone::Warn;
        if (root_.asking) return "Type your password";
        if (!root_.error.empty()) return root_.error;
        if (!note_.empty()) return note_;
        if (libraries_on_ && library_count() > 0) return launcher_apt::libraries_warning();
        tone = cp0::ui::CaptionTone::Normal;
        return library_count() > 0 ? std::string(launcher_apt::libraries_off_line()) : std::string();
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
            if (refresh_phase_ != launcher_apt::RefreshPhase::Running) /* systemd-run returned, no status yet */
                return settings_wait::with_elapsed("Starting the helper...", lv_tick_elaps(refresh_tick_));
            return settings_wait::with_elapsed(refresh_.text.empty() ? std::string("Refreshing the package lists")
                                                                     : refresh_.text,
                                               lv_tick_elaps(refresh_tick_));
        }
        const launcher_apt::RefreshPhase phase = refresh_phase_;
        if (refresh_.updated + kStaleRefreshS >= opened_at_) {
            const std::string failure = launcher_apt::refresh_failure(refresh_, phase);
            if (!failure.empty()) return failure;
        }
        if (!o.scanned || box_wanted()) { /* the working box says it all (P4) */
            tone = cp0::ui::CaptionTone::Normal;
            return std::string();
        }
        if (scanning()) { /* a rescan under the list already shown: a small inline status */
            tone = cp0::ui::CaptionTone::Normal;
            return settings_wait::with_elapsed(launcher_apt::scan_stage_text(std::max(1, scan_.stage)),
                                               lv_tick_elaps(scan_tick_));
        }
        if (repair_offered()) return "An installation was interrupted: Repair";
        if (o.failed || !o.report.valid) return "No plan from apt: try apt-get -s upgrade";
        if (o.lists_time <= 0) return "Lists never refreshed: refresh them first";
        if (lists_stale())
            return "Lists are " + std::to_string(launcher_apt::lists_age_days(o.lists_time, launcher_apt::platform::now())) +
                   " days old: refresh them first";
        tone = cp0::ui::CaptionTone::Normal;
        if (!planned() && o.report.safe.size() + o.report.libraries.size() > 0)
            return settings_wait::with_elapsed(launcher_apt::scan_stage_text(4), lv_tick_elaps(scan_tick_));
        // P4: the safe set upgrades from here; what stays the terminal's is said (P3's line when nothing can be upgraded)
        if (scan_.plan.count() > 0 || scan_.plan_libraries.count() > 0) return launcher_apt::packages_line(o.report);
        return launcher_apt::info_line();
    }

    void show_title()
    {
        std::vector<std::string> title = title_;
        const bool packages = level_ == Level::System || level_ == Level::Terminal || level_ == Level::Confirm ||
                              level_ == Level::AptProgress || level_ == Level::AptResult;
        const bool repair = apt_status().job == "repair";
        if (packages) title.push_back("Packages");
        if (level_ == Level::Terminal) title.push_back("Terminal");
        if (level_ == Level::Confirm) title.push_back("Upgrade");
        if (level_ == Level::AptProgress) title.push_back(repair ? "Repairing" : "Upgrading");
        if (level_ == Level::AptResult) title.push_back(repair ? "Repair" : "Upgrade");
        if (level_ != Level::Root && !packages) title.push_back("Launcher");
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
        list_dense_ = dense_level(level_);
        list_confirm_ = level_ == Level::Confirm; /* P4: the confirmation page's rows sit under its text block */
        list_ = std::make_unique<cp0::ui::CompactList>(page_.root(),
                                                       list_confirm_ ? layout_confirm_
                                                       : list_dense_ ? (with_bar ? layout_dense_bar_ : layout_dense_plain_)
                                                                     : (with_bar ? layout_bar_ : layout_plain_),
                                                       std::move(events));
        list_with_bar_ = with_bar;
        if (list_confirm_)
            lv_obj_add_event_cb(list_->focus_object(), confirm_key_cb,
                                static_cast<lv_event_code_t>(LV_EVENT_KEY | LV_EVENT_PREPROCESS), this);
        if (dialog_) dialog_->raise(); /* the question keeps the keys; close_dialog() gives them to the list */
        else focus_in(group_, list_->focus_object());
    }

    /* What the bar shows now (0..100), -1 for no bar: the download of the offered release (its bytes; the check of
     * the finished file: full), the Updating page (the run's steps). */
    /* -2: a moving segment (P4: a wait of unknown length, between systemd-run and the helper's first status) */
    static constexpr int kSweep = -2;

    int bar_value() const
    {
        if (level_ == Level::Progress) return step_percent(progress_status());
        if (level_ == Level::AptProgress) {
            if (upgrade_waiting_ && upgrade_phase_ != launcher_apt::UpgradePhase::Running) return kSweep;
            return launcher_apt::progress_percent(apt_status()); /* P4: -1 for a repair */
        }
        if (level_ == Level::System && refreshing()) {
            if (refresh_phase_ != launcher_apt::RefreshPhase::Running) return kSweep;
            return launcher_apt::refresh_percent(refresh_); /* P3: apt-get update */
        }
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
        const bool want = (value >= 0 || value == kSweep) && bar_ != nullptr;
        if (want != list_with_bar_ && list_ && !text_level()) {
            const int selected = std::max(0, list_->selected());
            const std::vector<ListRow> current = rows();
            make_list(want);
            list_->reset_rows(current, selected);
            shown_ = current;
        }
        if (!bar_) return;
        if (want && value == kSweep) { /* the moving segment, 4 steps a second at most */
            int start = 0, end = 0;
            const uint32_t since = level_ == Level::AptProgress ? apt_start_tick_ : refresh_tick_;
            const long long t = lv_tick_elaps(since);
            settings_wait::sweep(t - t % 250, start, end);
            if (!bar_range_) {
                lv_bar_set_mode(bar_, LV_BAR_MODE_RANGE);
                bar_range_ = true;
            }
            lv_bar_set_start_value(bar_, start, LV_ANIM_OFF);
            lv_bar_set_value(bar_, end, LV_ANIM_OFF);
            lv_obj_remove_flag(bar_, LV_OBJ_FLAG_HIDDEN);
        } else if (want) {
            if (bar_range_) {
                lv_bar_set_mode(bar_, LV_BAR_MODE_NORMAL);
                bar_range_ = false;
            }
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
                if (selected < 0) { /* rows came or went: the highlight stays on the same row when it is still there; P4:
                                       after the working box (no row shown) on the row the box came from, else the first */
                    const int current = list_->selected();
                    const bool had_row = list_->count() > 0 && current >= 0;
                    const std::intptr_t id = had_row ? list_->row(current).id : reselect_;
                    if (had_row && current < static_cast<int>(next.size()) && next[static_cast<std::size_t>(current)].id == id)
                        target = current; /* rows added below it (the progress page's steps share one id) */
                    else
                        for (std::size_t i = 0; id != 0 && i < next.size(); ++i)
                            if (next[i].id == id) {
                                target = static_cast<int>(i);
                                break;
                            }
                }
                if (!next.empty()) reselect_ = 0;
                list_->reset_rows(next, target);
            }
            shown_ = next;
            if (level_ == Level::Progress) { /* the highlight on the running step */
                const std::vector<StepRow> steps = progress_rows(progress_status(), platform::now());
                for (std::size_t i = 0; i < steps.size(); ++i)
                    if (steps[i].mark == StepMark::Current && list_->selected() != static_cast<int>(i))
                        list_->select(static_cast<int>(i));
            }
            if (level_ == Level::AptProgress) { /* P4: the highlight moves to the running step when the step changes (the
                                                   Cancel row under it stays reachable by the keys meanwhile) */
                const std::vector<launcher_apt::StepLine> steps =
                    launcher_apt::progress_steps(apt_status(), launcher_apt::platform::now());
                for (std::size_t i = 0; i < steps.size(); ++i)
                    if (steps[i].state == launcher_apt::StepState::Current && apt_step_shown_ != static_cast<int>(i)) {
                        apt_step_shown_ = static_cast<int>(i);
                        list_->select(static_cast<int>(i));
                    }
            }
        }
        update_wait_box();
        show_title();
        show_caption();
        hold_screen();
    }

    void set_level(Level level)
    {
        if (level == Level::Launcher && phase_ == HelperPhase::Running) level = Level::Progress;
        if (level == Level::System && apt_running()) level = Level::AptProgress; /* P4: the run's page while it works */
        const Level from = level_;
        level_ = level;
        note_.clear();
        // P4: back on Packages after an upgrade or a repair, the plan is read again (the lists' age does not change); entering
        // it after a scan was cancelled (or never ran) scans again
        if (level == Level::System &&
            (from == Level::AptResult || from == Level::AptProgress || (!scan_.overview.scanned && !scan_.running))) {
            begin_scan();
            scan_ = launcher_apt::Scanner::instance().snapshot();
        }
        if (level != Level::System) wait_plan_ = false;
        if (from == Level::Confirm && level != Level::Confirm && confirm_text_) {
            unfocus(confirm_text_->focus_object());
            confirm_text_.reset();
        }
        if (level == Level::AptProgress) apt_step_shown_ = -1;
        if (level == Level::WhatsNew || level == Level::Log || level == Level::Terminal || level == Level::AptResult) {
            unfocus(list_->focus_object());
            list_->set_hidden(true);
            open_text(level == Level::Log ? log_text()
                      : level == Level::Terminal ? launcher_apt::terminal_text(scan_.overview.report)
                      : level == Level::AptResult ? apt_result_text()
                                                  : std::string());
            show(-1);
            hold_screen();
            return;
        }
        if (text_) {
            unfocus(text_->focus_object());
            text_.reset();
        }
        if (level == Level::Confirm) { /* its text block and its rows under it */
            build_confirm(0);
            focus_in(group_, list_->focus_object());
            show(-1);
            hold_screen();
            return;
        }
        if (list_confirm_ || list_dense_ != dense_level(level)) make_list(list_with_bar_); /* Packages: the dense list */
        list_->set_hidden(false);
        focus_in(group_, list_->focus_object());
        // a level opens on its first row, which is also the row it was opened from (Launcher, What's new); back from the
        // terminal page the highlight returns to the package it was opened from
        show(from == Level::Terminal && level == Level::System ? terminal_from_ : 0);
        hold_screen();
    }

    /* The lock screen stays away while a progress page shows (the helper's own systemd-inhibit covers the system), and
     * (P4) while Packages waits for a scan, a plan or a refresh: the user watches the wait. */
    void hold_screen()
    {
        const bool hold = level_ == Level::Progress || level_ == Level::AptProgress ||
                          (level_ == Level::System && (scan_.running || refreshing() || wait_plan_));
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
        if (apt_running() || settings_apps::package_job_running()) { /* P4: one package job at a time */
            note_ = apt_running() ? "A package job is running" : "Settings > Apps is installing an app";
            show(-1);
            return;
        }
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
        case kUpgrade: open_upgrade(); break;
        case kAptRepair: start_repair(); break;
        case kAptRunning: set_level(Level::AptProgress); break;
        case kSwitch: toggle_libraries(); break;
        case kUpgradeNow: upgrade_now(); break;
        case kConfirmCancel: set_level(Level::System); break;
        case kAptCancel: cancel_upgrade(); break;
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
        if (note_.empty() && apt_running()) note_ = "A package job is running"; /* P4: one apt job at a time */
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
        // P4: the measured waits, one log line each ("[UPDATE] phase <name> took <ms>"), and their elapsed counters
        if (snap.running && !snap_.running) check_tick_ = lv_tick_get();
        if (!snap.running && snap_.running) SLOGI("[UPDATE] phase release-check took %u ms", lv_tick_elaps(check_tick_));
        if (changed) {
            snap_ = std::move(snap);
            seen();
        }
        // the download: its progress every poll while it runs, the question once it is checked
        DownloadSnapshot down = Downloader::instance().snapshot();
        if (down.generation != down_.generation || down.done != down_.done) changed = true;
        const bool now_ready = down.phase == DownloadPhase::Ready && down.generation != down_.generation;
        if (down.phase == DownloadPhase::Downloading && down_.phase != DownloadPhase::Downloading) download_tick_ = lv_tick_get();
        if (down.phase != DownloadPhase::Downloading && down_.phase == DownloadPhase::Downloading)
            SLOGI("[UPDATE] phase download took %u ms", lv_tick_elaps(download_tick_));
        down_ = std::move(down);
        // the system packages: the scan's result, the refresh's status
        const launcher_apt::ScanSnapshot scan = launcher_apt::Scanner::instance().snapshot();
        if (scan.done != scan_.done || scan.running != scan_.running || scan.planned != scan_.planned ||
            scan.planning != scan_.planning)
            changed = true;
        const bool replanned = scan.planned != scan_.planned;
        scan_ = scan;
        // the root step: the prompt, its failure, the helper's unit started
        const RootSnapshot root = RootJob::instance().snapshot();
        const bool started = root.started != root_.started;
        if (root.apt_started != root_.apt_started) { /* systemd-run returned: apt-refresh writes its first status within a second */
            refresh_waiting_ = true;
            refresh_since_ = platform::now();
            refresh_tick_ = lv_tick_get();
            read_refresh();
            changed = true;
        }
        const bool apt_started = root.upgrade_started != root_.upgrade_started; /* P4: apt-upgrade / apt-repair's unit runs */
        if (apt_started) {
            upgrade_waiting_ = true;
            upgrade_since_ = platform::now();
            apt_start_tick_ = apt_change_tick_ = apt_phase_tick_ = lv_tick_get();
            apt_phase_seen_ = "start";
            cancel_asked_ = false;
            changed = true;
        }
        if (started) helper_start_tick_ = lv_tick_get();
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
                SLOGI("[UPDATE] phase refresh took %u ms", lv_tick_elaps(refresh_tick_));
                begin_scan();
                changed = true;
            }
        }
        // P4: the package job's status file: every poll while its page shows, else with the helper's (every second)
        const launcher_apt::UpgradePhase apt_before = upgrade_phase_;
        if (apt_started || level_ == Level::AptProgress || helper_polls_ == 0) {
            const launcher_apt::UpgradeStatus was = upgrade_;
            read_apt();
            const bool moved = upgrade_.phase != was.phase || upgrade_.done != was.done || upgrade_.percent != was.percent ||
                               upgrade_.state != was.state || upgrade_.text != was.text;
            if (moved) apt_change_tick_ = lv_tick_get(); /* the 20 s line waits again */
            if (upgrade_phase_ != apt_before || moved || level_ == Level::AptProgress) changed = true;
            // one line per helper phase as this launcher saw it (the start: systemd-run to the first status)
            const std::string phase_now = upgrade_phase_ == launcher_apt::UpgradePhase::Running ? upgrade_.phase
                                          : apt_phase_seen_.empty()                            ? std::string()
                                                                                               : std::string("end");
            if (!apt_phase_seen_.empty() && phase_now != apt_phase_seen_ &&
                (upgrade_phase_ == launcher_apt::UpgradePhase::Running || upgrade_.started + 2 >= upgrade_since_)) {
                SLOGI("[UPDATE] phase helper.%s took %u ms", apt_phase_seen_.c_str(), lv_tick_elaps(apt_phase_tick_));
                apt_phase_tick_ = lv_tick_get();
                apt_phase_seen_ = phase_now == "end" ? std::string() : phase_now;
            }
        }
        if (upgrade_waiting_ && (upgrade_phase_ == launcher_apt::UpgradePhase::Running ||
                                 upgrade_.started >= upgrade_since_ - 2 || platform::now() - upgrade_since_ >= 15))
            upgrade_waiting_ = false; /* its run is in the status file (or it never wrote one) */
        if (apt_started && (level_ == Level::Confirm || level_ == Level::System)) {
            set_level(Level::AptProgress);
            return;
        }
        if (level_ == Level::AptProgress && !apt_running()) {
            set_level(Level::AptResult); /* done, failed, refused, cancelled: the result page */
            return;
        }
        if (upgrade_phase_ == launcher_apt::UpgradePhase::Running && apt_before != launcher_apt::UpgradePhase::Running &&
            (level_ == Level::System || level_ == Level::Confirm)) {
            set_level(Level::AptProgress); /* a run started elsewhere (another launcher before a restart) */
            return;
        }
        if (replanned && level_ == Level::Confirm && list_) build_confirm(list_->selected()); /* the text follows the plan */
        // P4: the plan the user waited for is ready: on to the confirmation (or why not)
        if (wait_plan_ && planned() && level_ == Level::System) {
            wait_plan_ = false;
            open_upgrade();
            return;
        }
        // P4: a scan cancelled elsewhere (the page left and came back) while Packages shows: back to Updates
        if (scan_.cancelled != cancelled_seen_) {
            cancelled_seen_ = scan_.cancelled;
            changed = true;
        }
        // the visible waits: the box (elapsed, the bar), the status lines' elapsed counters (redrawn when the second changes)
        if (wait_box_ && (wait_box_->visible() || box_wanted())) update_wait_box();
        if (timed_wait() || level_ == Level::AptProgress) {
            show_caption();
            if (bar_value() == kSweep) update_bar();
            if (level_ == Level::AptProgress) changed = true; /* the reassurance row after 20 s */
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
    launcher_apt::UpgradeStatus upgrade_;                               /* P4: the helper's apt-upgrade-status */
    launcher_apt::UpgradePhase upgrade_phase_ = launcher_apt::UpgradePhase::None;
    bool upgrade_waiting_ = false;                                      /* systemd-run returned, no status of that run yet */
    long long upgrade_since_ = 0;
    bool cancel_asked_ = false;                                         /* the cancel flag was written for this run */
    bool libraries_on_ = false;                                         /* the confirmation page's switch (D14) */
    std::intptr_t asked_ = 0;                                           /* the row whose root step asks now */
    bool asked_repair_ = false;                                         /* the run asked is a repair */
    int asked_count_ = 0;                                               /* the packages of the run asked */
    int apt_step_shown_ = -1;                                           /* the step the highlight was moved to */
    cp0_ui_list_layout_t layout_confirm_ {};                            /* the confirmation page's rows */
    bool list_confirm_ = false;                                         /* the list is the confirmation page's */
    std::unique_ptr<cp0::ui::TextBlock> confirm_text_;                  /* the confirmation page's text block */
    /* P4, the visible waits (settings_wait_model.hpp): the working box, the moments the waits began (lv_tick: what the
     * screen shows is the board's own time), the bar that never moves back */
    std::unique_ptr<settings_wait::WaitBox> wait_box_;
    settings_wait::Monotonic wait_bar_;
    bool slow_ = false;                 /* a 512 MB board: the reassurance at once */
    bool wait_plan_ = false;            /* Upgrade pressed before the plan was ready: the box until it is */
    std::intptr_t reselect_ = 0;        /* the row highlighted when the list replaces the box (0: the first row) */
    bool bar_range_ = false;            /* the page's bar shows the moving segment */
    uint32_t scan_tick_ = 0, stage_tick_ = 0, check_tick_ = 0, download_tick_ = 0, refresh_tick_ = 0;
    uint32_t apt_start_tick_ = 0, apt_change_tick_ = 0, apt_phase_tick_ = 0, helper_start_tick_ = 0;
    int stage_seen_ = -1, checks_seen_ = -1;
    std::uint64_t cancelled_seen_ = 0;
    std::string apt_phase_seen_;        /* the helper phase last logged ("start" before its first status) */
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
