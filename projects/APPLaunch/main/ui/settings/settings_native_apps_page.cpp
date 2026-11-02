/*
 * SPDX-License-Identifier: MIT
 *
 * The native Settings > Apps page (see settings_native_apps_page.hpp).
 *
 * The jobs are the legacy page's (settings_apps_jobs: detached workers, the sudo password through the shared prompt);
 * the page reads what changed every 200 ms as the legacy page did, gives it to the model (settings_apps_model) and
 * draws the model's rows. The letter shortcuts come from the raw keyboard event on the page's screen, as on the legacy
 * page: a letter's LVGL key is its evdev code, and W / E / R / T share theirs with the arrows. On the lists, Enter is
 * taken before the roller (the legacy direct action); the roller's own activation is then a tap (an action list).
 */

#include "settings_native_apps_page.hpp"

#include <atomic>

#include "settings_apps_jobs.hpp"
#include "settings_apps_model.hpp"

#include "cp0_font_service.hpp"
#include "cp0_ui_list.h"
#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_widgets.hpp"
#include "input_keys.h"
#include "keyboard_input.h"
#include "settings_fonts.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using settings_apps::Action;
using settings_apps::ActionKind;
using settings_apps::Confirm;
using settings_apps::Row;
using settings_apps::RowType;
using settings_apps::Level;
using settings_apps::Tone;

constexpr uint32_t kPollMs = 200;            /* the legacy page's poll of the workers */
constexpr uint32_t kRawLetterMs = 300;       /* a W / E / R / T press just seen: its arrow code is the letter */
constexpr std::size_t kMaxSourceBytes = 200; /* the legacy entry's limit */

/* the legacy page's value colours (green: the roller's own) */
constexpr int kUpdateRgb = 0xF0B400;
constexpr int kBadRgb = 0xFF6B6B;
constexpr int kMutedRgb = 0x8A929B;

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

std::string key_of(const Row &row)
{
    return std::to_string(static_cast<int>(row.type)) + ":" + row.key;
}

int rgb_of(Tone tone)
{
    switch (tone) {
    case Tone::Update: return kUpdateRgb;
    case Tone::Bad: return kBadRgb;
    case Tone::Muted: return kMutedRgb;
    case Tone::Good:
    case Tone::Normal: break;
    }
    return -1;
}

cp0::ui::RowSpec spec_of(const Row &row)
{
    cp0::ui::RowSpec spec;
    spec.label = row.label;
    if (row.busy) {
        spec.kind = cp0::ui::RowKind::Busy;
    } else {
        spec.value = row.value;
        spec.value_rgb = rgb_of(row.tone);
        spec.chevron = row.type == RowType::Description; /* informational: opens the read-only text page */
    }
    return spec;
}

bool same_spec(const cp0::ui::RowSpec &a, const cp0::ui::RowSpec &b)
{
    return a.kind == b.kind && a.label == b.label && a.value == b.value && a.value_rgb == b.value_rgb &&
           a.chevron == b.chevron;
}

cp0::ui::CaptionTone tone_of(settings_apps::Severity severity)
{
    switch (severity) {
    case settings_apps::Severity::Warn: return cp0::ui::CaptionTone::Warn;
    case settings_apps::Severity::Error: return cp0::ui::CaptionTone::Error;
    case settings_apps::Severity::Normal: break;
    }
    return cp0::ui::CaptionTone::Normal;
}

std::atomic<bool> g_open_at_installed{false};

class AppsPanel : public SettingsNativePanel {
public:
    explicit AppsPanel(const SettingsNativePanelContext &context)
        : page_(context.page), group_(context.group), title_(context.title), host_back_(context.back)
    {
        jobs_ = std::make_unique<settings_apps::Jobs>();
        model_.set_backend_missing(!settings_apps::Jobs::available());
        page_.set_caption_wrap(true); /* a source's failure reason or a step with its counters: never cut */

        cp0::ui::RollerEvents events;
        events.activate = [this](int row) { activate(row); };
        events.back = host_back_;
        roller_ = std::make_unique<cp0::ui::Roller>(page_.root(), page_.layout(), std::move(events));
        // Enter, Left / Right (the levels), Del, and the arrow codes of W / E / R / T: before the roller reads them
        lv_obj_add_event_cb(roller_->focus_object(), key_cb,
                            static_cast<lv_event_code_t>(LV_EVENT_KEY | LV_EVENT_PREPROCESS), this);
        focus_in(group_, roller_->focus_object());
        // U, S, A, D: the raw keyboard event, which the page's screen receives
        keyboard_root_ = lv_obj_get_screen(page_.root());
        if (keyboard_root_ && LV_EVENT_KEYBOARD != 0)
            keyboard_dsc_ = lv_obj_add_event_cb(keyboard_root_, keyboard_cb,
                                                static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), this);
        timer_ = lv_timer_create(poll_cb, kPollMs, this);

