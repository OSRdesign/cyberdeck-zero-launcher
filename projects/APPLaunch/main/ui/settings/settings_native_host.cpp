/*
 * SPDX-License-Identifier: MIT
 *
 * The native Settings host (see settings_native_host.hpp).
 */

#include "settings_native_host.hpp"

#include "lvgl_components.hpp"
#include "settings_native_compat.hpp"
#include "settings_native_mode.hpp"
#include "settings_page.hpp"
#include "ui_app_page.hpp"

#include "../native_ui.hpp"

#include "cp0_ui_metrics_lvgl.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iterator>
#include <tuple>
#include <utility>

namespace {

/* "70%" -> 70 */
bool parse_percent(const std::string &text, int &value)
{
    if (text.size() < 2 || text.back() != '%') return false;
    int parsed = 0;
    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        parsed = parsed * 10 + (text[i] - '0');
        if (parsed > 1000) return false;
    }
    value = parsed;
    return true;
}

/* A value list made only of percentages (brightness levels) is shown as a slider: its options' percents, else none. */
std::vector<int> percent_options(const NodeIter &node)
{
    std::vector<int> percents;
    for (auto option = node.begin(); option != node.end(); ++option) {
        int value = 0;
        if (!parse_percent(option->label, value)) return {};
        percents.push_back(value);
    }
    if (percents.size() < 3) return {};
    return percents;
}

std::string option_label(const NodeIter &node, int index)
{
    int i = 0;
    for (auto option = node.begin(); option != node.end(); ++option, ++i)
        if (i == index) return option->label;
    return {};
}

int option_count(const NodeIter &node)
{
    int count = 0;
    for (auto option = node.begin(); option != node.end(); ++option) ++count;
    return count;
}

} // namespace

struct SettingsNativeHost::Row {
    NodeIter node;
    RowType type = RowType::Plain;
    std::unique_ptr<DComponens::LvglComponensBase> page; /* Choice / Slider: the value page, logic only */
    SettingsChoiceBinding *binding = nullptr;
    std::unique_ptr<Observer> observer;
    bool on = false;             /* Toggle */
    bool direct_pending = false; /* Toggle with a Direct status read: polled while its operation runs */
    std::vector<int> percents;   /* Slider: option -> percent */
    int slider_value = -1;       /* Slider: the value just set, shown until the binding reports (-1: none) */
};

class SettingsNativeHost::Observer : public SettingsChoiceObserver {
public:
    Observer(SettingsNativeHost *host, int row) : host_(host), row_(row) {}
    void choice_selected(int index) override { host_->choice_selected(row_, index); }
    void choice_status(const std::string &text, bool error) override { host_->choice_status(row_, text, error); }

private:
    SettingsNativeHost *host_;
    int row_;
};

/* The section's toggle status reads: the legacy component base's async tasks (a worker thread, the result on the
 * LVGL thread), cancelled and joined when the section goes away, before anything may change the tree. */
class SettingsNativeHost::StatusTasks : public DComponens::LvglComponensBase {
public:
    void create_ui(lv_obj_t *) override {}
    void LoadNextPage() override {}
    void LeaveNextPage() override {}
};

SettingsNativeHost::SettingsNativeHost(AppPageRoot &page, lv_obj_t *parent, Tree &tree, std::function<void()> go_home)
    : page_root_(page), tree_(tree), go_home_(std::move(go_home))
{
    cp0_ui_roller_page(cp0_ui_metrics_get(), native_ui::status_strip_left(), &layout_);
    page_ = std::make_unique<cp0::ui::Page>(
        parent, layout_, [](lv_obj_t *header, const char *text) { return native_ui::add_title(header, text); },
        [this] { back(); });
    binding_host_ = settings_binding_host_create(page_->root());

    cp0::ui::RollerEvents events;
    events.activate = [this](int index) { activate(index); };
    events.slide = [this](int index, int value) { slide(index, value); };
    events.back = [this] { back(); };
    roller_ = std::make_unique<cp0::ui::Roller>(page_->root(), layout_, std::move(events));
    if (lv_group_t *group = page.input_group()) {
        lv_group_add_obj(group, roller_->focus_object());
        lv_group_focus_obj(roller_->focus_object());
    }
    // The shared status strip goes in when the screen is shown: it reads the Wi-Fi / Bluetooth state at once only
    // on the active screen (native_ui.cpp), so it matches the home grid's from the first frame.
    if (lv_obj_t *screen = page.screen()) lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, this);

    compat_ = std::make_unique<SettingsCompatPath>(
        page, [this](std::function<void()> callback) { post(std::move(callback)); },
        [this] { build_rows(roller_->selected()); });

    path_.push_back(tree_.begin());
    build_rows(0);
}

