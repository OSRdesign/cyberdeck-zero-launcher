/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Screen > Idle dim level, Terminal text size, Screen size (2026-11-18; rules in
 * model/screen_prefs_model.hpp, storage in screen_prefs.hpp). Three value pages (a ChoicePage each in the native host),
 * one class, native Settings only (the legacy roller Settings does not get them):
 *
 *   Idle dim level      Default (the board profile's level) / 5 / 10 / 20 / 30 / 50 %. Applied at once: the next idle dim
 *                       (clock screensaver, an app's idle dim request) uses it. Shown where the backlight has levels.
 *   Terminal text size  Auto (20 px, 14 px on a portrait screen under 480 wide, or board.conf's APPLAUNCH_TERM_FONT) /
 *                       Small 14 / Medium 17 / Large 20 / XL 24 px, each row with the grid it gives on this screen
 *                       ("57 x 40" columns x rows, measured with the terminal's own font and clamps). Read the next time
 *                       the terminal (CLI, SSH) opens; the grey hint says so.
 *   Screen size         Auto (board profile) / 2.8 / 3.5 / 4.3 / 5 / 5.5 / 7 / 8 / 10.1 in. Sets APPLAUNCH_PANEL_MM at the
 *                       next launcher start; the grey hint shows the size in use and where it came from (installer,
 *                       setting, board profile). Shown where a board profile exists (the deck and an unknown board have
 *                       none, as for install.sh). No custom diagonal: it needs a number form page (open question).
 *
 * Every row of the section list carries the grey chevron (choice_chevron) and the value in force; no confirmation, no
 * restart. Esc / the header's Esc go back without a change.
 */
#pragma once

#include "lvgl_components.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace screen_prefs_ui {

enum class Kind { IdleDim, TerminalFont, PanelSize };

/* What one of the lists shows: built from the stored values, the screen and the environment each time. */
struct View {
    std::vector<std::string> labels; /* the rows */
    std::vector<std::string> notes;  /* a grey note per row (the terminal's grid), else empty strings */
    std::vector<std::string> diags;  /* PanelSize: the diagonal of each row, "" for Auto */
    std::vector<int> values;         /* IdleDim: percent per row (0 = Default); TerminalFont: px (0 = Auto) */
    int current = 0;                 /* the row in force (what is stored) */
    std::string row_value;           /* the value text of the row in the section list */
    std::string hint;                /* the grey line while the list is open */
};

/* The canvas of the screen in px as the launcher's display has it. */
struct Screen {
    int w = 0, h = 0;
};
Screen screen_of_default_display();

/* The terminal font cell at `px`: its advance and line height (JetBrainsMono-Bold, as the terminal measures). */
struct Cell {
    int w = 0, h = 0;
};
using MeasureFn = Cell (*)(int px);
Cell measure_terminal_cell(int px);

View view(Kind kind, Screen screen, MeasureFn measure = measure_terminal_cell);

/* The rows' labels for the tree (the host takes its option labels from the tree's children). */
std::vector<std::string> labels(Kind kind);

/* The Screen size row is offered where a board profile exists (APPLAUNCH_BOARD set and not the deck's zero2w). */
bool size_offered();

} // namespace screen_prefs_ui

class LvSettingScreenPrefChoicePage3 : public LvSettingValuePage3Base {
public:
    LvSettingScreenPrefChoicePage3(lv_obj_t *parent, const NodeIter &parent_node, std::function<void()> back_callback,
                                   screen_prefs_ui::Kind kind);
    ~LvSettingScreenPrefChoicePage3() override;

    bool choice_chevron() const override { return true; }
    std::string choice_row_value() const override { return view_.row_value; }
    std::string choice_option_note(int index) const override;
    std::string choice_hint() const override { return view_.hint; }

protected:
    int initial_selection() const override;
    SettingApiResult activate_selected() override;

private:
    void request_back();
    bool store(int row);

    screen_prefs_ui::Kind kind_;
    screen_prefs_ui::View view_;
    screen_prefs_ui::Screen screen_;
    bool back_requested_ = false;
    std::function<void()> back_callback_;
};

std::unique_ptr<DComponens::LvglComponensBase> settings_idle_dim_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);
std::unique_ptr<DComponens::LvglComponensBase> settings_terminal_font_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);
std::unique_ptr<DComponens::LvglComponensBase> settings_panel_size_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);

namespace settings_screen_prefs {

// Appends the rows to the Screen section (native Settings only): Idle dim level when the backlight has levels
// (`with_dim`), Terminal text size, and Screen size where the board has a profile.
void append_entries(Tree &tree, const NodeIter &screen, bool with_dim);

} // namespace settings_screen_prefs
