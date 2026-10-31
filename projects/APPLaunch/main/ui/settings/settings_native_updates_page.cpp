/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates (see settings_native_updates_page.hpp).
 */

#include "settings_native_updates_page.hpp"

#include "settings_update_check.hpp"
#include "settings_update_model.hpp"
#include "settings_update_platform.hpp"

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
constexpr int kUpdateRgb = 0xF0B400;   /* the Apps page's tokens: amber "update offered" ... */
constexpr int kMutedRgb = 0x8A929B;    /* ... and its grey */

enum RowId : std::intptr_t { kLauncher = 1, kCheckNow, kLastChecked, kWhatsNew, kDevInfo, kCheckAgain };

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
        // the lists start under the page's status line (Page::set_caption draws it over the top of the body)
        const cp0_ui_roller_page_t &pl = page_.layout();
        const int line_h = lv_font_get_line_height(cp0_ui_font_px(pl.caption_px));
        const int top = pl.caption_y + line_h + cp0_ui_tok(CP0_TOK_SPACE_S);
        cp0_ui_list_layout_t layout;
        cp0_ui_list_layout(cp0_ui_metrics_get(), CP0_UI_LIST_COMFORTABLE, pl.roller_x, top, pl.roller_w,
                           std::max(1, pl.h - top), &layout);
        cp0::ui::CompactListEvents events;
        events.activate = [this](int, std::intptr_t id) { post([this, id] { activate(id); }); };
        events.back = host_back_;
        list_ = std::make_unique<cp0::ui::CompactList>(page_.root(), layout, std::move(events));
        focus_in(group_, list_->focus_object());
        timer_ = lv_timer_create(poll_cb, kPollMs, this);

        Checker::instance().start(); /* D3: a check each time the page opens */
        snap_ = Checker::instance().snapshot();
        seen();
        show(0);
    }

    ~UpdatesPanel() override
    {
        if (timer_) lv_timer_delete(timer_);
        timer_ = nullptr;
        lv_async_call_cancel(run_posted_cb, this);
        posted_.clear();
        text_.reset();
        list_.reset(); /* a check still running finishes on its own: the next page or the background reads it */
        page_.set_caption("", false);
    }

    bool back() override
    {
        switch (level_) {
        case Level::WhatsNew: set_level(Level::Launcher); return true;
        case Level::Launcher: set_level(Level::Root); return true;
        case Level::Root: break;
        }
        return false;
    }

