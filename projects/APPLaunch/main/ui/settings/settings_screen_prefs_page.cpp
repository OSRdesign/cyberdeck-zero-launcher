/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Screen > Idle dim level, Terminal text size, Screen size (see settings_screen_prefs_page.hpp).
 */

#include "settings_screen_prefs_page.hpp"

#include "../launcher_media_controls.h"
#include "../model/st_viewport_model.hpp"
#include "../screen_prefs.hpp"

#include "cp0_font_service.hpp"

#include <cstdlib>
#include <utility>

namespace screen_prefs_ui {
namespace {

using namespace screen_prefs;

std::string size_source_text(SizeSource source)
{
    return source == SizeSource::Setting ? "from the setting"
           : source == SizeSource::Installer ? "from the installer"
                                             : "from the board profile";
}

View dim_view()
{
    View v;
    const Stored stored = screen_prefs::stored();
    const int profile = launcher_media_controls::profile_dim_percent();
    v.values = dim_choices_with(stored.dim);
    for (const int value : v.values) {
        v.labels.push_back(value == 0 ? dim_default_label(profile) : dim_label(value));
        v.notes.emplace_back();
    }
    const int row = dim_row_of(stored.dim);
    v.current = row >= 0 ? row : static_cast<int>(v.values.size()) - 1;
    v.row_value = v.labels[static_cast<std::size_t>(v.current)];
    v.hint = "Brightness while the screen is dimmed. Applies at once.";
    return v;
}

View terminal_view(Screen screen, MeasureFn measure)
{
    View v;
    const Stored stored = screen_prefs::stored();
    const int auto_px = st_viewport::font_px_resolved(screen.w, screen.h, 0, std::getenv("APPLAUNCH_TERM_FONT"));
    v.values = term_choices();
    int row = term_row_of(stored.terminal);
    if (row < 0) {
        v.values.push_back(stored.terminal); /* a hand-edited size outside the list */
        row = static_cast<int>(v.values.size()) - 1;
    }
    for (const int px : v.values) {
        const int shown = px == 0 ? auto_px : px;
        v.labels.push_back(term_label(px, auto_px));
        const Cell cell = measure ? measure(shown) : Cell{};
        v.notes.push_back(cell.w > 0 && cell.h > 0 && screen.w > 0 && screen.h > 0
                              ? grid_text(st_viewport::grid_cols(screen.w, cell.w, st_viewport::max_cols_for(screen.w, screen.h)),
                                          st_viewport::grid_rows(screen.h, cell.h, st_viewport::max_rows_for(screen.w, screen.h)))
                              : std::string());
    }
    v.current = row;
    v.row_value = v.labels[static_cast<std::size_t>(row)];
    v.hint = "Columns x rows. Applies the next time the terminal opens.";
    return v;
}

View size_view(Screen screen)
{
    (void)screen;
    View v;
    const Stored stored = screen_prefs::stored();
    v.labels.push_back(size_auto_label());
    v.diags.emplace_back();
    for (const std::string &diag : diagonals()) {
        v.labels.push_back(diag_label(diag));
        v.diags.push_back(diag);
    }
    int row = size_row_of(stored.diag, stored.mm);
    if (row < 0) {
        v.labels.push_back(diag_label(stored.diag)); /* a hand-edited diagonal outside the list */
        v.diags.push_back(stored.diag);
        row = static_cast<int>(v.labels.size()) - 1;
    }
    v.notes.assign(v.labels.size(), std::string());
    v.current = row;
    v.row_value = row == 0 ? "Auto" : diag_label(stored.diag);

    // the size in force at this start and where it came from, then when a change takes effect
    const SizeSource source = size_source_in_force();
    std::string in_use;
    if (source == SizeSource::Setting) in_use = diag_label(startup().stored.diag);
    else if (source == SizeSource::Installer) in_use = installer_size();
    else if (const char *mm = std::getenv("APPLAUNCH_PANEL_MM"); mm && *mm) in_use = std::string(mm) + " mm";
    else in_use = "default density";
    v.hint = "In use: " + in_use + " " + size_source_text(source) + ". A change applies at next start.";
    return v;
}

} // namespace

Screen screen_of_default_display()
{
    Screen s;
    if (lv_display_t *display = lv_display_get_default()) {
        s.w = static_cast<int>(lv_display_get_horizontal_resolution(display));
        s.h = static_cast<int>(lv_display_get_vertical_resolution(display));
    }
    return s;
}

Cell measure_terminal_cell(int px)
{
    Cell cell;
    const lv_font_t *font = cp0_fonts().get("JetBrainsMono-Bold.ttf", static_cast<uint16_t>(px),
                                            LV_FREETYPE_FONT_STYLE_NORMAL, LV_FREETYPE_FONT_RENDER_MODE_BITMAP);
    if (font) {
        cell.w = static_cast<int>(lv_font_get_glyph_width(font, 'M', 0));
        cell.h = static_cast<int>(lv_font_get_line_height(font));
    }
    return cell;
}

View view(Kind kind, Screen screen, MeasureFn measure)
{
    switch (kind) {
    case Kind::IdleDim: return dim_view();
    case Kind::TerminalFont: return terminal_view(screen, measure);
    case Kind::PanelSize: return size_view(screen);
    }
    return {};
}

std::vector<std::string> labels(Kind kind)
{
    return view(kind, screen_of_default_display()).labels;
}

bool size_offered()
{
    const char *board = std::getenv("APPLAUNCH_BOARD");
    return board && *board && std::string(board) != "zero2w";
}

} // namespace screen_prefs_ui

