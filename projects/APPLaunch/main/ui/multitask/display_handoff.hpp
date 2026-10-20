/*
 * SPDX-License-Identifier: MIT
 *
 * Display suspended mode (multitasking P-b, task 015 section 5): how the launcher hands the panel to a fullscreen app
 * while its own main loop keeps running (the supervisor's bridge timer, lv_async_call), instead of blocking the UI
 * thread with every LVGL timer off as the legacy path does.
 *
 * LVGL thread only. Used by multitask_ui and by the render harness (scene multitask_handoff).
 */
#pragma once

#include "lvgl/lvgl.h"

#include <cstddef>

namespace display_handoff {

/* The app takes the panel: the display is suspended (cp0_display_suspend: no launcher pixel is written any more,
 * the touch screen reads as released), every running LVGL timer but `keep` is paused (refresh, input reads,
 * animations, the launcher's own timers: the quiet of lv_timer_enable(false) without a busy main loop) and the
 * input devices are reset (a press in progress ends without a click). Idempotent. */
void enter_app_front(lv_timer_t *keep);

/* The launcher takes the panel back: cp0_display_resume() (writes allowed, every screen and layer invalidated, panned
 * back to page 0), then the timers enter_app_front() paused are resumed (those deleted meanwhile are skipped).
 * Idempotent. The caller then refreshes (lv_refr_now) as the legacy path does. */
void leave_app_front();

bool app_front();
/* Timers paused by enter_app_front() (tests). */
std::size_t paused_timers();

} // namespace display_handoff