SettingsNativeHost::~SettingsNativeHost()
{
    lv_async_call_cancel(run_posted_cb, this);
    posted_.clear();
    compat_.reset();
    release_rows();
    if (lv_obj_t *screen = page_root_.screen())
        lv_obj_remove_event_cb_with_user_data(screen, screen_loaded_cb, this);
    roller_.reset();
    page_.reset(); /* deletes the binding host too, after the value pages that lived on it */
}

void SettingsNativeHost::screen_loaded_cb(lv_event_t *event)
{
    auto *self = static_cast<SettingsNativeHost *>(lv_event_get_user_data(event));
    if (!self || self->strip_added_ || !self->page_) return;
    self->strip_added_ = true;
    native_ui::add_status_icons(self->page_->header());
}

void SettingsNativeHost::post(std::function<void()> callback)
{
    posted_.push_back(std::move(callback));
    if (!post_armed_) post_armed_ = lv_async_call(run_posted_cb, this) == LV_RESULT_OK;
}

void SettingsNativeHost::run_posted_cb(void *data)
{
    auto *self = static_cast<SettingsNativeHost *>(data);
    if (!self) return;
    self->post_armed_ = false;
    std::vector<std::function<void()>> callbacks;
    callbacks.swap(self->posted_);
    for (auto &callback : callbacks) callback();
}

// ------------------------------------------------------------------ levels

void SettingsNativeHost::release_rows()
{
    if (direct_poll_) {
        lv_timer_delete(direct_poll_);
        direct_poll_ = nullptr;
    }
    status_tasks_.reset();
    for (auto &row : rows_)
        if (row->binding) row->binding->choice_set_observer(nullptr);
    rows_.clear(); /* destroys the value pages (their own async tasks are cancelled and joined) */
    choice_row_ = -1;
    checked_ = -1;
    ++generation_;
}

void SettingsNativeHost::open_section(const NodeIter &node)
{
    const int selected = roller_->selected();
    release_rows(); /* nothing holds the old section's nodes while the tree is refreshed */
    values_.clear();
    path_selected_.push_back(selected);
    path_.push_back(node);
    settings_prepare_section(node);
    build_rows(0);
}

void SettingsNativeHost::build_rows(int selected)
{
    release_rows();
    const NodeIter section = path_.back();
    const bool dev = settings_ui::dev_pages();
    for (auto child = section.begin(); child != section.end(); ++child) {
        auto row = std::make_unique<Row>();
        row->node = child;
        switch (settings_node_kind(row->node)) {
        case SettingsNodeKind::Section: row->type = RowType::Section; break;
        case SettingsNodeKind::Choice: row->type = RowType::Choice; break;
        case SettingsNodeKind::Toggle: row->type = RowType::Toggle; break;
        case SettingsNodeKind::Action: row->type = RowType::Action; break;
        case SettingsNodeKind::Custom: row->type = RowType::Custom; break;
        case SettingsNodeKind::Plain: row->type = RowType::Plain; break;
        }
        if (row->type == RowType::Custom && !dev) continue; /* D3: a release build hides unmigrated pages */
        rows_.push_back(std::move(row));
    }
    status_tasks_ = std::make_unique<StatusTasks>();
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        if (rows_[static_cast<size_t>(i)]->type == RowType::Choice && !create_binding(*rows_[static_cast<size_t>(i)], i))
            rows_[static_cast<size_t>(i)]->type = RowType::Plain;
    show_section(selected);
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        if (rows_[static_cast<size_t>(i)]->type == RowType::Toggle) read_toggle(i);
}

