/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_st.hpp"
#include "../model/st_viewport_model.hpp"
#include "../native_ui.hpp"
#include "cp0_ui_widgets.hpp"

// ------------------------------------------------------------------ the on-screen keyboard (task 014 P4b)

/* The terminal has no text field: on the native display it asks for the fallback keyboard for its whole session. The
 * keyboard service shows it only while the launcher's policy allows (launcher_osk: no physical keyboard, setting Auto,
 * no lock screen) and asks that policy again every 250 ms, so it goes when a keyboard appears and comes back when the
 * last one has gone. Its Tab types a tab character (a multi-line request); its Esc is the shell's Esc and, held, the
 * launcher's hold-Esc (it moves the Esc state like the toolbar's). Builds without the dpi-scaled backend (SDL): nothing. */
void UISTPage::setup_keyboard()
{
    if (keyboard_wanted_ || !native_ || !native_ui::enabled() || !terminal_container_) return;
    keyboard_wanted_ = true;
    keyboard_owner_ = terminal_container_;
    keyboard_listener_ = cp0::ui::osk::add_inset_listener([this](int inset) { on_keyboard_inset(inset); });
    cp0::ui::osk::request(keyboard_owner_, cp0::ui::TextType::Text, true);
    on_keyboard_inset(cp0::ui::osk::bottom_inset()); /* none after the page change (native_ui::begin_page) */
}

/* Another owner (a sudo prompt's field over the terminal) took the keyboard and let it go: ask again (the cursor timer,
 * every 500 ms). A page change clears the request too, but the terminal goes with that page at once. */
void UISTPage::keep_keyboard_request()
{
    if (keyboard_wanted_ && keyboard_owner_ && !cp0::ui::osk::owner())
        cp0::ui::osk::request(keyboard_owner_, cp0::ui::TextType::Text, true);
}

void UISTPage::teardown_keyboard()
{
    if (keyboard_listener_) cp0::ui::osk::remove_inset_listener(keyboard_listener_);
    keyboard_listener_ = 0;
    if (keyboard_owner_) cp0::ui::osk::release(keyboard_owner_); /* ignored when another page owns it now */
    keyboard_owner_ = nullptr;
    keyboard_wanted_ = false;
}

void UISTPage::on_keyboard_inset(int inset)
{
    inset = std::max(0, inset);
    if (inset == keyboard_inset_) return;
    keyboard_inset_ = inset;
    if (!renderer_ready()) return;
    fit_rows_to_view();
}

int UISTPage::fitted_rows() const
{
    return st_viewport::rows_for(normal_rows_, term_h_, keyboard_inset_, char_h_);
}

/* The grid for the room above the keyboard (st_viewport_model.hpp): fewer rows while it is up, the cursor line kept
 * on screen (the lines above that no longer fit go to the scrollback), the full grid back when it goes (those lines
 * return); the PTY gets the new size. The 80x24 view (F6) keeps its size: only its window shrinks and follows the
 * cursor. */