LvSettingScreenPrefChoicePage3::LvSettingScreenPrefChoicePage3(lv_obj_t *parent, const NodeIter &parent_node,
                                                               std::function<void()> back_callback,
                                                               screen_prefs_ui::Kind kind)
    : LvSettingValuePage3Base(parent_node, {}), kind_(kind)
{
    screen_ = screen_prefs_ui::screen_of_default_display();
    view_ = screen_prefs_ui::view(kind_, screen_); /* read before initialize(): it selects this row */
    back_callback_ = std::move(back_callback);
    LeaveSelfPage = [this] { request_back(); };
    initialize(parent);
}

LvSettingScreenPrefChoicePage3::~LvSettingScreenPrefChoicePage3()
{
    LeaveSelfPage = nullptr;
    back_callback_ = nullptr;
}

std::string LvSettingScreenPrefChoicePage3::choice_option_note(int index) const
{
    return index >= 0 && index < static_cast<int>(view_.notes.size()) ? view_.notes[static_cast<std::size_t>(index)]
                                                                       : std::string();
}

int LvSettingScreenPrefChoicePage3::initial_selection() const
{
    return view_.current;
}

bool LvSettingScreenPrefChoicePage3::store(int row)
{
    using screen_prefs_ui::Kind;
    if (row < 0 || row >= static_cast<int>(view_.labels.size())) return false;
    switch (kind_) {
    case Kind::IdleDim: {
        const int percent = view_.values[static_cast<std::size_t>(row)];
        if (!screen_prefs::save_dim(percent)) return false;
        launcher_media_controls::set_idle_dim_override(percent); /* the next dim uses it */
        return true;
    }
    case Kind::TerminalFont:
        return screen_prefs::save_terminal_font(view_.values[static_cast<std::size_t>(row)]);
    case Kind::PanelSize: {
        const std::string &diag = view_.diags[static_cast<std::size_t>(row)];
        if (diag.empty()) return screen_prefs::save_size("", "");
        const std::string mm = screen_prefs::panel_mm(diag, screen_.w, screen_.h);
        return !mm.empty() && screen_prefs::save_size(diag, mm);
    }
    }
    return false;
}

SettingApiResult LvSettingScreenPrefChoicePage3::activate_selected()
{
    if (selected_index == view_.current) {
        request_back(); /* nothing to change */
        return SettingApiResult::Success;
    }
    if (!store(selected_index)) {
        select(view_.current); /* the list's check stays on the value in force */
        report_status("Could not be saved", true);
        return SettingApiResult::Failure;
    }
    view_.current = selected_index;
    request_back();
    return SettingApiResult::Success;
}

void LvSettingScreenPrefChoicePage3::request_back()
{
    if (back_requested_) return;
    back_requested_ = true;
    if (back_callback_) back_callback_();
}

std::unique_ptr<DComponens::LvglComponensBase> settings_idle_dim_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback)
{
    return std::make_unique<LvSettingScreenPrefChoicePage3>(parent, page_node, std::move(back_callback),
                                                            screen_prefs_ui::Kind::IdleDim);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_terminal_font_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback)
{
    return std::make_unique<LvSettingScreenPrefChoicePage3>(parent, page_node, std::move(back_callback),
                                                            screen_prefs_ui::Kind::TerminalFont);
}

std::unique_ptr<DComponens::LvglComponensBase> settings_panel_size_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback)
{
    return std::make_unique<LvSettingScreenPrefChoicePage3>(parent, page_node, std::move(back_callback),
                                                            screen_prefs_ui::Kind::PanelSize);
}

namespace settings_screen_prefs {

namespace {
void append_rows(Tree &tree, const NodeIter &screen, const char *label, SettingPageFactory factory,
                 screen_prefs_ui::Kind kind)
{
    NodeIter entry = tree.append_child(screen, SettingEntry{label, std::move(factory)});
    for (const std::string &row : screen_prefs_ui::labels(kind)) tree.append_child(entry, SettingEntry{row});
}
} // namespace

void append_entries(Tree &tree, const NodeIter &screen, bool with_dim)
{
    if (with_dim)
        append_rows(tree, screen, "Idle dim level", settings_idle_dim_choice_page_factory,
                    screen_prefs_ui::Kind::IdleDim);
    append_rows(tree, screen, "Terminal text size", settings_terminal_font_choice_page_factory,
                screen_prefs_ui::Kind::TerminalFont);
    if (screen_prefs_ui::size_offered())
        append_rows(tree, screen, "Screen size", settings_panel_size_choice_page_factory,
                    screen_prefs_ui::Kind::PanelSize);
}

} // namespace settings_screen_prefs