/* The node's own value page, built on the binding host: its logic only. */
bool SettingsNativeHost::create_binding(Row &row, int index)
{
    const std::uint64_t generation = generation_;
    const SettingEntry *entry = &*row.node;
    row.page = row.node->page_factory(binding_host_, row.node, [this, generation, entry] {
        post([this, generation, entry] { binding_done(generation, entry); });
    });
    row.binding = dynamic_cast<SettingsChoiceBinding *>(row.page.get());
    if (!row.binding) {
        row.page.reset();
        return false;
    }
    row.observer = std::make_unique<Observer>(this, index);
    row.binding->choice_set_observer(row.observer.get());
    row.percents = percent_options(row.node);
    row.type = !row.percents.empty() && row.binding->choice_shows_value() ? RowType::Slider : RowType::Choice;
    if (row.binding->choice_ready()) values_[entry] = row.binding->choice_selection();
    return true;
}

std::vector<std::string> SettingsNativeHost::title_path() const
{
    std::vector<std::string> path;
    for (const NodeIter &node : path_) path.push_back(node->label);
    if (choice_row_ >= 0) path.push_back(rows_[static_cast<size_t>(choice_row_)]->node->label);
    return path;
}

int SettingsNativeHost::known_value(const Row &row) const
{
    if (!row.binding) return -1;
    if (row.binding->choice_ready()) return row.binding->choice_selection();
    const auto found = values_.find(&*row.node);
    return found != values_.end() ? found->second : -1;
}

cp0::ui::RowSpec SettingsNativeHost::row_spec(const Row &row) const
{
    cp0::ui::RowSpec spec;
    spec.label = row.node->label;
    switch (row.type) {
    case RowType::Choice: {
        const int value = known_value(row);
        if (value >= 0 && row.binding->choice_shows_value()) spec.value = option_label(row.node, value);
        break;
    }
    case RowType::Slider: {
        spec.kind = cp0::ui::RowKind::Slider;
        const auto bounds = std::minmax_element(row.percents.begin(), row.percents.end());
        spec.slider_min = *bounds.first;
        spec.slider_max = *bounds.second;
        spec.slider_step = std::max(1, std::abs(row.percents[0] - row.percents[1]));
        const int value = known_value(row);
        const int option = value >= 0 ? value : row.binding->choice_selection();
        spec.slider_value = row.slider_value >= 0 ? row.slider_value
                            : option >= 0 && option < static_cast<int>(row.percents.size())
                                ? row.percents[static_cast<size_t>(option)]
                                : spec.slider_min;
        if (value >= 0 || row.slider_value >= 0) spec.value = std::to_string(spec.slider_value) + "%";
        break;
    }
    case RowType::Toggle:
        spec.kind = cp0::ui::RowKind::Toggle;
        spec.on = row.on;
        break;
    default:
        break;
    }
    return spec;
}

cp0::ui::RowSpec SettingsNativeHost::option_spec(const Row &row, int option) const
{
    cp0::ui::RowSpec spec;
    spec.kind = cp0::ui::RowKind::Choice;
    spec.label = option_label(row.node, option);
    spec.on = option == checked_;
    return spec;
}

void SettingsNativeHost::show_section(int selected)
{
    choice_row_ = -1;
    checked_ = -1;
    std::vector<cp0::ui::RowSpec> specs;
    specs.reserve(rows_.size());
    for (const auto &row : rows_) specs.push_back(row_spec(*row));
    roller_->set_rows(specs, selected, true);
    page_->set_title(title_path());
    clear_caption();
}

void SettingsNativeHost::show_choice(int index)
{
    Row &row = *rows_[static_cast<size_t>(index)];
    choice_row_ = index;
    const int current = known_value(row);
    checked_ = row.binding->choice_shows_value() ? current : -1;
    std::vector<cp0::ui::RowSpec> specs;
    const int count = option_count(row.node);
    for (int option = 0; option < count; ++option) specs.push_back(option_spec(row, option));
    roller_->set_rows(specs, current >= 0 ? current : row.binding->choice_selection(), false);
    page_->set_title(title_path());
    clear_caption();
}

