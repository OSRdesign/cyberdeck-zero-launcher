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

/* Levels for the request socket and the status feed (report 035). The caller serialises them (app_media.cpp). */
int read_volume_percent();                  // 0..100
bool read_mute_state();
int set_volume_percent(int percent);        // un-mutes, saves the level like a key press; the level now in force
bool set_mute_state(bool muted);            // the state now in force
int read_brightness_percent();              // percent of the maximum, snapped to the launcher's steps
int set_brightness_percent(int percent);    // snapped to the steps; the percent now in force

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

/* The idle dim level in percent for dim_backlight() (the clock screensaver, the idle dim of the request socket).
 * The board profile may set it with APPLAUNCH_DIM_PERCENT (some panels are black at 10 %); read once. */
constexpr int kDefaultDimPercent = 10;
int parse_dim_percent(const char *text);    // an integer 1..100; nullptr, empty or anything else: kDefaultDimPercent
int idle_dim_percent();                     // parse_dim_percent(getenv("APPLAUNCH_DIM_PERCENT")), read on first use

} // namespace launcher_media_controls
