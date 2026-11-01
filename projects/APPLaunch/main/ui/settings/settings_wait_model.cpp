/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates: the rules of a visible wait (see settings_wait_model.hpp).
 */

#include "settings_wait_model.hpp"

#include <algorithm>
#include <cstdio>

namespace settings_wait {

std::string elapsed_text(long long ms)
{
    const long long s = std::max(0LL, ms) / 1000;
    char text[24];
    std::snprintf(text, sizeof text, "%02lld:%02lld", s / 60, s % 60);
    return text;
}

std::string with_elapsed(const std::string &text, long long ms)
{
    return text + "  " + elapsed_text(ms);
}

int stage_percent(int stage, int total, double within)
{
    if (total <= 0 || stage <= 0) return 0;
    stage = std::min(stage, total);
    within = std::max(0.0, std::min(1.0, within));
    const int percent = static_cast<int>(((stage - 1) + within) * 100.0 / total);
    return std::max(0, std::min(99, percent));
}

double unknown_fraction(int steps)
{
    if (steps <= 0) return 0.0;
    return 1.0 - 1.0 / (1.0 + steps / 3.0); /* 1 check 0.25, 3 checks 0.5, 9 checks 0.75: grows, never 1 */
}

bool still(long long since_change_ms)
{
    return since_change_ms >= kStillMs;
}

void sweep(long long elapsed_ms, int &start, int &end)
{
    const long long period = kSweepMs;
    const long long t = std::max(0LL, elapsed_ms) % period;
    const int head = static_cast<int>(t * (100 + kSweepWidth) / period); /* 0 .. 129: the segment enters and leaves */
    end = std::min(100, head);
    start = std::max(0, head - kSweepWidth);
    if (start > end) start = end;
}

int Monotonic::value(int raw)
{
    raw = std::max(0, std::min(100, raw));
    last_ = std::max(last_, raw);
    return last_;
}

const char *slow_line()
{
    return "This board is slow: it can take a minute. Nothing is frozen.";
}

const char *still_line()
{
    return "Still working... the packages list is long";
}

const char *slow_row()
{
    return "Slow board: nothing is frozen";
}

const char *still_row()
{
    return "Still working: the list is long";
}

} // namespace settings_wait