void SettingsNativeHost::back()
{
    if (compat_ && compat_->active()) return; /* the compat page handles its own Esc */
    if (choice_row_ >= 0) {
        show_section(choice_row_);
        return;
    }
    if (path_.size() <= 1) {
        if (go_home_) go_home_();
        return;
    }
    const NodeIter left = path_.back();
    release_rows();
    values_.clear();
    path_.pop_back();
    const int selected = path_selected_.empty() ? 0 : path_selected_.back();
    if (!path_selected_.empty()) path_selected_.pop_back();
    settings_leave_section(left);
    build_rows(selected);
}

// ------------------------------------------------------------------ activation

void SettingsNativeHost::activate(int index)
{
    if (choice_row_ >= 0) activate_choice(index);
    else activate_entry(index);
}

void SettingsNativeHost::activate_entry(int index)
{
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    Row &row = *rows_[static_cast<size_t>(index)];
    // an entry may refuse to open (Set Manually while Network Time is on): say why, as the legacy submenu does
    if (row.node->activation_gate) {
        if (const ActivationBlock *block = row.node->activation_gate()) {
            std::string text = block->title ? block->title : "Unavailable";
            if (block->message && block->message[0]) text += std::string(": ") + block->message;
            caption_row_ = -1;
            page_->set_caption(text, true);
            return;
        }
    }
    switch (row.type) {
    case RowType::Section: {
        const NodeIter node = row.node; /* the row goes away with its level */
        open_section(node);
        break;
    }
    case RowType::Choice:
        show_choice(index);
        break;
    case RowType::Toggle:
        toggle(index);
        break;
    case RowType::Action:
        try {
            if (row.node->Componens_api) row.node->Componens_api(SettingApiActivate, nullptr);
            else if (row.node->Async_api) row.node->Async_api(SettingApiActivate, nullptr);
        } catch (...) {
        }
        break;
    case RowType::Custom:
        open_custom(index);
        break;
    case RowType::Slider:
    case RowType::Plain:
        break;
    }
}

void SettingsNativeHost::activate_choice(int option)
{
    if (choice_row_ < 0) return;
    Row &row = *rows_[static_cast<size_t>(choice_row_)];
    if (!row.binding || option < 0 || option >= row.binding->choice_count()) return;
    // the value page applies it exactly as Enter on its legacy row; success arrives as its back callback
    row.binding->choice_activate(option);
}

void SettingsNativeHost::slide(int index, int value)
{
    if (choice_row_ >= 0 || index < 0 || index >= static_cast<int>(rows_.size())) return;
    Row &row = *rows_[static_cast<size_t>(index)];
    if (row.type != RowType::Slider || !row.binding || row.percents.empty()) return;
    std::size_t best = 0;
    for (std::size_t i = 1; i < row.percents.size(); ++i)
        if (std::abs(row.percents[i] - value) < std::abs(row.percents[best] - value)) best = i;
    row.slider_value = row.percents[best];
    roller_->update_row(index, row_spec(row));
    row.binding->choice_activate(static_cast<int>(best));
}

void SettingsNativeHost::open_custom(int index)
{
    if (!settings_ui::dev_pages() || !compat_) return;
    const NodeIter node = rows_[static_cast<size_t>(index)]->node;
    compat_->open(node);
}

/* A value page finished (applied, or nothing to change): it is single-use, so the section gets fresh ones that read
 * the new value; a level 3 list closes. */
void SettingsNativeHost::binding_done(std::uint64_t generation, const SettingEntry *entry)
{
    if (generation != generation_) return;
    int index = -1;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        if (&*rows_[static_cast<size_t>(i)]->node == entry) index = i;
    if (index < 0) return;
    if (choice_row_ >= 0 && choice_row_ != index) {
        // another row's list is open: renew only the finished row's value page
        Row &row = *rows_[static_cast<size_t>(index)];
        row.binding->choice_set_observer(nullptr);
        row.binding = nullptr;
        row.observer.reset();
        row.page.reset();
        row.slider_value = -1;
        if (!create_binding(row, index)) row.type = RowType::Plain;
        return;
    }
    build_rows(index); /* all of them: a Month changes the Day list, for one */
}