private:
    enum class Level { Root, Launcher, WhatsNew };

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

    std::vector<NotesSource> sources() const
    {
        return notes_sources(snap_.releases, snap_.offer, snap_.state, snap_.build,
                             platform::shipped_notes(snap_.build.version));
    }

    std::vector<ListRow> rows() const
    {
        std::vector<ListRow> rows;
        const RowAffordance check = snap_.running ? RowAffordance::ActionRunning : RowAffordance::Action;
        if (level_ == Level::Root) {
            const RowValue value = root_value(snap_.state, snap_.build);
            // "Launcher" alone: with its version ("Launcher 0.8.12", the study's mockup) the label is cut beside
            // "0.8.13 available" up to 640 px; the running version is in the Launcher level's status line
            rows.push_back(ListRow::item("Launcher", value.text, tone_of(value.tone), kLauncher, RowAffordance::Open));
            rows.push_back(ListRow::item("Check now", "", ValueTone::Normal, kCheckNow, check));
            rows.push_back(ListRow::item("Last checked", last_checked_text(snap_.state.last_check, platform::now()),
                                         ValueTone::Normal, kLastChecked, RowAffordance::Info));
        } else {
            const std::vector<NotesSource> notes = sources();
            rows.push_back(ListRow::item(notes.empty() ? std::string("What's new")
                                                       : "What's new in " + notes.front().version,
                                         "", ValueTone::Normal, kWhatsNew, RowAffordance::Open));
            if (development())
                rows.push_back(ListRow::item("Development build", "install by hand", ValueTone::Normal, kDevInfo,
                                             RowAffordance::Info)); /* D4: why there is no Install */
            rows.push_back(ListRow::item("Check again", "", ValueTone::Normal, kCheckAgain, check));
        }
        return rows;
    }

    /* The status line: a check on its way, why the last one failed, a release without this board's bundle; on the
     * Launcher level otherwise the versions, the channel and the architecture. */
    void show_caption()
    {
        std::string text;
        cp0::ui::CaptionTone tone = cp0::ui::CaptionTone::Normal;
        if (level_ == Level::WhatsNew) {
            text.clear();
        } else if (snap_.running) {
            text = "Checking for updates...";
        } else if (snap_.done > 0 && failed(snap_.outcome)) {
            text = outcome_text(snap_.outcome, snap_.http_status, clock_text(snap_.state.rate_reset));
            tone = cp0::ui::CaptionTone::Warn;
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
    }

    void show_title()
    {
        std::vector<std::string> title = title_;
        if (level_ != Level::Root) title.push_back("Launcher");
        if (level_ == Level::WhatsNew) title.push_back("What's new");
        page_.set_title(title);
    }

    /* selected >= 0: new content (a level), that row highlighted; -1: the live refresh (texts in place). */
    void show(int selected)
    {
        if (level_ != Level::WhatsNew && list_) {
            const std::vector<ListRow> next = rows();
            if (selected >= 0 || static_cast<int>(next.size()) != list_->count()) {
                list_->reset_rows(next, std::max(0, selected));
            } else {
                list_->set_rows(next);
            }
        }
        show_title();
        show_caption();
    }

    void set_level(Level level)
    {
        level_ = level;
        if (level == Level::WhatsNew) {
            unfocus(list_->focus_object());
            list_->set_hidden(true);
            open_text();
            show(-1);
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
    }

    void open_text()
    {
        const cp0_ui_roller_page_t &layout = page_.layout();
        const int px = cp0_ui_metrics_get()->text_px[CP0_TEXT_BODY];
        const lv_font_t *font = settings_fonts::cjk_sans(static_cast<uint16_t>(px));
        if (!font) font = cp0_ui_font(CP0_TEXT_BODY);
        const MeasureText measure = [font](const std::string &text) {
            lv_point_t size;
            lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            return static_cast<int>(size.x);
        };
        const int width = layout.roller_w - 2 * layout.inset - 2; /* the TextBlock's label: its box less the insets */
        const std::string text = blocks_text(whats_new_blocks(sources(), snap_.build.version), measure, width);
        text_ = std::make_unique<cp0::ui::TextBlock>(page_.root(), layout, text, font, host_back_);
        focus_in(group_, text_->focus_object());
    }

    // ---------------------------------------------------------------- rows and the check

    /* A release this page shows needs no background toast any more (one notice per version, D3). */
    void seen()
    {
        if (snap_.state.available.empty() || compare_versions(snap_.state.available, snap_.state.notified) <= 0) return;
        Checker::instance().update_state([](State &state) { take_available_toast(state); });
        snap_ = Checker::instance().snapshot();
    }

    void activate(std::intptr_t id)
    {
        switch (id) {
        case kLauncher: set_level(Level::Launcher); break;
        case kWhatsNew: set_level(Level::WhatsNew); break;
        case kCheckNow:
        case kCheckAgain:
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
        const bool changed = snap.running != snap_.running || snap.done != snap_.done;
        if (changed) {
            snap_ = std::move(snap);
            seen();
        }
        if (++polls_ >= kRefreshPolls) {
            polls_ = 0;
            force = true;
        }
        if (changed || force) show(-1);
    }

    cp0::ui::Page &page_;
    lv_group_t *group_;
    std::vector<std::string> title_;
    std::function<void()> host_back_;

    Level level_ = Level::Root;
    Snapshot snap_;
    std::unique_ptr<cp0::ui::CompactList> list_;
    std::unique_ptr<cp0::ui::TextBlock> text_;
    std::string caption_;
    cp0::ui::CaptionTone caption_tone_ = cp0::ui::CaptionTone::Normal;
    lv_timer_t *timer_ = nullptr;
    int polls_ = 0;
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