void UISTPage::fit_rows_to_view()
{
    if (big_mode_) {
        follow_cursor_in_big_mode();
        dirty_all();
        update_big_mode_ui();
        render_all();
        return;
    }
    const int rows = fitted_rows();
    if (rows == term_rows_) {
        update_scrollbar();
        return;
    }
    const int old_rows = term_rows_;
    const st_viewport::Resize plan = st_viewport::resize(old_rows, rows, cursor_.y, keyboard_pushed_rows_,
                                                         static_cast<int>(scrollback_.size()));
    if (plan.scroll_up > 0) {
        for (int y = 0; y < plan.scroll_up; ++y) append_scrollback_row(screen_[static_cast<size_t>(y)]);
        for (int y = 0; y + plan.scroll_up < old_rows; ++y)
            screen_[static_cast<size_t>(y)] = screen_[static_cast<size_t>(y + plan.scroll_up)];
    }
    if (rows < old_rows) {
        // the rows under the new grid (under the cursor, or moved up) are dropped
        const Glyph blank{};
        for (int y = rows; y < MAX_ROWS; ++y) screen_[static_cast<size_t>(y)].fill(blank);
    }
    if (plan.pull_back > 0) {
        for (int y = rows - 1; y >= plan.pull_back; --y)
            screen_[static_cast<size_t>(y)] = screen_[static_cast<size_t>(y - plan.pull_back)];
        const size_t first = scrollback_.size() - static_cast<size_t>(plan.pull_back);
        for (int y = 0; y < plan.pull_back; ++y)
            screen_[static_cast<size_t>(y)] = scrollback_[first + static_cast<size_t>(y)];
        scrollback_.erase(scrollback_.begin() + static_cast<std::ptrdiff_t>(first), scrollback_.end());
    }
    saved_cursor_.y = clamp(saved_cursor_.y + plan.cursor_y - cursor_.y, 0, rows - 1);
    cursor_.y = clamp(plan.cursor_y, 0, rows - 1);
    keyboard_pushed_rows_ = plan.pushed;
    term_rows_ = rows;
    scroll_top_ = 0;
    scroll_bot_ = rows - 1;
    scrollback_offset_ = 0; /* the prompt in view */
    dirty_all();
    render_all();
    pty_resize();
}

/* The F4..F8 bar of the 80x24 view sits at the bottom of the part above the keyboard (the whole screen without it). */
void UISTPage::place_big_mode_bar()
{
    const int slot_w = term_w_ / BOTTOM_BAR_SLOTS;
    for (int index = 0; index < BOTTOM_BAR_SLOTS; ++index) {
        const size_t slot = static_cast<size_t>(index);
        if (bottom_labels_[slot]) lv_obj_set_pos(bottom_labels_[slot], index * slot_w, view_h() - BIG_BOTTOM_H);
        if (bottom_indicators_[slot]) lv_obj_set_pos(bottom_indicators_[slot], index * slot_w, view_h() - 4);
    }
}