void SettingsNativeHost::choice_selected(int row_index, int index)
{
    if (row_index < 0 || row_index >= static_cast<int>(rows_.size())) return;
    Row &row = *rows_[static_cast<size_t>(row_index)];
    if (!row.binding) return;
    if (row.binding->choice_ready()) values_[&*row.node] = index;
    row.slider_value = -1;
    if (choice_row_ == row_index) {
        const int now = row.binding->choice_shows_value() ? known_value(row) : -1;
        if (now == checked_) return;
        const int previous = checked_;
        checked_ = now;
        if (previous >= 0) roller_->update_row(previous, option_spec(row, previous));
        if (now >= 0) roller_->update_row(now, option_spec(row, now));
    } else if (choice_row_ < 0) {
        roller_->update_row(row_index, row_spec(row));
    }
}

/* The status line shows the latest message; a row clears it only when the message is its own (several value pages
 * of a section load at once). */
void SettingsNativeHost::choice_status(int row_index, const std::string &text, bool error)
{
    if (choice_row_ >= 0 && choice_row_ != row_index) return;
    if (text.empty()) {
        if (caption_row_ != row_index) return;
        caption_row_ = -1;
    } else {
        caption_row_ = row_index;
    }
    page_->set_caption(text, error);
}

void SettingsNativeHost::clear_caption()
{
    caption_row_ = -1;
    page_->set_caption("", false);
}

// ------------------------------------------------------------------ toggles (the legacy submenu's protocol)

void SettingsNativeHost::toggle(int index)
{
    Row &row = *rows_[static_cast<size_t>(index)];
    try {
        // no legacy page to pass: an API that would draw its own warning on it gets nullptr and stays silent
        if (row.node->Componens_api) row.node->Componens_api(SettingApiActivate, nullptr);
        else if (row.node->Async_api) row.node->Async_api(SettingApiActivate, nullptr);
    } catch (...) {
    }
    ++row.node->status_generation;
    read_toggle(index);
}

void SettingsNativeHost::read_toggle(int index)
{
    Row &row = *rows_[static_cast<size_t>(index)];
    if (!row.node->Componens_api) return;
    const NodeIter node = row.node;
    if (node->status_read_policy == SettingStatusReadPolicy::Direct) {
        std::atomic_bool started{false};
        SettingApiReadFlagTimeStartData result = std::make_tuple(false, &started);
        try {
            node->Componens_api(SettingApiReadFlagTimeStart, &result);
        } catch (...) {
            return;
        }
        row.on = std::get<0>(result);
        row.direct_pending = started.load(std::memory_order_acquire);
        if (choice_row_ < 0) roller_->update_row(index, row_spec(row));
        if (row.direct_pending && !direct_poll_) direct_poll_ = lv_timer_create(direct_poll_cb, 100, this);
        return;
    }
    if (!status_tasks_) return;
    const std::uint64_t generation = generation_;
    const std::uint64_t status_generation = node->status_generation;
    DComponens::LvglComponensBase::AsyncTaskCallbacks<bool> callbacks;
    callbacks.execute = [node]() {
        std::atomic_bool started{false};
        SettingApiReadFlagTimeStartData result = std::make_tuple(false, &started);
        node->Componens_api(SettingApiReadFlagTimeStart, &result);
        return std::get<0>(result);
    };
    callbacks.on_complete = [this, generation, status_generation, index, node](
                                DComponens::LvglComponensBase::AsyncTaskContext &, const bool &on) {
        if (generation != generation_ || node->status_generation != status_generation) return;
        if (index >= static_cast<int>(rows_.size())) return;
        Row &current = *rows_[static_cast<size_t>(index)];
        current.on = on;
        if (choice_row_ < 0) roller_->update_row(index, row_spec(current));
    };
    status_tasks_->run_async_task(std::move(callbacks));
}

void SettingsNativeHost::direct_poll_cb(lv_timer_t *timer)
{
    auto *self = static_cast<SettingsNativeHost *>(lv_timer_get_user_data(timer));
    if (self) self->poll_direct_toggles();
}

void SettingsNativeHost::poll_direct_toggles()
{
    bool pending = false;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        Row &row = *rows_[static_cast<size_t>(i)];
        if (row.type != RowType::Toggle || !row.direct_pending) continue;
        read_toggle(i);
        pending = pending || row.direct_pending;
    }
    if (!pending && direct_poll_) {
        lv_timer_delete(direct_poll_);
        direct_poll_ = nullptr;
    }
}
