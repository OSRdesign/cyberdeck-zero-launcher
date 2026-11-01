/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates (report 047 P4, the owner's "is it frozen?" requirement): the rules of a visible wait, with no LVGL.
 * The Zero 2 W takes 15-60 s and more for an apt scan, a plan's closure, the helper's check or a list refresh; every such
 * wait shows that it is alive: a step in plain words, a bar (real progress when the stages are known, a moving segment
 * otherwise), an elapsed counter mm:ss (the proof of life, redrawn once a second), a reassurance line on the slow boards at
 * once, and a second line when 20 s pass without a step changing. Unit-tested in tests/test_settings_wait_model.cpp.
 *
 *   elapsed_text     milliseconds as "mm:ss" (minutes not capped: "75:02")
 *   with_elapsed     "TEXT  mm:ss" (two spaces: a cut status line keeps the time whole, Page::set_caption)
 *   stage_percent    stage n of N (1-based) and how far inside it: 0..99 (a wait never shows 100 % before it ends)
 *   unknown_fraction how far inside a stage of unknown length after `steps` steps (the closure's checks): grows, never 1
 *   Monotonic        a bar that never moves backwards during one wait (a new estimate lower than the last is ignored)
 *   sweep            the moving segment of an indeterminate bar (lv_bar's range mode): 30 % wide, across in 2 s
 *   still            20 s and more without a step change: the second line
 *   slow_line, still_line, slow_row, still_row   the lines (the box's sentences, and the short ones for a list row)
 */
#pragma once

#include <string>

namespace settings_wait {

constexpr long long kStillMs = 20000;
constexpr int kSweepWidth = 30;      /* percent of the bar */
constexpr long long kSweepMs = 2000; /* one pass across */

std::string elapsed_text(long long ms);
std::string with_elapsed(const std::string &text, long long ms);
int stage_percent(int stage, int total, double within);
double unknown_fraction(int steps);
bool still(long long since_change_ms);
/* start / end of the segment (0..100, end - start <= kSweepWidth) at `elapsed_ms` */
void sweep(long long elapsed_ms, int &start, int &end);

class Monotonic {
public:
    void reset() { last_ = 0; }
    /* max(last, raw), clamped to 0..100 */
    int value(int raw);
    int last() const { return last_; }

private:
    int last_ = 0;
};

const char *slow_line();  /* "This board is slow: it can take a minute. Nothing is frozen." */
const char *still_line(); /* "Still working... the packages list is long" */
const char *slow_row();   /* "Slow board: nothing is frozen" (one list row at 480 px) */
const char *still_row();  /* "Still working: the list is long" */

} // namespace settings_wait