void UISTPage::update_scrollbar()
{
    if (!scrollbar_track_ || !scrollbar_thumb_) return;
    if (big_mode_) {
        int max_y = max_viewport_y();
        lv_obj_clear_flag(scrollbar_track_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(scrollbar_thumb_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(scrollbar_track_, SCROLLBAR_W, visible_h());
        lv_obj_set_pos(scrollbar_track_, term_w_ - SCROLLBAR_W - 1, 0);

        int thumb_height = std::max(8, visible_h() * visible_rows() / std::max(term_rows_, 1));
        thumb_height = std::min(visible_h(), thumb_height);
        int range = visible_h() - thumb_height;
        int thumb_y = max_y > 0 ? viewport_y_ * range / max_y : 0;
        lv_obj_set_size(scrollbar_thumb_, SCROLLBAR_W, thumb_height);
        lv_obj_set_pos(scrollbar_thumb_, term_w_ - SCROLLBAR_W - 1, thumb_y);
        lv_obj_move_foreground(scrollbar_track_);
        lv_obj_move_foreground(scrollbar_thumb_);
        return;
    }

    int history_rows = static_cast<int>(scrollback_.size());
    if (history_rows <= 0) {
        lv_obj_add_flag(scrollbar_track_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scrollbar_thumb_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(scrollbar_track_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(scrollbar_thumb_, LV_OBJ_FLAG_HIDDEN);
    const int track_h = view_h(); /* above the on-screen keyboard while it is up */
    lv_obj_set_size(scrollbar_track_, SCROLLBAR_W, track_h);
    lv_obj_set_pos(scrollbar_track_, term_w_ - SCROLLBAR_W - 1, 0);
    int total_rows = history_rows + term_rows_;
    int thumb_height = std::max(8, track_h * term_rows_ / std::max(total_rows, 1));
    thumb_height = std::min(track_h, thumb_height);
    int range = track_h - thumb_height;
    int thumb_y = range - scrollback_offset_ * range / std::max(1, history_rows);
    lv_obj_set_size(scrollbar_thumb_, SCROLLBAR_W, thumb_height);
    lv_obj_set_pos(scrollbar_thumb_, term_w_ - SCROLLBAR_W - 1, thumb_y);
    lv_obj_move_foreground(scrollbar_track_);
    lv_obj_move_foreground(scrollbar_thumb_);
}

void UISTPage::update_hscrollbar()
{
    if (!hscrollbar_track_ || !hscrollbar_thumb_) return;
    if (!big_mode_) {
        lv_obj_add_flag(hscrollbar_track_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hscrollbar_thumb_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(hscrollbar_track_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(hscrollbar_thumb_, LV_OBJ_FLAG_HIDDEN);
    int y = visible_h();
    int width = term_w_ - SCROLLBAR_W - 2;
    lv_obj_set_size(hscrollbar_track_, width, 3);
    lv_obj_set_pos(hscrollbar_track_, 0, y);
    int max_x = max_viewport_x();
    int thumb_width = std::max(18, width * visible_cols() / std::max(term_cols_, 1));
    thumb_width = std::min(width, thumb_width);
    int range = width - thumb_width;
    int thumb_x = max_x > 0 ? viewport_x_ * range / max_x : 0;
    lv_obj_set_size(hscrollbar_thumb_, thumb_width, 3);
    lv_obj_set_pos(hscrollbar_thumb_, thumb_x, y);
    lv_obj_move_foreground(hscrollbar_track_);
    lv_obj_move_foreground(hscrollbar_thumb_);
}

void UISTPage::set_bottom_label(int index, const char *text)
{
    if (index < 0 || index >= BOTTOM_BAR_SLOTS || !bottom_labels_[static_cast<size_t>(index)])
        return;
    lv_label_set_text(bottom_labels_[static_cast<size_t>(index)], text);
}

void UISTPage::update_big_mode_ui()
{
    for (auto *label : bottom_labels_) {
        if (!label) continue;
        if (big_mode_) lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    for (auto *indicator : bottom_indicators_) {
        if (!indicator) continue;
        if (big_mode_) lv_obj_clear_flag(indicator, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
    }
    place_big_mode_bar();
    update_hscrollbar();
    update_scrollbar();
}

void UISTPage::switch_big_mode(bool enable)
{
    if (big_mode_ == enable) return;
    terminal_active_ = false;
    stop_timers();
    stop_pty();
    big_mode_ = enable;
    term_cols_ = big_mode_ ? BIG_COLS : normal_cols_;
    term_rows_ = big_mode_ ? BIG_ROWS : fitted_rows();
    viewport_x_ = 0;
    viewport_y_ = 0;
    big_view_locked_ = false;
    reset_terminal();
    update_big_mode_ui();
    start_shell();
}

void UISTPage::leave_scrollback()
{
    if (scrollback_offset_ == 0) return;
    scrollback_offset_ = 0;
    dirty_all();
}

void UISTPage::pan_big_view(int dx, int dy)
{
    if (!big_mode_) return;
    big_view_locked_ = true;
    int old_x = viewport_x_;
    int old_y = viewport_y_;
    viewport_x_ = clamp(viewport_x_ + dx, 0, max_viewport_x());
    viewport_y_ = clamp(viewport_y_ + dy, 0, max_viewport_y());
    if (viewport_x_ != old_x || viewport_y_ != old_y) dirty_all();
}

void UISTPage::follow_cursor_in_big_mode()
{
    if (!big_mode_ || big_view_locked_) return;
    int old_x = viewport_x_;
    int old_y = viewport_y_;
    if (cursor_.x < viewport_x_)
        viewport_x_ = cursor_.x;
    else if (cursor_.x >= viewport_x_ + visible_cols())
        viewport_x_ = cursor_.x - visible_cols() + 1;
    if (cursor_.y < viewport_y_)
        viewport_y_ = cursor_.y;
    else if (cursor_.y >= viewport_y_ + visible_rows())
        viewport_y_ = cursor_.y - visible_rows() + 1;

    viewport_x_ = clamp(viewport_x_, 0, max_viewport_x());
    viewport_y_ = clamp(viewport_y_, 0, max_viewport_y());
    if (viewport_x_ != old_x || viewport_y_ != old_y) dirty_all();
}


