/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_st.hpp"

#include <sstream>

#include <cctype>
#include <cstdlib>
#include <cstring>

UISTPage::UISTPage(TerminalHelpFactory help_factory)
    : AppPage(), help_factory_(help_factory)
{
    init_geometry();
    set_page_title("CLI");
    reset_terminal();
    create_ui();
    create_help();
    if (!renderer_ready()) {
        if (terminal_container_) lv_obj_delete(terminal_container_);
        return;
    }
    bind_events();
    start_shell();
}
UISTPage::~UISTPage()
{
    terminal_active_ = false;
    if (root_screen_)
        lv_obj_remove_event_cb_with_user_data(
            root_screen_, UISTPage::static_event_cb, this);
    stop_timers();
    stop_pty();
    detach_renderer_callbacks();
}

void UISTPage::exec(std::string command)
{
    std::vector<std::string> tokens;
    std::istringstream stream(command);
    std::string token;
    while (stream >> token) tokens.push_back(token);
    std::list<std::string> arguments;
    for (size_t index = 1; index < tokens.size(); ++index) arguments.push_back(tokens[index]);
    exec(tokens.empty() ? std::string() : tokens.front(), arguments);
}

void UISTPage::exec(const std::string &command, const std::list<std::string> &arguments)
{
    stop_timers();
    stop_pty();

    terminal_active_ = true;
    waiting_key_to_exit_ = false;
    hide_help();
    big_mode_ = false;
    term_cols_ = normal_cols_;
    term_rows_ = normal_rows_;
    viewport_x_ = 0;
    viewport_y_ = 0;
    big_view_locked_ = false;
    reset_terminal();
    update_big_mode_ui();
    render_all();

    if (command.empty()) {
        static constexpr char ERROR_MESSAGE[] = "Error: empty command\r\n";
        process_bytes(ERROR_MESSAGE, sizeof(ERROR_MESSAGE) - 1);
        render_all();
        terminal_active_ = false;
        waiting_key_to_exit_ = true;
        return;
    }

    start_command(command, arguments, command.c_str(), "Error: openpty/fork failed\r\n");
}

// On a display wider than the compat 320x170 the terminal fills the whole panel: the
// font cell is measured from the actual font and the grid is derived from the pixels.
void UISTPage::init_geometry()
{
    lv_display_t *display = root_screen_ ? lv_obj_get_display(root_screen_) : lv_display_get_default();
    native_ = display && lv_display_get_horizontal_resolution(display) > COMPAT_TERM_W;
    if (!native_) return;

    term_w_ = lv_display_get_horizontal_resolution(display);
    term_h_ = lv_display_get_vertical_resolution(display);
    font_px_ = 20;
    if (const char *requested = std::getenv("APPLAUNCH_TERM_FONT")) {
        const int value = std::atoi(requested);
        if (value >= 10 && value <= 48) font_px_ = value;
    }
    if (const lv_font_t *font = terminal_font()) {
        const int advance = static_cast<int>(lv_font_get_glyph_width(font, 'M', 0));
        const int height = static_cast<int>(lv_font_get_line_height(font));
        if (advance > 0) char_w_ = advance;
        if (height > 0) char_h_ = height;
    }
    normal_cols_ = clamp(term_w_ / char_w_, 20, MAX_COLS);
    normal_rows_ = clamp(term_h_ / char_h_, 8, MAX_ROWS);
    term_cols_ = normal_cols_;
    term_rows_ = normal_rows_;
    scroll_bot_ = normal_rows_ - 1;

    // No top bar: the container takes the whole screen.
    disable_top_bar();
    if (ui_APP_Container) {
        lv_obj_set_size(ui_APP_Container, term_w_, term_h_);
        lv_obj_set_pos(ui_APP_Container, 0, 0);
    }
    SLOGI("[ST] native terminal %dx%d px, font %d px, cell %dx%d, grid %dx%d", term_w_, term_h_,
          font_px_, char_w_, char_h_, normal_cols_, normal_rows_);
}

// rows > 0 scrolls towards older output (finger dragged down), rows < 0 towards the newest.
void UISTPage::scroll_by_rows(int rows)
{
    if (big_mode_ || rows == 0) return;
    const int limit = static_cast<int>(scrollback_.size());
    const int target = clamp(scrollback_offset_ + rows, 0, limit);
    if (target == scrollback_offset_) return;
    scrollback_offset_ = target;
    dirty_all();
    render_all();
    update_scrollbar();
}

void UISTPage::drag_cb(lv_event_t *event)
{
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        drag_accum_ = 0;
        break;
    case LV_EVENT_PRESSING: {
        lv_indev_t *indev = lv_indev_active();
        if (!indev) break;
        lv_point_t vector{};
        lv_indev_get_vect(indev, &vector);
        drag_accum_ += vector.y;
        const int rows = drag_accum_ / char_h_;
        if (rows != 0) {
            drag_accum_ -= rows * char_h_;
            scroll_by_rows(rows);
        }
        break;
    }
    default:
        break;
    }
}

void UISTPage::static_drag_cb(lv_event_t *event) noexcept
{
    auto *self = static_cast<UISTPage *>(lv_event_get_user_data(event));
    if (!self) return;
    try {
        self->drag_cb(event);
    } catch (...) {
        self->recover_callback_failure();
    }
}

