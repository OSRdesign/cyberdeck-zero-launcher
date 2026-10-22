/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "cp0_backlight_profile.h"
#include "model/setup_value_policy.hpp"

namespace launcher_media_controls {

constexpr int VOLUME_STEP_PERCENT =
    setup_values::volume_metric(setup_values::VolumeMetric::StepPercent);

int adjust_volume(int delta_percent);
int adjust_brightness(int delta_percent);
bool toggle_mute();

// Once at startup: set the backlight as cp0_backlight_startup_level() says (the
// saved brightness, or the maximum when nothing is saved and the panel is below
// half). Nothing for no backlight or an on/off one. Does not persist changes or
// reuse a screen-off hardware reading. 1 written, 0 left alone, -1 failed.
int restore_startup_backlight(cp0_backlight_kind_t kind);

/* Drive the panel backlight to zero without persisting anything, for the
 * screen-off gesture.  Returns the raw value to hand back to
 * restore_backlight(), or -1 when the backlight could not be suspended.  The
 * persisted brightness must survive untouched: the normal step-based control
 * can never express zero, so reusing it here would overwrite the user's
 * setting and leave the panel dark after wake-up. */
int suspend_backlight();
void restore_backlight(int raw);

/* Lower the backlight to `percent` of its maximum (never to zero) without persisting
 * anything, for the clock screensaver. Returns the raw value to pass to
 * restore_backlight(), or -1 when the backlight could not be changed. */
int dim_backlight(int percent);

} // namespace launcher_media_controls
