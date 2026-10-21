/*
 * SPDX-License-Identifier: MIT
 *
 * Display suspended mode (see display_handoff.hpp).
 */
#include "display_handoff.hpp"

#include <cstddef>
#include <vector>

/* The Pi display backend (cp0_lvgl_dpi_scaled.c) has these; builds without it (the SDL simulator) link
 * without them and simply do not suspend. */
extern "C" {
__attribute__((weak)) void cp0_display_suspend(void);
__attribute__((weak)) void cp0_display_resume(void);
}

namespace display_handoff {
namespace {

bool s_app_front = false;
std::vector<lv_timer_t *> s_paused;

bool timer_alive(lv_timer_t *timer)
{
    for (lv_timer_t *t = lv_timer_get_next(nullptr); t; t = lv_timer_get_next(t))
        if (t == timer) return true;
    return false;
}

} // namespace

void enter_app_front(lv_timer_t *keep)
{
    if (s_app_front) return;
    s_app_front = true;
    if (cp0_display_suspend) cp0_display_suspend();
    s_paused.clear();
    for (lv_timer_t *t = lv_timer_get_next(nullptr); t; t = lv_timer_get_next(t)) {
        if (t == keep || lv_timer_get_paused(t)) continue;
        lv_timer_pause(t);
        s_paused.push_back(t);
    }
    lv_indev_reset(nullptr, nullptr);
}

void leave_app_front()
{
    if (!s_app_front) return;
    s_app_front = false;
    if (cp0_display_resume) cp0_display_resume();
    for (lv_timer_t *t : s_paused)
        if (timer_alive(t) && lv_timer_get_paused(t)) lv_timer_resume(t);
    s_paused.clear();
}

bool app_front()
{
    return s_app_front;
}

std::size_t paused_timers()
{
    return s_paused.size();
}

} // namespace display_handoff