int UISTPage::clamp(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

std::string UISTPage::printable(uint32_t codepoint)
{
    if (codepoint < 32 || codepoint == 127) return " ";
    std::string output;
    terminal_utf8_append(output, codepoint);
    return output;
}

lv_color_t UISTPage::palette(uint32_t color)
{
    static constexpr uint32_t COLORS[] = {
        0x0D1117, 0xFF5F56, 0x27C93F, 0xFFBD2E,
        0x2F81F7, 0xBC8CFF, 0x39C5CF, 0xF0F6FC,
        0x6E7681, 0xFFA198, 0x56D364, 0xE3B341,
        0x79C0FF, 0xD2A8FF, 0x56D4DD, 0xFFFFFF,
    };
    return lv_color_hex(COLORS[color < 16 ? color : DEFAULT_FG]);
}

const lv_font_t *UISTPage::terminal_font() const
{
    return launcher_fonts().get("JetBrainsMono-Bold.ttf", font_px_,
                                LV_FREETYPE_FONT_STYLE_NORMAL,
                                LV_FREETYPE_FONT_RENDER_MODE_BITMAP);
}

uint32_t UISTPage::xterm256_to_palette(int color)
{
    color = clamp(color, 0, 255);
    if (color < 16) return static_cast<uint32_t>(color);
    if (color >= 232) return color >= 244 ? 15 : 8;

    int index = color - 16;
    int red = index / 36;
    int green = (index / 6) % 6;
    int blue = index % 6;
    if (red >= green && red >= blue) return red >= 3 ? 9 : 1;
    if (green >= red && green >= blue) return green >= 3 ? 10 : 2;
    return blue >= 3 ? 12 : 4;
}

uint32_t UISTPage::rgb_to_palette(int red, int green, int blue)
{
    red = clamp(red, 0, 255);
    green = clamp(green, 0, 255);
    blue = clamp(blue, 0, 255);
    int maximum = std::max(red, std::max(green, blue));
    int minimum = std::min(red, std::min(green, blue));
    if (maximum < 80) return 0;
    if (maximum - minimum < 35) return maximum > 180 ? 15 : 8;
    if (red >= green && red >= blue) return red > 180 ? 9 : 1;
    if (green >= red && green >= blue) return green > 180 ? 10 : 2;
    return blue > 180 ? 12 : 4;
}

UISTPage::Glyph UISTPage::blank_glyph() const
{
    Glyph glyph;
    glyph.u = ' ';
    glyph.attr = cursor_.attr.attr;
    glyph.fg = cursor_.attr.fg;
    glyph.bg = cursor_.attr.bg;
    return glyph;
}

void UISTPage::dirty_row(int row)
{
    if (big_mode_) {
        int view_row = row - viewport_y_;
        if (view_row >= 0 && view_row < visible_rows()) dirty_[view_row] = true;
        return;
    }
    if (row >= 0 && row < normal_rows_) dirty_[row] = true;
}

void UISTPage::dirty_all()
{
    dirty_.fill(true);
    for (auto &segments : row_segments_) {
        for (auto &segment : segments) {
            segment.text.clear();
            segment.fg = UINT32_MAX;
            segment.bg = UINT32_MAX;
            segment.hidden = true;
            if (segment.label) lv_obj_add_flag(segment.label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

int UISTPage::visible_cols() const
{
    return normal_cols_;
}

int UISTPage::visible_rows() const
{
    return big_mode_ ? big_view_rows() : normal_rows_;
}

int UISTPage::visible_h() const
{
    return visible_rows() * char_h_;
}

int UISTPage::max_viewport_x() const
{
    return std::max(0, term_cols_ - visible_cols());
}

int UISTPage::max_viewport_y() const
{
    return std::max(0, term_rows_ - visible_rows());
}

void UISTPage::append_scrollback_row(const std::array<Glyph, MAX_COLS> &row)
{
    scrollback_.push_back(row);
    if (static_cast<int>(scrollback_.size()) <= SCROLLBACK_MAX_ROWS) return;

    int drop_count = static_cast<int>(scrollback_.size()) - SCROLLBACK_MAX_ROWS;
    scrollback_.erase(scrollback_.begin(), scrollback_.begin() + drop_count);
    scrollback_offset_ = std::max(0, scrollback_offset_ - drop_count);
}

const std::array<UISTPage::Glyph, UISTPage::MAX_COLS> &UISTPage::display_row(int row) const
{
    static const std::array<Glyph, MAX_COLS> EMPTY_ROW{};
    if (big_mode_) {
        int screen_row = clamp(viewport_y_ + row, 0, term_rows_ - 1);
        return screen_[static_cast<size_t>(screen_row)];
    }

    int history_rows = static_cast<int>(scrollback_.size());
    int total_rows = history_rows + term_rows_;
    int index = total_rows - term_rows_ - scrollback_offset_ + row;
    if (index < 0 || index >= total_rows) return EMPTY_ROW;
    if (index < history_rows) return scrollback_[static_cast<size_t>(index)];
    return screen_[static_cast<size_t>(index - history_rows)];
}

void UISTPage::scrollback_page(int direction)
{
    int old_offset = scrollback_offset_;
    int page_size = std::max(1, visible_rows() - 1);
    if (direction > 0)
        scrollback_offset_ = std::min(static_cast<int>(scrollback_.size()),
                                      scrollback_offset_ + page_size);
    else
        scrollback_offset_ = std::max(0, scrollback_offset_ - page_size);
    if (scrollback_offset_ != old_offset) dirty_all();
}
