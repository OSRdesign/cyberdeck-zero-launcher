/*
 * SPDX-License-Identifier: MIT
 *
 * CompactList, the apps' list (see cp0_ui_widgets.hpp; maths and the key / tap / refresh rules in cp0_ui_list.c).
 *
 * Objects: list (the area; scrolls, takes the keys, draws the scrollbar) > one row object per row at its top from
 * cp0_ui_list_tops() (absolute positions, no flex layout: a refresh moves nothing that did not change), each with a
 * label and a value; the empty area (the whole list area, shown only with no row). The selected row draws the
 * roller's highlight bar as its own background and its label in bold. Row objects are reused across set_rows() calls:
 * a live refresh only sets the texts and colours that changed.
 */

#include "cp0_ui_widgets.hpp"

#include "cp0_font_service.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace cp0 {
namespace ui {
namespace {

const lv_font_t *list_font(int px, bool bold)
{
    return cp0_fonts().get(bold ? "Montserrat-Bold.ttf" : "Montserrat-Medium.ttf", static_cast<uint16_t>(px));
}

int text_width(const std::string &text, const lv_font_t *font)
{
    if (text.empty() || !font) return 0;
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

/* A section's title in upper case (ASCII letters; other bytes, UTF-8 included, as they are) */
std::string upper(const std::string &text)
{
    std::string out = text;
    for (char &c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

cp0_ui_list_kind_t kind_of(ListRowKind kind)
{
    return kind == ListRowKind::Section ? CP0_UI_LIST_SECTION : CP0_UI_LIST_ITEM;
}

cp0_ui_list_tone_t tone_of(ValueTone tone)
{
    switch (tone) {
    case ValueTone::Ok: return CP0_UI_LIST_TONE_OK;
    case ValueTone::Warn: return CP0_UI_LIST_TONE_WARN;
    case ValueTone::Bad: return CP0_UI_LIST_TONE_BAD;
    case ValueTone::Muted: return CP0_UI_LIST_TONE_MUTED;
    default: return CP0_UI_LIST_TONE_NORMAL;
    }
}

cp0_ui_list_key_t list_key(uint32_t key)
{
    switch (key) {
    case LV_KEY_UP: return CP0_UI_LIST_KEY_UP;
    case LV_KEY_DOWN: return CP0_UI_LIST_KEY_DOWN;
    case LV_KEY_LEFT: return CP0_UI_LIST_KEY_LEFT;
    case LV_KEY_RIGHT: return CP0_UI_LIST_KEY_RIGHT;
    case LV_KEY_ENTER: return CP0_UI_LIST_KEY_ENTER;
    case LV_KEY_ESC: return CP0_UI_LIST_KEY_ESC;
    case LV_KEY_HOME: return CP0_UI_LIST_KEY_HOME;
    case LV_KEY_END: return CP0_UI_LIST_KEY_END;
    case LV_KEY_PREV: return CP0_UI_LIST_KEY_PAGE_UP; /* reaches the list only when the indev has no group */
    case LV_KEY_NEXT: return CP0_UI_LIST_KEY_PAGE_DOWN;
    default: return CP0_UI_LIST_KEY_OTHER;
    }
}

const ListRow &empty_row()
{
    static const ListRow row;
    return row;
}

} // namespace

CompactList::CompactList(lv_obj_t *parent, const cp0_ui_list_layout_t &layout, CompactListEvents events,
                         CompactListOptions options)
    : layout_(layout), events_(std::move(events)), options_(std::move(options))
{
    list_ = lv_obj_create(parent);
    lv_obj_remove_style_all(list_);
    if (lv_obj_get_group(list_)) lv_group_remove_obj(list_); /* the page adds it to its own group */
    lv_obj_set_pos(list_, layout_.x, layout_.y);
    lv_obj_set_size(list_, layout_.w, layout_.h);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_remove_flag(list_, LV_OBJ_FLAG_SCROLL_WITH_ARROW);
    lv_obj_remove_flag(list_, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    // the scrollbar: thin, at the right edge (inside the values' inset), shown while scrolling (show_scrollbar)
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(list_, layout_.scrollbar_w, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(list_, layout_.scrollbar_pad, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(list_, layout_.scrollbar_pad, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_bottom(list_, layout_.scrollbar_pad, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list_, lv_color_hex(CP0_UI_LIST_SCROLLBAR_RGB), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list_, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list_, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(list_, list_event_cb, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(list_, list_delete_cb, LV_EVENT_DELETE, this);

    empty_ = lv_obj_create(list_);
    lv_obj_remove_style_all(empty_);
    lv_obj_remove_flag(empty_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(empty_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(empty_, 0, 0);
    lv_obj_set_size(empty_, layout_.w, layout_.h);
    empty_label_ = lv_label_create(empty_);
    lv_label_set_long_mode(empty_label_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_color(empty_label_, lv_color_hex(CP0_UI_LIST_SECTION_RGB), 0);
    if (const lv_font_t *font = list_font(layout_.text_px, false)) {
        lv_obj_set_style_text_font(empty_label_, font, 0);
        lv_obj_set_height(empty_label_, lv_font_get_line_height(font));
        lv_obj_set_pos(empty_label_, layout_.inset, std::max(0, (layout_.row_h - lv_font_get_line_height(font)) / 2));
    }
    lv_obj_set_width(empty_label_, std::max(0, layout_.w - 2 * layout_.inset));
    lv_label_set_text(empty_label_, options_.empty_text.c_str());

    hide_timer_ = lv_timer_create(hide_timer_cb, CP0_UI_LIST_SCROLLBAR_HIDE_MS, this);
    lv_timer_pause(hide_timer_);
    tops_.assign(1, 0);
    update_empty();
}

CompactList::~CompactList()
{
    if (hide_timer_) lv_timer_delete(hide_timer_);
    hide_timer_ = nullptr;
    if (list_) {
        lv_obj_remove_event_cb_with_user_data(list_, list_delete_cb, this);
        lv_obj_remove_event_cb_with_user_data(list_, list_event_cb, this);
        lv_obj_delete(list_);
    }
}

void CompactList::list_delete_cb(lv_event_t *event)
{
    auto *self = static_cast<CompactList *>(lv_event_get_user_data(event));
    if (!self) return;
    if (self->hide_timer_) lv_timer_delete(self->hide_timer_);
    self->hide_timer_ = nullptr;
    self->list_ = self->empty_ = self->empty_label_ = nullptr;
    self->rows_.clear();
}

// ------------------------------------------------------------------ rows

void CompactList::build_row(int index)
{
    Row &row = rows_[static_cast<size_t>(index)];
    row.obj = lv_obj_create(list_);
    lv_obj_remove_style_all(row.obj);
    lv_obj_remove_flag(row.obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row.obj, layout_.w);
    lv_obj_set_style_bg_color(row.obj, lv_color_hex(CP0_UI_LIST_BAR_RGB), 0);
    lv_obj_set_style_bg_opa(row.obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(row.obj, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row.obj, lv_color_hex(CP0_UI_LIST_SEPARATOR_RGB), 0);
    lv_obj_set_style_border_opa(row.obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row.obj, 0, 0);
    lv_obj_set_user_data(row.obj, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
    lv_obj_add_event_cb(row.obj, row_event_cb, LV_EVENT_CLICKED, this);

    row.label = lv_label_create(row.obj);
    lv_label_set_long_mode(row.label, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(row.label, "");
    row.value = lv_label_create(row.obj);
    lv_label_set_long_mode(row.value, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(row.value, "");
    row.styled_selected = false;
    style_kind(row);
    fill_row(row, nullptr);
}

/* What depends on the row's kind: height, fonts, colours, whether a tap reaches it. */
void CompactList::style_kind(Row &row)
{
    const bool section = row.spec.kind == ListRowKind::Section;
    lv_obj_set_height(row.obj, section ? layout_.section_h : layout_.row_h);
    if (section || options_.read_only) lv_obj_remove_flag(row.obj, LV_OBJ_FLAG_CLICKABLE); /* drags reach the list */
    else lv_obj_add_flag(row.obj, LV_OBJ_FLAG_CLICKABLE);
    if (section && row.styled_selected) { /* an item that was selected became a section */
        row.styled_selected = false;
        lv_obj_set_style_bg_opa(row.obj, LV_OPA_TRANSP, 0);
    }
    lv_obj_set_style_text_font(row.label, label_font(row), 0);
    lv_obj_set_style_text_color(row.label, lv_color_hex(section ? CP0_UI_LIST_SECTION_RGB : CP0_UI_LIST_TEXT_RGB), 0);
    lv_obj_set_style_text_letter_space(row.label, section ? 1 : 0, 0);
    lv_obj_set_style_text_font(row.value, list_font(layout_.value_px, false), 0);
    if (section) lv_obj_add_flag(row.value, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(row.value, LV_OBJ_FLAG_HIDDEN);
}

/* Texts, the value's colour and the label / value widths; previous (NULL: everything) skips what did not change. */
void CompactList::fill_row(Row &row, const ListRow *previous)
{
    const bool section = row.spec.kind == ListRowKind::Section;
    const bool kind_same = previous && previous->kind == row.spec.kind;
    const bool label_same = kind_same && previous->label == row.spec.label;
    const bool value_same = kind_same && previous->value == row.spec.value;
    if (!label_same) lv_label_set_text(row.label, section ? upper(row.spec.label).c_str() : row.spec.label.c_str());
    if (section) {
        if (!value_same) lv_label_set_text(row.value, "");
    } else {
        if (!value_same) lv_label_set_text(row.value, row.spec.value.c_str());
        if (!kind_same || previous->tone != row.spec.tone)
            lv_obj_set_style_text_color(row.value, lv_color_hex(cp0_ui_list_tone_rgb(tone_of(row.spec.tone))), 0);
    }
    if (label_same && value_same) return;

    const int h = section ? layout_.section_h : layout_.row_h;
    const lv_font_t *font = lv_obj_get_style_text_font(row.label, LV_PART_MAIN);
    const int value_w = section ? 0 : text_width(row.spec.value, lv_obj_get_style_text_font(row.value, LV_PART_MAIN));
    const int room = std::max(0, layout_.w - 2 * layout_.inset);
    if (value_w > 0) {
        lv_obj_set_width(row.value, std::min(value_w, room) + 1); /* +1: a rounding pixel never brings the dots */
        lv_obj_align(row.value, LV_ALIGN_RIGHT_MID, -layout_.inset, 0);
    }
    lv_obj_set_width(row.label, cp0_ui_list_label_w(layout_.w, layout_.inset, layout_.gap, value_w));
    lv_obj_set_height(row.label, font ? lv_font_get_line_height(font) : h); /* one line: dots, never a wrap */
    lv_obj_align(row.label, LV_ALIGN_LEFT_MID, layout_.inset, 0);
}

/* The highlight bar and the bold label on the selected item; plain on the others. */
void CompactList::style_selected(int index)
{
    if (index < 0 || index >= count()) return;
    Row &row = rows_[static_cast<size_t>(index)];
    const bool on = index == selected_ && row.spec.kind == ListRowKind::Item && !options_.read_only;
    if (on == row.styled_selected) return;
    row.styled_selected = on;
    lv_obj_set_style_bg_opa(row.obj, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(row.label, label_font(row), 0);
}

const lv_font_t *CompactList::label_font(const Row &row) const
{
    if (row.spec.kind == ListRowKind::Section) return list_font(layout_.section_px, false);
    return list_font(layout_.text_px, row.styled_selected);
}

/* Row tops and heights from the C layout, separators between two items. */
void CompactList::relayout()
{
    const int n = count();
    kinds_.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) kinds_[static_cast<size_t>(i)] = kind_of(rows_[static_cast<size_t>(i)].spec.kind);
    tops_.resize(static_cast<size_t>(n) + 1);
    cp0_ui_list_tops(&layout_, kinds_.data(), n, tops_.data());
    for (int i = 0; i < n; ++i) {
        Row &row = rows_[static_cast<size_t>(i)];
        lv_obj_set_pos(row.obj, 0, tops_[static_cast<size_t>(i)]);
        const bool line = layout_.separator_px > 0 && kinds_[static_cast<size_t>(i)] == CP0_UI_LIST_ITEM && i + 1 < n &&
                          kinds_[static_cast<size_t>(i) + 1] == CP0_UI_LIST_ITEM;
        lv_obj_set_style_border_width(row.obj, line ? layout_.separator_px : 0, 0);
    }
}

void CompactList::update_empty()
{
    if (!empty_) return;
    if (rows_.empty()) lv_obj_remove_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
}

void CompactList::set_empty_text(const std::string &text)
{
    options_.empty_text = text;
    if (empty_label_) lv_label_set_text(empty_label_, text.c_str());
}

const ListRow &CompactList::row(int index) const
{
    if (index < 0 || index >= count()) return empty_row();
    return rows_[static_cast<size_t>(index)].spec;
}

void CompactList::set_rows(const std::vector<ListRow> &rows)
{
    if (!list_) return;
    const bool was_empty = rows_.empty();
    bool structural = rows.size() != rows_.size();
    rebuilding_ = true;
    while (rows_.size() > rows.size()) {
        lv_obj_delete(rows_.back().obj);
        rows_.pop_back();
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i >= rows_.size()) {
            rows_.push_back(Row{});
            rows_.back().spec = rows[i];
            build_row(static_cast<int>(i));
            continue;
        }
        Row &row = rows_[i];
        const ListRow previous = row.spec;
        row.spec = rows[i];
        if (previous.kind != row.spec.kind) {
            structural = true;
            style_kind(row);
        }
        fill_row(row, &previous);
    }
    if (structural) {
        relayout();
        lv_obj_update_layout(list_);
        // keep the selected index and the offset (clamped); never scroll to the selection
        cp0_ui_list_state_t s = state();
        s.scroll_y = lv_obj_get_scroll_y(list_);
        const cp0_ui_list_result_t keep = cp0_ui_list_keep(&s);
        const int previous = selected_;
        selected_ = keep.index;
        style_selected(previous);
        style_selected(selected_);
        const int max = cp0_ui_list_max_scroll(tops_.back(), layout_.h);
        if (lv_obj_get_scroll_y(list_) > max) {
            lv_anim_delete(list_, nullptr);
            busy_ = false;
            lv_obj_scroll_to_y(list_, max, LV_ANIM_OFF);
        }
    }
    update_empty();
    rebuilding_ = false;
    if (was_empty && !rows_.empty() && cp0_ui_list_max_scroll(tops_.back(), layout_.h) > 0) show_scrollbar();
}

void CompactList::reset_rows(const std::vector<ListRow> &rows, int selected)
{
    if (!list_) return;
    lv_anim_delete(list_, nullptr);
    busy_ = false;
    rebuilding_ = true;
    lv_obj_scroll_to_y(list_, 0, LV_ANIM_OFF);
    rebuilding_ = false;
    set_rows(rows);
    if (rows_.empty()) return;
    // the requested item (a section: the next item), shown with one row of context from the top
    const int target = cp0_ui_list_step(kinds_.data(), count(), std::clamp(selected, 0, count() - 1), 0, 0);
    cp0_ui_list_result_t r{CP0_UI_LIST_MOVE, options_.read_only ? -1 : target, 0};
    if (target >= 0) r.scroll_y = cp0_ui_list_scroll_to_show(tops_.data(), count(), target, 0, layout_.h, 1);
    rebuilding_ = true;
    apply(r, false, false);
    rebuilding_ = false;
    if (cp0_ui_list_max_scroll(tops_.back(), layout_.h) > 0) show_scrollbar();
}

void CompactList::update_row(int index, const ListRow &row)
{
    if (index < 0 || index >= count()) return;
    Row &current = rows_[static_cast<size_t>(index)];
    if (current.spec.kind != row.kind) {
        std::vector<ListRow> rows;
        rows.reserve(rows_.size());
        for (const Row &r : rows_) rows.push_back(r.spec);
        rows[static_cast<size_t>(index)] = row;
        set_rows(rows);
        return;
    }
    const ListRow previous = current.spec;
    current.spec = row;
    fill_row(current, &previous);
}

void CompactList::set_value(int index, const std::string &value, ValueTone tone)
{
    if (index < 0 || index >= count()) return;
    ListRow row = rows_[static_cast<size_t>(index)].spec;
    row.value = value;
    row.tone = tone;
    update_row(index, row);
}

void CompactList::set_hidden(bool hidden)
{
    if (!list_) return;
    if (hidden) lv_obj_add_flag(list_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(list_, LV_OBJ_FLAG_HIDDEN);
}

// ------------------------------------------------------------------ selection and scrolling

/* The offset the next move starts from: where a running keyboard scroll is heading, else where the list is. */
int CompactList::scroll_base() const
{
    if (!list_) return 0;
    return busy_ ? busy_target_ : static_cast<int>(lv_obj_get_scroll_y(list_));
}

cp0_ui_list_state_t CompactList::state() const
{
    cp0_ui_list_state_t s{};
    s.kinds = kinds_.data();
    s.tops = tops_.data();
    s.count = count();
    s.view_h = layout_.h;
    s.row_h = layout_.row_h;
    s.selected = selected_;
    s.scroll_y = scroll_base();
    s.read_only = options_.read_only ? 1 : 0;
    s.wrap = options_.wrap ? 1 : 0;
    return s;
}

void CompactList::set_selected(int index, bool notify)
{
    if (index == selected_) return;
    const int previous = selected_;
    selected_ = index;
    style_selected(previous);
    style_selected(selected_);
    if (notify && events_.selected && selected_ >= 0) events_.selected(selected_);
}

void CompactList::scroll_to(int y, bool animated)
{
    if (!list_) return;
    if (y == static_cast<int>(lv_obj_get_scroll_y(list_))) {
        if (busy_) lv_anim_delete(list_, nullptr); /* a running scroll heading elsewhere stops here */
        busy_ = false;
        return;
    }
    if (animated) {
        // LVGL ends the running scroll animation (LV_EVENT_SCROLL_END clears busy_) before it starts the new one:
        // busy_ is set after the call
        lv_obj_scroll_to_y(list_, y, LV_ANIM_ON);
        busy_ = true;
        busy_target_ = y;
    } else {
        lv_anim_delete(list_, nullptr);
        busy_ = false;
        lv_obj_scroll_to_y(list_, y, LV_ANIM_OFF);
    }
}

/* A MOVE / SCROLL result: the highlight first, then the scroll (the selected callback runs last). */
void CompactList::apply(const cp0_ui_list_result_t &result, bool animated, bool notify)
{
    if (result.action == CP0_UI_LIST_MOVE) {
        const int previous = selected_;
        set_selected(result.index, false);
        scroll_to(result.scroll_y, animated);
        if (notify && events_.selected && selected_ >= 0 && selected_ != previous) events_.selected(selected_);
    } else if (result.action == CP0_UI_LIST_SCROLL) {
        scroll_to(result.scroll_y, animated);
    }
}

void CompactList::select(int index, bool animated)
{
    if (!list_ || rows_.empty()) return;
    index = std::clamp(index, 0, count() - 1);
    cp0_ui_list_state_t s = state();
    if (options_.read_only) {
        cp0_ui_list_result_t r{CP0_UI_LIST_SCROLL, -1,
                               cp0_ui_list_scroll_to_show(s.tops, s.count, index, s.scroll_y, s.view_h, 0)};
        apply(r, animated, false);
        return;
    }
    const int target = cp0_ui_list_step(s.kinds, s.count, index, 0, 0);
    if (target < 0) return;
    cp0_ui_list_result_t r{CP0_UI_LIST_MOVE, target,
                           cp0_ui_list_scroll_to_show(s.tops, s.count, target, s.scroll_y, s.view_h, 1)};
    apply(r, animated, true);
}

void CompactList::show_scrollbar()
{
    if (!list_ || !hide_timer_) return;
    if (!bar_shown_) {
        bar_shown_ = true;
        lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO); /* drawn only when the rows do not all fit */
    }
    lv_timer_reset(hide_timer_);
    lv_timer_resume(hide_timer_);
}

void CompactList::hide_timer_cb(lv_timer_t *timer)
{
    auto *self = static_cast<CompactList *>(lv_timer_get_user_data(timer));
    if (!self || !self->list_) return;
    if (lv_obj_has_state(self->list_, LV_STATE_SCROLLED)) return; /* still scrolling: look again in a second */
    lv_timer_pause(timer);
    if (self->bar_shown_) {
        self->bar_shown_ = false;
        lv_obj_set_scrollbar_mode(self->list_, LV_SCROLLBAR_MODE_OFF);
    }
}

// ------------------------------------------------------------------ input

void CompactList::on_key(uint32_t key)
{
    const cp0_ui_list_state_t s = state();
    const cp0_ui_list_result_t r = cp0_ui_list_key(&s, list_key(key));
    switch (r.action) {
    case CP0_UI_LIST_MOVE:
    case CP0_UI_LIST_SCROLL:
        apply(r, true, true);
        break;
    case CP0_UI_LIST_ACTIVATE:
        if (events_.activate) events_.activate(r.index, rows_[static_cast<size_t>(r.index)].spec.id); /* last */
        break;
    case CP0_UI_LIST_BACK:
        if (events_.back) events_.back(); /* last */
        break;
    case CP0_UI_LIST_NONE:
        break;
    }
}

/* The Roller's rule: a tap selects; a tap on the selected item activates it, unless a keyboard scroll is still moving
 * the rows (the row under the finger may not be the one the user saw). */
void CompactList::on_row_clicked(int index)
{
    const cp0_ui_list_state_t s = state();
    const cp0_ui_list_result_t r = cp0_ui_list_tap(&s, index, busy_ ? 1 : 0);
    if (r.action == CP0_UI_LIST_MOVE) apply(r, true, true);
    else if (r.action == CP0_UI_LIST_ACTIVATE && events_.activate)
        events_.activate(r.index, rows_[static_cast<size_t>(r.index)].spec.id); /* may replace the rows: last */
}

void CompactList::list_event_cb(lv_event_t *event)
{
    auto *self = static_cast<CompactList *>(lv_event_get_user_data(event));
    if (!self || !self->list_) return;
    switch (lv_event_get_code(event)) {
    case LV_EVENT_KEY:
        lv_event_stop_processing(event);
        self->on_key(lv_event_get_key(event));
        break;
    case LV_EVENT_SCROLL_BEGIN:
        if (!lv_event_get_param(event)) self->busy_ = false; /* a finger took over */
        if (!self->rebuilding_) self->show_scrollbar();
        break;
    case LV_EVENT_SCROLL:
        if (!self->rebuilding_) self->show_scrollbar();
        break;
    case LV_EVENT_SCROLL_END:
        self->busy_ = false;
        if (!self->rebuilding_) self->show_scrollbar(); /* hides a second after the last movement */
        break;
    default:
        break;
    }
}

void CompactList::row_event_cb(lv_event_t *event)
{
    auto *self = static_cast<CompactList *>(lv_event_get_user_data(event));
    auto *row = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    if (!self || !row || !self->list_) return;
    self->on_row_clicked(static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(row))));
}

} // namespace ui
} // namespace cp0