        if (!model_.backend_missing()) jobs_->load();
        rebuild(0);
        if (g_open_at_installed.exchange(false)) set_level(Level::Installed);
        poll();
    }

    ~AppsPanel() override
    {
        if (keyboard_root_ && keyboard_dsc_) lv_obj_remove_event_dsc(keyboard_root_, keyboard_dsc_);
        keyboard_dsc_ = nullptr;
        if (timer_) lv_timer_delete(timer_);
        timer_ = nullptr;
        lv_async_call_cancel(run_posted_cb, this);
        posted_.clear();
        field_.reset(); /* it stops editing: input context, intercept and the keyboard restored */
        dialog_.reset();
        text_.reset();
        roller_.reset();
        jobs_.reset(); /* a job still running finishes on its own: nothing of it reaches the page any more */
        page_.set_caption("", false);
        page_.set_caption_wrap(false); /* the host's other pages keep their one line */
    }

    bool back() override
    {
        if (dialog_) {
            close_dialog();
            return true;
        }
        if (text_) {
            close_description();
            return true;
        }
        if (model_.in_actions()) {
            close_actions();
            return true;
        }
        if (model_.level() != Level::Root) {
            set_level(Level::Root);
            return true;
        }
        return false;
    }

private:
    enum class DialogKind { None, Remove, Failure, AddSource };

    // ---------------------------------------------------------------- posting (out of the widgets' own events)

    void post(std::function<void()> callback)
    {
        posted_.push_back(std::move(callback));
        if (!post_armed_) post_armed_ = lv_async_call(run_posted_cb, this) == LV_RESULT_OK;
    }

    static void run_posted_cb(void *data)
    {
        auto *self = static_cast<AppsPanel *>(data);
        if (!self) return;
        self->post_armed_ = false;
        std::vector<std::function<void()>> callbacks;
        callbacks.swap(self->posted_);
        for (auto &callback : callbacks) callback();
    }

    // ---------------------------------------------------------------- rows, levels and status line

    /* The model's rows; rows that kept their kind change in place (the highlight and the scroll stay), otherwise the
     * list is rebuilt with the highlight on the same row (selected >= 0: on that row instead). */
    /* The panel's current logical size (a PDA / portrait switch changes it at run time): the Available list follows. */
    bool sync_screen()
    {
        const cp0_ui_metrics_t *panel = cp0_ui_metrics_panel();
        return model_.set_screen(panel ? panel->w : 0, panel ? panel->h : 0);
    }

    void rebuild(int selected = -1)
    {
        if (!roller_) return;
        sync_screen();
        std::vector<Row> rows = model_.rows();
        std::vector<cp0::ui::RowSpec> specs;
        specs.reserve(rows.size());
        for (const Row &row : rows) {
            specs.push_back(spec_of(row));
            if (row.type == RowType::Description) specs.back().value = fit_description(row.label, row.value);
        }

        bool same = selected < 0 && rows.size() == rows_.size();
        for (std::size_t i = 0; same && i < rows.size(); ++i)
            same = specs[i].kind == specs_[i].kind && key_of(rows[i]) == key_of(rows_[i]);
        if (same) {
            for (std::size_t i = 0; i < rows.size(); ++i)
                if (!same_spec(specs[i], specs_[i])) roller_->update_row(static_cast<int>(i), specs[i]);
        } else {
            int target = selected;
            if (target < 0) {
                const int current = roller_->selected();
                target = std::min(current, static_cast<int>(rows.size()) - 1);
                if (current >= 0 && current < static_cast<int>(rows_.size()))
                    for (std::size_t i = 0; i < rows.size(); ++i)
                        if (key_of(rows[i]) == key_of(rows_[static_cast<std::size_t>(current)]))
                            target = static_cast<int>(i);
            }
            roller_->set_rows(specs, std::max(0, target), true);
        }
        rows_ = std::move(rows);
        specs_ = std::move(specs);
    }

    /* The Description row's value: the first line cut with dots so that the highlighted row's bold label stays whole
     * (the roller gives a long value what the label leaves over, and cuts the label to a third when it does not
     * fit); nothing when not even a few characters fit. */
    std::string fit_description(const std::string &label, const std::string &line) const
    {
        const cp0_ui_roller_page_t &layout = page_.layout();
        const lv_font_t *bold = cp0_fonts().get("Montserrat-Bold.ttf", static_cast<uint16_t>(layout.text_px));
        const lv_font_t *plain = cp0_fonts().get("Montserrat-Medium.ttf", static_cast<uint16_t>(layout.value_px));
        if (!bold || !plain) return line;
        auto width = [](const std::string &text, const lv_font_t *font) {
            lv_point_t size;
            lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            return static_cast<int>(size.x);
        };
        cp0_ui_list_layout_t chevron{};
        chevron.text_px = layout.text_px;
        const int room = layout.roller_w - 2 * layout.inset - cp0_ui_list_chevron_w(&chevron) - 2 * layout.gap -
                         width(label, bold) - 4;
        std::size_t chars = 0;
        for (std::size_t i = 0; i < line.size(); ++i)
            if ((static_cast<unsigned char>(line[i]) & 0xC0) != 0x80) ++chars;
        for (; chars >= 4; --chars) {
            const std::string text = settings_apps::elide(line, chars);
            if (width(text, plain) <= room) return text;
        }
        return std::string();
    }

    void show_caption()
    {
        settings_apps::Severity severity = settings_apps::Severity::Normal;
        const std::string text = model_.caption(roller_ ? roller_->selected() : 0, severity);
        if (text == caption_ && severity == caption_severity_) return;
        caption_ = text;
        caption_severity_ = severity;
        page_.set_caption(text, tone_of(severity));
        if (dialog_) dialog_->raise();
    }

    /* "Settings > Apps", "> Installed" / "> Available" / "> Sources" on a level, "> <app or source>" on an action
     * list. */
    void show_title()
    {
        std::vector<std::string> title = title_;
        switch (model_.level()) {
        case Level::Installed: title.push_back("Installed"); break;
        case Level::Available: title.push_back("Available"); break;
        case Level::Sources: title.push_back("Sources"); break;
        case Level::Root: break;
        }
        if (model_.in_actions()) title.push_back(model_.actions_title());
        page_.set_title(title);
    }

    static RowType row_of(Level level)
    {
        switch (level) {
        case Level::Installed: return RowType::Installed;
        case Level::Available: return RowType::Available;
        case Level::Sources:
        case Level::Root: break;
        }
        return RowType::Sources;
    }

    void set_level(Level level)
    {
        if (dialog_) return;
        const Level from = model_.level();
        const bool listed = model_.in_actions();
        model_.set_level(level);
        show_title();
        if (level != from || listed) {
            // a level opens on its first row; back on the root the highlight is on the row it came from
            rebuild(level == Level::Root && from != Level::Root ? std::max(0, model_.find_row(row_of(from))) : 0);
        }
        show_caption();
    }

    /* Row `index` (an app or a source): its action list, a level of its own. */
    void open_actions(int index)
    {
        if (dialog_ || !model_.open_actions(index)) return;
        show_title();
        rebuild(model_.first_actionable_row()); /* not on a read-only fact */
        show_caption();
    }

    /* Back to the list, the highlight on the app or the source the list was for. */
    void close_actions()
    {
        if (!model_.in_actions()) return;
        close_description();
        const int row = model_.actions_row();
        model_.close_actions();
        show_title();
        rebuild(std::max(0, row));
        show_caption();
    }

    // ---------------------------------------------------------------- the workers

    static void poll_cb(lv_timer_t *timer)
    {
        auto *self = static_cast<AppsPanel *>(lv_timer_get_user_data(timer));
        if (self) self->poll();
    }

    void poll()
    {
        settings_apps::JobsUpdate update = jobs_->take();
        bool changed = sync_screen();
        if (update.lists) {
            model_.set_lists(std::move(update.apps), std::move(update.sources));
            changed = true;
        }
        if (update.sync) {
            model_.set_sync(update.progress);
            changed = true;
        }
        if (update.status) model_.set_status(update.status_text, update.status_error, update.status_warn);
        if (update.busy != model_.busy() || update.package_id != model_.package_id()) {
            model_.set_busy(update.busy, update.package_id);
            changed = true;
        }
        // the app or the source of the open action list went away (removed): back to the list
        if (model_.in_actions() && !model_.actions_valid() && !dialog_) close_actions();
        else if (changed) rebuild();
        if (update.failure) open_failure(update.failure_title, update.failure_detail);
        show_caption(); /* also follows the highlight (a failed source says why) */
        if (update.sudo) jobs_->start_sudo();
    }

    /* Runs what the model asked for; index: the row it came from. */
    void run(const Action &action, int index)
    {
        switch (action.kind) {
        case ActionKind::None:
            return;
        case ActionKind::Say:
            model_.set_status(action.message);
            break;
        case ActionKind::ShowLevel:
            set_level(action.level);
            return;
        case ActionKind::AddSource:
            open_add_source();
            return;
        case ActionKind::OpenActions:
            open_actions(index);
            return;
        case ActionKind::CloseActions:
            close_actions();
            return;
        case ActionKind::ShowDescription:
            open_description();
            return;
        case ActionKind::AskRemove:
            if (model_.ask_remove(index)) open_remove();
            return;
        case ActionKind::Install:
            jobs_->package("install", action.id, action.name, action.size);
            break;
        case ActionKind::Upgrade:
            jobs_->package("upgrade", action.id, action.name, action.size);
            break;
        case ActionKind::Uninstall:
            jobs_->package("uninstall", action.id, action.name, action.size);
            break;
        case ActionKind::UpdateAll:
            jobs_->update_all(action.items, action.size);
            break;
        case ActionKind::Sync:
            jobs_->sync(action.id);
            break;
        case ActionKind::ToggleSource:
            jobs_->toggle_source(action.id, action.enable);
            break;
        case ActionKind::RemoveSource:
            close_actions();
            if (jobs_->remove_source(action.id)) rebuild(0); /* the legacy page goes back to the first row */
            break;
        }
        close_actions(); /* a job started from an action list: back to the list, where its row shows it */
        poll();          /* the job's first state at once: its row, its first message */
    }

    /* The app's description as plain text in the read-only TextBlock (the Updates page's What's new), over the list;
     * the title stays the app's (the action list's). Esc / Left go back to the action list. */
    void open_description()
    {
        const std::string body = model_.actions_description();
        if (text_ || body.empty() || !roller_) return;
        const int px = cp0_ui_metrics_get()->text_px[CP0_TEXT_BODY];
        const lv_font_t *font = settings_fonts::cjk_sans(static_cast<uint16_t>(px));
        if (!font) font = cp0_ui_font(CP0_TEXT_BODY);
        unfocus(roller_->focus_object());
        roller_->set_hidden(true);
        text_ = std::make_unique<cp0::ui::TextBlock>(page_.root(), page_.layout(), body, font, host_back_);
        focus_in(group_, text_->focus_object());
    }

    void close_description()
    {
        if (!text_) return;
        unfocus(text_->focus_object());
        text_.reset();
        if (!roller_) return;
        roller_->set_hidden(false);
        if (!dialog_) focus_in(group_, roller_->focus_object());
    }

    /* The roller's activation. On the lists Enter never gets here (key_cb): this is a tap on the highlighted row. */
    void activate(int index)
    {
        if (dialog_) return;
        run(model_.in_actions() ? model_.activate(index) : model_.tap(index), index);
    }

    void ask_remove()
    {
        if (dialog_ || !roller_ || model_.in_actions()) return;
        if (model_.ask_remove(roller_->selected())) open_remove();
    }

    // ---------------------------------------------------------------- keys

    static void key_cb(lv_event_t *event)
    {
        auto *self = static_cast<AppsPanel *>(lv_event_get_user_data(event));
        if (self && self->on_key(lv_event_get_key(event))) lv_event_stop_processing(event);
    }

    /* LV_EVENT_KEY on the roller, before it: true when the page took the key. */
    bool on_key(uint32_t key)
    {
        // W, E, R, T arrive as 17..20 = LV_KEY_UP/DOWN/RIGHT/LEFT: a real letter press (seen just before) is not an
        // arrow. The window is not cleared on use: a held letter repeats.
        if (key >= LV_KEY_UP && key <= LV_KEY_LEFT && raw_letter_ && lv_tick_elaps(raw_letter_tick_) < kRawLetterMs)
            return true;
        if (model_.in_actions()) return false; /* an action list: the roller's keys (Enter, Right, Left / Esc back) */
        switch (key) {
        case LV_KEY_ENTER: /* a level, an installed app's list, an install, a source on / off (legacy), ... */
            if (roller_ && !dialog_) run(model_.activate(roller_->selected()), roller_->selected());
            return true;
        case LV_KEY_RIGHT: /* the root: opens the highlighted level; elsewhere nothing (never an install or a toggle) */
            if (roller_ && !dialog_ && model_.level() == Level::Root)
                run(model_.activate(roller_->selected()), roller_->selected());
            return true;
        case LV_KEY_LEFT: /* a level: back to the root; the root: the roller's back */
            if (model_.level() == Level::Root) return false;
            set_level(Level::Root);
            return true;
        case LV_KEY_DEL:
            ask_remove();
            return true;
        default:
            return false;
        }
    }

    static void keyboard_cb(lv_event_t *event)
    {
        auto *self = static_cast<AppsPanel *>(lv_event_get_user_data(event));
        auto *item = static_cast<const key_item *>(lv_event_get_param(event));
        if (self && item) self->shortcut(*item);
    }

    /* U, S, A, D by their physical key code, on the lists. */
    void shortcut(const key_item &item)
    {
        if (item.key_code == KEY_W || item.key_code == KEY_E || item.key_code == KEY_R || item.key_code == KEY_T) {
            raw_letter_ = true; /* on_key must not take the matching LVGL key for an arrow */
            raw_letter_tick_ = lv_tick_get();
            return;
        }
        // a dialog is up (its field types these letters), an action list shows or a modifier is held: not a shortcut
        if (item.key_state != KBD_KEY_PRESSED || dialog_ || model_.in_actions() ||
            (item.mods & (KBD_MOD_CTRL | KBD_MOD_ALT)))
            return;
        const int index = roller_ ? roller_->selected() : 0;
        if (item.key_code == KEY_U && model_.level() == Level::Installed) {
            run(model_.update_all(), index);
        } else if (item.key_code == KEY_S) {
            run(model_.sync(), index);
        } else if (item.key_code == KEY_A) {
            run(model_.add_source(), index);
        } else if (item.key_code == KEY_D) {
            ask_remove();
        }
    }

    // ---------------------------------------------------------------- dialogs

    void open_dialog(DialogKind kind, cp0::ui::DialogSpec spec)
    {
        close_dialog();
        dialog_kind_ = kind;
        spec.page = &page_; /* small screens with the keyboard up: the title in the header (cp0_ui_dialog.h) */
        dialog_ = std::make_unique<cp0::ui::Dialog>(page_.root(), page_.layout(), std::move(spec));
    }

    void enter_dialog()
    {
        dialog_->relayout();
        unfocus(roller_->focus_object()); /* Up / Down / Tab stay in the dialog */
        if (field_) focus_in(group_, field_->focus_object()); /* it edits: the keyboard comes */
        else focus_in(group_, dialog_->focus_object());       /* Enter / Esc: its buttons */
    }

    void close_dialog()
    {
        if (!dialog_) return;
        focus_in(group_, text_ ? text_->focus_object() : roller_->focus_object());
        field_.reset(); /* it stops editing: input context, intercept and the keyboard restored */
        dialog_.reset();
        dialog_kind_ = DialogKind::None;
        model_.cancel();
        caption_.clear(); /* the status line shows again */
        show_caption();
    }

    void open_remove()
    {
        const bool app = model_.pending() == Confirm::RemoveApp;
        cp0::ui::DialogSpec spec;
        spec.title = app ? "Remove this app?" : "Remove this source?";
        spec.buttons = {{"Cancel", cp0::ui::ButtonRole::Cancel, [this] { post([this] { close_dialog(); }); }},
                        {"Remove", cp0::ui::ButtonRole::Primary, [this] { post([this] { confirm_remove(); }); }}};
        const std::string name = model_.pending_name();
        open_dialog(DialogKind::Remove, std::move(spec));
        dialog_->add_text(name);
        enter_dialog();
    }

    void confirm_remove()
    {
        const Action action = model_.confirm();
        close_dialog();
        run(action, -1);
    }

    /* A failed install / update / remove: what went wrong, in words (the legacy page's message panel). */
    void open_failure(const std::string &title, const std::string &detail)
    {
        cp0::ui::DialogSpec spec;
        spec.title = title;
        spec.buttons = {{"OK", cp0::ui::ButtonRole::Primary, [this] { post([this] { close_dialog(); }); }}};
        open_dialog(DialogKind::Failure, std::move(spec));
        if (!detail.empty()) dialog_->add_text(detail);
        enter_dialog();
    }

    void open_add_source()
    {
        cp0::ui::DialogSpec spec;
        spec.title = "Add a GitHub source";
        spec.message_line = true;
        spec.buttons = {{"Cancel", cp0::ui::ButtonRole::Cancel, [this] { post([this] { close_dialog(); }); }},
                        {"Add", cp0::ui::ButtonRole::Primary, [this] { post([this] { submit_add_source(); }); }}};
        open_dialog(DialogKind::AddSource, std::move(spec));
        cp0::ui::TextFieldSpec field;
        field.label = "owner/repo (or a registry.json address)";
        field.max_bytes = kMaxSourceBytes;
        cp0::ui::TextFieldEvents events;
        events.submit = [this](const std::string &) { post([this] { submit_add_source(); }); }; /* Enter: Add */
        events.escape = [this] { post([this] { close_dialog(); }); };                           /* Esc: Cancel */
        events.back = events.escape;
        field_ = std::make_unique<cp0::ui::TextField>(dialog_->body(), dialog_->field_layout(), field, std::move(events));
        enter_dialog();
    }

    void submit_add_source()
    {
        if (dialog_kind_ != DialogKind::AddSource || !field_) return;
        std::string url, name, error;
        if (!settings_apps::Model::parse_source(field_->text(), url, name, error)) {
            dialog_->set_message(error, true); /* the field keeps editing */
            return;
        }
        if (!jobs_->add_source(url, name)) {
            dialog_->set_message("Busy, try again", true);
            return;
        }
        close_dialog();
        poll(); /* "Adding owner/repo..." */
    }

    cp0::ui::Page &page_;
    lv_group_t *group_;
    std::vector<std::string> title_;
    std::function<void()> host_back_;

    settings_apps::Model model_;
    std::unique_ptr<settings_apps::Jobs> jobs_;
    std::unique_ptr<cp0::ui::Roller> roller_;
    std::unique_ptr<cp0::ui::Dialog> dialog_;
    std::unique_ptr<cp0::ui::TextField> field_;
    std::unique_ptr<cp0::ui::TextBlock> text_; /* an app's description page */
    DialogKind dialog_kind_ = DialogKind::None;
    std::vector<Row> rows_;
    std::vector<cp0::ui::RowSpec> specs_;
    std::string caption_;
    settings_apps::Severity caption_severity_ = settings_apps::Severity::Normal;

    lv_timer_t *timer_ = nullptr;
    lv_obj_t *keyboard_root_ = nullptr;
    lv_event_dsc_t *keyboard_dsc_ = nullptr;
    bool raw_letter_ = false; /* a W / E / R / T press was just seen */
    uint32_t raw_letter_tick_ = 0;
    std::vector<std::function<void()>> posted_;
    bool post_armed_ = false;
};

} // namespace

std::unique_ptr<SettingsNativePanel> settings_apps_panel_create(const SettingsNativePanelContext &context)
{
    return std::make_unique<AppsPanel>(context);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_apps_page_factory(lv_obj_t *, const NodeIter &,
                                                                         std::function<void()>)
{
    return nullptr;
}

void settings_apps_open_at_installed()
{
    g_open_at_installed = true;
}
